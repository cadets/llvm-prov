//! @file FlowFinder.cc  Definition of @ref llvm::prov::FlowFinder.
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

#include <llvm/ADT/iterator_range.h>
#include <llvm/Analysis/MemorySSA.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/User.h>
#include <llvm/Support/raw_ostream.h>

#include <unordered_map>
#include <unordered_set>

using namespace llvm;
using namespace llvm::prov;
using std::unordered_map;
using std::unordered_set;


typedef unordered_set<Value*> ValueSet;

/**
 * Find all memory operations that may have clobbered the location being
 * accessed by an Instruction.
 */
static ValueSet ClobberersOf(Instruction *, MemorySSA &);

/**
 * Recursively walk backwards through MemoryPhi operations until we reach
 * MemoryDef operations and real Instruction values that clobber memory.
 */
static ValueSet PhiClobberers(MemoryPhi *, MemorySSA &, ValueSet &Seen);


FlowFinder::FlowSet
FlowFinder::FindPairwise(Function &Fn, MemorySSA& MSSA) const {
  FlowFinder::FlowSet Flows;

  for (auto &I : instructions(Fn)) {
    CollectPairwise(&I, MSSA, Flows);
  }

  return Flows;
}

FlowFinder::ValueSet
FlowFinder::FindEventual(const FlowSet& Pairs, Value *Source,
                         ValuePredicate F) const
{
  ValueSet Seen, Sinks;

  // Reverse mapping: (Src -> (Sink, Kind)) rather than (Sink -> (Src, Kind))
  FlowSet SrcToSink;
  for (auto i : Pairs) {
    Value *Dest = i.first;
    Value *Src = i.second.first;
    FlowKind Kind = i.second.second;

    SrcToSink.insert({ Src, { Dest, Kind }});
  }

  CollectEventual(Sinks, Seen, SrcToSink, Source, F);

  return Sinks;
}

void FlowFinder::CollectEventual(ValueSet &Sinks, ValueSet &Seen,
                                 const FlowSet &Pairs, Value *Source,
                                 ValuePredicate F) const
{
  Seen.insert(Source);

  auto Range = Pairs.equal_range(Source);
  for (auto i = Range.first; i != Range.second; i++) {
    Value *Dest = i->second.first;
    assert(Dest != Source);

    if (Seen.find(Dest) != Seen.end()) {
      continue;
    }

    if (F(Dest)) {
      Sinks.emplace(Dest);
    }

    CollectEventual(Sinks, Seen, Pairs, Dest, F);
  }
}

void
FlowFinder::CollectPairwise(Value *V, MemorySSA &MSSA, FlowSet& Flows) const {

  auto *Dest = dyn_cast<User>(V);
  if (not Dest) {
    return;
  }

  // Ignore constants and already-considered nodes.
  if (isa<Constant>(Dest) or Flows.find(Dest) != Flows.end()) {
    return;
  }

  // Add explicit Value-User flows exposed as LLVM operands.
  for (Value *Operand : Dest->operands()) {
    // Ignore constants and non-User values (but include Arguments).
    if (isa<Constant>(Operand) or
        (not isa<User>(Operand) and not isa<Argument>(Operand))) {
      continue;
    }

    Flows.insert({ Dest, { Operand, FlowKind::Operand }});
  }

  // Load instructions have an implicit dependency on instructions that have
  // clobbered the location being loaded from. If the value we're inspecting is
  // an Instruction, and if it has significance to MemorySSA, and if that
  // significance is that it's a MemoryUse, figure out who clobbered the memory.
  if (auto *Inst = dyn_cast<Instruction>(Dest)) {
    for (Value *V : ClobberersOf(Inst, MSSA)) {
      Flows.insert({ Dest, { V, FlowKind::Memory }});
    }
  }
}

static void Describe(const Value *V, llvm::raw_ostream &Out) {
  std::string Colour = "#eeeeee";
  std::string Shape = "box";

  if (isa<AllocaInst>(V)) {
    Colour = "#9999ff";
    Shape = "ellipse";
  } else if (isa<BranchInst>(V)) {
    Colour = "#ff99ff";
    Shape = "cds";
  } else if (isa<GetElementPtrInst>(V)) {
    Colour = "#999999";
    Shape = "tab";
  } else if (isa<CallInst>(V)) {
    Colour = "#ffff99";
    Shape = "cds";
  } else if (isa<LoadInst>(V)) {
    Colour = "#99ff99";
    Shape = "house";
  } else if (isa<StoreInst>(V)) {
    Colour = "#ff9999";
    Shape = "invhouse";
  }

  Out << "\t\t\"" << V << "\" [ style = \"filled\", label = \"";
  V->print(Out);
  Out
    << "\", fillcolor = \"" << Colour << "99\""
    << ", shape = \"" << Shape << "\""
    << " ];\n"
    ;
}

