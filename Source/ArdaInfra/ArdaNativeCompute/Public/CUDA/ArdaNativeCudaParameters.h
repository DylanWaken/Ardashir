#pragma once

#include "CUDA/ArdaNativeCudaProvider.h"

namespace arda
{
	/**
	 * Start an operand with the resource slots and parameter ABI of an ARDA_CUDA_PARAMETER_STRUCT.
	 * @param Id Nonempty operand registry ID.
	 * @param Metadata CUDA parameter metadata for the shared host type.
	 * @return Operand descriptor. Add variants or a shader fallback before registering it.
	 */
	[[nodiscard]] TArdaRHIResult<FArdaNativeOperandDesc> DescribeArdaNativeCudaOperand(const char* Id,
	    const FArdaCudaParameterMetadata& Metadata);

	/**
	 * Bind a linked CUDA entry and derive its resource patches from the same parameter schema.
	 * @param Metadata CUDA parameter metadata used by the kernel.
	 * @param Entry Retained linked kernel entry.
	 * @return Native source for a registered variant, or a validation error.
	 */
	[[nodiscard]] TArdaRHIResult<eastl::shared_ptr<const IArdaNativeKernelSource>> CreateArdaNativeCudaLinkedSource(
	    const FArdaCudaParameterMetadata& Metadata,
	    eastl::shared_ptr<const IArdaCudaKernelEntry> Entry);

	/**
	 * Pack the host values and retain the active resource views for route selection.
	 * @param Host Host parameter object declared by ARDA_CUDA_PARAMETER_STRUCT.
	 * @return Invocation with scalar bytes and RHI views. Shader routes can use views without CUDA sharing.
	 */
	template <class Parameters>
	[[nodiscard]] TArdaRHIResult<FArdaNativeInvocation> MakeArdaNativeCudaInvocation(const Parameters& Host)
	{
		static_assert(eastl::is_same_v<Parameters, typename Parameters::FArdaParameters>,
		    "Pass the host parameter type.");
		FArdaCudaDispatch Packed;
		if (auto Status = Parameters::GetCudaMetadata().PackArguments(&Host, Packed); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		FArdaNativeInvocation Invocation;
		Invocation.mParameters = eastl::move(Packed.mKernels.front().mParameters);
		Invocation.mResources.reserve(Packed.mBindings.size());
		for (auto& Binding : Packed.mBindings)
		{
			Invocation.mResources.push_back({eastl::move(Binding.mResource), Binding.mBufferRange, Binding.mMipLevel});
		}
		return {eastl::move(Invocation), {}};
	}
}
