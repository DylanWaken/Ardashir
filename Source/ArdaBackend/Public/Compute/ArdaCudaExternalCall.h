/** @file ArdaCudaExternalCall.h
 * Typed adapters for external CUDA libraries using the ordinary operand and sequence APIs.
 * This header requires no CUDA SDK; include library headers only in adapter implementations.
 */
#pragma once
#include "ArdaCudaKernelVariants.h"
#include <cstring>

namespace arda
{
	/** Supplies the parameter signature and aligned typed copy for a library factory.
     * Override PrepareCall to construct exclusive native state under the provider context.
     * The existing operand BindKernelVariants/SelectKernel hooks also select library calls.
     */
	template <class ParameterType>
	class TArdaCudaExternalCall : public IArdaCudaExternalCall
	{
	public:
		/** Patched plain parameter object, using pointers/surfaces instead of RHI references. */
		using FCuda = typename ParameterType::FCuda;

		/** Reports exact parameter identity without initializing CUDA. */
		FArdaCudaKernelSignature GetSignature() const noexcept final
		{
			return {&typeid(FCuda),
			    sizeof(FCuda),
			    alignof(FCuda),
			    std::is_trivially_copyable_v<FCuda> && std::is_standard_layout_v<FCuda>};
		}

		/** Validates borrowed inputs and copies them into an aligned typed value before preparation. */
		TArdaRHIResult<eastl::unique_ptr<IArdaCudaPreparedCall>> Prepare(const FArdaCudaExternalCallContext& Context,
		    const void* Parameters,
		    size_t ParameterSize) const final
		{
			if constexpr (std::is_trivially_copyable_v<FCuda> && std::is_standard_layout_v<FCuda>)
			{
				if (Context.mContext && Context.mStream && Parameters && ParameterSize == sizeof(FCuda))
				{
					FCuda Values{};
					std::memcpy(&Values, Parameters, sizeof(Values));
					return PrepareCall(Context, Values);
				}
			}
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			        "Invalid external CUDA call context or parameter schema.")};
		}

		/** Creates per-recording library state. Copy any needed values; Parameters is temporary.
         * Resource addresses are resolved, but their contents must not be touched until Enqueue.
         */
		virtual TArdaRHIResult<eastl::unique_ptr<IArdaCudaPreparedCall>> PrepareCall(
		    const FArdaCudaExternalCallContext& Context,
		    const FCuda& Parameters) const = 0;
	};

	/** Binds a library factory to an operand registry without requiring nvcc or a build manifest. */
	template <class Payload>
	TArdaCudaExternalCallVariant<Payload> BindArdaCudaExternalCall(const char* Name,
	    Payload Info,
	    eastl::shared_ptr<const IArdaCudaExternalCall> Call,
	    FArdaCudaKernelRequirements Requirements = {})
	{
		return {eastl::move(Call), eastl::move(Info), Name ? Name : "", Requirements};
	}
}
