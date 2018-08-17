//! @file GraphFlowsPass.cc  @b opt pass to graph data flows
/*
 * Copyright (c) 2016-2018 Jonathan Anderson
 * All rights reserved.
 *
 * This software was developed by BAE Systems, the University of Cambridge
 * Computer Laboratory, and Memorial University under DARPA/AFRL contract
 * FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent Computing
 * (TC) research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "CallSemantics.hh"
#include "FlowFinder.hh"
#include "PosixCallSemantics.hh"

#include "loom/Instrumenter.hh"

#include <llvm/Pass.h>
#include <llvm/Analysis/MemorySSA.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/ModuleSlotTracker.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

#include <sstream>
#include <unordered_map>
#include <unordered_set>

using namespace llvm;
using namespace llvm::prov;
using namespace loom;
using std::string;
using std::unordered_map;
using std::unordered_set;


namespace {
  struct GraphFlowsPass : public FunctionPass {
    static char ID;
    GraphFlowsPass(raw_fd_ostream &Out)
      : FunctionPass(ID), YamlOutput(Out)
    {
    }

    bool runOnFunction(Function&) override;
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      // Our instrumentation injects instructions and may extend system calls,
      // but it doesn't modify the control-flow graph of *our* code (i.e.,
      // it doesn't add/remove BasicBlocks or modify our own branches/returns).
      AU.setPreservesCFG();

      // We need analysis of which loads depend on which stores.
      AU.addRequiredTransitive<AAResultsWrapperPass>();
      AU.addPreserved<AAResultsWrapperPass>();
      AU.addRequiredTransitive<MemorySSAWrapperPass>();
    }

  private:
    raw_fd_ostream &YamlOutput;
  };

  struct GraphFlowsModulePass : public ModulePass {
    static char ID;
    GraphFlowsModulePass() : ModulePass(ID) {}

    bool runOnModule(Module&) override;
  };

  std::string Indent;
}

cl::opt<string> FlowOutputFilename("flow-output", cl::init("flows.yaml"),
    cl::desc("Name of data flow output file (YAML format)"),
    cl::value_desc("filename"));

cl::opt<bool> ShowBasicBlocks("show-bbs", cl::init(true),
    cl::desc("Show basic blocks in data flow graphs"));

bool GraphFlowsPass::runOnFunction(Function &Fn)
{
  std::string FnName = Fn.getName();
  YamlOutput << Indent << FnName << ":\n";

  ModuleSlotTracker SlotTracker(Fn.getParent());
  SlotTracker.incorporateFunction(Fn);
  std::unordered_map<const Value*, std::string> Names;

  std::function<std::string(const Value*, const BasicBlock*)> Name =
    [&](const Value *V, const BasicBlock *BB) {
      auto i = Names.find(V);
      if (i != Names.end()) {
        return i->second;
      }

      std::string Context = (BB ? Name(BB, nullptr) : FnName) + "::";
      if (V->hasName()) {
        return Context + V->getName().str();
      }

      int Slot = SlotTracker.getLocalSlot(V);
      if (Slot >= 0) {
        return Context + std::to_string(Slot);
      }

      return Context + std::to_string(reinterpret_cast<unsigned long long>(V));
    };

  YamlOutput << Indent << "  arguments:\n";
  for (const Argument &A : Fn.args()) {
    YamlOutput
      << Indent << "    \"" << Name(&A, nullptr) << "\":\n"
      << Indent <<"      type: \"" << *A.getType() << "\"\n"
      << Indent <<"      label: \""
      ;

    A.print(YamlOutput);
    YamlOutput << "\"\n";
  }

  PosixCallSemantics CS;
  FlowFinder FF(CS);
  MemorySSA &MSSA = getAnalysis<MemorySSAWrapperPass>().getMSSA();
  std::unordered_map<const CallInst*, const Function*> Calls;
  FlowFinder::FlowSet Flows = FF.FindPairwise(Fn, MSSA);

  YamlOutput << Indent << "  blocks:\n";

  for (auto &BB : Fn) {
    YamlOutput << Indent << "    \"" << Name(&BB, nullptr) << "\":\n";

    for (const Instruction &I : BB) {
      std::string InstName = Name(&I, &BB);
      Names.emplace(&I, InstName);

      if (const CallInst *Call = dyn_cast<CallInst>(&I)) {
        if (const Function *Target = Call->getCalledFunction()) {
          Calls.emplace(Call, Target);
        }
      }

      YamlOutput
        << Indent << "      \"" << InstName << "\":\n"
        << Indent << "        opcode: { value: " << I.getOpcode()
        << Indent << ", name: \"" << I.getOpcodeName() << "\" }\n"
        << Indent << "        type: \"" << *I.getType() << "\"\n"
        << Indent << "        label: \""
        ;
      I.print(YamlOutput);
      YamlOutput << "\"\n";
    }
  }

  YamlOutput << Indent << "  calls:\n";
  for (auto i : Calls) {
    const CallInst *Call = i.first;
    const Function *Target = i.second;

    assert(Names.find(Call) != Names.end());
    assert(Target->hasName());

    YamlOutput
      << Indent << "    - from: \"" << Names[Call] << "\"\n"
      << Indent << "      to: \"" << Target->getName() << "\"\n"
      << Indent << "      kind: \"call\"\n";
      ;
  }

  YamlOutput << Indent << "  flows:\n";
  for (auto& Flow : Flows) {
    const Value *Dest = Flow.first;
    const Value *Src = Flow.second.first;
    const FlowFinder::FlowKind Kind = Flow.second.second;

    YamlOutput
      << Indent << "    - from: \""
      << Name(Src, nullptr) << "\"\n"
      << Indent << "      to: \""
      << Name(Dest, nullptr) << "\"\n"
      << Indent << "      kind: \""
      << str(Kind) << "\"\n"
      ;
  }

  YamlOutput << "\n";

  return false;
}

bool GraphFlowsModulePass::runOnModule(Module &M)
{
  std::error_code Err;
  raw_fd_ostream FlowGraph(FlowOutputFilename, Err,
      sys::fs::CreationDisposition::CD_CreateAlways,
      sys::fs::FileAccess::FA_Read | sys::fs::FileAccess::FA_Write,
      sys::fs::OpenFlags::F_Text);

  if (Err) {
    errs() << "Error opening graph file: " << Err.message() << "\n";
    return false;
  }

  legacy::FunctionPassManager FPM(&M);

  FPM.add(new GraphFlowsPass(FlowGraph));

  FlowGraph << "functions:\n";
  Indent = "  ";

  for (Function &F : M) {
    FPM.run(F);
  }

  return false;
}

char GraphFlowsPass::ID = 0;
char GraphFlowsModulePass::ID = 0;

static RegisterPass<GraphFlowsModulePass> X("graph-flows", "GraphFlowsPass tracking");
