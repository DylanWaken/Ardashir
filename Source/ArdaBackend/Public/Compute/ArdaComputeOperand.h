/** @file ArdaComputeOperand.h
 * Defines a reusable operation with one resource contract and selectable CUDA or
 * graphics implementations. Registration/scheduling of RDG nodes is separate.
 */
#pragma once
#include "RHI/ArdaRHIDevice.h"

namespace arda::backend
{
    /** Ordered input/output declaration shared by every implementation of an operand. */
    struct FArdaComputePort
    {
        /** Unique, nonempty name for diagnostics and application binding conventions. */
        eastl::string mName;
        /** Buffer range or texture surface expected at this port's index. */
        rhi::EArdaComputeBindingType mType = rhi::EArdaComputeBindingType::Buffer;
        /** Authoritative read/write declaration, applied to the caller's binding copy. */
        rhi::EArdaComputeAccess mAccess = rhi::EArdaComputeAccess::Read;
        /** Minimum resolved byte count for a buffer port. */
        uint64_t mMinimumBytes = 1;
        /** Required buffer byte-offset alignment; must be nonzero. */
        uint32_t mAlignment = 1;
        /** Required surface storage format, or Unknown to accept any format. */
        rhi::EArdaRHIFormat mTextureFormat = rhi::EArdaRHIFormat::Unknown;
        /** Required surface dimension, or Unknown to accept any dimension. */
        rhi::EArdaRHITextureDimension mTextureDimension = rhi::EArdaRHITextureDimension::Unknown;
    };

    /** Per-call resources, application-defined parameter bytes and nonzero logical work extent. */
    struct FArdaComputeInvocation
    {
        /** Exactly one retained binding per port, in declaration order. */
        eastl::vector<rhi::FArdaCudaBinding> mBindings;
        /** Owned operand parameters. The derived operand defines and validates their schema. */
        eastl::vector<eastl::vector<uint8_t>> mParameters;
        /** Logical work size; the selected implementation chooses its own grid/block tiling. */
        uint32_t mExtent[3] = {1, 1, 1};
    };

    /** Filters implementation families before tuning or kernel-building callbacks run. */
    enum class EArdaComputePolicy : uint8_t
    {
        /** Admit either family; the default selector chooses the first eligible registration. */
        Auto,
        /** Exclude graphics alternatives; unavailable CUDA yields Unsupported. */
        RequireCuda,
        /** Exclude CUDA alternatives even when the device supports them. */
        RequireGraphics
    };
    /** Builds an ordered PTX launch sequence without recording or mutating live resources. */
    using FArdaComputeKernelBuilder = eastl::function<rhi::TArdaRHIResult<
        eastl::vector<rhi::FArdaCudaKernel>>(const FArdaComputeInvocation&)>;
    /** Records the graphics alternative, including its transitions, pipeline and resource bindings. */
    using FArdaComputeShaderDispatch = eastl::function<rhi::FArdaRHIStatus(
        rhi::IArdaRHICommandList&, const FArdaComputeInvocation&)>;

    /** Inherit to define an operand. Ports are the single binding contract for all variants.
     * Registration occurs in the derived constructor; dispatch is read-only and thread-safe
     * if the supplied callbacks are. Selection must not benchmark or mutate live inputs.
     * @document-protected The protected registration and tuning hooks are author-facing API.
     */
    class FArdaComputeOperand
    {
    public:
        /** Releases registrations; callers must finish concurrent dispatch callbacks first. */
        virtual ~FArdaComputeOperand() = default;
        /** Returns the stable operation name supplied by the derived constructor. */
        [[nodiscard]] const eastl::string& GetName() const noexcept { return mName; }
        /** Returns the immutable ordered binding contract. */
        [[nodiscard]] const eastl::vector<FArdaComputePort>& GetPorts() const noexcept { return mPorts; }
        /**
         * Validates ports and selects one implementation for an open command list.
         * @param Commands Command list on this invocation's device; recording is caller-serialized.
         * @param Invocation Resources and parameters; access declarations are copied from ports.
         * @param Policy Eligible implementation family; Auto preserves registration preference.
         * @param PinnedVariant Optional exact registration name; pinning never bypasses eligibility.
         * @return Selected name on success, InvalidArgument for malformed input/selection, or
         * Unsupported when no variant qualifies. A recording failure is propagated without retry.
         */
        [[nodiscard]] rhi::TArdaRHIResult<eastl::string> Dispatch(
            rhi::IArdaRHICommandList& Commands, const FArdaComputeInvocation& Invocation,
            EArdaComputePolicy Policy = EArdaComputePolicy::Auto,
            const char* PinnedVariant = nullptr) const;

