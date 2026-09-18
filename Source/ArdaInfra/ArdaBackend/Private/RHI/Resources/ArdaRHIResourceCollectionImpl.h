/** Retained resource collection and its descriptor-table facade. */
#pragma once

#include "RHI/Resources/ArdaRHIResourceCollection.h"
#include "RHI/Resources/ArdaRHIResourceImpl.h"
#include <mutex>

namespace arda::detail
{
	class FArdaResourceCollection final : public FArdaResource, public IArdaRHIResourceCollection
	{
	public:
		FArdaResourceCollection(FArdaRHIResourceCollectionDesc Desc,
		    FArdaRHIDescriptorTableRef DescriptorTable,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(EArdaRHIResourceType::ResourceCollection,
		          Desc.mDebugName,
		          Owner,
		          eastl::move(LifetimeTracker)),
		      mDesc(eastl::move(Desc)),
		      mDescriptorTable(eastl::move(DescriptorTable))
		{
		}

		const FArdaRHIResourceCollectionDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		uint32_t GetFirstDescriptorIndexInHeap() const noexcept override
		{
			return mDescriptorTable ? mDescriptorTable->GetFirstDescriptorIndexInHeap() : 0xffffffffu;
		}

		FArdaRHIDescriptorTableRef GetDescriptorTable() const override
		{
			std::lock_guard<std::mutex> Lock(mMutex);
			return mDescriptorTable;
		}

		FArdaRHIResourceCollectionDesc mDesc;
		FArdaRHIDescriptorTableRef mDescriptorTable;
		mutable std::mutex mMutex;
	};
}
