#include "ArdaTestComputeOperand.h"

namespace arda
{
    namespace
    {
        const char* AddPtx = R"ptx(.version 8.0
.target sm_70
.address_size 64
.visible .entry add_values(.param .u64 src, .param .u64 dst, .param .u32 count, .param .u32 bias)
{
 .reg .pred p; .reg .b32 r<6>; .reg .b64 a<5>;
 ld.param.u64 a0,[src]; ld.param.u64 a1,[dst]; ld.param.u32 r0,[count]; ld.param.u32 r1,[bias];
 mov.u32 r2,%ctaid.x; mov.u32 r3,%ntid.x; mov.u32 r4,%tid.x;
 mad.lo.u32 r2,r2,r3,r4; setp.ge.u32 p,r2,r0; @p bra done;
 mul.wide.u32 a2,r2,4; add.u64 a3,a0,a2; add.u64 a4,a1,a2;
 ld.global.u32 r5,[a3]; add.u32 r5,r5,r1; st.global.u32 [a4],r5;
done: ret;
})ptx";

        FArdaRHIStatus ValidateAddParameters(const FArdaAddParameters& P)
        {
            if (!P.mCount || !P.mInput.mBuffer || !P.mOutput.mBuffer)
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Add requires buffers and a nonzero count.");
            for (const auto* View : {&P.mInput, &P.mOutput})
            {
                const uint64_t Size = View->mBuffer->GetDesc().mByteSize;
                const auto& R = View->mRange;
                if (R.mByteOffset >= Size || R.mByteOffset % 4 ||
                    (R.mByteSize != ArdaRHIWholeBuffer && (!R.mByteSize || R.mByteSize > Size - R.mByteOffset)) ||
                    R.Resolve(View->mBuffer->GetDesc()).mByteSize != uint64_t(P.mCount) * 4)
                    return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Add requires one uint32 per element in each aligned view.");
            }
            if (!P.mInputShape.empty() || !P.mOutputShape.empty())
            {
                if (P.mInputShape.size() != 2 || P.mInputShape != P.mOutputShape ||
                    !P.mInputShape[0] || !P.mInputShape[1] || P.mInputShape[0] > UINT32_MAX / P.mInputShape[1] ||
                    P.mInputShape[0] * P.mInputShape[1] != P.mCount)
                    return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Add requires matching rank-two shapes consistent with count.");
            }
            return {};
        }
    }

    const char* GetArdaTestAddPtx() { return AddPtx; }

    FArdaAddOperand::FArdaAddOperand(FArdaRHIDeviceRef Device, FArdaAddShaderDispatch Graphics,
        eastl::vector<eastl::shared_ptr<const FArdaCudaModule>> Modules)
        : mDevice(eastl::move(Device)), mGraphics(eastl::move(Graphics)), mModules(eastl::move(Modules)) {}

    FArdaRHIStatus FArdaAddOperand::GetOperandSupport() const
    {
        if (!mDevice) return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Add has no device.");
        const auto C = mDevice->GetCudaCapabilities();
        if ((mGraphics && mbPreferGraphics) || (C && C.mComputeCapability >= 70) || mGraphics) return {};
        return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Add needs CUDA SM 70+ or its supplied graphics implementation.");
    }

    FArdaRHIStatus FArdaAddOperand::Dispatch(const FArdaAddParameters& P)
    {
        mLastSubmission = 0;
        if (auto S = GetOperandSupport(); !S) return S;
        if (auto S = ValidateAddParameters(P); !S) return S;
        auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
        if (!Commands) return Commands.mStatus;
        if (auto S = Commands.mValue->Open(); !S) return S;
        if (auto S = DispatchDeferred(*Commands.mValue, P); !S) return S;
        if (auto S = Commands.mValue->Close(); !S) return S;
        auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
        if (!Submitted) return Submitted.mStatus;
        mLastSubmission = Submitted.mValue;
        return {};
    }

    FArdaRHIStatus FArdaAddOperand::DispatchDeferred(IArdaRHICommandList& Commands, const FArdaAddParameters& P)
    {
        mLastImplementation.clear();
        mLastBlockSize = 0;
        if (auto S = GetOperandSupport(); !S) return S;
        if (auto S = ValidateAddParameters(P); !S) return S;
        if (Commands.GetDevice() != mDevice.Get())
            return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice, "Add command list belongs to another device.");
        if (Commands.GetQueueType() == EArdaRHIQueueType::Copy)
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Add cannot execute on a copy queue.");

        // Support, shape policy, tiling and fallback are normal user C++ code here.
        const auto C = mDevice->GetCudaCapabilities();
        const bool CanUseCuda = C && C.mComputeCapability >= 70 &&
            (C.mLaunchMode != EArdaCudaLaunchMode::D3D12CiG || Commands.GetQueueType() == EArdaRHIQueueType::Graphics) &&
            P.mInput.mBuffer->GetCudaResourceInfo().mbSharingEnabled && P.mOutput.mBuffer->GetCudaResourceInfo().mbSharingEnabled;
        if (mbPreferGraphics || !CanUseCuda)
        {
            if (!mGraphics) return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Add has no compatible graphics implementation.");
            auto Status = mGraphics(Commands, P);
            if (Status) mLastImplementation = "graphics.add";
            return Status;
        }
        const uint32_t Block = !P.mInputShape.empty() && P.mInputShape[1] <= 64 ? 32 : 128;
        FArdaCudaKernel First;
        First.mPtx = AddPtx;
        First.mEntryPoint = "add_values";
        First.mBlockSize[0] = Block;
        First.mGridSize[0] = 1 + (P.mCount - 1) / Block;
        First.mArguments = {FArdaCudaArgument::Binding(0), FArdaCudaArgument::Binding(1),
            FArdaCudaArgument::Value(P.mCount), FArdaCudaArgument::Value(7u)};
        auto Second = First;
        Second.mArguments[0] = FArdaCudaArgument::Binding(1);
        Second.mArguments[3] = FArdaCudaArgument::Value(11u);
        if (!mModules.empty())
        {
            // Optional runtime compilation, not a restriction of the operand interface.
            if (mModules.size() != 2 || !mModules[0] || !mModules[1])
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "This sample expects two source modules.");
            auto A = mModules[0]->GetPtx(C.mComputeCapability);
            if (!A) return A.mStatus;
            auto B = mModules[1]->GetPtx(C.mComputeCapability);
            if (!B) return B.mStatus;
            First.mPtx = eastl::move(A.mValue);
            Second.mPtx = eastl::move(B.mValue);
            Second.mEntryPoint = "finish_values";
        }
        eastl::vector<FArdaComputeResourceAccess> Accesses;
        if (auto S = GetParameterMetadata().GetResourceAccesses(&P, Accesses); !S) return S;
        FArdaCudaDispatch Work;
        for (const auto& A : Accesses) Work.mBindings.push_back({A.mResource, A.mAccess, A.mBufferRange});
        Work.mKernels = {eastl::move(First), eastl::move(Second)};
        // The RHI validates/records this sequence; it retains submitted dependencies.
        // A library-backed implementation can use its own direct Dispatch instead.
        auto Status = Commands.DispatchCuda(Work);
        if (Status) { mLastBlockSize = Block; mLastImplementation = "cuda.sequence"; }
        return Status;
    }
}
