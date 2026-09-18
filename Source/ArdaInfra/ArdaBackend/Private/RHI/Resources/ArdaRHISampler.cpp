#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Resources/ArdaRHISampler.h"

#include "RHI/Resources/ArdaHash.h"

#include <EASTL/algorithm.h>

#include <cmath>

namespace arda
{
	namespace
	{
		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}
	}

	bool FArdaRHISamplerDesc::operator==(const FArdaRHISamplerDesc& O) const noexcept
	{
		return mBorderColor == O.mBorderColor && mMaxAnisotropy == O.mMaxAnisotropy && mMipBias == O.mMipBias &&
		    mbMinFilter == O.mbMinFilter && mbMagFilter == O.mbMagFilter && mbMipFilter == O.mbMipFilter &&
		    mAddressU == O.mAddressU && mAddressV == O.mAddressV && mAddressW == O.mAddressW &&
		    mReduction == O.mReduction;
	}

	size_t HashValue(const FArdaRHISamplerDesc& V) noexcept
	{
		size_t H = 0;
		ArdaHashCombine(H, V.mBorderColor.mR);
		ArdaHashCombine(H, V.mBorderColor.mG);
		ArdaHashCombine(H, V.mBorderColor.mB);
		ArdaHashCombine(H, V.mBorderColor.mA);
		ArdaHashCombine(H, V.mMaxAnisotropy);
		ArdaHashCombine(H, V.mMipBias);
		ArdaHashCombine(H, V.mbMinFilter);
		ArdaHashCombine(H, V.mbMagFilter);
		ArdaHashCombine(H, V.mbMipFilter);
		ArdaHashCombine(H, static_cast<uint8_t>(V.mAddressU));
		ArdaHashCombine(H, static_cast<uint8_t>(V.mAddressV));
		ArdaHashCombine(H, static_cast<uint8_t>(V.mAddressW));
		ArdaHashCombine(H, static_cast<uint8_t>(V.mReduction));
		return H;
	}

	FArdaRHIStatus Validate(const FArdaRHISamplerDesc& V) noexcept
	{
		if (!std::isfinite(V.mMaxAnisotropy) || V.mMaxAnisotropy < 1.f || !std::isfinite(V.mMipBias))
		{
			return Invalid("Sampler anisotropy and mip bias must be finite; anisotropy must be at least one.");
		}
		return {};
	}
}
