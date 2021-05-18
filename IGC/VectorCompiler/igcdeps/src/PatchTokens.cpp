/*========================== begin_copyright_notice ============================

Copyright (C) 2021 Intel Corporation

SPDX-License-Identifier: MIT

============================= end_copyright_notice ===========================*/

#include "vc/igcdeps/cmc.h"

#include "AdaptorOCL/OCL/sp/sp_g8.h"

using namespace vc;

// Implementation of CGen8CMProgram.
CGen8CMProgram::CGen8CMProgram(PLATFORM platform, const WA_TABLE& WATable)
    : CGen8OpenCLProgramBase(platform, m_ContextProvider, WATable),
      m_programInfo(new IGC::SOpenCLProgramInfo) {}

CGen8CMProgram::~CGen8CMProgram() {
  for (auto kernel : m_kernels)
    delete kernel;
}

void CGen8CMProgram::CreateKernelBinaries() {
  CreateProgramScopePatchStream(*m_programInfo);
  for (auto *kernel : m_kernels) {
    // Create the kernel binary streams.
    iOpenCL::KernelData data;
    data.kernelBinary = new Util::BinaryStream;

    m_StateProcessor.CreateKernelBinary(
        reinterpret_cast<const char *>(kernel->getProgramOutput().m_programBin),
        kernel->getProgramOutput().m_programSize, kernel->m_kernelInfo,
        *m_programInfo, kernel->m_btiLayout, *(data.kernelBinary),
        m_pSystemThreadKernelOutput,
        kernel->getProgramOutput().m_unpaddedProgramSize);

    if (kernel->getProgramOutput().m_debugDataVISASize) {
      data.kernelDebugData = new Util::BinaryStream();
      m_StateProcessor.CreateKernelDebugData(
          reinterpret_cast<const char *>(
              kernel->getProgramOutput().m_debugDataVISA),
          kernel->getProgramOutput().m_debugDataVISASize,
          reinterpret_cast<const char *>(
              kernel->getProgramOutput().m_debugDataGenISA),
          kernel->getProgramOutput().m_debugDataGenISASize,
          kernel->m_kernelInfo.m_kernelName, *(data.kernelDebugData));
    }
    m_KernelBinaries.push_back(data);
  }
}

void CGen8CMProgram::GetZEBinary(llvm::raw_pwrite_stream &programBinary,
                                 unsigned pointerSizeInBytes) {
  iOpenCL::ZEBinaryBuilder zebuilder{m_Platform, pointerSizeInBytes == 8,
                                     *m_programInfo, nullptr, 0};
  zebuilder.setGfxCoreFamilyToELFMachine(m_Platform.eRenderCoreFamily);

  for (auto *kernel : m_kernels) {
    zebuilder.createKernel(
        reinterpret_cast<const char *>(kernel->getProgramOutput().m_programBin),
        kernel->getProgramOutput().m_programSize, kernel->m_kernelInfo,
        kernel->m_GRFSizeInBytes);
  }
  zebuilder.getBinaryObject(programBinary);
}
