/*========================== begin_copyright_notice ============================

Copyright (c) 2019-2021 Intel Corporation

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

#include "Compiler/CISACodeGen/CrossPhaseConstProp.hpp"
#include "LLVMWarningsPush.hpp"
#include "llvm/Pass.h"
#include "LLVMWarningsPop.hpp"
#include "Compiler/CISACodeGen/PixelShaderCodeGen.hpp"
#include "Compiler/CISACodeGen/helper.h"
#include "Compiler/MetaDataUtilsWrapper.h"
using namespace llvm;
namespace
{
    class CrossPhaseConstProp : public FunctionPass
    {
    public:
        static char ID; // Pass identification, replacement for typeid

        CrossPhaseConstProp(IGC::PSSignature* signature=nullptr);

        bool runOnFunction(Function& F) override;

        void getAnalysisUsage(AnalysisUsage& AU) const override
        {
            AU.addRequired<IGC::MetaDataUtilsWrapper>();
            AU.setPreservesCFG();
        }
    private:
        IGC::PSSignature* m_signature;
    };
}  // namespace


#define PASS_FLAG "igc-crossphaseconstprop"
#define PASS_DESCRIPTION "Special const prop for multirate shading"
#define PASS_CFG_ONLY false
#define PASS_ANALYSIS false
IGC_INITIALIZE_PASS_BEGIN(CrossPhaseConstProp, PASS_FLAG, PASS_DESCRIPTION, PASS_CFG_ONLY, PASS_ANALYSIS)
IGC_INITIALIZE_PASS_DEPENDENCY(MetaDataUtilsWrapper)
IGC_INITIALIZE_PASS_END(CrossPhaseConstProp, PASS_FLAG, PASS_DESCRIPTION, PASS_CFG_ONLY, PASS_ANALYSIS)

CrossPhaseConstProp::CrossPhaseConstProp(IGC::PSSignature* signature) : FunctionPass(ID), m_signature(signature)
{
    initializeCrossPhaseConstPropPass(*PassRegistry::getPassRegistry());
}

char CrossPhaseConstProp::ID = 0;

FunctionPass* IGC::createCrossPhaseConstPropPass(IGC::PSSignature* signature)
{
    return new CrossPhaseConstProp(signature);
}

bool CrossPhaseConstProp::runOnFunction(Function& F)
{
    IGC::IGCMD::MetaDataUtils* pMdUtils = getAnalysis<IGC::MetaDataUtilsWrapper>().getMetaDataUtils();
    if (!IGC::isEntryFunc(pMdUtils, &F) || m_signature == nullptr)
    {
        return false;
    }

    std::vector<Instruction*> instructionsToRemove;
    bool isCoarse = IGC::isCoarsePhaseFunction(&F);
    bool isPerPixel = IGC::isPixelPhaseFunction(&F);
    if (isCoarse || isPerPixel)
    {
        for (auto& BB : F)
        {
            for (auto& I : BB)
            {
                if (GenIntrinsicInst * inst = dyn_cast<GenIntrinsicInst>(&I))
                {
                    GenISAIntrinsic::ID IID = inst->getIntrinsicID();
                    if (isPerPixel && IID == GenISAIntrinsic::GenISA_PHASE_INPUT)
                    {
                        unsigned int index = (unsigned int)cast<llvm::ConstantInt>(inst->getOperand(0))->getZExtValue();
                        auto constantOutput = m_signature->PSConstantOutput.find(index);
                        if (constantOutput != m_signature->PSConstantOutput.end())
                        {
                            inst->replaceAllUsesWith(
                                ConstantFP::get(inst->getType(),
                                    APFloat(inst->getType()->getFltSemantics(), APInt(inst->getType()->getScalarSizeInBits(), constantOutput->second))));
                            instructionsToRemove.push_back(inst);
                        }
                    }
                    else if (isCoarse && IID == GenISAIntrinsic::GenISA_PHASE_OUTPUT)
                    {
                        if (auto fpValue = dyn_cast<ConstantFP>(inst->getArgOperand(0)))
                        {
                            unsigned int index = (unsigned int)cast<llvm::ConstantInt>(inst->getArgOperand(1))->getZExtValue();
                            m_signature->PSConstantOutput[index] = fpValue->getValueAPF().bitcastToAPInt().getLimitedValue();
                            instructionsToRemove.push_back(inst);
                        }
                    }
                }
            }
        }

        for (auto inst : instructionsToRemove)
        {
            inst->eraseFromParent();
        }
    }

    return instructionsToRemove.size() > 0;
}
