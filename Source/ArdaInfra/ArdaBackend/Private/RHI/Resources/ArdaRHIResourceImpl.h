/** Intrusive facade resource lifetime and native-resource storage. */
#pragma once

#include "RHI/Context/ArdaRHILifetimeContext.h"
#include "RHI/Providers/ArdaProviderObject.h"
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/utility.h>
#include <EASTL/atomic.h>

namespace arda::detail
{
	class FArdaResource : public virtual IArdaRHIResource
	{
	public:
		FArdaResource(EArdaRHIResourceType Type,
		    eastl::string Name,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker = {})
		    : mType(Type),
		      mName(eastl::move(Name)),
		      mOwner(Owner),
		      mLifetimeTracker(eastl::move(LifetimeTracker))
		{
			if (mLifetimeTracker)
			{
				mLifetimeTracker->Add(mType);
			}
		}

		void AddRef() noexcept final
		{
			mReferences.fetch_add(1, eastl::memory_order_relaxed);
		}

		void Release() noexcept final
		{
			if (mReferences.fetch_sub(1, eastl::memory_order_acq_rel) == 1)
			{
				delete this;
			}
		}

		EArdaRHIResourceType GetResourceType() const noexcept final
		{
			return mType;
		}

		const char* GetDebugName() const noexcept final
		{
			return mName.c_str();
		}

		const void* GetOwner() const noexcept
		{
			// The retained tracker uniquely identifies a device generation even after facade-address reuse.
			return mLifetimeTracker ? mLifetimeTracker.get() : mOwner;
		}

	protected:
		~FArdaResource() override
		{
			if (mLifetimeTracker)
			{
				mLifetimeTracker->Remove(mType);
			}
		}

	private:
		eastl::atomic<uint32_t> mReferences{0};
		EArdaRHIResourceType mType;
		eastl::string mName;
		const void* mOwner = nullptr;
		eastl::shared_ptr<FArdaLifetimeTracker> mLifetimeTracker;
	};

	template <typename Interface, typename Desc, EArdaRHIResourceType Type>
	class TArdaNativeResource : public FArdaResource, public Interface
	{
	public:
		TArdaNativeResource(Desc Descriptor,
		    FArdaProviderObjectRef Native,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker,
		    eastl::shared_ptr<void> LifetimeToken = {},
		    FArdaRHIMemoryAllocationInfo ImportedAllocationInfo = {})
		    : FArdaResource(Type, Descriptor.mDebugName, Owner, eastl::move(LifetimeTracker)),
		      mDesc(eastl::move(Descriptor)),
		      mNative(eastl::move(Native)),
		      mLifetimeToken(eastl::move(LifetimeToken)),
		      mImportedAllocationInfo(ImportedAllocationInfo)
		{
		}

		const Desc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		FArdaCudaResourceInfo GetCudaResourceInfo() const noexcept override
		{
			return mNative ? mNative->GetCudaResourceInfo() : FArdaCudaResourceInfo{};
		}

		const void* GetPhysicalIdentity() const noexcept
		{
			return mNative ? mNative->GetIdentity() : nullptr;
		}

		FArdaRHIMemoryAllocationInfo GetMemoryAllocationInfo() const noexcept override
		{
			const auto NativeInfo = mNative ? mNative->GetMemoryAllocationInfo() : FArdaRHIMemoryAllocationInfo{};
			return NativeInfo.mbKnown ? NativeInfo : mImportedAllocationInfo;
		}

		Desc mDesc;
		FArdaProviderObjectRef mNative;
		eastl::shared_ptr<void> mLifetimeToken;
		FArdaRHIMemoryAllocationInfo mImportedAllocationInfo;
	};
}
