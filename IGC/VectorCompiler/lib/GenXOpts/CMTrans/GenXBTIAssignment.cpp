/*========================== begin_copyright_notice ============================

Copyright (C) 2021 Intel Corporation

SPDX-License-Identifier: MIT

============================= end_copyright_notice ===========================*/

//===----------------------------------------------------------------------===//
//
/// GenXAssignBTI
/// -----------------
///
/// This pass calculates BT indices for kernel memory object arguments
/// that include buffers and images.
///
/// Calculated BTI are then used instead of corresponging kernel arguments
/// throughout the code. Additionally, all assigned values are saved to
/// kernel metadata to be retrieved later by runtime info pass.
///
//===----------------------------------------------------------------------===//

#include "vc/GenXOpts/GenXOpts.h"
#include "vc/GenXOpts/Utils/KernelInfo.h"
#include "vc/Support/BackendConfig.h"

#include "llvm/GenXIntrinsics/GenXIntrinsics.h"

#include "Probe/Assertion.h"

#include "llvmWrapper/ADT/StringRef.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>

#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

namespace {
class BTIAssignment final {
  Module &M;
  const GenXBackendConfig &BC;

public:
  BTIAssignment(Module &InM, const GenXBackendConfig &InBC)
      : M(InM), BC(InBC) {}

  bool run();

private:
  // Helper function to assign bti from corresponding category.
  // ZipTy -- tuple of ID, ArgKind and ArgDesc.
  // assignSRV return value -- current state of IDs for surface and
  // sampler assignment.
  template <typename ZipTy>
  std::pair<int, int> assignSRV(int SurfaceID, int SamplerID, ZipTy &&Zippy);
  template <typename ZipTy> int assignUAV(int SurfaceID, ZipTy &&Zippy);

  std::vector<int>
  computeBTIndices(genx::KernelMetadata &KM,
                   const std::vector<StringRef> &ExtendedArgDescs);
  bool rewriteArguments(genx::KernelMetadata &KM, Function &F,
                        const std::vector<int> &BTIndices,
                        const std::vector<StringRef> &ExtendedArgDescs);

  bool processKernel(Function &F);
};

class GenXBTIAssignment final : public ModulePass {
public:
  static char ID;

  GenXBTIAssignment() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<GenXBackendConfig>();
  }

  StringRef getPassName() const override { return "GenX BTI Assignment"; }

  bool runOnModule(Module &M) override;
};
} // namespace

char GenXBTIAssignment::ID = 0;

INITIALIZE_PASS_BEGIN(GenXBTIAssignment, "GenXBTIAssignment",
                      "GenXBTIAssignment", false, false);
INITIALIZE_PASS_DEPENDENCY(GenXBackendConfig)
INITIALIZE_PASS_END(GenXBTIAssignment, "GenXBTIAssignment", "GenXBTIAssignment",
                    false, false);

namespace llvm {
ModulePass *createGenXBTIAssignmentPass() {
  initializeGenXBTIAssignmentPass(*PassRegistry::getPassRegistry());
  return new GenXBTIAssignment();
}
} // namespace llvm

bool GenXBTIAssignment::runOnModule(Module &M) {
  auto &BC = getAnalysis<GenXBackendConfig>();
  BTIAssignment BA(M, BC);

  return BA.run();
}

// Surfaces starting from 240 are reserved.
static constexpr int MaxAvailableSurfaceIndex = 239;
static constexpr int MaxAvailableSamplerIndex = 14;

static bool isDescImageType(StringRef TypeDesc) {
  return IGCLLVM::contains_insensitive(TypeDesc, "image1d_t") ||
         IGCLLVM::contains_insensitive(TypeDesc, "image1d_array_t") ||
         IGCLLVM::contains_insensitive(TypeDesc, "image2d_t") ||
         IGCLLVM::contains_insensitive(TypeDesc, "image2d_array_t") ||
         IGCLLVM::contains_insensitive(TypeDesc, "image2d_media_block_t") ||
         IGCLLVM::contains_insensitive(TypeDesc, "image3d_t") ||
         IGCLLVM::contains_insensitive(TypeDesc, "image1d_buffer_t");
}

