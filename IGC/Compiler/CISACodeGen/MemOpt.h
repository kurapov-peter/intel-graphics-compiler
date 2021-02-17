/*========================== begin_copyright_notice ============================

Copyright (c) 2014-2021 Intel Corporation

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom
the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.

============================= end_copyright_notice ===========================*/

#ifndef _CISA_MEMOPT_H_
#define _CISA_MEMOPT_H_

#include "common/LLVMWarningsPush.hpp"
#include <llvm/Pass.h>
#include <llvm/PassRegistry.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/InstructionSimplify.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Analysis/ValueTracking.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalAlias.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/raw_ostream.h>
#include "common/LLVMWarningsPop.hpp"

#include "Compiler/CISACodeGen/ShaderCodeGen.hpp"

void initializeMemOptPass(llvm::PassRegistry&);
llvm::FunctionPass* createMemOptPass(bool AllowNegativeSymPtrsForLoad);

#endif // _CISA_MEMOPT_H_
