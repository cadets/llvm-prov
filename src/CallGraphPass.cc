//! @file CallGraphPass.cc  @b opt pass to graph data flows
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

#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Pass.h>

#include <unordered_set>

using namespace llvm;
using std::string;


namespace llvm {
  struct CallGraphPass : public ModulePass {
    static char ID;
    CallGraphPass() : ModulePass(ID) {}

    bool runOnModule(Module&) override;
  };
}

namespace {
  struct FunctionData {
    const string Name;
    std::unordered_set<string> CallTargets;
    std::unordered_set<string> FlowSources;
  };

  using FnVec = std::vector<const FunctionData>;

  struct FileFormat {
    enum class Kind { Dot, JSON, YAML };

    virtual string Filename(StringRef Prefix) const = 0;
    virtual void Write(raw_ostream&, const FnVec&)
      const = 0;

    static std::unique_ptr<FileFormat> Create(Kind);

    virtual ~FileFormat() {}
  };

  cl::opt<FileFormat::Kind> CGFormat("cg-format",
    cl::desc("callgraph output format"),
    cl::values(
      clEnumValN(FileFormat::Kind::Dot,  "dot",  "GraphViz dot"),
      clEnumValN(FileFormat::Kind::JSON, "json", "JavaScript Object Notation"),
      clEnumValN(FileFormat::Kind::YAML, "yaml", "Yet Another Markup Language")
    )
  );
}


bool CallGraphPass::runOnModule(Module &M)
{
  auto Format = FileFormat::Create(CGFormat);
  assert(Format);

  std::error_code Err;
  raw_fd_ostream FlowGraph(Format->Filename((M.getName() + ".callgraph").str()),
      Err, sys::fs::OpenFlags::F_RW | sys::fs::OpenFlags::F_Text);

  if (Err) {
    errs() << "Error opening graph file: " << Err.message() << "\n";
    return false;
  }

  std::vector<const FunctionData> Functions;

  for (auto &Fn : M) {
    FunctionData FnData = { .Name = Fn.getName() };

    for (auto& I : instructions(Fn)) {
      if (CallInst* Call = dyn_cast<CallInst>(&I)) {
        if (Function *DirectTarget = Call->getCalledFunction()) {
          FnData.CallTargets.insert(DirectTarget->getName());
        }
      }
    }

    if (not FnData.CallTargets.empty()) {
      Functions.emplace_back(FnData);
    }
  }

  Format->Write(FlowGraph, Functions);

  return false;
}


struct DotFormat : public FileFormat {
  string Filename(StringRef Prefix) const override {
    return (Prefix + ".dot").str();
  }

  void Write(raw_ostream &Out, const FnVec &Functions) const override {
    Out
      << "digraph {\n"
      << "  node [ shape = \"rectangle\" ];\n"
      << "  rankdir = TB;\n"
      << "\n"
      ;

    for (const FunctionData& Fn : Functions) {
      Out
        << "  \"" << Fn.Name << "\" [ label = \"" << Fn.Name << "\" ];\n"
        ;
    }

    Out << "\n";

    for (const FunctionData& Fn : Functions) {
      for (const auto &Target : Fn.CallTargets) {
        Out << "  \"" << Fn.Name << "\" -> \"" << Target << "\";\n";
      }
      for (const auto &Source : Fn.FlowSources) {
        Out << "  \"" << Source << "\" -> \"" << Fn.Name << "\";\n";
      }
    }

    Out << "}\n";
  }
};

struct JSONFormat : public FileFormat {
  string Filename(StringRef Prefix) const override {
    return (Prefix + ".json").str();
  }

  void Write(raw_ostream &Out, const FnVec &Functions) const override {
    Out
      << "{"
      << "\"functions\":{"
      ;

    // Sigh, JSON... sigh.
    for (size_t i = 0, Len = Functions.size(); i < Len; i++) {
      const FunctionData &Fn = Functions[i];
      Out
        << "\"" << Fn.Name << "\":{"
        << "\"calls\":["
        ;

      size_t Count = 0;
      Len = Fn.CallTargets.size();
      for (const auto &Target : Fn.CallTargets) {
        Out << "\"" << Target << "\"";
        if ((++Count) < Len) {
          Out << ',';
        }
      }

      Out << "],\"flows\":[";

      Count = 0, Len = Fn.FlowSources.size();
      for (const auto &Source : Fn.FlowSources) {
        Out << "\"" << Source << "\"";
        if ((++Count) < Len) {
          Out << ',';
        }
      }

      Out << "]}";
      if (i < (Len - 1)) {
        Out << ',';
      }
    }

    Out << "}}\n";
  }
};

struct YAMLFormat : public FileFormat {
  string Filename(StringRef Prefix) const override {
    return (Prefix + ".yaml").str();
  }

  void Write(raw_ostream &Out, const FnVec &Functions) const override {
    Out << "functions:\n";

    for (const FunctionData& Fn : Functions) {
      Out
        << "  " << Fn.Name << ":\n"
        << "    calls:\n"
        ;

      for (const auto &Target : Fn.CallTargets) {
        Out << "      - " << Target << "\n";
      }

      Out << "    flows:\n";
      for (const auto &Source : Fn.FlowSources) {
        Out << "      - " << Source << "\n";
      }
    }
  }
};


std::unique_ptr<FileFormat> FileFormat::Create(Kind K) {
  switch (K) {
  case Kind::Dot:
    return std::unique_ptr<FileFormat>(new DotFormat());

  case Kind::JSON:
    return std::unique_ptr<FileFormat>(new JSONFormat());

  case Kind::YAML:
    return std::unique_ptr<FileFormat>(new YAMLFormat());
  }

  assert(false);
}

char CallGraphPass::ID = 0;

static RegisterPass<CallGraphPass> X("callgraph", "output call graph");
