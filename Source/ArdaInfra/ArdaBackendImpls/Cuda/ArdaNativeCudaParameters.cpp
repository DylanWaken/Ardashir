#include "CUDA/ArdaNativeCudaParameters.h"

namespace arda
{
	TArdaRHIResult<FArdaNativeOperandDesc> DescribeArdaNativeCudaOperand(const char* Id,
	    const FArdaCudaParameterMetadata& Metadata)
	{
		if (!Id || !*Id)
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "A native operand needs an ID.")};
		}
		if (!Metadata.GetStatus())
		{
			return {{}, Metadata.GetStatus()};
		}
		FArdaNativeOperandDesc Operand;
		Operand.mId = Id;
		Operand.mParameterSize = Metadata.GetSignature().mSize;
		FArdaNativeResourceSlotsCreator Slots;
		for (const auto& Member : Metadata.GetMembers())
		{
			switch (Member.mKind)
			{
			case EArdaComputeParameterKind::Value:
				break;
			case EArdaComputeParameterKind::Buffer:
				Slots.AddBuffer(Member.mName, Member.mAccess);
				break;
			case EArdaComputeParameterKind::Texture:
				Slots.AddTexture(Member.mName, Member.mAccess, Member.mFormat);
				break;
			default:
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Unknown CUDA schema member.")};
			}
		}
		Operand.mResources = Slots.Finalize();
		return {eastl::move(Operand), {}};
	}

	TArdaRHIResult<eastl::shared_ptr<const IArdaNativeKernelSource>> CreateArdaNativeCudaLinkedSource(
	    const FArdaCudaParameterMetadata& Metadata,
	    eastl::shared_ptr<const IArdaCudaKernelEntry> Entry)
	{
		if (!Metadata.GetStatus())
		{
			return {{}, Metadata.GetStatus()};
		}
		const auto Expected = Metadata.GetSignature();
		const auto Actual = Entry ? Entry->GetSignature() : FArdaCudaKernelSignature{};
		if (!Actual.mbSupported || !Actual.mType || *Actual.mType != *Expected.mType ||
		    Actual.mSize != Expected.mSize || Actual.mAlignment != Expected.mAlignment)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			        "The linked CUDA entry has a different parameter type.")};
		}
		FArdaNativeCudaSourceDesc Source;
		Source.mEntry = eastl::move(Entry);
		for (const auto& Member : Metadata.GetMembers())
		{
			if (Member.mKind != EArdaComputeParameterKind::Value)
			{
				const size_t Alignment =
				    Member.mKind == EArdaComputeParameterKind::Buffer ? Member.mElementAlignment : 1;
				Source.mPatches.push_back(
				    {static_cast<uint32_t>(Source.mPatches.size()), Member.mCudaOffset, Alignment});
			}
		}
		return CreateArdaNativeCudaSource(eastl::move(Source));
	}
}