static std::string LineAttrs(FlowFinder::FlowKind Kind)
{
  switch (Kind) {
  case FlowFinder::FlowKind::Operand:
    return "[ color = \"lightsteelblue4\", style = \"solid\" ]";

  case FlowFinder::FlowKind::Memory:
    return "[ color = \"orangered3\" ]";

  case FlowFinder::FlowKind::Meta:
    return "[ color = \"olivedrab\", penwidth = 2, style = \"dotted\" ]";
  }
}

static void Describe(const Value *Source, const Value *Dest,
                     FlowFinder::FlowKind Kind, llvm::raw_ostream &Out)
{
  Out << "\t\"" << Source << "\" -> \"" << Dest << "\" " << LineAttrs(Kind)
    << "\n"
    ;
}

void FlowFinder::Graph(const FlowSet& Flows, StringRef Label, bool ShowBBs,
                       raw_ostream &Out) const {
  Out << "digraph {\n"
    << "\tfontname = \"Inconsolata\";\n"
    << "\tlabel = \"" << Label << "\";\n"
    << "\tnode [ fontname = \"Inconsolata\" ];\n"
    << "\n"
    ;

  unordered_set<const Argument*> Args;
  unordered_map<const BasicBlock*, unordered_set<const Instruction*>> Blocks;

  auto SaveValue = [&Args, &Blocks](const Value *V) {
    if (auto *I = dyn_cast<Instruction>(V)) {
      Blocks[I->getParent()].insert(I);

    } else if (auto *A = dyn_cast<Argument>(V)) {
      Args.insert(A);

    } else {
      assert(false && "unreachable");
    }
  };

  for (auto& Flow : Flows) {
    const Value *Dest = Flow.first;
    const Value *Src = Flow.second.first;
    const FlowKind Kind = Flow.second.second;

    SaveValue(Src);
    SaveValue(Dest);

    Describe(Src, Dest, Kind, Out);
  }

  for (auto *A : Args) {
    Describe(A, Out);
  }

  for (auto i : Blocks) {
    const BasicBlock *BB = i.first;
    auto &Instructions = i.second;

    if (ShowBBs) {
      Out << "\tsubgraph \"cluster_" << BB->getName() << "\" {\n"
        << "\t\tlabel = \"" << BB->getName() << "\";\n"
        << "\t\tlabeljust = \"l\";\n"
        << "\t\tstyle = \"filled\";\n"
        << "\t\tstyle = \"filled\";\n"
        << "\n"
        ;
    }

    for (auto *I : Instructions) {
      Describe(I, Out);
    }

    if (ShowBBs) {
      Out << "\t}\n";
    }
  }

  Out << "}\n";
}

static ValueSet ClobberersOf(Instruction *I, MemorySSA &MSSA)
{
  MemoryAccess *MA = MSSA.getMemoryAccess(I);
  if (not MA) {
    return {};
  }

  ValueSet Clobberers;
  MemoryAccess *Clobberer = MSSA.getWalker()->getClobberingMemoryAccess(MA);

  if (auto *Def = dyn_cast<MemoryDef>(Clobberer)) {
    // The memory was written to by an easily-discernable instruction like
    // a store that comes earlier in the function.
    if (not MSSA.isLiveOnEntryDef(Def)) {
      Clobberers.insert(Def->getMemoryInst());
    }

  } else if (auto *Phi = dyn_cast<MemoryPhi>(Clobberer)) {
    // Is there a potentially more complex scenario in which multiple stores
    // can clobber the memory location? If so, we'll need to (recursively)
    // chase down all of the possible clobbering instructions.
    ValueSet Seen;
    for (Value *V : PhiClobberers(Phi, MSSA, Seen)) {
      if (V != I) {
        Clobberers.insert(V);
      }
    }
  }

  return Clobberers;
}


static ValueSet PhiClobberers(MemoryPhi *Phi, MemorySSA &MSSA, ValueSet &Seen)
{
  if (Seen.find(Phi) != Seen.end()) {
    return {};
  }

  Seen.insert(Phi);
  ValueSet Clobberers;

  for (Use &U : Phi->incoming_values()) {
    Value *V = U.get();

    if (auto *SubPhi = dyn_cast<MemoryPhi>(V)) {
      auto Sub = PhiClobberers(SubPhi, MSSA, Seen);
      Clobberers.insert(Sub.begin(), Sub.end());
      continue;
    }

    auto *MD = dyn_cast<MemoryDef>(U.get());
    assert(MD);

    if (MSSA.isLiveOnEntryDef(MD)) {
      continue;
    }

    assert(MD->getMemoryInst());
    Clobberers.insert(MD->getMemoryInst());
  }

  return Clobberers;
}


