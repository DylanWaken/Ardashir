#include "RHI/Scheduling/ArdaRHICommandListImpl.h"
#include <cstring>
#include <mutex>
#include "RHI/Device/ArdaRHIDeviceImpl.h"

namespace arda::detail
{
	FArdaRHIStatus FArdaCommandList::BuildBottomLevelAccelStruct(IArdaRHIAccelStruct& Resource,
	    const eastl::vector<FArdaRHIRayTracingGeometryDesc>& Geometries,
	    EArdaRHIAccelStructBuildFlags Flags)
	{
		auto* AccelStruct = Cast<FArdaAccelStruct>(&Resource);
		if (!AccelStruct || !RetainOwned(AccelStruct))
		{
			return WrongDevice();
		}
		if (AccelStruct->mDesc.mbTopLevel || Geometries.empty())
		{
			return Invalid("A BLAS build requires BLAS geometry.");
		}
		if (HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::PerformUpdate) &&
		    !HasAnyFlags(AccelStruct->mDesc.mBuildFlags, EArdaRHIAccelStructBuildFlags::AllowUpdate))
		{
			return Invalid("A BLAS update requires AllowUpdate at creation.");
		}
		auto NativeGeometries = ResolveRayTracingGeometries(*mDevice, Geometries);
		if (!NativeGeometries)
		{
			return NativeGeometries.mStatus;
		}
		const FArdaRHIStatus Status =
		    mNative->BuildBottomLevelAccelStruct(AccelStruct->mNative, NativeGeometries.mValue, Flags);
		if (Status)
		{
			mFacadeAccelStructStates[AccelStruct] = {EArdaRHIResourceState::AccelStructRead,
			    HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::PerformUpdate)
			        ? EArdaRHIAccelStructBuildState::Updated
			        : EArdaRHIAccelStructBuildState::Built,
			    true};
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::BuildTopLevelAccelStruct(IArdaRHIAccelStruct& Resource,
	    const eastl::vector<FArdaRHIRayTracingInstanceDesc>& Instances,
	    EArdaRHIAccelStructBuildFlags Flags)
	{
		auto* AccelStruct = Cast<FArdaAccelStruct>(&Resource);
		if (!AccelStruct || !RetainOwned(AccelStruct))
		{
			return WrongDevice();
		}
		if (!AccelStruct->mDesc.mbTopLevel || Instances.size() > AccelStruct->mDesc.mTopLevelMaxInstances)
		{
			return Invalid("TLAS instance count exceeds its capacity.");
		}
		if (Instances.empty() && !HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::AllowEmptyInstances))
		{
			return Invalid("An empty TLAS build requires AllowEmptyInstances.");
		}
		if (HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::PerformUpdate) &&
		    !HasAnyFlags(AccelStruct->mDesc.mBuildFlags, EArdaRHIAccelStructBuildFlags::AllowUpdate))
		{
			return Invalid("A TLAS update requires AllowUpdate at creation.");
		}
		eastl::vector<FArdaProviderRayTracingInstance> NativeInstances;
		NativeInstances.reserve(Instances.size());
		for (const auto& Instance : Instances)
		{
			auto* BottomLevel = Cast<FArdaAccelStruct>(Instance.mBottomLevelAccelStruct.Get());
			if (!BottomLevel || !RetainOwned(BottomLevel) || BottomLevel->mDesc.mbTopLevel)
			{
				return WrongDevice();
			}
			FArdaProviderRayTracingInstance Native;
			std::memcpy(Native.mTransform, Instance.mTransform, sizeof(Instance.mTransform));
			Native.mInstanceID = Instance.mInstanceId;
			Native.mInstanceMask = Instance.mInstanceMask;
			Native.mInstanceContributionToHitGroupIndex = Instance.mHitGroupContribution;
			Native.mFlags = Instance.mFlags;
			Native.mBottomLevelAccelStruct = BottomLevel->mNative;
			NativeInstances.push_back(eastl::move(Native));
		}
		const FArdaRHIStatus Status =
		    mNative->BuildTopLevelAccelStruct(AccelStruct->mNative, NativeInstances, Flags);
		if (Status)
		{
			mFacadeAccelStructStates[AccelStruct] = {EArdaRHIResourceState::AccelStructRead,
			    HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::PerformUpdate)
			        ? EArdaRHIAccelStructBuildState::Updated
			        : EArdaRHIAccelStructBuildState::Built,
			    true};
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::BuildTopLevelAccelStructFromBuffer(IArdaRHIAccelStruct& Resource,
	    IArdaRHIBuffer& Instances,
	    uint64_t Offset,
	    size_t InstanceCount,
	    EArdaRHIAccelStructBuildFlags Flags)
	{
		auto* AccelStruct = Cast<FArdaAccelStruct>(&Resource);
		auto* Buffer = Cast<FArdaBuffer>(&Instances);
		if (!AccelStruct || !Buffer || !RetainOwned(AccelStruct) || !RetainOwned(Buffer))
		{
			return WrongDevice();
		}
		if (!AccelStruct->mDesc.mbTopLevel || InstanceCount > AccelStruct->mDesc.mTopLevelMaxInstances ||
		    Offset % 16 != 0 || Offset > Buffer->mDesc.mByteSize ||
		    InstanceCount > (Buffer->mDesc.mByteSize - Offset) / 64 ||
		    !HasAnyFlags(Buffer->mDesc.mUsage, EArdaRHIBufferUsage::AccelStructBuildInput))
		{
			return Invalid("TLAS instance-buffer range, alignment or usage is invalid.");
		}
		const FArdaRHIStatus Status = mNative->BuildTopLevelAccelStructFromBuffer(AccelStruct->mNative,
		    Buffer->mNative,
		    Offset,
		    InstanceCount,
		    Flags);
		if (Status)
		{
			mFacadeAccelStructStates[AccelStruct] = {EArdaRHIResourceState::AccelStructRead,
			    HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::PerformUpdate)
			        ? EArdaRHIAccelStructBuildState::Updated
			        : EArdaRHIAccelStructBuildState::Built,
			    true};
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::CopyAccelStruct(IArdaRHIAccelStruct& DestinationResource,
	    IArdaRHIAccelStruct& SourceResource)
	{
		auto* Destination = Cast<FArdaAccelStruct>(&DestinationResource);
		auto* Source = Cast<FArdaAccelStruct>(&SourceResource);
		if (!Destination || !Source || !RetainOwned(Destination) || !RetainOwned(Source))
		{
			return WrongDevice();
		}
		if (mQueue != EArdaRHIQueueType::Graphics && mQueue != EArdaRHIQueueType::Compute)
		{
			return Invalid("Acceleration-structure cloning requires a graphics or compute queue.");
		}
		if (Destination == Source || Destination->mDesc.mbTopLevel != Source->mDesc.mbTopLevel ||
		    Destination->mDesc.mBuildFlags != Source->mDesc.mBuildFlags ||
		    Destination->mRequirements.mResultSize < Source->mRequirements.mResultSize)
		{
			return Invalid(
			    "Acceleration-structure cloning requires a distinct matching destination with sufficient result storage.");
		}
		const auto Existing = mFacadeAccelStructStates.find(Source);
		const auto BuildState =
		    Existing != mFacadeAccelStructStates.end() ? Existing->second.mBuildState : Source->GetBuildState();
		if (BuildState == EArdaRHIAccelStructBuildState::Unbuilt)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Acceleration-structure cloning requires a built source.");
		}
		const FArdaRHIStatus Status = mNative->CopyAccelStruct(Destination->mNative, Source->mNative);
		if (Status)
		{
			mFacadeAccelStructStates[Destination] = {EArdaRHIResourceState::AccelStructRead, BuildState, true};
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::CompactAccelStruct(IArdaRHIAccelStruct& DestinationResource,
	    IArdaRHIAccelStruct& SourceResource)
	{
		auto* Destination = Cast<FArdaAccelStruct>(&DestinationResource);
		auto* Source = Cast<FArdaAccelStruct>(&SourceResource);
		if (!Destination || !Source || !RetainOwned(Destination) || !RetainOwned(Source))
		{
			return WrongDevice();
		}
		if (Destination == Source || Destination->mDesc.mbTopLevel != Source->mDesc.mbTopLevel ||
		    !HasAnyFlags(Source->mDesc.mBuildFlags, EArdaRHIAccelStructBuildFlags::AllowCompaction) ||
		    !Destination->mDesc.mResultSizeOverride)
		{
			return Invalid("Acceleration-structure compaction requires a distinct compact destination.");
		}
		const FArdaRHIStatus Status = mNative->CompactAccelStruct(Destination->mNative, Source->mNative);
		if (Status)
		{
			mFacadeAccelStructStates[Destination] = {EArdaRHIResourceState::AccelStructRead,
			    EArdaRHIAccelStructBuildState::Compacted,
			    true};
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::BuildOpacityMicromap(IArdaRHIOpacityMicromap& Resource)
	{
		auto* Micromap = Cast<FArdaOpacityMicromap>(&Resource);
		if (!Micromap || !RetainOwned(Micromap))
		{
			return WrongDevice();
		}
		if (!mDevice->GetCapabilities().mRayTracing.mbOpacityMicromaps)
		{
			return Unsupported("Opacity micromaps are unsupported by this device.");
		}
		const FArdaRHIStatus Status = mNative->BuildOpacityMicromap(Micromap->mNative);
		if (Status)
		{
			mFacadeOpacityMicromapStates[Micromap] = {EArdaRHIResourceState::OpacityMicromapBuildInput,
			    EArdaRHIAccelStructBuildState::Built,
			    true};
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::CompactOpacityMicromap(IArdaRHIOpacityMicromap& DestinationResource,
	    IArdaRHIOpacityMicromap& SourceResource)
	{
		auto* Destination = Cast<FArdaOpacityMicromap>(&DestinationResource);
		auto* Source = Cast<FArdaOpacityMicromap>(&SourceResource);
		if (!Destination || !Source || !RetainOwned(Destination) || !RetainOwned(Source))
		{
			return WrongDevice();
		}
		if (Destination == Source ||
		    !HasAnyFlags(Source->mDesc.mFlags, EArdaRHIOpacityMicromapBuildFlags::AllowCompaction) ||
		    !Destination->mDesc.mResultSizeOverride)
		{
			return Invalid("Opacity-micromap compaction requires a distinct compact destination.");
		}
		const FArdaRHIStatus Status = mNative->CompactOpacityMicromap(Destination->mNative, Source->mNative);
		if (Status)
		{
			mFacadeOpacityMicromapStates[Destination] = {EArdaRHIResourceState::OpacityMicromapBuildInput,
			    EArdaRHIAccelStructBuildState::Compacted,
			    true};
		}
		return Status;
	}

	TArdaRHIResult<FArdaRHIResourceStateSnapshot> FArdaCommandList::QueryOpacityMicromapState(
	    IArdaRHIOpacityMicromap& Resource) const
	{
		auto* Micromap = Cast<FArdaOpacityMicromap>(&Resource);
		if (!Micromap || !RetainOwned(Micromap))
		{
			return {{}, WrongDevice()};
		}
		FArdaAccelStructTracking Tracking;
		const auto Existing = mFacadeOpacityMicromapStates.find(Micromap);
		if (Existing != mFacadeOpacityMicromapStates.end())
		{
			Tracking = Existing->second;
		}
		else
		{
			std::lock_guard<std::mutex> Lock(Micromap->mStateMutex);
			Tracking.mState = Micromap->mFacadeState;
			Tracking.mBuildState = Micromap->mBuildState;
		}
		auto Native = mNative->QueryOpacityMicromapState(Micromap->mNative);
		if (!Native)
		{
			return {{}, eastl::move(Native.mStatus)};
		}
		FArdaRHIResourceStateSnapshot Snapshot;
		Snapshot.mFacadeState = Tracking.mState;
		Snapshot.mQueue = mQueue;
		Snapshot.mNative = eastl::move(Native.mValue);
		Snapshot.mbFacadeKnown = true;
		return {eastl::move(Snapshot), {}};
	}
}