static bool isDescReadOnly(StringRef TypeDesc) {
  return IGCLLVM::contains_insensitive(TypeDesc, "read_only");
}

static bool isDescSvmPtr(StringRef TypeDesc) {
  return IGCLLVM::contains_insensitive(TypeDesc, "svmptr_t");
}

template <typename ZipTy>
std::pair<int, int> BTIAssignment::assignSRV(int SurfaceID, int SamplerID,
                                             ZipTy &&Zippy) {
  // SRV (read only) and samplers.
  for (auto &&[Idx, Kind, Desc] : Zippy) {
    if (Kind == genx::KernelMetadata::AK_SAMPLER) {
      Idx = SamplerID++;
      continue;
    }
    if (Kind == genx::KernelMetadata::AK_SURFACE && isDescReadOnly(Desc)) {
      IGC_ASSERT_MESSAGE(isDescImageType(Desc),
                         "RW qualifiers are allowed on images only");
      Idx = SurfaceID++;
      continue;
    }
  }
  return {SurfaceID, SamplerID};
}

template <typename ZipTy>
int BTIAssignment::assignUAV(int SurfaceID, ZipTy &&Zippy) {
  // UAV -- writable entities.
  for (auto &&[Idx, Kind, Desc] : Zippy) {
    // Already assigned entities should be skipped.
    if (Idx != -1)
      continue;

    if (Kind == genx::KernelMetadata::AK_SURFACE) {
      Idx = SurfaceID++;
      continue;
    }
    if (Kind == genx::KernelMetadata::AK_NORMAL && isDescSvmPtr(Desc)) {
      Idx = SurfaceID++;
      continue;
    }
    // These 'ands' are definitely super buggy. Kinds should be
    // matched with masking and comparision (as in KernelArgInfo).
    // Anding will match more kinds that supposed. Here, for example,
    // SB_BTI is matches too that makes some tests magically work
    // on L0 runtime.
    // FIXME(aus): investigate the reason and rewrite with KAI.
    if (Kind & genx::KernelMetadata::IMP_OCL_PRINTF_BUFFER) {
      Idx = SurfaceID++;
      continue;
    }
    if (Kind & genx::KernelMetadata::IMP_OCL_PRIVATE_BASE) {
      Idx = SurfaceID++;
      continue;
    }
  }
  return SurfaceID;
}

// Assign a BTI value to a surface or sampler, NEO path only.
// SRV and UAV is sort of direct3d terminology, though they
// are used across binary format structures.
// SRV -- constant resources -- samplers and read only images.
// UAV -- writeable resources -- buffers and rw/wo images.
// Additionally, ranges for SRV and UAV should be separate and contiguous
// so this code assigns SRV and then UAV resources.
std::vector<int> BTIAssignment::computeBTIndices(
    genx::KernelMetadata &KM, const std::vector<StringRef> &ExtendedArgDescs) {
  int SurfaceID = 0;
  int SamplerID = 0;

  // If module has Debuggable Kernels, then BTI=0 is reserved
  if (BC.emitDebuggableKernels())
    SurfaceID = SamplerID = 1;

  std::vector<int> Indices(KM.getArgKinds().size(), -1);

  ArrayRef<unsigned> ArgKinds = KM.getArgKinds();
  auto Zippy = llvm::zip(Indices, KM.getArgKinds(), ExtendedArgDescs);
  std::tie(SurfaceID, SamplerID) = assignSRV(SurfaceID, SamplerID, Zippy);
  SurfaceID = assignUAV(SurfaceID, Zippy);

  if (SurfaceID > MaxAvailableSurfaceIndex)
    llvm::report_fatal_error("not enough surface indices");
  if (SamplerID > MaxAvailableSamplerIndex)
    llvm::report_fatal_error("not enough sampler indices");

  return Indices;
}

