/** @file ArdaComputeOperand.h
 * Defines a reusable operation with one resource contract and selectable CUDA or
 * graphics implementations. Registration/scheduling of RDG nodes is separate.
 */
#pragma once
#include "ArdaCudaModule.h"
#include "RHI/ArdaRHIDevice.h"

namespace arda
{
    /** Ordered input/output declaration shared by every implementation of an operand. */
    struct FArdaComputePort
    {
        /** Unique, nonempty name for diagnostics and application binding conventions. */
        eastl::string mName;
        /** Buffer range or texture surface expected at this port's index. */
        arda::EArdaComputeBindingType mType = arda::EArdaComputeBindingType::Buffer;
        /** Authoritative read/write declaration, applied to the caller's binding copy. */
        arda::EArdaComputeAccess mAccess = arda::EArdaComputeAccess::Read;
        /** Minimum resolved byte count for a buffer port. */
        uint64_t mMinimumBytes = 1;
        /** Required buffer byte-offset alignment; must be nonzero. */
        uint32_t mAlignment = 1;
        /** Required surface storage format, or Unknown to accept any format. */
        arda::EArdaRHIFormat mTextureFormat = arda::EArdaRHIFormat::Unknown;
        /** Required surface dimension, or Unknown to accept any dimension. */
        arda::EArdaRHITextureDimension mTextureDimension = arda::EArdaRHITextureDimension::Unknown;
    };

    /** Per-call resources, application-defined parameter bytes and nonzero logical work extent. */
    struct FArdaComputeInvocation
    {
        /** Exactly one retained binding per port, in declaration order. */
        eastl::vector<arda::FArdaCudaBinding> mBindings;
        /** Owned operand parameters. The derived operand defines and validates their schema. */
        eastl::vector<eastl::vector<uint8_t>> mParameters;
        /** Logical work size; the selected implementation chooses its own grid/block tiling. */
        uint32_t mExtent[3] = {1, 1, 1};
        /** Optional tensor dimensions per binding, in port order, independent of launch extent.
         * Empty outer vector omits shapes; otherwise provide one shape per binding. Each axis
         * must be nonzero; an empty inner vector denotes a scalar. ValidateInvocation must check
         * operand-specific rank, dtype/layout parameters and storage bounds before selection.
         */
        eastl::vector<eastl::vector<uint64_t>> mBindingDimensions;
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
    using FArdaComputeKernelBuilder = eastl::function<arda::TArdaRHIResult<
        eastl::vector<arda::FArdaCudaKernel>>(const FArdaComputeInvocation&)>;
    /** Records the graphics alternative, including its transitions, pipeline and resource bindings. */
    using FArdaComputeShaderDispatch = eastl::function<arda::FArdaRHIStatus(
        arda::IArdaRHICommandList&, const FArdaComputeInvocation&)>;
    /** Pure shape/parameter admission check, called only after family/device/resource filtering. */
    using FArdaComputeVariantSupport = eastl::function<bool(
        const FArdaComputeInvocation&, const FArdaCudaCapabilities&)>;
    /** Selects an eligible registration index from validated dimensions, parameters and device facts.
     * Must not record work or benchmark live inputs. Returning an ineligible index is rejected.
     */
    using FArdaComputeKernelSelector = eastl::function<size_t(
        const FArdaComputeInvocation&, const FArdaCudaCapabilities&, const eastl::vector<size_t>&)>;

    /** One invocation of an exported symbol in a reusable CUDA module. */
    struct FArdaComputeCudaLaunch
    {
        /** Index into the selected implementation's mModules. */
        uint32_t mModuleIndex = 0;
        /** Exported PTX symbol, normally an extern "C" CUDA C++ kernel name. */
        eastl::string mEntryPoint;
        /** Nonzero block count per axis, chosen from invocation dimensions. */
        uint32_t mGridSize[3] = {1, 1, 1};
        /** Nonzero threads per axis, subject to device axis and product limits. */
        uint32_t mBlockSize[3] = {1, 1, 1};
        /** Dynamic shared-memory bytes per block. */
        uint32_t mSharedMemoryBytes = 0;
        /** Ordered resource indices and owned value bytes matching the exported symbol's ABI. */
        eastl::vector<FArdaCudaArgument> mArguments;
    };
    /** Host invocation entry point: packs arguments and returns an ordered launch sequence.
     * Receives validated shapes and device limits; must not launch GPU work or retain input references.
     */
    using FArdaComputeCudaInvocation = eastl::function<TArdaRHIResult<
        eastl::vector<FArdaComputeCudaLaunch>>(const FArdaComputeInvocation&, const FArdaCudaCapabilities&)>;

