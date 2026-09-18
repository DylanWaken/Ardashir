/** Facade texture subresource state bookkeeping. */

#include "RHI/Scheduling/ArdaRHITextureStates.h"
#include "RHI/Resources/ArdaRHISubresources.h"

namespace arda::detail
{
	size_t TextureStateIndex(const FArdaRHITextureDesc& Desc,
	    uint32_t MipLevel,
	    uint32_t ArraySlice,
	    uint32_t Plane) noexcept
	{
		return static_cast<size_t>(Plane) * Desc.mMipLevels * Desc.mArraySize +
		    static_cast<size_t>(ArraySlice) * Desc.mMipLevels + MipLevel;
	}

	void StoreFacadeTextureState(eastl::vector<EArdaRHIResourceState>& States,
	    const FArdaRHITextureDesc& Desc,
	    const FArdaRHITextureSubresourceRange& InputRange,
	    EArdaRHIResourceState State)
	{
		const auto Range = InputRange.Resolve(Desc);
		for (const auto [MipLevel, ArraySlice, Plane] : FArdaTextureSubresources(Range))
		{
			States[TextureStateIndex(Desc, MipLevel, ArraySlice, Plane)] = State;
		}
	}

	TArdaRHIResult<EArdaRHIResourceState> LoadFacadeTextureState(const eastl::vector<EArdaRHIResourceState>& States,
	    const FArdaRHITextureDesc& Desc,
	    const FArdaRHITextureSubresourceRange& InputRange)
	{
		const auto Range = InputRange.Resolve(Desc);
		const EArdaRHIResourceState State =
		    States[TextureStateIndex(Desc, Range.mBaseMipLevel, Range.mBaseArraySlice, Range.mBasePlane)];
		for (const auto [MipLevel, ArraySlice, Plane] : FArdaTextureSubresources(Range))
		{
			if (States[TextureStateIndex(Desc, MipLevel, ArraySlice, Plane)] != State)
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				        "Texture range contains mixed facade states.")};
			}
		}
		return {State, {}};
	}
}
