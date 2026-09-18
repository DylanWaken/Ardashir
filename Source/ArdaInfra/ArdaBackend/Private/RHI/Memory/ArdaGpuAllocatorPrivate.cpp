#include "ArdaBackendCorePch.h"
#include "RHI/Memory/ArdaGpuAllocatorPrivate.h"
#include "RHI/Context/ArdaGpuAllocatorContext.h"

#include <EASTL/algorithm.h>
#include <EASTL/unordered_map.h>
#include <mutex>
#include <type_traits>

namespace arda
{
	namespace
	{

		template <typename T>
		T CacheDesc(T Desc)
		{
			Desc.mDebugName.clear();
			return Desc;
		}

		bool Cacheable(const FArdaRHIBufferDesc& Desc)
		{
			return !Desc.mbCudaInterop && !Desc.mbTiled && Desc.mMaxVersions <= 1 &&
			    (!Desc.mbVirtual || Desc.mCpuAccess == EArdaRHICpuAccess::None);
		}

		bool Cacheable(const FArdaRHITextureDesc& Desc)
		{
			return !Desc.mbCudaInterop && !Desc.mbTiled;
		}

		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}
	}

	eastl::shared_ptr<FArdaGpuAllocator::FArdaState::FArdaLease> FArdaGpuAllocator::FArdaState::FindLease(const FArdaProviderObjectRef& Object)
	{
		std::lock_guard<std::mutex> Lock(mMutex);
		const auto Found = mActive.find(Object.get());
		return Found != mActive.end() ? Found->second.lock() : nullptr;
	}

	FArdaProviderObjectRef FArdaGpuAllocator::FArdaState::Lease(FArdaCacheEntry Entry, const FArdaProviderObjectRef& HeapLease)
	{
		auto Owner = eastl::make_shared<FArdaLease>();
		Owner->mState = shared_from_this();
		Owner->mEntry = eastl::move(Entry);
		Owner->mHeapLease = HeapLease;
		{
			std::lock_guard<std::mutex> Lock(mMutex);
			mActive[Owner->mEntry.mObject.get()] = Owner;
		}
		// Providers may cast the pointee. The alias must expose the original native object.
		return FArdaProviderObjectRef(Owner, Owner->mEntry.mObject.get());
	}

	void FArdaGpuAllocator::FArdaState::RemoveCachedStats(const FArdaCacheEntry& Entry)
	{
		if (Entry.mKind == EArdaGpuCacheKind::Heap)
		{
			mStats.mCachedHeapBytes -= Entry.mBytes;
		}
		else
		{
			--mStats.mCachedResources;
			mStats.mCachedCommittedBytes -= Entry.mBytes;
		}
	}

	FArdaGpuAllocator::FArdaState::FArdaCacheEntry FArdaGpuAllocator::FArdaState::Take(size_t Index)
	{
		FArdaCacheEntry Entry = eastl::move(mCache[Index]);
		RemoveCachedStats(Entry);
		mCache.erase(mCache.begin() + Index);
		return Entry;
	}

	void FArdaGpuAllocator::FArdaState::Forget(const FArdaCacheEntry& Entry)
	{
		if (Entry.mKind == EArdaGpuCacheKind::Heap)
		{
			mStats.mHeapBytes -= Entry.mBytes;
		}
		else
		{
			mStats.mCommittedBytes -= Entry.mBytes;
		}
	}

	void FArdaGpuAllocator::FArdaState::Evict(size_t Index, eastl::vector<FArdaCacheEntry>& Retired)
	{
		FArdaCacheEntry Entry = Take(Index);
		if (Entry.mKind == EArdaGpuCacheKind::Heap)
		{
			// Cached placed objects own the native heap too. Release them with the heap.
			for (size_t Child = 0; Child < mCache.size();)
			{
				if (mCache[Child].mHeap.get() == Entry.mObject.get())
				{
					Evict(Child, Retired);
				}
				else
				{
					++Child;
				}
			}
		}
		Forget(Entry);
		Retired.push_back(eastl::move(Entry));
	}

	void FArdaGpuAllocator::FArdaState::EnforceByteLimit(eastl::vector<FArdaCacheEntry>& Retired)
	{
		while (mStats.mCachedHeapBytes > mOptions.mMaxCachedBytes ||
		    mStats.mCachedCommittedBytes > mOptions.mMaxCachedBytes - mStats.mCachedHeapBytes)
		{
			size_t Oldest = mCache.size();
			for (size_t Index = 0; Index < mCache.size(); ++Index)
			{
				if (mCache[Index].mBytes &&
				    (Oldest == mCache.size() || mCache[Index].mLastUsedCycle < mCache[Oldest].mLastUsedCycle))
				{
					Oldest = Index;
				}
			}
			if (Oldest == mCache.size())
			{
				break;
			}
			Evict(Oldest, Retired);
		}
	}

	void FArdaGpuAllocator::FArdaState::Return(FArdaCacheEntry Entry)
	{
		eastl::vector<FArdaCacheEntry> Retired;
		{
			std::lock_guard<std::mutex> Lock(mMutex);
			mActive.erase(Entry.mObject.get());
			const bool bRetain = Entry.mbCacheable && mOptions.mMaxCachedBytes;
			Entry.mLastUsedCycle = mStats.mCollectionCycle;
			if (Entry.mKind == EArdaGpuCacheKind::Heap)
			{
				mStats.mCachedHeapBytes += Entry.mBytes;
			}
			else
			{
				++mStats.mCachedResources;
				mStats.mCachedCommittedBytes += Entry.mBytes;
			}
			mCache.push_back(eastl::move(Entry));
			if (!bRetain)
			{
				Evict(mCache.size() - 1, Retired);
			}
			EnforceByteLimit(Retired);
		}
		// Destruction can enter native provider code, so it must occur outside mMutex.
	}

	void FArdaGpuAllocator::FArdaState::Collect(bool bTrim)
	{
		eastl::vector<FArdaCacheEntry> Retired;
		{
			std::lock_guard<std::mutex> Lock(mMutex);
			++mStats.mCollectionCycle;
			for (size_t Index = 0; Index < mCache.size();)
			{
				const auto& Entry = mCache[Index];
				const uint64_t Age = mStats.mCollectionCycle - Entry.mLastUsedCycle;
				bool bEvict = bTrim;
				if (Entry.mKind == EArdaGpuCacheKind::Heap)
				{
					bEvict |= Age >= mOptions.mHeapRetentionCycles;
				}
				else if (Age >= mOptions.mResourceRetentionCycles)
				{
					const uint32_t Capacity = Entry.mKind == EArdaGpuCacheKind::Buffer
					    ? mOptions.mBufferCacheCapacity
					    : mOptions.mTextureCacheCapacity;
					const auto Count = eastl::count_if(mCache.begin(),
					    mCache.end(),
					    [&](const auto& Other)
					    {
						    return Entry.mKind == Other.mKind && Entry.mHeap.get() == Other.mHeap.get() &&
						        Entry.mQueue == Other.mQueue;
					    });
					bEvict |= Count > Capacity;
				}
				if (bEvict)
				{
					Evict(Index, Retired);
					// Heap eviction can erase children preceding Index.
					Index = 0;
				}
				else
				{
					++Index;
				}
			}
			EnforceByteLimit(Retired);
		}
	}

	template <typename DescType>
	constexpr EArdaGpuCacheKind FArdaGpuAllocator::FArdaState::Kind()
	{
		return std::is_same_v<DescType, FArdaRHIBufferDesc> ? EArdaGpuCacheKind::Buffer
		                                                    : EArdaGpuCacheKind::Texture;
	}

	template <typename DescType>
	FArdaGpuAllocator::FArdaState::FArdaCacheEntry FArdaGpuAllocator::FArdaState::ResourceEntry(const DescType& Desc, FArdaProviderObjectRef Object)
	{
		FArdaCacheEntry Entry;
		Entry.mKind = Kind<DescType>();
		Entry.mObject = eastl::move(Object);
		if constexpr (std::is_same_v<DescType, FArdaRHIBufferDesc>)
		{
			Entry.mBufferDesc = CacheDesc(Desc);
		}
		else
		{
			Entry.mTextureDesc = CacheDesc(Desc);
		}
		Entry.mbCacheable = Cacheable(Desc) && !Desc.mbVirtual;
		if (!Desc.mbVirtual)
		{
			const auto Allocation = Entry.mObject->GetMemoryAllocationInfo();
			if (Allocation.mbKnown)
			{
				Entry.mBytes = Allocation.mByteSize;
			}
			else
			{
				TArdaRHIResult<FArdaRHIMemoryRequirements> Requirements;
				if constexpr (std::is_same_v<DescType, FArdaRHIBufferDesc>)
				{
					Requirements = mProvider->GetBufferMemoryRequirements(Entry.mObject, Desc);
				}
				else
				{
					Requirements = mProvider->GetTextureMemoryRequirements(Entry.mObject, Desc);
				}
				Entry.mBytes = Requirements ? Requirements.mValue.mSize : 0;
			}
			// Unknown native sizes cannot be safely retained under the idle-byte limit.
			Entry.mbCacheable &= Entry.mBytes != 0;
		}
		return Entry;
	}

	template <typename DescType>
	FArdaProviderObjectResult FArdaGpuAllocator::FArdaState::CreateNative(const DescType& Desc)
	{
		FArdaProviderObjectResult Result;
		if constexpr (std::is_same_v<DescType, FArdaRHIBufferDesc>)
		{
			Result = mProvider->CreateBuffer(Desc);
		}
		else
		{
			Result = mProvider->CreateTexture(Desc);
		}
		if (Result && Result.mValue)
		{
			std::lock_guard<std::mutex> Lock(mMutex);
			if constexpr (std::is_same_v<DescType, FArdaRHIBufferDesc>)
			{
				++mStats.mBufferCreations;
			}
			else
			{
				++mStats.mTextureCreations;
			}
		}
		return Result;
	}

	template <typename DescType>
	FArdaProviderObjectRef FArdaGpuAllocator::FArdaState::FindResource(const DescType& Desc,
	    const FArdaProviderObjectRef& Heap,
	    uint64_t Offset,
	    const FArdaProviderObjectRef& HeapLease,
	    EArdaRHIQueueType Queue)
	{
		const auto Key = CacheDesc(Desc);
		for (;;)
		{
			FArdaCacheEntry Entry;
			{
				std::lock_guard<std::mutex> Lock(mMutex);
				for (size_t Index = 0; Index < mCache.size(); ++Index)
				{
					const auto& Candidate = mCache[Index];
					if (Candidate.mKind != Kind<DescType>() || Candidate.mHeap.get() != Heap.get() ||
					    Candidate.mOffset != Offset || Candidate.mQueue != Queue)
					{
						continue;
					}
					bool bMatch;
					if constexpr (std::is_same_v<DescType, FArdaRHIBufferDesc>)
					{
						bMatch = Candidate.mBufferDesc == Key;
					}
					else
					{
						bMatch = Candidate.mTextureDesc == Key;
					}
					if (bMatch)
					{
						Entry = Take(Index);
						break;
					}
				}
			}
			if (!Entry.mObject)
			{
				return {};
			}
			bool bReusable;
			if constexpr (std::is_same_v<DescType, FArdaRHIBufferDesc>)
			{
				bReusable = mProvider->CanReuseBufferForQueue(Entry.mObject, Desc, Queue);
			}
			else
			{
				bReusable = mProvider->CanReuseTexture(Entry.mObject, Desc);
			}
			{
				std::lock_guard<std::mutex> Lock(mMutex);
				if (!bReusable)
				{
					Forget(Entry);
				}
				else if constexpr (std::is_same_v<DescType, FArdaRHIBufferDesc>)
				{
					++mStats.mBufferCacheHits;
				}
				else
				{
					++mStats.mTextureCacheHits;
				}
			}
			if (bReusable)
			{
				return Lease(eastl::move(Entry), HeapLease);
			}
			// A mismatched state is evicted, never reset by changing bookkeeping.
		}
	}

	template <typename DescType>
	FArdaProviderObjectResult FArdaGpuAllocator::FArdaState::CreateResource(const DescType& Desc,
	    EArdaRHIQueueType Queue)
	{
		if (Cacheable(Desc) && !Desc.mbVirtual)
		{
			if (auto Cached = FindResource(Desc, {}, 0, {}, Queue))
			{
				return {eastl::move(Cached), {}};
			}
		}
		auto Result = CreateNative(Desc);
		if (!Result || !Result.mValue)
		{
			return Result;
		}
		auto Entry = ResourceEntry(Desc, eastl::move(Result.mValue));
		Entry.mQueue = Queue;
		{
			std::lock_guard<std::mutex> Lock(mMutex);
			mStats.mCommittedBytes += Entry.mBytes;
		}
		return {Lease(eastl::move(Entry)), {}};
	}

	template <typename DescType>
	FArdaRHIStatus FArdaGpuAllocator::FArdaState::BindNative(const FArdaProviderObjectRef& Object,
	    const DescType& Desc,
	    const FArdaProviderObjectRef& Heap,
	    uint64_t Offset)
	{
		if constexpr (std::is_same_v<DescType, FArdaRHIBufferDesc>)
		{
			return mProvider->BindBufferMemory(Object, Desc, Heap, Offset);
		}
		else
		{
			return mProvider->BindTextureMemory(Object, Desc, Heap, Offset);
		}
	}

	template <typename DescType>
	FArdaRHIStatus FArdaGpuAllocator::FArdaState::BindResource(FArdaProviderObjectRef& Object,
	    const DescType& Desc,
	    const FArdaProviderObjectRef& Heap,
	    uint64_t Offset)
	{
		if (!Object || !Heap || !Desc.mbVirtual)
		{
			return Invalid("Heap binding requires a virtual resource and a valid heap.");
		}
		auto Owner = FindLease(Object);
		if (!Owner)
		{
			// Unmanaged objects retain the caller's full heap ownership through the provider.
			return BindNative(Object, Desc, Heap, Offset);
		}
		if (Owner->mEntry.mKind != Kind<DescType>() || Owner->mEntry.mHeap)
		{
			return Invalid("The allocator resource kind is invalid or its memory is already bound.");
		}
		bool bDescriptorMatches;
		if constexpr (std::is_same_v<DescType, FArdaRHIBufferDesc>)
		{
			bDescriptorMatches = Owner->mEntry.mBufferDesc == CacheDesc(Desc);
		}
		else
		{
			bDescriptorMatches = Owner->mEntry.mTextureDesc == CacheDesc(Desc);
		}
		if (!bDescriptorMatches)
		{
			return Invalid("The binding descriptor differs from the allocator resource descriptor.");
		}
		auto HeapOwner = FindLease(Heap);
		if (HeapOwner && HeapOwner->mEntry.mKind != EArdaGpuCacheKind::Heap)
		{
			return Invalid("The allocation is not a heap.");
		}
		const auto NativeHeap = HeapOwner ? HeapOwner->mEntry.mObject : Heap;
		const bool bCacheable = Cacheable(Desc) && HeapOwner && HeapOwner->mEntry.mbCacheable;
		if (bCacheable)
		{
			if (auto Cached = FindResource(Desc, NativeHeap, Offset, Heap))
			{
				Object = eastl::move(Cached);
				return {};
			}
		}
		if (auto Status = BindNative(Owner->mEntry.mObject, Desc, NativeHeap, Offset); !Status)
		{
			return Status;
		}
		Owner->mEntry.mHeap = NativeHeap;
		Owner->mEntry.mOffset = Offset;
		Owner->mEntry.mbCacheable = bCacheable;
		Owner->mHeapLease = Heap;
		return {};
	}

	template <typename DescType>
	FArdaProviderObjectResult FArdaGpuAllocator::FArdaState::CreatePlaced(const DescType& Desc,
	    const FArdaProviderObjectRef& Heap,
	    uint64_t Offset)
	{
		if (!Heap || !Desc.mbVirtual)
		{
			return {{}, Invalid("Placed creation requires a virtual descriptor and a valid heap.")};
		}
		auto HeapOwner = FindLease(Heap);
		if (HeapOwner && HeapOwner->mEntry.mKind != EArdaGpuCacheKind::Heap)
		{
			return {{}, Invalid("The allocation is not a heap.")};
		}
		const auto NativeHeap = HeapOwner ? HeapOwner->mEntry.mObject : Heap;
		const bool bCacheable = Cacheable(Desc) && HeapOwner && HeapOwner->mEntry.mbCacheable;
		if (bCacheable)
		{
			if (auto Cached = FindResource(Desc, NativeHeap, Offset, Heap))
			{
				return {eastl::move(Cached), {}};
			}
		}
		auto Result = CreateNative(Desc);
		if (!Result || !Result.mValue)
		{
			return Result;
		}
		if (auto Status = BindNative(Result.mValue, Desc, NativeHeap, Offset); !Status)
		{
			return {{}, Status};
		}
		auto Entry = ResourceEntry(Desc, eastl::move(Result.mValue));
		Entry.mHeap = NativeHeap;
		Entry.mOffset = Offset;
		Entry.mbCacheable = bCacheable;
		return {Lease(eastl::move(Entry), Heap), {}};
	}

	FArdaGpuAllocator::FArdaState::FArdaLease::~FArdaLease()
	{
		if (auto State = mState.lock())
		{
			State->Return(eastl::move(mEntry));
		}
	}

	FArdaGpuAllocator::FArdaGpuAllocator(eastl::shared_ptr<IArdaRHIProviderDevice> Provider)
	    : mState(eastl::make_shared<FArdaState>(eastl::move(Provider)))
	{
		const eastl::weak_ptr<FArdaState> State = mState;
		const eastl::weak_ptr<IArdaRHIProviderDevice> NativeProvider = mState->mProvider;
		mState->mProvider->SetBufferAllocator(
		    [State, NativeProvider](const FArdaRHIBufferDesc& Desc,
		        EArdaRHIQueueType Queue) -> FArdaProviderObjectResult
		    {
			    if (auto Allocator = State.lock())
			    {
				    return Allocator->CreateResource(Desc, Queue);
			    }
			    if (auto Native = NativeProvider.lock())
			    {
				    return Native->CreateBuffer(Desc);
			    }
			    return {{},
			        FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			            "The GPU allocator's provider has been destroyed.")};
		    });
	}

	FArdaGpuAllocator::~FArdaGpuAllocator() = default;

	FArdaProviderObjectResult FArdaGpuAllocator::CreateBuffer(const FArdaRHIBufferDesc& Desc)
	{
		return mState->CreateResource(Desc);
	}

	FArdaProviderObjectResult FArdaGpuAllocator::CreateTexture(const FArdaRHITextureDesc& Desc)
	{
		return mState->CreateResource(Desc);
	}

	FArdaProviderObjectResult FArdaGpuAllocator::CreateHeap(const FArdaRHIHeapDesc& Desc)
	{
		FArdaState::FArdaCacheEntry Entry;
		{
			std::lock_guard<std::mutex> Lock(mState->mMutex);
			for (size_t Index = 0; Index < mState->mCache.size(); ++Index)
			{
				const auto& Cached = mState->mCache[Index];
				if (Cached.mKind == EArdaGpuCacheKind::Heap && Cached.mHeapDesc.mCapacity == Desc.mCapacity &&
				    Cached.mHeapDesc.mType == Desc.mType && Cached.mHeapDesc.mMemoryTypeBits == Desc.mMemoryTypeBits)
				{
					Entry = mState->Take(Index);
					++mState->mStats.mHeapCacheHits;
					break;
				}
			}
		}
		if (!Entry.mObject)
		{
			auto Result = mState->mProvider->CreateHeap(Desc);
			if (!Result || !Result.mValue)
			{
				return Result;
			}
			Entry.mKind = EArdaGpuCacheKind::Heap;
			Entry.mObject = eastl::move(Result.mValue);
			Entry.mHeapDesc = CacheDesc(Desc);
			const auto Allocation = Entry.mObject->GetMemoryAllocationInfo();
			Entry.mBytes = Allocation.mbKnown ? Allocation.mByteSize : Desc.mCapacity;
			Entry.mbCacheable = Desc.mType == EArdaRHIHeapType::DeviceLocal;
			{
				std::lock_guard<std::mutex> Lock(mState->mMutex);
				++mState->mStats.mHeapCreations;
				mState->mStats.mHeapBytes += Entry.mBytes;
			}
		}
		return {mState->Lease(eastl::move(Entry)), {}};
	}

	FArdaProviderObjectResult FArdaGpuAllocator::CreatePlacedBuffer(const FArdaRHIBufferDesc& Desc,
	    const FArdaProviderObjectRef& Heap,
	    uint64_t Offset)
	{
		return mState->CreatePlaced(Desc, Heap, Offset);
	}

	FArdaProviderObjectResult FArdaGpuAllocator::CreatePlacedTexture(const FArdaRHITextureDesc& Desc,
	    const FArdaProviderObjectRef& Heap,
	    uint64_t Offset)
	{
		return mState->CreatePlaced(Desc, Heap, Offset);
	}

	FArdaRHIStatus FArdaGpuAllocator::BindBufferMemory(FArdaProviderObjectRef& Object,
	    const FArdaRHIBufferDesc& Desc,
	    const FArdaProviderObjectRef& Heap,
	    uint64_t Offset)
	{
		return mState->BindResource(Object, Desc, Heap, Offset);
	}

	FArdaRHIStatus FArdaGpuAllocator::BindTextureMemory(FArdaProviderObjectRef& Object,
	    const FArdaRHITextureDesc& Desc,
	    const FArdaProviderObjectRef& Heap,
	    uint64_t Offset)
	{
		return mState->BindResource(Object, Desc, Heap, Offset);
	}

	void FArdaGpuAllocator::Collect(bool bTrim)
	{
		mState->Collect(bTrim);
	}

	FArdaGpuAllocatorStats FArdaGpuAllocator::GetStats() const
	{
		std::lock_guard<std::mutex> Lock(mState->mMutex);
		return mState->mStats;
	}

	FArdaRHIStatus FArdaGpuAllocator::SetOptions(const FArdaGpuAllocatorOptions& Options)
	{
		eastl::vector<FArdaState::FArdaCacheEntry> Retired;
		{
			std::lock_guard<std::mutex> Lock(mState->mMutex);
			mState->mOptions = Options;
			if (!Options.mMaxCachedBytes)
			{
				while (!mState->mCache.empty())
				{
					mState->Evict(0, Retired);
				}
			}
			else
			{
				mState->EnforceByteLimit(Retired);
			}
		}
		return {};
	}
}
