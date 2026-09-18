#include "RHI/Device/ArdaRHIDeviceImpl.h"
#include "RHI/Resources/ArdaRHISamplerImpl.h"
#include <mutex>

namespace arda::detail
{
	TArdaRHIResult<FArdaRHISamplerRef> FArdaRHIDeviceImpl::CreateSampler(const FArdaRHISamplerDesc& Desc)
	{
		if (auto Status = Validate(Desc); !Status)
		{
			return Failure<FArdaRHISamplerRef>(eastl::move(Status));
		}
		std::lock_guard<std::mutex> Lock(mCacheMutex);
		if (auto Existing = mSamplerCache.Find(Desc))
		{
			return {Existing, {}};
		}
		auto Native = mDevice->CreateSampler(Desc);
		if (!Native)
		{
			return Failure<FArdaRHISamplerRef>(eastl::move(Native.mStatus));
		}
		FArdaRHISamplerRef Result(new FArdaSampler(Desc, eastl::move(Native.mValue), this, mLifetimeTracker));
		mSamplerCache.Insert(Desc, Result);
		return {Result, {}};
	}
}
