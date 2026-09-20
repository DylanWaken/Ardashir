/** Logical resource identity, storage descriptions and declared accesses. */
#pragma once

#include "RHI/Resources/ArdaRHIBuffer.h"
#include "RHI/Resources/ArdaRHITexture.h"
#include "RHI/Resources/ArdaRHIAccelerationStructures.h"

namespace arda
{
	/** A graph-owned logical resource. Repeated writes to a region follow node attachment order. */
	struct FArdaDependencyResourceHandle
	{
		uint64_t mGraph = 0;
		uint32_t mIndex = UINT32_MAX;
		uint64_t mGeneration = 0;

		explicit operator bool() const noexcept
		{
			return mGraph && mIndex != UINT32_MAX;
		}

		bool operator==(const FArdaDependencyResourceHandle& Other) const noexcept
		{
			return mGraph == Other.mGraph && mIndex == Other.mIndex && mGeneration == Other.mGeneration;
		}

		bool operator!=(const FArdaDependencyResourceHandle& Other) const noexcept
		{
			return !(*this == Other);
		}
	};

	/** ReadWrite consumes the preceding value and writes in place; initial contents require external storage. */
	enum class EArdaDependencyAccess : uint8_t
	{
		Read,
		Write,
		ReadWrite
	};

	/** One dependency derived from a node's typed parameters. */
	struct FArdaDependencyAccess
	{
		FArdaDependencyResourceHandle mResource;
		EArdaDependencyAccess mAccess = EArdaDependencyAccess::Read;
		EArdaRHIResourceState mState = EArdaRHIResourceState::ShaderResource;
		FArdaRHIBufferRange mBufferRange;
		FArdaRHITextureSubresourceRange mTextureRange;
	};

	/** Frozen resource description. External storage is retained by the graph. */
	struct FArdaDependencyResourceDesc
	{
		eastl::string mName;
		bool mbTexture = false;
		bool mbPersistent = false;
		bool mbOutput = false;
		FArdaRHIBufferDesc mBuffer;
		FArdaRHITextureDesc mTexture;
		FArdaRHIBufferRef mExternalBuffer;
		FArdaRHITextureRef mExternalTexture;
		/** Imported built or unbuilt acceleration structure; accesses cover the whole object. */
		FArdaRHIAccelStructRef mExternalAccelerationStructure;
	};
}
