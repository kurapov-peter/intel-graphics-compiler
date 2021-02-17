/*========================== begin_copyright_notice ============================

Copyright (c) 2000-2021 Intel Corporation

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

#include "Compiler/Optimizer/OpenCLPasses/DisableLoopUnrollOnRetry/DisableLoopUnrollOnRetry.hpp"
#include "Compiler/IGCPassSupport.h"

#include "common/LLVMWarningsPush.hpp"
#include <llvm/Pass.h>
#include <llvm/Transforms/Utils/UnrollLoop.h>
#include "common/LLVMWarningsPop.hpp"

using namespace llvm;
using namespace IGC;

#define PASS_FLAG "igc-disable-loop-unroll"
#define PASS_DESCRIPTION "Disable loop unroll on retry"
#define PASS_CFG_ONLY false
#define PASS_ANALYSIS false
IGC_INITIALIZE_PASS_BEGIN(DisableLoopUnrollOnRetry, PASS_FLAG, PASS_DESCRIPTION, PASS_CFG_ONLY, PASS_ANALYSIS)
IGC_INITIALIZE_PASS_END(DisableLoopUnrollOnRetry, PASS_FLAG, PASS_DESCRIPTION, PASS_CFG_ONLY, PASS_ANALYSIS)

char DisableLoopUnrollOnRetry::ID = 0;

DisableLoopUnrollOnRetry::DisableLoopUnrollOnRetry()
    : LoopPass(ID)
{
    initializeDisableLoopUnrollOnRetryPass(*PassRegistry::getPassRegistry());
}

bool DisableLoopUnrollOnRetry::runOnLoop(Loop* L, LPPassManager& LPM)
{
    bool changed = false;
    if (MDNode * LoopID = L->getLoopID())
    {
        if (!(GetUnrollMetadata(LoopID, "llvm.loop.unroll.enable") ||
            GetUnrollMetadata(LoopID, "llvm.loop.unroll.full")))
        {
            L->setLoopAlreadyUnrolled(); //This sets loop to llvm.loop.unroll.disable
            changed = true;
        }
    }
    else
    {
        L->setLoopAlreadyUnrolled(); //This sets loop to llvm.loop.unroll.disable
        changed = true;
    }
    return changed;
}
