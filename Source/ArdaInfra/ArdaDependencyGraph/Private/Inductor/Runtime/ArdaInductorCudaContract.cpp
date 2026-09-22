#include "Inductor/Runtime/ArdaInductorCudaContract.h"

#include <EASTL/algorithm.h>

namespace arda
{
	namespace
	{
		bool Allows(EArdaDependencyAccess Access, bool bWrite)
		{
			return Access == EArdaDependencyAccess::ReadWrite ||
			    Access == (bWrite ? EArdaDependencyAccess::Write : EArdaDependencyAccess::Read);
		}

		template <class GetRange>
		bool Covers(const FArdaCudaBinding& Binding,
		    const eastl::vector<FArdaInductorCudaResourceAccess>& Declared,
		    uint64_t Begin,
		    uint64_t End,
		    bool bWrite,
		    GetRange&& Range)
		{
			while (Begin < End)
			{
				uint64_t CoveredEnd = Begin;
				for (const auto& Access : Declared)
				{
					if (Access.mResource != Binding.mResource ||
					    Access.mAccess.mState != EArdaRHIResourceState::UnorderedAccess ||
					    !Allows(Access.mAccess.mAccess, bWrite))
					{
						continue;
					}
					uint64_t RangeBegin = 0, RangeEnd = 0;
					if (Range(Access.mAccess, RangeBegin, RangeEnd) && RangeBegin <= Begin)
					{
						CoveredEnd = eastl::max(CoveredEnd, RangeEnd);
					}
				}
				if (CoveredEnd == Begin)
				{
					return false;
				}
				Begin = CoveredEnd;
			}
			return true;
		}

		FArdaRHIStatus ContractError(const FArdaCudaBinding& Binding, const char* Reason)
		{
			eastl::string Message = "CUDA operand resource '";
			const char* Name = Binding.mResource ? Binding.mResource->GetDebugName() : nullptr;
			Message += Name ? Name : "<unnamed>";
			Message += "' ";
			Message += Reason;
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, Message.c_str());
		}
	}

	FArdaRHIStatus ValidateArdaInductorCudaBindings(const eastl::vector<FArdaCudaBinding>& Bindings,
	    const eastl::vector<FArdaInductorCudaResourceAccess>& Declared)
	{
		for (const auto& Binding : Bindings)
		{
			if (!Binding.mResource ||
			    (Binding.mAccess != EArdaComputeAccess::Read && Binding.mAccess != EArdaComputeAccess::Write &&
			        Binding.mAccess != EArdaComputeAccess::ReadWrite))
			{
				return ContractError(Binding, "has an invalid resource or access requirement.");
			}
			for (const bool bWrite : {false, true})
			{
				if ((bWrite && Binding.mAccess == EArdaComputeAccess::Read) ||
				    (!bWrite && Binding.mAccess == EArdaComputeAccess::Write))
				{
					continue;
				}
				bool bCovered = false;
				if (const auto* Buffer = dynamic_cast<const IArdaRHIBuffer*>(Binding.mResource.Get()))
				{
					const auto& Desc = Buffer->GetDesc();
					const auto& Requested = Binding.mBufferRange;
					if (Requested.mByteOffset >= Desc.mByteSize || !Requested.mByteSize ||
					    (Requested.mByteSize != ArdaRHIWholeBuffer &&
					        Requested.mByteSize > Desc.mByteSize - Requested.mByteOffset))
					{
						return ContractError(Binding, "has a byte range outside its allocation.");
					}
					const auto Range = Requested.Resolve(Desc);
					bCovered = Covers(Binding,
					    Declared,
					    Range.mByteOffset,
					    Range.mByteOffset + Range.mByteSize,
					    bWrite,
					    [&](const FArdaDependencyAccess& Access, uint64_t& Begin, uint64_t& End)
					    {
						    const auto Allowed = Access.mBufferRange.Resolve(Desc);
						    Begin = Allowed.mByteOffset;
						    if (Begin > Desc.mByteSize || Allowed.mByteSize > Desc.mByteSize - Begin)
						    {
							    return false;
						    }
						    End = Begin + Allowed.mByteSize;
						    return true;
					    });
				}
				else if (const auto* Texture = dynamic_cast<const IArdaRHITexture*>(Binding.mResource.Get()))
				{
					const auto& Desc = Texture->GetDesc();
					if (Binding.mMipLevel >= Desc.mMipLevels)
					{
						return ContractError(Binding, "has a mip outside its allocation.");
					}
					// CUDA surfaces expose every array layer at one mip; a cropped declaration is insufficient.
					bCovered = Covers(Binding,
					    Declared,
					    0,
					    Desc.mArraySize,
					    bWrite,
					    [&](const FArdaDependencyAccess& Access, uint64_t& Begin, uint64_t& End)
					    {
						    const auto Allowed = Access.mTextureRange.Resolve(Desc);
						    Begin = Allowed.mBaseArraySlice;
						    End = Begin + Allowed.mArraySliceCount;
						    return Allowed.mBaseMipLevel <= Binding.mMipLevel && Allowed.mMipLevelCount &&
						        Binding.mMipLevel - Allowed.mBaseMipLevel < Allowed.mMipLevelCount &&
						        Allowed.mBasePlane == 0 && Allowed.mPlaneCount && End <= Desc.mArraySize;
					    });
				}
				if (!bCovered)
				{
					return ContractError(Binding,
					    bWrite ? "writes outside the current node's declared CUDA resource accesses."
					           : "reads outside the current node's declared CUDA resource accesses.");
				}
			}
		}
		return {};
	}
}
