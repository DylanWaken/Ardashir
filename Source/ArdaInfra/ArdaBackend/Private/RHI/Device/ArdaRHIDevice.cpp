#include "RHI/Device/ArdaRHIDeviceImpl.h"

namespace arda::detail
{
	void FArdaRHIDeviceImpl::TrimDescriptorCaches()
	{
		std::lock_guard<std::mutex> Lock(mCacheMutex);
		mSamplerCache.Clear();
		mBindingLayoutCache.Clear();
		mInputLayoutCache.Clear();
		mRasterStateCache.Clear();
		mBlendStateCache.Clear();
		mDepthStateCache.Clear();
		mRayTracingPipelineCache.Clear();
		mTextureImportCache.Clear();
		mBufferImportCache.Clear();
	}

	FArdaRHICacheStats FArdaRHIDeviceImpl::GetDescriptorCacheStats() const noexcept
	{
		std::lock_guard<std::mutex> Lock(mCacheMutex);
		FArdaRHICacheStats Stats;
		Stats.mSamplers = mSamplerCache.Size();
		Stats.mBindingLayouts = mBindingLayoutCache.Size();
		Stats.mInputLayouts = mInputLayoutCache.Size();
		Stats.mRasterStates = mRasterStateCache.Size();
		Stats.mBlendStates = mBlendStateCache.Size();
		Stats.mDepthStencilStates = mDepthStateCache.Size();
		Stats.mRayTracingPipelines = mRayTracingPipelineCache.Size();
		return Stats;
	}

	FArdaRHIResourceLifetimeStats FArdaRHIDeviceImpl::GetResourceLifetimeStats() const noexcept
	{
		FArdaRHIResourceLifetimeStats Stats;
		for (size_t Index = 0; Index < static_cast<size_t>(EArdaRHIResourceType::Count); ++Index)
		{
			Stats.mLiveResources[Index] = mLifetimeTracker->Get(static_cast<EArdaRHIResourceType>(Index));
		}
		const FArdaProviderLifetimeStats Native = mDevice->GetLifetimeStats();
		Stats.mResourceDescriptors = Native.mResourceDescriptors;
		Stats.mSamplerDescriptors = Native.mSamplerDescriptors;
		Stats.mDescriptorSets = Native.mDescriptorSets;
		Stats.mPendingSubmissions = Native.mPendingSubmissions;
		return Stats;
	}

	void FArdaRHIDeviceImpl::FlushAndDisablePipelineCachePersistence() noexcept
	{
		if (!mbPipelineCacheDetached && mDevice)
		{
			mDevice->FlushPipelineCache();
			mbPipelineCacheDetached = true;
		}
	}
}

namespace arda
{
	FArdaRHIDeviceRef CreateArdaRHIDevice(eastl::shared_ptr<IArdaRHIProviderDevice> Device)
	{
		return Device ? FArdaRHIDeviceRef(new detail::FArdaRHIDeviceImpl(eastl::move(Device))) : FArdaRHIDeviceRef{};
	}
}
