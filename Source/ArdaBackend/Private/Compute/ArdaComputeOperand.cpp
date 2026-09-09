/** @file ArdaComputeOperand.cpp
 * Resolves one immutable operand contract into an eligible native implementation.
 * Selection precedes recording so fallback never repeats partially recorded GPU work.
 */
#include "Compute/ArdaComputeOperand.h"
#include "RHI/ArdaRHICudaValidation.h"

#include <EASTL/algorithm.h>

namespace arda
{
    namespace
    {
        bool HasValidCudaImplementation(const FArdaComputeCudaImplementation& Implementation)
        {
            return Implementation.mInvoke && !Implementation.mModules.empty() &&
                eastl::all_of(Implementation.mModules.begin(), Implementation.mModules.end(),
                    [](const auto& Module) { return bool(Module); });
        }
    }

    TArdaRHIResult<eastl::vector<FArdaCudaKernel>> BuildArdaComputeCudaKernels(
        const FArdaComputeCudaImplementation& Implementation, const FArdaComputeInvocation& Invocation,
        const FArdaCudaCapabilities& Capabilities)
    {
        if (!Capabilities)
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                Capabilities.mUnavailableReason.c_str())};
        }
        if (!HasValidCudaImplementation(Implementation))
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                "CUDA implementation requires source modules and an invocation entry point.")};
        }
        auto Launches = Implementation.mInvoke(Invocation, Capabilities);
        if (!Launches)
        {
            return {{}, eastl::move(Launches.mStatus)};
        }
        if (Launches.mValue.empty())
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "CUDA dispatch has no kernels.")};
        }
        eastl::vector<FArdaCudaKernel> Kernels;
        Kernels.reserve(Launches.mValue.size());
        for (auto& Launch : Launches.mValue)
        {
            if (Launch.mModuleIndex >= Implementation.mModules.size())
            {
                return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                    "CUDA invocation selected an invalid source module index.")};
            }
            if (auto Status = ValidateArdaCudaLaunch(Launch, Invocation.mBindings.size(), Capabilities); !Status)
            {
                return {{}, eastl::move(Status)};
            }
            FArdaCudaKernel Kernel;
            Kernel.mEntryPoint = eastl::move(Launch.mEntryPoint);
            for (size_t Axis = 0; Axis < 3; ++Axis)
            {
                Kernel.mGridSize[Axis] = Launch.mGridSize[Axis];
                Kernel.mBlockSize[Axis] = Launch.mBlockSize[Axis];
            }
            Kernel.mSharedMemoryBytes = Launch.mSharedMemoryBytes;
            Kernel.mArguments = eastl::move(Launch.mArguments);
            Kernels.push_back(eastl::move(Kernel));
        }
        for (size_t Index = 0; Index < Kernels.size(); ++Index)
        {
            auto Ptx = Implementation.mModules[Launches.mValue[Index].mModuleIndex]->GetPtx(
                Capabilities.mComputeCapability);
            if (!Ptx)
            {
                return {{}, eastl::move(Ptx.mStatus)};
            }
            Kernels[Index].mPtx = eastl::move(Ptx.mValue);
        }
        return {eastl::move(Kernels), {}};
    }

    FArdaRHIStatus FArdaComputeOperand::Register(FVariant V)
    {
        const bool bModularCuda = HasValidCudaImplementation(V.mCudaImplementation);
        if (V.mName.empty() || V.mName.find('\0') != eastl::string::npos ||
            unsigned(bool(V.mCuda)) + unsigned(bool(V.mGraphics)) + unsigned(bModularCuda) != 1 ||
            V.mMinimumArchitecture > V.mMaximumArchitecture ||
            eastl::any_of(mVariants.begin(), mVariants.end(), [&](const auto& Other) { return Other.mName == V.mName; }))
            return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Compute variant is empty, duplicated or has invalid architecture limits.");
        mVariants.push_back(eastl::move(V));
        return {};
    }
    FArdaRHIStatus FArdaComputeOperand::RegisterCuda(eastl::string Name,
        uint32_t Minimum, uint32_t Maximum, FArdaComputeKernelBuilder Builder,
        FArdaComputeVariantSupport Supports)
    {
        return Register({eastl::move(Name), Minimum, Maximum, eastl::move(Builder), {}, {}, eastl::move(Supports)});
    }
    FArdaRHIStatus FArdaComputeOperand::RegisterCuda(eastl::string Name,
        uint32_t Minimum, uint32_t Maximum, FArdaComputeCudaImplementation Implementation,
        FArdaComputeVariantSupport Supports)
    {
        return Register({eastl::move(Name), Minimum, Maximum, {}, {}, eastl::move(Implementation), eastl::move(Supports)});
    }
    FArdaRHIStatus FArdaComputeOperand::RegisterGraphics(eastl::string Name,
        FArdaComputeShaderDispatch Dispatch, FArdaComputeVariantSupport Supports)
    {
        return Register({eastl::move(Name), 0, UINT32_MAX, {}, eastl::move(Dispatch), {}, eastl::move(Supports)});
    }

    TArdaRHIResult<eastl::string> FArdaComputeOperand::Dispatch(IArdaRHICommandList& Commands,
        const FArdaComputeInvocation& Input, EArdaComputePolicy Policy, const char* Pinned) const
    {
        const auto Invalid = [](const char* Reason) -> TArdaRHIResult<eastl::string>
        { return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Reason)}; };
        if (mName.empty() || Policy > EArdaComputePolicy::RequireGraphics ||
            Input.mBindings.size() != mPorts.size() || !Input.mExtent[0] || !Input.mExtent[1] || !Input.mExtent[2] ||
            (!Input.mBindingDimensions.empty() && Input.mBindingDimensions.size() != mPorts.size()))
            return Invalid("Compute invocation does not match the operand's ports or extent.");
        for (const auto& Dimensions : Input.mBindingDimensions)
        {
            if (eastl::find(Dimensions.begin(), Dimensions.end(), uint64_t(0)) != Dimensions.end())
            {
                return Invalid("Compute binding dimensions must be nonzero.");
            }
        }
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
        if (!Commands.GetDevice())
        {
            return Invalid("Compute invocation requires a command-list device.");
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
            const bool bCuda = V.mCuda || V.mCudaImplementation.mInvoke;
            const bool bEligible = bCuda ?
                Policy != EArdaComputePolicy::RequireGraphics && Capabilities && QueueSupportsCuda && ResourcesSupportCuda &&
                    Capabilities.mComputeCapability >= V.mMinimumArchitecture && Capabilities.mComputeCapability <= V.mMaximumArchitecture :
                Policy != EArdaComputePolicy::RequireCuda;
            if (bEligible && (!V.mSupports || V.mSupports(Invocation, Capabilities)))
            {
                Eligible.push_back(I);
            }
        }
        if (Eligible.empty()) return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
            "No compute variant satisfies the requested policy, architecture, resources and input dimensions.")};
        // Tuning selects among valid choices; it cannot bypass resource/SM admission.
        const size_t Index = SelectVariant(Invocation, Capabilities, Eligible);
        if (eastl::find(Eligible.begin(), Eligible.end(), Index) == Eligible.end())
            return Invalid("Compute tuning policy selected an ineligible variant.");
        const auto& V = mVariants[Index];
        FArdaRHIStatus Status;
        if (V.mCuda || V.mCudaImplementation.mInvoke)
        {
            auto Kernels = V.mCuda ? V.mCuda(Invocation) :
                BuildArdaComputeCudaKernels(V.mCudaImplementation, Invocation, Capabilities);
            if (!Kernels) return {{}, Kernels.mStatus};
            if (auto Validation = ValidateArdaCudaKernels(Kernels.mValue, Invocation.mBindings.size(), Capabilities); !Validation)
            {
                return {{}, eastl::move(Validation)};
            }
            Status = Commands.DispatchCuda({Invocation.mBindings, eastl::move(Kernels.mValue)});
        }
        else Status = V.mGraphics(Commands, Invocation);
        return {Status ? V.mName : eastl::string{}, eastl::move(Status)};
    }
}
