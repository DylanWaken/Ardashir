/** @file ArdaComputeOperand.h
 * User-defined operations with typed host parameters and inspectable resource access.
 */
#pragma once
#include "ArdaComputeParameters.h"

namespace arda
{
    /** Common identity and parameter contract for user-implemented operands.
     * Implementations own their device/runtime dependencies. The base does not own
     * source files, register kernels, select variants, tune, or choose fallback paths.
     */
    class FArdaComputeOperand
    {
    public:
        /** Destroy only after callbacks using this operand have finished. */
        virtual ~FArdaComputeOperand() = default;
        /** Returns an implementation-owned stable diagnostic name. */
        [[nodiscard]] virtual const char* GetName() const noexcept = 0;
        /** User-defined support query for the bound device, architecture and runtime.
         * Return Unsupported with a reason when execution is unavailable. This query
         * must not launch work; shape-specific validation belongs in dispatch.
         * @threading The implementation defines synchronization of its bound runtime state.
         */
        [[nodiscard]] virtual FArdaRHIStatus GetOperandSupport() const = 0;
        /** Returns static metadata for the concrete typed dispatch parameters. */
        [[nodiscard]] virtual const FArdaComputeParameterMetadata& GetParameterMetadata() const = 0;
    };

    /** Derive using an application-defined compute parameter struct and override either
     * or both dispatch hooks. The implementation performs support/input checks and owns
     * CPU logic, algorithm selection, tuning, kernel launches and native library calls.
     * Copy parameters as their actual C++ type: resources and host values can be nontrivial.
     */
    template<typename ParameterType>
    class TArdaComputeOperand : public FArdaComputeOperand
    {
    public:
        /** User-defined C++ parameter type accepted by both hooks. */
        using FParameters = ParameterType;
        /** Returns the parameter type's metadata without executing user dispatch code. */
        [[nodiscard]] const FArdaComputeParameterMetadata& GetParameterMetadata() const final
        { return ParameterType::GetStaticMetadata(); }

        /** Executes host code now; it may directly launch kernels or call a native runtime.
         * Implementation helpers may span any number of .cpp/.cu/.cuh files; no PTX or
         * launch-list result is required. Parameters are borrowed for this call only.
         * Retain dependencies of asynchronous work and document stream/completion ownership:
         * success is not a GPU fence. Default: Unsupported without side effects.
         * @threading The author synchronizes mutable state, runtime calls and in-flight storage.
         */
        [[nodiscard]] virtual FArdaRHIStatus Dispatch(const ParameterType&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "This operand does not implement immediate Dispatch.");
        }

        /** Runs recording logic now and appends work to a caller-owned open command list.
         * The implementation may record several CUDA/shader operations with host logic and
         * tuning between them. It must not close/submit the list or launch unordered external
         * work. Native libraries need an explicit compatible submission adapter for this hook.
         * Borrow parameters only during recording; retain dependencies through execution and
         * propagate recording errors without retrying partial writes. Default: Unsupported;
         * it never silently calls immediate Dispatch. RDG scheduling is future integration.
         * @threading Serialize the caller's command list and any mutable operand state.
         */
        [[nodiscard]] virtual FArdaRHIStatus DispatchDeferred(IArdaRHICommandList&, const ParameterType&)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                "This operand does not implement deferred Dispatch.");
        }
    };
}
