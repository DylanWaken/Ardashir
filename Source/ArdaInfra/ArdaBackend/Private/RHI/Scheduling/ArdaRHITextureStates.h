/** Facade texture subresource state bookkeeping. */
#pragma once

#include "RHI/Resources/ArdaRHITexture.h"
#include "RHI/Config/ArdaRHIStatus.h"
#include <EASTL/vector.h>

namespace arda::detail
{
	size_t TextureStateIndex(const FArdaRHITextureDesc& Desc, uint32_t MipLevel, uint32_t ArraySlice, uint32_t Plane) noexcept;
	void StoreFacadeTextureState(eastl::vector<EArdaRHIResourceState>& States, const FArdaRHITextureDesc& Desc, const FArdaRHITextureSubresourceRange& InputRange, EArdaRHIResourceState State);
	TArdaRHIResult<EArdaRHIResourceState> LoadFacadeTextureState(const eastl::vector<EArdaRHIResourceState>& States, const FArdaRHITextureDesc& Desc, const FArdaRHITextureSubresourceRange& InputRange);
}
