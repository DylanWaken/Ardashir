#pragma once
#include "ArdaCudaParameters.h"
#include "RHI/ArdaRHIDevice.h"
#include <utility>

namespace arda
{
    /** Algorithm restrictions, independent of native binary coverage. */
    struct FArdaCudaKernelRequirements
    {
        uint32_t mMinimumComputeCapability = 0;
        /** Bit mask over EArdaCudaLaunchMode; zero accepts all admitted modes. */
        uint32_t mLaunchModes = 0;
    };
    /** Each compiled symbol has its own wrapper type and user-owned host policy payload. */
    template<auto Kernel, class Payload> struct TArdaCudaKernelVariant
    {
        eastl::shared_ptr<const IArdaCudaKernelEntry> mEntry;
        Payload mPayload;
        eastl::string mName;
        FArdaCudaKernelRequirements mRequirements;
    };
    /** Immutable-after-binding registry; signature and resource eligibility checks occur at runtime. */
    template<class Parameters, class Payload> class TArdaCudaKernelRegistry
    {
    public:
        struct FVariant
        {
            uint32_t mId = 0;
            eastl::shared_ptr<const IArdaCudaKernelEntry> mEntry;
            Payload mPayload;
            eastl::string mName;
            FArdaCudaKernelRequirements mRequirements;
        };
        using FVariants = eastl::vector<FVariant>;
        /** Adds a symbol, retaining the first validation failure even if the caller ignores it. */
        template<auto Kernel> FArdaRHIStatus Add(TArdaCudaKernelVariant<Kernel, Payload> Variant)
        {
            if (mbFrozen) return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "CUDA registry is immutable after binding.");
            if (!mStatus) return mStatus;
            const auto Fail = [&](const char* Why) { return mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Why); };
            const auto& Metadata = Parameters::GetCudaMetadata();
            if (!Metadata.GetStatus()) return mStatus = Metadata.GetStatus();
            if (!Variant.mEntry || Variant.mName.empty()) return Fail("CUDA variant requires a compiled entry and diagnostic name.");
            const auto Actual = Variant.mEntry->GetSignature();
            const auto Expected = Metadata.GetSignature();
            if (!Actual.mbSupported || !Actual.mType || *Actual.mType != *Expected.mType ||
                Actual.mSize != Expected.mSize || Actual.mAlignment != Expected.mAlignment)
                return Fail("All CUDA variants must accept the operand's exact CUDA parameter struct by value.");
            const auto& Build = Variant.mEntry->GetBuildInfo();
            if (Build.mName.empty() || Build.mIdentity.empty() || Build.mArchitectures.empty())
                return Fail("CUDA variant is missing its build-generated native-code manifest.");
            for (const auto& A : Build.mArchitectures) if (!A.mComputeCapability) return Fail("Invalid compiled architecture.");
            for (const auto& V : mVariants) if (V.mName == Variant.mName) return Fail("Duplicate CUDA variant name.");
            mVariants.push_back({static_cast<uint32_t>(mVariants.size()), eastl::move(Variant.mEntry),
                eastl::move(Variant.mPayload), eastl::move(Variant.mName), Variant.mRequirements});
            return {};
        }
        /** Seals bindings without initializing CUDA. */
        FArdaRHIStatus Freeze()
        {
            mbFrozen = true;
            if (mStatus && mVariants.empty()) mStatus = FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "No precompiled CUDA variants were bound.");
            return mStatus;
        }
        const FArdaRHIStatus& GetStatus() const noexcept { return mStatus; }
        const FVariants& GetVariants() const noexcept { return mVariants; }
        /** Filters native targets and execution requirements without launching work. */
        FVariants GetCompatibleVariants(const FArdaCudaCapabilities& C) const
        {
            FVariants Result;
            if (!mStatus || !C) return Result;
            for (const auto& V : mVariants)
            {
                if (V.mRequirements.mMinimumComputeCapability > C.mComputeCapability ||
                    (V.mRequirements.mLaunchModes && !(V.mRequirements.mLaunchModes & (1u << uint32_t(C.mLaunchMode))))) continue;
                for (const auto& A : V.mEntry->GetBuildInfo().mArchitectures)
                    if (A.Supports(C.mComputeCapability)) { Result.push_back(V); break; }
            }
            return Result;
        }
    private:
        FVariants mVariants;
        FArdaRHIStatus mStatus;
        bool mbFrozen = false;
    };
    /** Enumerates template values with a C++17 callable. Use if constexpr to prune combinations. */
    template<class T, T... Values, class Visitor>
    void ForEachArdaCudaPermutation(std::integer_sequence<T, Values...>, Visitor&& Visit)
    { (Visit(std::integral_constant<T, Values>{}), ...); }
    /** One immutable selection decision. */
    struct FArdaCudaKernelSelection
    {
        uint32_t mVariantId = UINT32_MAX;
        FArdaCudaLaunchConfig mLaunch;
        /** Allowed only when the operand's mathematical contract permits an empty operation. */
        bool mbNoWork = false;
    };
    /** Host metadata available during selection; streams and addresses stay in the provider. */
    struct FArdaCudaSelectionContext
    {
        FArdaCudaCapabilities mCapabilities;
        EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
    };
    /** Queue completion identity. Zero denotes explicit NoWork. */
    struct FArdaCudaSubmission
    {
        FArdaRHIDeviceRef mDevice;
        EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
        uint64_t mInstance = 0;
        FArdaCudaKernelSelection mSelection;
    };
    /** Frozen values/resources/selection. Returned as shared_ptr<const> by operand preparation. */
    struct FArdaCudaDispatchPlan
    {
        FArdaRHIDeviceRef mDevice;
        EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
        FArdaCudaKernelSelection mSelection;
        FArdaCudaDispatch mDispatch;
    };
}