    /** Modular CUDA implementation: source ownership and host invocation are independent. */
    struct FArdaComputeCudaImplementation
    {
        /** Nonempty retained modules; several variants may share one module and its compilation cache. */
        eastl::vector<eastl::shared_ptr<const FArdaCudaModule>> mModules;
        /** Required invocation entry point, run only after this implementation is selected. */
        FArdaComputeCudaInvocation mInvoke;
    };
    /** Prepares and validates a modular implementation without recording GPU work.
     * Checks the whole launch sequence before compiling only the referenced modules. Errors propagate
     * without trying another implementation. Direct callers must first validate their operand inputs.
     */
    [[nodiscard]] TArdaRHIResult<eastl::vector<FArdaCudaKernel>> BuildArdaComputeCudaKernels(
        const FArdaComputeCudaImplementation& Implementation, const FArdaComputeInvocation& Invocation,
        const FArdaCudaCapabilities& Capabilities);

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
        [[nodiscard]] arda::TArdaRHIResult<eastl::string> Dispatch(
            arda::IArdaRHICommandList& Commands, const FArdaComputeInvocation& Invocation,
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
            /** Modular CUDA source and invocation, mutually exclusive with legacy CUDA and graphics. */
            FArdaComputeCudaImplementation mCudaImplementation;
            /** Optional shape/parameter filter, applied before selection and compilation. */
            FArdaComputeVariantSupport mSupports;
        };
        /** Stores ports and an optional dimension-based selector; register variants before publishing.
         * The operand owns value captures; reference captures must outlive every dispatch.
         */
        FArdaComputeOperand(eastl::string Name, eastl::vector<FArdaComputePort> Ports,
            FArdaComputeKernelSelector Selector = {})
            : mName(eastl::move(Name)), mPorts(eastl::move(Ports)), mSelector(eastl::move(Selector)) {}
        /** Registers a CUDA builder without invoking it or requiring a CUDA-enabled device.
         * @param Name Nonempty variant name, unique across CUDA and graphics registrations.
         * @param MinimumArchitecture Inclusive SM lower bound, encoded as major * 10 + minor.
         * @param MaximumArchitecture Inclusive SM upper bound; UINT32_MAX removes the upper bound.
         * @param Builder Nonempty callback that builds ordered kernels from a validated invocation.
         * @param Supports Optional pure shape/parameter admission check before selection.
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
        [[nodiscard]] arda::FArdaRHIStatus RegisterCuda(eastl::string Name,
            uint32_t MinimumArchitecture, uint32_t MaximumArchitecture,
            FArdaComputeKernelBuilder Builder, FArdaComputeVariantSupport Supports = {});
        /** Registers reusable source modules and a separate host invocation entry point.
         * Names/SM limits follow the builder overload. Modules and mInvoke must be nonempty.
         * Supports optionally filters input dimensions before selection; pinning never bypasses it.
         * Registration retains modules/callbacks without compiling. Only the selected invocation
         * runs; its whole launch sequence is validated and compiled before command recording.
         * Register before publication; callbacks must support concurrent dispatches on separate
         * command lists. Reference captures must outlive dispatch; modules own their source/cache.
         */
        [[nodiscard]] arda::FArdaRHIStatus RegisterCuda(eastl::string Name,
            uint32_t MinimumArchitecture, uint32_t MaximumArchitecture,
            FArdaComputeCudaImplementation Implementation, FArdaComputeVariantSupport Supports = {});
        /** Registers a compute-shader alternative usable without CUDA.
         * @param Name Nonempty variant name, unique across both implementation families.
         * @param Dispatch Nonempty callback responsible for transitions, pipeline/binding setup and recording.
         * @param Supports Optional pure shape/parameter admission check before selection.
         * @return Success after storage; InvalidArgument for an empty/duplicate name or empty callback.
         * @ownership The operand owns the moved callback and its value captures. Reference captures
         * must outlive its dispatches; recorded GPU dependencies must survive queue completion.
         * @threading Register before publishing; do not race registration with dispatch. Callback
         * code must support the application's concurrency and serialize each command list.
         * @errors Callback failure is propagated by Dispatch without trying another implementation.
         */
        [[nodiscard]] arda::FArdaRHIStatus RegisterGraphics(eastl::string Name,
            FArdaComputeShaderDispatch Dispatch, FArdaComputeVariantSupport Supports = {});
        /** Check operation-specific shapes and parameters before either implementation runs. */
        virtual arda::FArdaRHIStatus ValidateInvocation(const FArdaComputeInvocation&) const { return {}; }
        /** Override for shape-based selection or an external tuning cache.
         * The default calls the constructor's selector, or preserves registration order if absent.
         * Returning an ineligible index is rejected even when a registration name was pinned.
         */
        virtual size_t SelectVariant(const FArdaComputeInvocation& Invocation,
            const arda::FArdaCudaCapabilities& Capabilities,
            const eastl::vector<size_t>& Eligible) const
        { return mSelector ? mSelector(Invocation, Capabilities, Eligible) : Eligible.front(); }
        /** Read-only variant metadata; indices correspond to SelectVariant's eligible indices. */
        [[nodiscard]] const eastl::vector<FVariant>& GetVariants() const noexcept { return mVariants; }

    private:
        arda::FArdaRHIStatus Register(FVariant Variant);
        eastl::string mName;
        eastl::vector<FArdaComputePort> mPorts;
        eastl::vector<FVariant> mVariants;
        FArdaComputeKernelSelector mSelector;
    };
}