    protected:
        /** Immutable registration visible to custom tuning selectors after construction. */
        struct FVariant
        {
            /** Unique variant name used by pinning and tuning caches. */
            eastl::string mName;
            /** Inclusive CUDA SM lower bound, encoded as major * 10 + minor. */
            uint32_t mMinimumArchitecture = 0;
            /** Inclusive CUDA SM upper bound; UINT32_MAX leaves it unbounded. */
            uint32_t mMaximumArchitecture = UINT32_MAX;
            /** CUDA builder, populated only for CUDA variants. */
            FArdaComputeKernelBuilder mCuda;
            /** Graphics recorder, populated only for graphics variants. */
            FArdaComputeShaderDispatch mGraphics;
        };
        /** Stores the operation's single binding contract; register variants before publishing it. */
        FArdaComputeOperand(eastl::string Name, eastl::vector<FArdaComputePort> Ports)
            : mName(eastl::move(Name)), mPorts(eastl::move(Ports)) {}
        /** Registers a CUDA builder without invoking it or requiring a CUDA-enabled device.
         * @param Name Nonempty variant name, unique across CUDA and graphics registrations.
         * @param MinimumArchitecture Inclusive SM lower bound, encoded as major * 10 + minor.
         * @param MaximumArchitecture Inclusive SM upper bound; UINT32_MAX removes the upper bound.
         * @param Builder Nonempty callback that builds ordered kernels from a validated invocation.
         * @return Success after storage; InvalidArgument for an empty/duplicate name, empty
         * callback, or MinimumArchitecture greater than MaximumArchitecture. Rejection adds nothing.
         * @ownership The callback is moved into the operand. Value captures live until operand
         * destruction; reference captures must outlive every dispatch. The invocation is borrowed
         * only for the callback duration and must not be retained by reference.
         * @threading Register during construction before publishing the operand. Registration
         * must not race with registration, selection or dispatch; callbacks must support any
         * concurrent dispatches the application permits, on separately serialized command lists.
         * @errors Registration validates metadata only; PTX and native launch failures occur at dispatch.
         */
        [[nodiscard]] rhi::FArdaRHIStatus RegisterCuda(eastl::string Name,
            uint32_t MinimumArchitecture, uint32_t MaximumArchitecture,
            FArdaComputeKernelBuilder Builder);
        /** Registers a compute-shader alternative usable without CUDA.
         * @param Name Nonempty variant name, unique across both implementation families.
         * @param Dispatch Nonempty callback responsible for transitions, pipeline/binding setup and recording.
         * @return Success after storage; InvalidArgument for an empty/duplicate name or empty callback.
         * @ownership The operand owns the moved callback and its value captures. Reference captures
         * must outlive its dispatches; recorded GPU dependencies must survive queue completion.
         * @threading Register before publishing; do not race registration with dispatch. Callback
         * code must support the application's concurrency and serialize each command list.
         * @errors Callback failure is propagated by Dispatch without trying another implementation.
         */
        [[nodiscard]] rhi::FArdaRHIStatus RegisterGraphics(eastl::string Name,
            FArdaComputeShaderDispatch Dispatch);
        /** Check operation-specific shapes and parameters before either implementation runs. */
        virtual rhi::FArdaRHIStatus ValidateInvocation(const FArdaComputeInvocation&) const { return {}; }
        /** Override to select a named eligible variant using an external tuning cache.
         * Returning an ineligible index is rejected. The default preserves registration order. */
        virtual size_t SelectVariant(const FArdaComputeInvocation&,
            const rhi::FArdaCudaCapabilities&,
            const eastl::vector<size_t>& Eligible) const { return Eligible.front(); }
        /** Read-only variant metadata; indices correspond to SelectVariant's eligible indices. */
        [[nodiscard]] const eastl::vector<FVariant>& GetVariants() const noexcept { return mVariants; }

    private:
        rhi::FArdaRHIStatus Register(FVariant Variant);
        eastl::string mName;
        eastl::vector<FArdaComputePort> mPorts;
        eastl::vector<FVariant> mVariants;
    };
}
