/** @file ArdaComputeOperand.cpp
 * Resolves one immutable operand contract into an eligible native implementation.
 * Selection precedes recording so fallback never repeats partially recorded GPU work.
 */
#include "Compute/ArdaComputeOperand.h"
#include <EASTL/algorithm.h>

namespace arda::backend
{
    using namespace rhi;
    FArdaRHIStatus FArdaComputeOperand::Register(FVariant V)
    {
        if (V.mName.empty() || (!V.mCuda && !V.mGraphics) ||
            V.mMinimumArchitecture > V.mMaximumArchitecture ||
            eastl::any_of(mVariants.begin(), mVariants.end(), [&](const auto& Other) { return Other.mName == V.mName; }))
            return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Compute variant is empty, duplicated or has invalid architecture limits.");
        mVariants.push_back(eastl::move(V));
        return {};
    }
    FArdaRHIStatus FArdaComputeOperand::RegisterCuda(eastl::string Name,
        uint32_t Minimum, uint32_t Maximum, FArdaComputeKernelBuilder Builder)
    { return Register({eastl::move(Name), Minimum, Maximum, eastl::move(Builder), {}}); }
    FArdaRHIStatus FArdaComputeOperand::RegisterGraphics(eastl::string Name, FArdaComputeShaderDispatch Dispatch)
    { return Register({eastl::move(Name), 0, UINT32_MAX, {}, eastl::move(Dispatch)}); }

    TArdaRHIResult<eastl::string> FArdaComputeOperand::Dispatch(IArdaRHICommandList& Commands,
        const FArdaComputeInvocation& Input, EArdaComputePolicy Policy, const char* Pinned) const
    {
        const auto Invalid = [](const char* Reason) -> TArdaRHIResult<eastl::string>
        { return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Reason)}; };
        if (mName.empty() || Policy > EArdaComputePolicy::RequireGraphics ||
            Input.mBindings.size() != mPorts.size() || !Input.mExtent[0] || !Input.mExtent[1] || !Input.mExtent[2])
            return Invalid("Compute invocation does not match the operand's ports or extent.");
        // Ports own access policy. Copying keeps caller bindings unchanged and gives
        // CUDA builders and graphics alternatives exactly the same validated inputs.
        auto Invocation = Input;
        bool ResourcesSupportCuda = true;
        for (size_t I = 0; I < mPorts.size(); ++I)
        {
            const auto& Port = mPorts[I];
            auto& Binding = Invocation.mBindings[I];
            Binding.mAccess = Port.mAccess;
            if (!Binding.mResource || Port.mName.empty() || !Port.mAlignment ||
                Port.mType > EArdaComputeBindingType::Surface || Port.mAccess > EArdaComputeAccess::ReadWrite ||
                eastl::any_of(mPorts.begin(), mPorts.begin() + I, [&](const auto& P) { return P.mName == Port.mName; }))
                return Invalid("Compute port or resource is invalid.");
            if (Port.mType == EArdaComputeBindingType::Buffer)
            {
                auto* Buffer = dynamic_cast<IArdaRHIBuffer*>(Binding.mResource.Get());
                if (!Buffer) return Invalid("Compute port requires a buffer.");
                const auto& D = Buffer->GetDesc();
                const auto& Range = Binding.mBufferRange;
                if (Range.mByteOffset >= D.mByteSize || (Range.mByteSize != ArdaRHIWholeBuffer &&
                    (!Range.mByteSize || Range.mByteSize > D.mByteSize - Range.mByteOffset)))
                    return Invalid("Compute buffer range is out of bounds.");
                const auto R = Binding.mBufferRange.Resolve(D);
                if (R.mByteOffset > D.mByteSize || R.mByteSize > D.mByteSize - R.mByteOffset ||
                    R.mByteSize < Port.mMinimumBytes || R.mByteOffset % Port.mAlignment)
                    return Invalid("Compute buffer range or alignment violates the port contract.");
            }
            else
            {
                auto* Texture = dynamic_cast<IArdaRHITexture*>(Binding.mResource.Get());
                if (!Texture || Binding.mMipLevel >= Texture->GetDesc().mMipLevels)
                    return Invalid("Compute port requires a valid texture mip.");
                if ((Port.mTextureFormat != EArdaRHIFormat::Unknown && Port.mTextureFormat != Texture->GetDesc().mFormat) ||
                    (Port.mTextureDimension != EArdaRHITextureDimension::Unknown && Port.mTextureDimension != Texture->GetDesc().mDimension))
                    return Invalid("Compute texture format or dimension violates the port contract.");
            }
            ResourcesSupportCuda &= Binding.mResource->GetCudaResourceInfo().mbSharingEnabled;
        }
        const auto Capabilities = Commands.GetDevice()->GetCudaCapabilities();
        const bool QueueSupportsCuda = Commands.GetQueueType() != EArdaRHIQueueType::Copy &&
            (Capabilities.mLaunchMode != EArdaCudaLaunchMode::D3D12CiG || Commands.GetQueueType() == EArdaRHIQueueType::Graphics);
        if (auto Status = ValidateInvocation(Invocation); !Status) return {{}, eastl::move(Status)};
        // Filter before invoking user code: a CUDA-disabled device or ordinary
        // graphics allocation must never execute a CUDA-only builder as a probe.
        eastl::vector<size_t> Eligible;
        for (size_t I = 0; I < mVariants.size(); ++I)
        {
            const auto& V = mVariants[I];
            if (Pinned && V.mName != Pinned) continue;
            if (V.mCuda && Policy != EArdaComputePolicy::RequireGraphics && Capabilities && QueueSupportsCuda && ResourcesSupportCuda &&
                Capabilities.mComputeCapability >= V.mMinimumArchitecture && Capabilities.mComputeCapability <= V.mMaximumArchitecture)
                Eligible.push_back(I);
            if (V.mGraphics && Policy != EArdaComputePolicy::RequireCuda) Eligible.push_back(I);
        }
        if (Eligible.empty()) return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
            "No compute variant satisfies the requested policy, architecture and resource representations.")};
        // Tuning selects among valid choices; it cannot bypass resource/SM admission.
        const size_t Index = SelectVariant(Invocation, Capabilities, Eligible);
        if (eastl::find(Eligible.begin(), Eligible.end(), Index) == Eligible.end())
            return Invalid("Compute tuning policy selected an ineligible variant.");
        const auto& V = mVariants[Index];
        FArdaRHIStatus Status;
        if (V.mCuda)
        {
            auto Kernels = V.mCuda(Invocation);
            if (!Kernels) return {{}, Kernels.mStatus};
            Status = Commands.DispatchCuda({Invocation.mBindings, eastl::move(Kernels.mValue)});
        }
        else Status = V.mGraphics(Commands, Invocation);
        return {Status ? V.mName : eastl::string{}, eastl::move(Status)};
    }
}
