#pragma once

#include "RHI/ArdaRHI.h"

#include <cstdint>
#include <EASTL/vector.h>

namespace arda
{
	enum class EArdaMemoryKind : uint8_t
	{
		Buffer,
		Texture,
		AccelerationStructure
	};

	/** One logical allocation, identified in the caller's dense identifier domain. */
	struct FArdaMemoryRequest
	{
		uint32_t mIdentifier = 0;
		/** Culled resources retain identifiers but consume no storage or budget. */
		bool mbUsed = true;
		EArdaMemoryKind mKind = EArdaMemoryKind::Buffer;
		FArdaRHIBufferDesc mBufferDesc;
		FArdaRHITextureDesc mTextureDesc;
		FArdaRHIBufferRef mExternalBuffer;
		FArdaRHITextureRef mExternalTexture;
		FArdaRHIAccelStructRef mExternalAccelerationStructure;
		/** Whole retained allocation, including parent heaps; callers may supply missing import metadata. */
		FArdaRHIMemoryAllocationInfo mExternalAllocationInfo;
		uint32_t mFirstUse = 0;
		uint32_t mLastUse = 0;
		/** Every unordered first/last use must be represented, not just one queue. */
		eastl::vector<uint32_t> mFirstUseNodes;
		eastl::vector<uint32_t> mLastUseNodes;
		bool mbPersistent = false;
		/** Allow descriptor qualification for native heap placement. CUDA calls can opt out. */
		bool mbAllowHeapPlacement = true;
		/** Set by Describe after checking native capabilities and descriptor restrictions. */
		bool mbUseHeapPlacement = false;
		/** Native heap capacity granularity, qualified alongside resource requirements. */
		uint64_t mHeapAllocationAlignment = 65536;
		/** Native allocation requirements, populated before planning. */
		FArdaRHIMemoryRequirements mRequirements;
	};

	struct FArdaMemoryAliasEdge
	{
		uint32_t mProducer = 0;
		uint32_t mConsumer = 0;
	};

	/** Activate a placed buffer before its first ordinary transition on this node. */
	struct FArdaMemoryAliasActivation
	{
		uint32_t mResource = 0;
		uint32_t mConsumer = 0;
		eastl::vector<uint32_t> mPreviousResources;
	};

	struct FArdaMemoryOptions
	{
		bool mbAllowReuse = true;
		/** Texture overlap requires activation/discard and retirement helpers in the executor. */
		bool mbAllowTextureAliasing = true;
		/** Permit the planner to request additional lifetime ordering edges. */
		bool mbAllowSerialization = true;
		uint64_t mWorkspaceBytes = 0;
		/** Independent transient pools; persistent/imported storage and retained adapters are shared. */
		uint32_t mFrameCount = 1;
		uint64_t mBudgetBytes = UINT64_MAX;
		/** Used to prove reuse safe when serialization is disabled. */
		bool (*mHappensBefore)(void*, uint32_t, uint32_t) = nullptr;
		void* mHappensBeforeContext = nullptr;
	};

	struct FArdaMemorySlot
	{
		uint32_t mRepresentative = 0;
		eastl::vector<uint32_t> mResources;
		uint64_t mBytes = 0;
		bool mbExternal = false;
		bool mbPersistent = false;
		uint32_t mHeapIndex = UINT32_MAX;
		uint64_t mOffset = 0;
	};

	struct FArdaMemoryHeap
	{
		FArdaRHIHeapDesc mDesc;
		EArdaMemoryKind mKind = EArdaMemoryKind::Buffer;
		eastl::vector<uint32_t> mResources;
		uint64_t mAlignment = 1;
	};

	/**
	 * Native heaps permit ordered byte-range aliasing. Optimal images additionally require
	 * activation/discard and retirement helpers, and exactly one first-use node per image.
	 * CUDA, persistent, CPU-visible, MSAA and render/depth target storage use committed objects.
	 */
	struct FArdaMemoryPlan
	{
		eastl::vector<FArdaMemoryRequest> mRequests;
		eastl::vector<FArdaMemorySlot> mSlots;
		eastl::vector<uint32_t> mResourceSlots;
		eastl::vector<uint64_t> mResourceBytes;
		eastl::vector<FArdaMemoryAliasEdge> mAliasEdges;
		eastl::vector<FArdaMemoryAliasActivation> mAliasActivations;
		eastl::vector<FArdaMemoryHeap> mHeaps;
		eastl::vector<uint32_t> mResourceHeapIndices;
		eastl::vector<uint64_t> mResourceOffsets;
		uint32_t mFrameCount = 1;
		/** Native storage required across every frame pool, including leases satisfied by a cache. */
		uint64_t mOwnedBytes = 0;
		/** One frame's transient heaps and committed resources. */
		uint64_t mTransientBytes = 0;
		/** Owned persistent storage, shared by all frame pools. */
		uint64_t mPersistentBytes = 0;
		uint64_t mExternalBytes = 0;
		uint64_t mWorkspaceBytes = 0;
		uint64_t mTotalBytes = 0;
		/** False for diagnostic plans returned with a budget failure. */
		bool mbBudgetAccepted = false;
	};

	struct FArdaMemoryResources
	{
		/** Dense logical identifiers; entries belonging to another kind are null. */
		eastl::vector<FArdaRHIBufferRef> mBuffers;
		eastl::vector<FArdaRHITextureRef> mTextures;
		eastl::vector<FArdaRHIAccelStructRef> mAccelerationStructures;
		eastl::vector<FArdaRHIHeapRef> mHeaps;
		/** Leased storage bytes, excluding shared persistent storage; cache hits retain their full cost. */
		uint64_t mAllocatedBytes = 0;
		uint64_t mSharedBytes = 0;
		uint64_t mExternalBytes = 0;
		uint64_t mTotalBytes = 0;
	};

	/**
	 * Queries native sizes without committing storage. External resources are queried directly.
	 * Owned resources use the device's descriptor-only requirement queries.
	 */
	[[nodiscard]] FArdaRHIStatus DescribeArdaMemory(IArdaRHIDevice& Device,
	    eastl::vector<FArdaMemoryRequest>& Requests);

	/** Pure deterministic allocation planning; rejects a candidate exceeding its accounted budget. */
	[[nodiscard]] TArdaRHIResult<FArdaMemoryPlan> PlanArdaMemory(const eastl::vector<FArdaMemoryRequest>& Requests,
	    const FArdaMemoryOptions& Options = {});

	/**
	 * Creates one frame pool and verifies native requirements before allocating. Additional frame
	 * pools pass the first pool as Shared to retain its persistent objects without allocating again.
	 * Synthetic scratch requests use the same dense identifier domain as ordinary resources.
	 */
	[[nodiscard]] TArdaRHIResult<FArdaMemoryResources> MaterializeArdaMemory(IArdaRHIDevice& Device,
	    const FArdaMemoryPlan& Plan,
	    const FArdaMemoryResources* Shared = nullptr);
}
