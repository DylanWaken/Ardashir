/** Logical resource identity, storage descriptions and declared accesses. */
#pragma once

#include "RHI/Resources/ArdaRHIBuffer.h"
#include "RHI/Resources/ArdaRHITexture.h"
#include "RHI/Resources/ArdaRHIAccelerationStructures.h"
#include "Graph/Resources/ArdaGraphBufferInitializer.h"

namespace arda
{
	/** A graph-owned logical resource. Repeated writes follow attachment order after edit revision ordering. */
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

	/** Hashes the complete logical resource identity, including edit-local generations. */
	struct FArdaDependencyResourceHandleHash
	{
		size_t operator()(FArdaDependencyResourceHandle Handle) const noexcept
		{
			return static_cast<size_t>(Handle.mGraph ^ (uint64_t(Handle.mIndex) << 32) ^ Handle.mGeneration);
		}
	};

	/** ReadWrite consumes the preceding value and writes in place; initial contents require external storage. */
	enum class EArdaDependencyAccess : uint8_t
	{
		Read,
		Write,
		ReadWrite,
		/** Only shader arguments may use this sentinel; their schema resolves it before dependency analysis. */
		AutoDeduction
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

	/** Serialized external storage category; remains meaningful before native storage is bound. */
	enum class EArdaGraphExternalResourceKind : uint8_t
	{
		None,
		Buffer,
		Texture,
		AccelerationStructure
	};

	/** Frozen resource declaration and optional binding for a named external slot. */
	struct FArdaDependencyResourceDesc
	{
		eastl::string mName;
		bool mbTexture = false;
		bool mbPersistent = false;
		bool mbOutput = false;
		/** Explicit edit revisions select preceding-value semantics even when a range has only one writer. */
		bool mbRevisionOrdered = false;
		FArdaRHIBufferDesc mBuffer;
		FArdaRHITextureDesc mTexture;
		FArdaRHIBufferRef mExternalBuffer;
		FArdaRHITextureRef mExternalTexture;
		/** Imported built or unbuilt acceleration structure; accesses cover the whole object. */
		FArdaRHIAccelStructRef mExternalAccelerationStructure;
		/** Stable external slot kind, independent of whether native storage is currently bound. */
		EArdaGraphExternalResourceKind mExternalKind = EArdaGraphExternalResourceKind::None;
		/** Expected acceleration-structure shape; geometry native resource references are always empty. */
		FArdaRHIAccelStructDesc mAccelerationStructure;
		/** Owned persistent buffer initial contents, uploaded once during cold physical preparation. */
		FArdaGraphBufferInitializer mBufferInitializer;

		/** Whether this declaration requires named native storage before compilation. */
		bool IsExternal() const noexcept
		{
			return mExternalKind != EArdaGraphExternalResourceKind::None;
		}

		/** Whether storage matching the declared external kind has been supplied. */
		bool HasExternalBinding() const noexcept
		{
			return mExternalKind == EArdaGraphExternalResourceKind::Buffer ? bool(mExternalBuffer)
			    : mExternalKind == EArdaGraphExternalResourceKind::Texture ? bool(mExternalTexture)
			    : mExternalKind == EArdaGraphExternalResourceKind::AccelerationStructure
			    ? bool(mExternalAccelerationStructure)
			    : false;
		}
	};
}
