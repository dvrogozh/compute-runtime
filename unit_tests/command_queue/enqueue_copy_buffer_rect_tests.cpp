/*
 * Copyright (c) 2017, Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "runtime/built_ins/built_ins.h"
#include "unit_tests/command_queue/enqueue_copy_buffer_rect_fixture.h"
#include "runtime/memory_manager/memory_constants.h"
#include "runtime/gen_common/reg_configs.h"
#include "runtime/helpers/dispatch_info.h"
#include "test.h"

using namespace OCLRT;

const size_t EnqueueCopyBufferRectTest::BufferRect::sizeInBytes = 100 * 100 * 100 * sizeof(cl_char);

HWTEST_F(EnqueueCopyBufferRectTest, null_src_mem_object) {
    auto retVal = CL_SUCCESS;
    size_t srcOrigin[] = {0, 0, 0};
    size_t dstOrigin[] = {0, 0, 0};
    size_t region[] = {1, 1, 0};

    retVal = clEnqueueCopyBufferRect(
        pCmdQ,
        nullptr,
        dstBuffer,
        srcOrigin,
        dstOrigin,
        region,
        10,
        0,
        10,
        0,
        0,
        nullptr,
        nullptr);

    EXPECT_EQ(CL_INVALID_MEM_OBJECT, retVal);
}

HWTEST_F(EnqueueCopyBufferRectTest, null_dst_mem_object) {
    auto retVal = CL_SUCCESS;
    size_t srcOrigin[] = {0, 0, 0};
    size_t dstOrigin[] = {0, 0, 0};
    size_t region[] = {1, 1, 0};

    retVal = clEnqueueCopyBufferRect(
        pCmdQ,
        srcBuffer,
        nullptr,
        srcOrigin,
        dstOrigin,
        region,
        10,
        0,
        10,
        0,
        0,
        nullptr,
        nullptr);

    EXPECT_EQ(CL_INVALID_MEM_OBJECT, retVal);
}

HWTEST_F(EnqueueCopyBufferRectTest, returnSuccess) {
    auto retVal = CL_SUCCESS;
    size_t srcOrigin[] = {0, 0, 0};
    size_t dstOrigin[] = {0, 0, 0};
    size_t region[] = {1, 1, 1};

    retVal = clEnqueueCopyBufferRect(
        pCmdQ,
        srcBuffer,
        dstBuffer,
        srcOrigin,
        dstOrigin,
        region,
        10,
        0,
        10,
        0,
        0,
        nullptr,
        nullptr);

    EXPECT_EQ(CL_SUCCESS, retVal);
}

HWTEST_F(EnqueueCopyBufferRectTest, alignsToCSR) {
    //this test case assumes IOQ
    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();
    csr.taskCount = pCmdQ->taskCount + 100;
    csr.taskLevel = pCmdQ->taskLevel + 50;

    enqueueCopyBufferRect2D<FamilyType>();
    EXPECT_EQ(csr.peekTaskCount(), pCmdQ->taskCount);
    EXPECT_EQ(csr.peekTaskLevel(), pCmdQ->taskLevel + 1);
}

HWTEST_F(EnqueueCopyBufferRectTest, 2D_GPGPUWalker) {
    typedef typename FamilyType::GPGPU_WALKER GPGPU_WALKER;
    enqueueCopyBufferRect2D<FamilyType>();

    auto *cmd = (GPGPU_WALKER *)cmdWalker;
    ASSERT_NE(nullptr, cmd);

    // Verify GPGPU_WALKER parameters
    EXPECT_NE(0u, cmd->getThreadGroupIdXDimension());
    EXPECT_NE(0u, cmd->getThreadGroupIdYDimension());
    EXPECT_EQ(1u, cmd->getThreadGroupIdZDimension());
    EXPECT_NE(0u, cmd->getRightExecutionMask());
    EXPECT_NE(0u, cmd->getBottomExecutionMask());
    EXPECT_EQ(GPGPU_WALKER::SIMD_SIZE_SIMD32, cmd->getSimdSize());
    EXPECT_NE(0u, cmd->getIndirectDataLength());
    EXPECT_EQ(0u, cmd->getIndirectDataLength() % GPGPU_WALKER::INDIRECTDATASTARTADDRESS_ALIGN_SIZE);
    EXPECT_FALSE(cmd->getIndirectParameterEnable());

    // Compute the SIMD lane mask
    size_t simd =
        cmd->getSimdSize() == GPGPU_WALKER::SIMD_SIZE_SIMD32 ? 32 : cmd->getSimdSize() == GPGPU_WALKER::SIMD_SIZE_SIMD16 ? 16 : 8;
    uint64_t simdMask = (1ull << simd) - 1;

    // Mask off lanes based on the execution masks
    auto laneMaskRight = cmd->getRightExecutionMask() & simdMask;
    auto lanesPerThreadX = 0;
    while (laneMaskRight) {
        lanesPerThreadX += laneMaskRight & 1;
        laneMaskRight >>= 1;
    }
}

HWTEST_F(EnqueueCopyBufferRectTest, 2D_bumpsTaskLevel) {
    auto taskLevelBefore = pCmdQ->taskLevel;

    enqueueCopyBufferRect2D<FamilyType>();
    EXPECT_GT(pCmdQ->taskLevel, taskLevelBefore);
}

HWTEST_F(EnqueueCopyBufferRectTest, 2D_addsCommands) {
    auto usedCmdBufferBefore = pCS->getUsed();

    enqueueCopyBufferRect2D<FamilyType>();
    EXPECT_NE(usedCmdBufferBefore, pCS->getUsed());
}

HWTEST_F(EnqueueCopyBufferRectTest, 2D_addsIndirectData) {
    auto dshBefore = pDSH->getUsed();
    auto iohBefore = pIOH->getUsed();
    auto ihBefore = pIH->getUsed();
    auto sshBefore = pSSH->getUsed();

    enqueueCopyBufferRect2D<FamilyType>();

    // Extract the kernel used
    MultiDispatchInfo multiDispatchInfo;
    auto &builder = BuiltIns::getInstance().getBuiltinDispatchInfoBuilder(EBuiltInOps::CopyBufferRect,
                                                                          pCmdQ->getContext(), pCmdQ->getDevice());
    ASSERT_NE(nullptr, &builder);

    BuiltinDispatchInfoBuilder::BuiltinOpParams dc;
    dc.srcMemObj = srcBuffer;
    dc.dstMemObj = dstBuffer;
    dc.srcOffset = {0, 0, 0};
    dc.dstOffset = {0, 0, 0};
    dc.size = {50, 50, 1};
    dc.srcRowPitch = rowPitch;
    dc.srcSlicePitch = slicePitch;
    dc.dstRowPitch = rowPitch;
    dc.dstSlicePitch = slicePitch;
    builder.buildDispatchInfos(multiDispatchInfo, dc);
    EXPECT_NE(0u, multiDispatchInfo.size());

    auto kernel = multiDispatchInfo.begin()->getKernel();
    ASSERT_NE(nullptr, kernel);

    EXPECT_NE(dshBefore, pDSH->getUsed());
    EXPECT_NE(iohBefore, pIOH->getUsed());
    EXPECT_NE(ihBefore, pIH->getUsed());
    if (kernel->requiresSshForBuffers()) {
        EXPECT_NE(sshBefore, pSSH->getUsed());
    }
}

HWTEST_F(EnqueueCopyBufferRectTest, 2D_LoadRegisterImmediateL3CNTLREG) {
    typedef typename FamilyType::MI_LOAD_REGISTER_IMM MI_LOAD_REGISTER_IMM;

    enqueueCopyBufferRect2D<FamilyType>();

    // All state should be programmed before walker
    auto itorCmd = findMmio<FamilyType>(cmdList.begin(), itorWalker, L3CNTLRegisterOffset<FamilyType>::registerOffset);
    ASSERT_NE(itorWalker, itorCmd);

    auto *cmd = genCmdCast<MI_LOAD_REGISTER_IMM *>(*itorCmd);
    ASSERT_NE(nullptr, cmd);

    auto RegisterOffset = L3CNTLRegisterOffset<FamilyType>::registerOffset;
    EXPECT_EQ(RegisterOffset, cmd->getRegisterOffset());
    auto l3Cntlreg = cmd->getDataDword();
    auto numURBWays = (l3Cntlreg >> 1) & 0x7f;
    auto L3ClientPool = (l3Cntlreg >> 25) & 0x7f;
    EXPECT_NE(0u, numURBWays);
    EXPECT_NE(0u, L3ClientPool);
}

HWTEST_F(EnqueueCopyBufferRectTest, 2D_StateBaseAddress) {
    typedef typename FamilyType::STATE_BASE_ADDRESS STATE_BASE_ADDRESS;

    enqueueCopyBufferRect2D<FamilyType>();

    auto *cmd = (STATE_BASE_ADDRESS *)cmdStateBaseAddress;
    ASSERT_NE(nullptr, cmd);

    // Verify all addresses are getting programmed
    EXPECT_TRUE(cmd->getDynamicStateBaseAddressModifyEnable());
    EXPECT_TRUE(cmd->getGeneralStateBaseAddressModifyEnable());
    EXPECT_TRUE(cmd->getSurfaceStateBaseAddressModifyEnable());
    EXPECT_TRUE(cmd->getIndirectObjectBaseAddressModifyEnable());
    EXPECT_TRUE(cmd->getInstructionBaseAddressModifyEnable());

    EXPECT_EQ((uintptr_t)pDSH->getCpuBase(), cmd->getDynamicStateBaseAddress());
    // Stateless accesses require GSH.base to be 0.
    EXPECT_EQ(0u, cmd->getGeneralStateBaseAddress());
    EXPECT_EQ((uintptr_t)pSSH->getCpuBase(), cmd->getSurfaceStateBaseAddress());
    EXPECT_EQ((uintptr_t)pIOH->getCpuBase(), cmd->getIndirectObjectBaseAddress());
    EXPECT_EQ((uintptr_t)pIH->getCpuBase(), cmd->getInstructionBaseAddress());

    // Verify all sizes are getting programmed
    EXPECT_TRUE(cmd->getDynamicStateBufferSizeModifyEnable());
    EXPECT_TRUE(cmd->getGeneralStateBufferSizeModifyEnable());
    EXPECT_TRUE(cmd->getIndirectObjectBufferSizeModifyEnable());
    EXPECT_TRUE(cmd->getInstructionBufferSizeModifyEnable());

    EXPECT_EQ(pDSH->getMaxAvailableSpace(), cmd->getDynamicStateBufferSize() * MemoryConstants::pageSize);
    EXPECT_NE(0u, cmd->getGeneralStateBufferSize());
    EXPECT_EQ(pIOH->getMaxAvailableSpace(), cmd->getIndirectObjectBufferSize() * MemoryConstants::pageSize);
    EXPECT_EQ(pIH->getMaxAvailableSpace(), cmd->getInstructionBufferSize() * MemoryConstants::pageSize);

    // Generically validate this command
    FamilyType::PARSE::template validateCommand<STATE_BASE_ADDRESS *>(cmdList.begin(), itorStateBaseAddress);
}

HWTEST_F(EnqueueCopyBufferRectTest, 2D_MediaInterfaceDescriptorLoad) {
    typedef typename FamilyType::MEDIA_INTERFACE_DESCRIPTOR_LOAD MEDIA_INTERFACE_DESCRIPTOR_LOAD;
    typedef typename FamilyType::INTERFACE_DESCRIPTOR_DATA INTERFACE_DESCRIPTOR_DATA;

    enqueueCopyBufferRect2D<FamilyType>();

    auto *cmd = (MEDIA_INTERFACE_DESCRIPTOR_LOAD *)cmdMediaInterfaceDescriptorLoad;
    ASSERT_NE(nullptr, cmd);

    // Verify we have a valid length -- multiple of INTERFACE_DESCRIPTOR_DATAs
    EXPECT_EQ(0u, cmd->getInterfaceDescriptorTotalLength() % sizeof(INTERFACE_DESCRIPTOR_DATA));

    // Validate the start address
    size_t alignmentStartAddress = 64 * sizeof(uint8_t);
    EXPECT_EQ(0u, cmd->getInterfaceDescriptorDataStartAddress() % alignmentStartAddress);

    // Validate the length
    EXPECT_NE(0u, cmd->getInterfaceDescriptorTotalLength());
    size_t alignmentTotalLength = 32 * sizeof(uint8_t);
    EXPECT_EQ(0u, cmd->getInterfaceDescriptorTotalLength() % alignmentTotalLength);

    // Generically validate this command
    FamilyType::PARSE::template validateCommand<MEDIA_INTERFACE_DESCRIPTOR_LOAD *>(cmdList.begin(), itorMediaInterfaceDescriptorLoad);
}

HWTEST_F(EnqueueCopyBufferRectTest, 2D_InterfaceDescriptorData) {
    typedef typename FamilyType::MEDIA_INTERFACE_DESCRIPTOR_LOAD MEDIA_INTERFACE_DESCRIPTOR_LOAD;
    typedef typename FamilyType::STATE_BASE_ADDRESS STATE_BASE_ADDRESS;
    typedef typename FamilyType::INTERFACE_DESCRIPTOR_DATA INTERFACE_DESCRIPTOR_DATA;

    enqueueCopyBufferRect2D<FamilyType>();

    auto *cmdSBA = (STATE_BASE_ADDRESS *)cmdStateBaseAddress;
    auto &IDD = *(INTERFACE_DESCRIPTOR_DATA *)cmdInterfaceDescriptorData;

    // Validate the kernel start pointer.  Technically, a kernel can start at address 0 but let's force a value.
    auto kernelStartPointer = ((uint64_t)IDD.getKernelStartPointerHigh() << 32) + IDD.getKernelStartPointer();
    EXPECT_LE(kernelStartPointer, cmdSBA->getInstructionBufferSize() * MemoryConstants::pageSize);

    EXPECT_NE(0u, IDD.getNumberOfThreadsInGpgpuThreadGroup());
    EXPECT_NE(0u, IDD.getCrossThreadConstantDataReadLength());
    EXPECT_NE(0u, IDD.getConstantIndirectUrbEntryReadLength());
}

HWTEST_F(EnqueueCopyBufferRectTest, 2D_PipelineSelect) {
    enqueueCopyBufferRect2D<FamilyType>();
    int numCommands = getNumberOfPipelineSelectsThatEnablePipelineSelect<FamilyType>();
    EXPECT_EQ(1, numCommands);
}

HWTEST_F(EnqueueCopyBufferRectTest, 2D_MediaVFEState) {
    typedef typename FamilyType::MEDIA_VFE_STATE MEDIA_VFE_STATE;

    enqueueCopyBufferRect2D<FamilyType>();

    auto *cmd = (MEDIA_VFE_STATE *)cmdMediaVfeState;

    // Verify we have a valid length
    EXPECT_EQ(pDevice->getHardwareInfo().pSysInfo->ThreadCount, cmd->getMaximumNumberOfThreads());
    EXPECT_NE(0u, cmd->getNumberOfUrbEntries());
    EXPECT_NE(0u, cmd->getUrbEntryAllocationSize());

    // Generically validate this command
    FamilyType::PARSE::template validateCommand<MEDIA_VFE_STATE *>(cmdList.begin(), itorMediaVfeState);
}

HWTEST_F(EnqueueCopyBufferRectTest, 3D_GPGPUWalker) {
    typedef typename FamilyType::GPGPU_WALKER GPGPU_WALKER;
    enqueueCopyBufferRect3D<FamilyType>();

    auto *cmd = (GPGPU_WALKER *)cmdWalker;
    ASSERT_NE(nullptr, cmd);

    // Verify GPGPU_WALKER parameters
    EXPECT_NE(0u, cmd->getThreadGroupIdXDimension());
    EXPECT_NE(0u, cmd->getThreadGroupIdYDimension());
    EXPECT_LT(1u, cmd->getThreadGroupIdZDimension());
    EXPECT_NE(0u, cmd->getRightExecutionMask());
    EXPECT_NE(0u, cmd->getBottomExecutionMask());
    EXPECT_EQ(GPGPU_WALKER::SIMD_SIZE_SIMD32, cmd->getSimdSize());
    EXPECT_NE(0u, cmd->getIndirectDataLength());
    EXPECT_EQ(0u, cmd->getIndirectDataLength() % GPGPU_WALKER::INDIRECTDATASTARTADDRESS_ALIGN_SIZE);
    EXPECT_FALSE(cmd->getIndirectParameterEnable());

    // Compute the SIMD lane mask
    size_t simd =
        cmd->getSimdSize() == GPGPU_WALKER::SIMD_SIZE_SIMD32 ? 32 : cmd->getSimdSize() == GPGPU_WALKER::SIMD_SIZE_SIMD16 ? 16 : 8;
    uint64_t simdMask = (1ull << simd) - 1;

    // Mask off lanes based on the execution masks
    auto laneMaskRight = cmd->getRightExecutionMask() & simdMask;
    auto lanesPerThreadX = 0;
    while (laneMaskRight) {
        lanesPerThreadX += laneMaskRight & 1;
        laneMaskRight >>= 1;
    }

    //auto numWorkItems = ( ( cmd->getThreadWidthCounterMaximum() - 1 ) * simd + lanesPerThreadX ) * cmd->getThreadGroupIdXDimension();
    //EXPECT_EQ( expectedWorkItems, numWorkItems );
}

HWTEST_F(EnqueueCopyBufferRectTest, 3D_bumpsTaskLevel) {
    auto taskLevelBefore = pCmdQ->taskLevel;

    enqueueCopyBufferRect3D<FamilyType>();
    EXPECT_GT(pCmdQ->taskLevel, taskLevelBefore);
}

HWTEST_F(EnqueueCopyBufferRectTest, 3D_addsCommands) {
    auto usedCmdBufferBefore = pCS->getUsed();

    enqueueCopyBufferRect3D<FamilyType>();
    EXPECT_NE(usedCmdBufferBefore, pCS->getUsed());
}

HWTEST_F(EnqueueCopyBufferRectTest, 3D_addsIndirectData) {
    auto usedIndirectHeapBefore = pDSH->getUsed();

    enqueueCopyBufferRect3D<FamilyType>();
    EXPECT_NE(usedIndirectHeapBefore, pDSH->getUsed());
}

HWTEST_F(EnqueueCopyBufferRectTest, 3D_LoadRegisterImmediateL3CNTLREG) {
    typedef typename FamilyType::MI_LOAD_REGISTER_IMM MI_LOAD_REGISTER_IMM;

    enqueueCopyBufferRect3D<FamilyType>();

    // All state should be programmed before walker
    auto itorCmd = findMmio<FamilyType>(cmdList.begin(), itorWalker, L3CNTLRegisterOffset<FamilyType>::registerOffset);
    ASSERT_NE(itorWalker, itorCmd);

    auto *cmd = genCmdCast<MI_LOAD_REGISTER_IMM *>(*itorCmd);
    ASSERT_NE(nullptr, cmd);

    auto RegisterOffset = L3CNTLRegisterOffset<FamilyType>::registerOffset;
    EXPECT_EQ(RegisterOffset, cmd->getRegisterOffset());
    auto l3Cntlreg = cmd->getDataDword();
    auto numURBWays = (l3Cntlreg >> 1) & 0x7f;
    auto L3ClientPool = (l3Cntlreg >> 25) & 0x7f;
    EXPECT_NE(0u, numURBWays);
    EXPECT_NE(0u, L3ClientPool);
}

HWTEST_F(EnqueueCopyBufferRectTest, 3D_StateBaseAddress) {
    typedef typename FamilyType::STATE_BASE_ADDRESS STATE_BASE_ADDRESS;

    enqueueCopyBufferRect3D<FamilyType>();

    auto *cmd = (STATE_BASE_ADDRESS *)cmdStateBaseAddress;
    ASSERT_NE(nullptr, cmd);

    // Verify all addresses are getting programmed
    EXPECT_TRUE(cmd->getDynamicStateBaseAddressModifyEnable());
    EXPECT_TRUE(cmd->getGeneralStateBaseAddressModifyEnable());
    EXPECT_TRUE(cmd->getSurfaceStateBaseAddressModifyEnable());
    EXPECT_TRUE(cmd->getIndirectObjectBaseAddressModifyEnable());
    EXPECT_TRUE(cmd->getInstructionBaseAddressModifyEnable());

    EXPECT_EQ((uintptr_t)pDSH->getCpuBase(), cmd->getDynamicStateBaseAddress());
    // Stateless accesses require GSH.base to be 0.
    EXPECT_EQ(0u, cmd->getGeneralStateBaseAddress());
    EXPECT_EQ((uintptr_t)pSSH->getCpuBase(), cmd->getSurfaceStateBaseAddress());
    EXPECT_EQ((uintptr_t)pIOH->getCpuBase(), cmd->getIndirectObjectBaseAddress());
    EXPECT_EQ((uintptr_t)pIH->getCpuBase(), cmd->getInstructionBaseAddress());

    // Verify all sizes are getting programmed
    EXPECT_TRUE(cmd->getDynamicStateBufferSizeModifyEnable());
    EXPECT_TRUE(cmd->getGeneralStateBufferSizeModifyEnable());
    EXPECT_TRUE(cmd->getIndirectObjectBufferSizeModifyEnable());
    EXPECT_TRUE(cmd->getInstructionBufferSizeModifyEnable());

    EXPECT_EQ(pDSH->getMaxAvailableSpace(), cmd->getDynamicStateBufferSize() * MemoryConstants::pageSize);
    EXPECT_NE(0u, cmd->getGeneralStateBufferSize());
    EXPECT_EQ(pIOH->getMaxAvailableSpace(), cmd->getIndirectObjectBufferSize() * MemoryConstants::pageSize);
    EXPECT_EQ(pIH->getMaxAvailableSpace(), cmd->getInstructionBufferSize() * MemoryConstants::pageSize);

    // Generically validate this command
    FamilyType::PARSE::template validateCommand<STATE_BASE_ADDRESS *>(cmdList.begin(), itorStateBaseAddress);
}

HWTEST_F(EnqueueCopyBufferRectTest, 3D_MediaInterfaceDescriptorLoad) {
    typedef typename FamilyType::MEDIA_INTERFACE_DESCRIPTOR_LOAD MEDIA_INTERFACE_DESCRIPTOR_LOAD;
    typedef typename FamilyType::INTERFACE_DESCRIPTOR_DATA INTERFACE_DESCRIPTOR_DATA;

    enqueueCopyBufferRect3D<FamilyType>();

    auto *cmd = (MEDIA_INTERFACE_DESCRIPTOR_LOAD *)cmdMediaInterfaceDescriptorLoad;
    ASSERT_NE(nullptr, cmd);

    // Verify we have a valid length -- multiple of INTERFACE_DESCRIPTOR_DATAs
    EXPECT_EQ(0u, cmd->getInterfaceDescriptorTotalLength() % sizeof(INTERFACE_DESCRIPTOR_DATA));

    // Validate the start address
    size_t alignmentStartAddress = 64 * sizeof(uint8_t);
    EXPECT_EQ(0u, cmd->getInterfaceDescriptorDataStartAddress() % alignmentStartAddress);

    // Validate the length
    EXPECT_NE(0u, cmd->getInterfaceDescriptorTotalLength());
    size_t alignmentTotalLength = 32 * sizeof(uint8_t);
    EXPECT_EQ(0u, cmd->getInterfaceDescriptorTotalLength() % alignmentTotalLength);

    // Generically validate this command
    FamilyType::PARSE::template validateCommand<MEDIA_INTERFACE_DESCRIPTOR_LOAD *>(cmdList.begin(), itorMediaInterfaceDescriptorLoad);
}

HWTEST_F(EnqueueCopyBufferRectTest, 3D_InterfaceDescriptorData) {
    typedef typename FamilyType::MEDIA_INTERFACE_DESCRIPTOR_LOAD MEDIA_INTERFACE_DESCRIPTOR_LOAD;
    typedef typename FamilyType::STATE_BASE_ADDRESS STATE_BASE_ADDRESS;
    typedef typename FamilyType::INTERFACE_DESCRIPTOR_DATA INTERFACE_DESCRIPTOR_DATA;

    enqueueCopyBufferRect3D<FamilyType>();

    auto *cmdSBA = (STATE_BASE_ADDRESS *)cmdStateBaseAddress;
    auto &IDD = *(INTERFACE_DESCRIPTOR_DATA *)cmdInterfaceDescriptorData;

    // Validate the kernel start pointer.  Technically, a kernel can start at address 0 but let's force a value.
    auto kernelStartPointer = ((uint64_t)IDD.getKernelStartPointerHigh() << 32) + IDD.getKernelStartPointer();
    EXPECT_LE(kernelStartPointer, cmdSBA->getInstructionBufferSize() * MemoryConstants::pageSize);

    EXPECT_NE(0u, IDD.getNumberOfThreadsInGpgpuThreadGroup());
    EXPECT_NE(0u, IDD.getCrossThreadConstantDataReadLength());
    EXPECT_NE(0u, IDD.getConstantIndirectUrbEntryReadLength());
}

HWTEST_F(EnqueueCopyBufferRectTest, 3D_PipelineSelect) {
    enqueueCopyBufferRect3D<FamilyType>();
    int numCommands = getNumberOfPipelineSelectsThatEnablePipelineSelect<FamilyType>();
    EXPECT_EQ(1, numCommands);
}

HWTEST_F(EnqueueCopyBufferRectTest, 3D_MediaVFEState) {
    typedef typename FamilyType::MEDIA_VFE_STATE MEDIA_VFE_STATE;

    enqueueCopyBufferRect3D<FamilyType>();

    auto cmd = (MEDIA_VFE_STATE *)cmdMediaVfeState;
    ASSERT_NE(nullptr, cmd);

    // Verify we have a valid length
    EXPECT_EQ(pDevice->getHardwareInfo().pSysInfo->ThreadCount, cmd->getMaximumNumberOfThreads());
    EXPECT_NE(0u, cmd->getNumberOfUrbEntries());
    EXPECT_NE(0u, cmd->getUrbEntryAllocationSize());

    // Generically validate this command
    FamilyType::PARSE::template validateCommand<MEDIA_VFE_STATE *>(cmdList.begin(), itorMediaVfeState);
}
