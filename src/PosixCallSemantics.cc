//! @file PosixCallSemantics.cc  Definition of @ref llvm::prov::PosixCallSemantics.
/*
 * Copyright (c) 2016 Jonathan Anderson
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

#include "PosixCallSemantics.hh"

#include <llvm/ADT/StringSet.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;
using namespace llvm::prov;

#if defined(__DARWIN_ALIAS)
#define DARWIN_SYMBOL_NAME(name) "\01_" name __DARWIN_SUF_UNIX03
#endif

std::unique_ptr<CallSemantics> CallSemantics::Posix() {
  return std::unique_ptr<CallSemantics>(new PosixCallSemantics());
}


PosixCallSemantics::PosixCallSemantics()
  : ArgNumbers({
    { "read", 1 },
    { "pread", 1 },
    { "readv", 1 },
    { "preadv", 1 },
    //{ "recv", 1 },   // TODO: add recv_metaio(2) to CADETS BSD?
    { "recvfrom", 1 },
    { "recvmsg", 1 },
    { "recvmmsg", 1 },
    { "mmap", 0 },
#if defined(DARWIN_SYMBOL_NAME)
    { DARWIN_SYMBOL_NAME("read"), 1 },
    { DARWIN_SYMBOL_NAME("pread"), 1 },
    { DARWIN_SYMBOL_NAME("readv"), 1 },
    //{ DARWIN_SYMBOL_NAME("recv"), 1 }, // TODO: add recv_metaio(2)?
    { DARWIN_SYMBOL_NAME("recvfrom"), 1 },
    { DARWIN_SYMBOL_NAME("recvmsg"), 1 },
    { DARWIN_SYMBOL_NAME("mmap"), 0 },
#endif
  })
{
}

SmallVector<Value*, 2>
PosixCallSemantics::CallOutputs(CallInst *Call) const {
  SmallVector<Value*, 2> Outputs = { Call };

  Function *Target = Call->getCalledFunction();
  if (not Target or not Target->hasName()) {
    return Outputs;
  }

  StringRef FnName = Target->getName();
  auto i = ArgNumbers.find(FnName);
  if (i == ArgNumbers.end()) {
    return Outputs;
  }

  const size_t ArgNum = i->second;
  if (ArgNum >= Call->getNumArgOperands()) {
    errs()
      << "ERROR: " << FnName << " has only "
      << Call->getNumArgOperands() << " params, expected at least "
      << (ArgNum + 1)
      ;

    return {};
  }

  Outputs.push_back(Call->getArgOperand(ArgNum));

  return Outputs;
}


bool PosixCallSemantics::IsSource(const CallInst *Call) const {
  Function *F = Call->getCalledFunction();
  if (not (F and F->hasName())) {
    return false;
  }

  return (ArgNumbers.find(F->getName()) != ArgNumbers.end());
}

bool PosixCallSemantics::CanSink(const CallInst *Call) const {
  Function *F = Call->getCalledFunction();
  if (not (F and F->hasName())) {
    return false;
  }

  StringRef Name = F->getName();

  static llvm::StringSet<> SinkNames({
    /* "mmap", */ // information actually flows when we write into the memory
    "pwrite", "pwritev",
    "sendmsg", "sendto",
    "write", "writev",
#if defined(DARWIN_SYMBOL_NAME)
    DARWIN_SYMBOL_NAME("pwrite"),
    DARWIN_SYMBOL_NAME("sendmsg"),
    DARWIN_SYMBOL_NAME("sendto"),
    DARWIN_SYMBOL_NAME("write"),
    DARWIN_SYMBOL_NAME("writev"),
#endif
  });

  return (SinkNames.find(Name) != SinkNames.end());
}
