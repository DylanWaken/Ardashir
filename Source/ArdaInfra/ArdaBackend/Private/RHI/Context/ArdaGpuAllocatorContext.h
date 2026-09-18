#pragma once

#include "RHI/Memory/ArdaGpuAllocatorPrivate.h"
#include <EASTL/unordered_map.h>
#include <mutex>

namespace arda
{
	// Per-device allocation leases, retained objects, and accounting.
	enum class EArdaGpuCacheKind : uint8_t
	{
		Heap,
		Buffer,
		Texture
	};

	struct FArdaGpuAllocator::FArdaState : eastl::enable_shared_from_this<FArdaState>
	{
		struct FArdaCacheEntry
		{
			EArdaGpuCacheKind mKind = EArdaGpuCacheKind::Buffer;
			FArdaProviderObjectRef mObject;
			// Original native heap ownership only. Keeping a lease here would prevent heap reuse.
			FArdaProviderObjectRef mHeap;
			FArdaRHIHeapDesc mHeapDesc;
			FArdaRHIBufferDesc mBufferDesc;
			FArdaRHITextureDesc mTextureDesc;
			uint64_t mOffset = 0;
			uint64_t mBytes = 0;
			uint64_t mLastUsedCycle = 0;
			EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
			bool mbCacheable = false;
		};

		struct FArdaLease
		{
			eastl::weak_ptr<FArdaState> mState;
			FArdaCacheEntry mEntry;
			// Active placed resources pin the heap lease until their final submission retires.
			FArdaProviderObjectRef mHeapLease;
			~FArdaLease();
		};

		explicit FArdaState(eastl::shared_ptr<IArdaRHIProviderDevice> Provider)
		    : mProvider(eastl::move(Provider))
		{
		}

		eastl::shared_ptr<IArdaRHIProviderDevice> mProvider;
		mutable std::mutex mMutex;
		FArdaGpuAllocatorOptions mOptions;
		FArdaGpuAllocatorStats mStats;
		eastl::vector<FArdaCacheEntry> mCache;
		eastl::unordered_map<const IArdaProviderObject*, eastl::weak_ptr<FArdaLease>> mActive;

		eastl::shared_ptr<FArdaLease> FindLease(const FArdaProviderObjectRef& Object);

		FArdaProviderObjectRef Lease(FArdaCacheEntry Entry, const FArdaProviderObjectRef& HeapLease = {});

		void RemoveCachedStats(const FArdaCacheEntry& Entry);

		FArdaCacheEntry Take(size_t Index);

		void Forget(const FArdaCacheEntry& Entry);

		void Evict(size_t Index, eastl::vector<FArdaCacheEntry>& Retired);

		void EnforceByteLimit(eastl::vector<FArdaCacheEntry>& Retired);

		void Return(FArdaCacheEntry Entry);

		void Collect(bool bTrim);

		template <typename DescType>
		static constexpr EArdaGpuCacheKind Kind();

		template <typename DescType>
		FArdaCacheEntry ResourceEntry(const DescType& Desc, FArdaProviderObjectRef Object);

		template <typename DescType>
		FArdaProviderObjectResult CreateNative(const DescType& Desc);

		template <typename DescType>
		FArdaProviderObjectRef FindResource(const DescType& Desc,
		    const FArdaProviderObjectRef& Heap = {},
		    uint64_t Offset = 0,
		    const FArdaProviderObjectRef& HeapLease = {},
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics);

		template <typename DescType>
		FArdaProviderObjectResult CreateResource(const DescType& Desc,
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics);

		template <typename DescType>
		FArdaRHIStatus BindNative(const FArdaProviderObjectRef& Object,
		    const DescType& Desc,
		    const FArdaProviderObjectRef& Heap,
		    uint64_t Offset);

		template <typename DescType>
		FArdaRHIStatus BindResource(FArdaProviderObjectRef& Object,
		    const DescType& Desc,
		    const FArdaProviderObjectRef& Heap,
		    uint64_t Offset);

		template <typename DescType>
		FArdaProviderObjectResult CreatePlaced(const DescType& Desc,
		    const FArdaProviderObjectRef& Heap,
		    uint64_t Offset);
	};
}
