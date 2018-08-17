//! @file ProvPass.cc  LLVM @b opt pass for adding provenance tracing to IR
/*
 * Copyright (c) 2016-2017 Jonathan Anderson
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
#include "IFFactory.hh"

#include "loom/Instrumenter.hh"

#include <llvm/Pass.h>
#include <llvm/Analysis/MemorySSA.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>

#include <sstream>

using namespace llvm;
using namespace llvm::prov;
using namespace loom;
using std::string;


namespace llvm {
  struct Provenance : public FunctionPass {
    static char ID;
    Provenance() : FunctionPass(ID) {}

    bool runOnFunction(Function&) override;
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      // Our instrumentation injects instructions and may extend system calls,
      // but it doesn't modify the control-flow graph of *our* code (i.e.,
      // it doesn't add/remove BasicBlocks or modify our own branches/returns).
      AU.setPreservesCFG();

      // We need analysis of which loads depend on which stores.
      AU.addRequiredTransitive<MemorySSAWrapperPass>();
    }
  };
}


static string JoinVec(const std::vector<string>&);


bool Provenance::runOnFunction(Function &Fn)
{
  auto S = InstrStrategy::Create(loom::InstrStrategy::Kind::Inline, false);

  auto IF = IFFactory::FreeBSDMetaIO(
    Instrumenter::Create(*Fn.getParent(), JoinVec, std::move(S)));
  const CallSemantics& CS = IF->CallSemantics();

  auto IsSink = [&CS](const Value *V) {
    if (auto *Call = dyn_cast<CallInst>(V)) {
        return CS.CanSink(Call);
    }

    return false;
  };

  FlowFinder FF(CS);

  auto &MSSA = getAnalysis<MemorySSAWrapperPass>().getMSSA();
  FlowFinder::FlowSet PairwiseFlows = FF.FindPairwise(Fn, MSSA);

  std::map<Value*, std::vector<Value*>> DataFlows;

  for (auto& I : instructions(Fn)) {
    if (CallInst* Call = dyn_cast<CallInst>(&I)) {
      if (not CS.IsSource(Call)) {
        continue;
      }

      for (Value *Sink : FF.FindEventual(PairwiseFlows, Call, IsSink)) {
        DataFlows[Call].push_back(Sink);
      }
    }
  }

  bool ModifiedIR = false;

  for (auto Flow : DataFlows) {
    Source Source = IF->TranslateSource(dyn_cast<CallInst>(Flow.first));

    for (Value *SinkCall : Flow.second) {
      IF->TranslateSink(dyn_cast<CallInst>(SinkCall), Source);
    }

    ModifiedIR = true;
  }

  return ModifiedIR;
}

static string JoinVec(const std::vector<string>& V) {
    std::ostringstream oss;
    std::copy(V.begin(), V.end() - 1, std::ostream_iterator<string>(oss, "_"));
    oss << *V.rbegin();
    return oss.str();
}

char Provenance::ID = 0;

static RegisterPass<Provenance> X("prov", "Provenance tracking");
