/** Acceleration-structure and opacity-micromap facade resources. */
#pragma once

#include "RHI/Resources/ArdaRHIResourceImpl.h"
#include "RHI/Resources/ArdaRHIAccelerationStructures.h"
#include "RHI/Memory/ArdaRHIHeap.h"
#include "RHI/Providers/ArdaProviderCommandTypes.h"
#include <mutex>

namespace arda::detail
{
	class FArdaRHIDeviceImpl;

	/** Resolves retained facade geometry inputs into provider-owned objects. */
	[[nodiscard]] TArdaRHIResult<eastl::vector<FArdaProviderRayTracingGeometry>> ResolveRayTracingGeometries(
	    FArdaRHIDeviceImpl& Device,
	    const eastl::vector<FArdaRHIRayTracingGeometryDesc>& Geometries);

	template <typename Interface, typename Desc, EArdaRHIResourceType Type, EArdaRHIResourceState InitialState>
	class TArdaAccelerationResource : public FArdaResource, public Interface
	{
	public:
		TArdaAccelerationResource(Desc Descriptor,
		    FArdaProviderObjectRef Native,
		    uint64_t DeviceAddress,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(Type, Descriptor.mDebugName, Owner, eastl::move(LifetimeTracker)),
		      mDesc(eastl::move(Descriptor)),
		      mNative(eastl::move(Native)),
		      mDeviceAddress(DeviceAddress)
		{
		}

		const Desc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		bool IsCompacted() const noexcept override
		{
			std::lock_guard<std::mutex> Lock(mStateMutex);
			return mBuildState == EArdaRHIAccelStructBuildState::Compacted;
		}

		uint64_t GetDeviceAddress() const noexcept override
		{
			return mDeviceAddress;
		}

		const void* GetPhysicalIdentity() const noexcept override
		{
			return mNative ? mNative->GetIdentity() : nullptr;
		}

		EArdaRHIAccelStructBuildState GetBuildState() const noexcept override
		{
			std::lock_guard<std::mutex> Lock(mStateMutex);
			return mBuildState;
		}

		Desc mDesc;
		FArdaProviderObjectRef mNative;
		uint64_t mDeviceAddress = 0;
		mutable std::mutex mStateMutex;
		EArdaRHIResourceState mFacadeState = InitialState;
		EArdaRHIAccelStructBuildState mBuildState = EArdaRHIAccelStructBuildState::Unbuilt;
	};

	using FArdaOpacityMicromap = TArdaAccelerationResource<IArdaRHIOpacityMicromap,
	    FArdaRHIOpacityMicromapDesc,
	    EArdaRHIResourceType::OpacityMicromap,
	    EArdaRHIResourceState::OpacityMicromapWrite>;
	using FArdaAccelStructBase = TArdaAccelerationResource<IArdaRHIAccelStruct,
	    FArdaRHIAccelStructDesc,
	    EArdaRHIResourceType::AccelStruct,
	    EArdaRHIResourceState::AccelStructRead>;

	class FArdaAccelStruct final : public FArdaAccelStructBase
	{
	public:
		FArdaAccelStruct(FArdaRHIAccelStructDesc Desc,
		    FArdaRHIAccelStructMemoryRequirements Requirements,
		    FArdaProviderObjectRef Native,
		    uint64_t DeviceAddress,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaAccelStructBase(eastl::move(Desc),
		          eastl::move(Native),
		          DeviceAddress,
		          Owner,
		          eastl::move(LifetimeTracker)),
		      mRequirements(Requirements)
		{
		}

		FArdaRHIMemoryAllocationInfo GetMemoryAllocationInfo() const noexcept override
		{
			return mHeap  ? mHeap->GetMemoryAllocationInfo()
			    : mNative ? mNative->GetMemoryAllocationInfo()
			              : FArdaRHIMemoryAllocationInfo{};
		}

		FArdaRHIAccelStructMemoryRequirements mRequirements;
		FArdaRHIHeapRef mHeap;
		uint64_t mHeapOffset = 0;
	};
}
