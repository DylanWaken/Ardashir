#pragma once
#include "Compute/ArdaComputeOperand.h"

namespace arda
{
#define ARDA_ADD_FIELDS(VALUE, BUFFER, SURFACE)                                                                        \
	BUFFER(uint32_t, mInput, EArdaComputeAccess::Read)                                                                 \
	BUFFER(uint32_t, mOutput, EArdaComputeAccess::Write)                                                               \
	VALUE(uint32_t, mCount)                                                                                            \
	VALUE(uint32_t, mBias)
	ARDA_CUDA_PARAMETER_STRUCT(FArdaAddParameters, ARDA_ADD_FIELDS)

	struct FArdaAddVariantInfo
	{
		uint32_t mBlockSize = 0;
	};

	class FArdaAddOperand final : public TArdaComputeOperand<FArdaAddParameters, FArdaAddVariantInfo>
	{
	public:
		using TArdaComputeOperand::TArdaComputeOperand;
		bool mbPreferFastMath = false;

		const char* GetName() const noexcept override
		{
			return "sample.add";
		}

		void BindKernelVariants(FRegistry& Registry) const override;
		TArdaRHIResult<FArdaCudaKernelSelection> SelectKernel(const FParameters& Parameters,
		    const FArdaCudaSelectionContext& Context,
		    const FVariants& Candidates) const override;
	};

#define ARDA_SURFACE_FIELDS(VALUE, BUFFER, SURFACE)                                                                    \
	SURFACE(uint32_t, mSurface, EArdaComputeAccess::Write, EArdaRHIFormat::R32UInt)                                    \
	VALUE(uint32_t, mWidth)                                                                                            \
	VALUE(uint32_t, mHeight)                                                                                           \
	VALUE(uint32_t, mValue)
	ARDA_CUDA_PARAMETER_STRUCT(FArdaSurfaceParameters, ARDA_SURFACE_FIELDS)

	class FArdaSurfaceOperand final : public TArdaComputeOperand<FArdaSurfaceParameters, uint32_t>
	{
	public:
		using TArdaComputeOperand::TArdaComputeOperand;

		const char* GetName() const noexcept override
		{
			return "sample.surface";
		}

		void BindKernelVariants(FRegistry& Registry) const override;
		TArdaRHIResult<FArdaCudaKernelSelection> SelectKernel(const FParameters& Parameters,
		    const FArdaCudaSelectionContext& Context,
		    const FVariants& Candidates) const override;
	};
}
