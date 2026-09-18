/** Resource descriptors resolved into native provider objects before backend calls. */
#pragma once

#include "RHI/Providers/ArdaProviderObject.h"
#include "RHI/Memory/ArdaRHITiling.h"
#include "RHI/Resources/ArdaRHIFramebuffer.h"
#include "RHI/Shaders/ArdaRHIBindingSet.h"
#include <EASTL/vector.h>

namespace arda
{
	struct FArdaProviderBinding
	{
		FArdaRHIBindingItem mItem;
		FArdaProviderObjectRef mObject;
	};

	struct FArdaProviderTextureTileMapping
	{
		eastl::vector<FArdaRHITiledTextureCoordinate> mCoordinates;
		eastl::vector<FArdaRHITiledTextureRegion> mRegions;
		eastl::vector<uint64_t> mByteOffsets;
		FArdaProviderObjectRef mHeap;
	};

	struct FArdaProviderBufferTileMapping
	{
		uint64_t mBufferOffset = 0;
		uint64_t mByteSize = 0;
		uint64_t mHeapOffset = 0;
		FArdaProviderObjectRef mHeap;
		bool mbCommit = true;
	};

	struct FArdaProviderFramebufferTarget
	{
		FArdaRHIFramebufferTarget mTarget;
		FArdaProviderObjectRef mTexture;
	};

	struct FArdaProviderFramebufferCreateInfo
	{
		const FArdaRHIFramebufferDesc& mDesc;
		eastl::vector<FArdaProviderFramebufferTarget> mColors;
		FArdaProviderFramebufferTarget mDepth;
	};
}