bool BTIAssignment::rewriteArguments(
    genx::KernelMetadata &KM, Function &F, const std::vector<int> &BTIndices,
    const std::vector<StringRef> &ExtendedArgDescs) {
  bool Changed = false;

  auto *I32Ty = Type::getInt32Ty(M.getContext());

  IRBuilder<> IRB{&F.front().front()};

  auto ArgKinds = KM.getArgKinds();
  IGC_ASSERT_MESSAGE(ArgKinds.size() == F.arg_size(),
                     "Inconsistent arg kinds metadata");
  for (auto &&[Arg, Kind, BTI, Desc] :
       llvm::zip(F.args(), ArgKinds, BTIndices, ExtendedArgDescs)) {
    if (Kind != genx::KernelMetadata::AK_SAMPLER &&
        Kind != genx::KernelMetadata::AK_SURFACE)
      continue;

    // For bindless resource argument is ExBSO.
    if (BC.useBindlessBuffers() && genx::isDescBufferType(Desc))
      continue;

    IGC_ASSERT_MESSAGE(BTI >= 0, "unassigned BTI");
    Value *BTIConstant = ConstantInt::get(I32Ty, BTI);

    Type *ArgTy = Arg.getType();
    // This code is to handle DPC++ contexts with correct OCL types.
    // Without actually doing something with users of args, we just
    // cast constant to pointer and replace arg with new value.
    // Later passes will do their work and clean up the mess.
    // FIXME(aus): proper unification of incoming IR is
    // required. Current approach will constantly blow all passes
    // where some additional case should be handled.
    if (ArgTy->isPointerTy())
      BTIConstant = IRB.CreateIntToPtr(BTIConstant, ArgTy, ".bti.cast");

    IGC_ASSERT_MESSAGE(ArgTy == BTIConstant->getType(),
                       "Only explicit i32 indices or opaque types are allowed "
                       "as bti argument");

    Arg.replaceAllUsesWith(BTIConstant);
    Changed = true;
  }

  return Changed;
}

// Get arg type descs that are extended to arg kinds size.
static std::vector<StringRef> getExtendedArgDescs(genx::KernelMetadata &KM) {
  ArrayRef<unsigned> ArgKinds = KM.getArgKinds();
  ArrayRef<StringRef> ArgTypeDescs = KM.getArgTypeDescs();
  // ArgDescs can be lesser if there are implicit parameters.
  IGC_ASSERT_MESSAGE(
      ArgKinds.size() >= ArgTypeDescs.size(),
      "Expected same or less number of arguments for kinds and descs");

  // All arguments without arg type desc will get default empty description.
  std::vector<StringRef> ArgTypeDescsExt{ArgTypeDescs.begin(),
                                         ArgTypeDescs.end()};
  ArgTypeDescsExt.resize(ArgKinds.size());
  return ArgTypeDescsExt;
}

bool BTIAssignment::processKernel(Function &F) {
  genx::KernelMetadata KM{&F};

  std::vector<StringRef> ExtArgDescs = getExtendedArgDescs(KM);

  std::vector<int> BTIndices = computeBTIndices(KM, ExtArgDescs);

  bool Changed = rewriteArguments(KM, F, BTIndices, ExtArgDescs);

  KM.updateBTIndicesMD(std::move(BTIndices));

  return Changed;
}

bool BTIAssignment::run() {
  NamedMDNode *KernelsMD = M.getNamedMetadata(genx::FunctionMD::GenXKernels);
  // There can be no kernels in module.
  if (!KernelsMD)
    return false;

  bool Changed = false;
  for (MDNode *Kernel : KernelsMD->operands()) {
    Metadata *FuncRef = Kernel->getOperand(genx::KernelMDOp::FunctionRef);
    Function *F = cast<Function>(cast<ValueAsMetadata>(FuncRef)->getValue());
    Changed |= processKernel(*F);
  }

  return Changed;
}
