/** @file ArdaRHIResourceCollection.h
 * Declares ResourceCollection definitions for the RHI resources module.
 */

#pragma once

#include "RHI/Resources/ArdaRHIAccelerationStructures.h"
#include "RHI/Resources/ArdaRHIBuffer.h"
#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Resources/ArdaRHISampler.h"
#include "RHI/Resources/ArdaRHITexture.h"
#include "RHI/Resources/ArdaRHIViews.h"
#include "RHI/Shaders/ArdaRHIDescriptorTable.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>

namespace arda
{
	/** Kind of resource retained by a general resource collection. */
	enum class EArdaRHIResourceCollectionItemType : uint8_t
	{
		Texture,
		TextureReference,
		Buffer,
		ShaderResourceView,
		UnorderedAccessView,
		AccelerationStructure,
		Sampler
	};

	/** One typed member of a general resource collection. */
	struct FArdaRHIResourceCollectionItem
	{
		EArdaRHIResourceCollectionItemType mType = EArdaRHIResourceCollectionItemType::Texture;
		FArdaRHITextureRef mTexture;
		FArdaRHITextureReferenceRef mTextureReference;
		FArdaRHIBufferRef mBuffer;
		FArdaRHIShaderResourceViewRef mShaderResourceView;
		FArdaRHIUnorderedAccessViewRef mUnorderedAccessView;
		FArdaRHIAccelStructRef mAccelerationStructure;
		FArdaRHISamplerRef mSampler;
	};

	/** Mutable collection used by bindless and ray/ML systems. */
	struct FArdaRHIResourceCollectionDesc
	{
		eastl::vector<FArdaRHIResourceCollectionItem> mItems;
		bool mbMutable = false;
		bool mbDirectlyIndexed = false;
		eastl::string mDebugName;
	};

	/** General resource collection with an optional native bindless base. */
	class IArdaRHIResourceCollection : public virtual IArdaRHIResource
	{
	public:
		[[nodiscard]] virtual const FArdaRHIResourceCollectionDesc& GetDesc() const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetFirstDescriptorIndexInHeap() const noexcept = 0;

		/** Returns the collection's bindable descriptor table; empty for host-only collections. */
		[[nodiscard]] virtual FArdaRHIDescriptorTableRef GetDescriptorTable() const = 0;
	};
}
