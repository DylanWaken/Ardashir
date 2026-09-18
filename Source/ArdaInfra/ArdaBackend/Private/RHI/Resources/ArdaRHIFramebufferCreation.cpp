#include "RHI/Device/ArdaRHIDeviceImpl.h"
#include "RHI/Resources/ArdaRHIFramebufferImpl.h"
#include "RHI/Resources/ArdaRHITextureBufferImpl.h"

namespace arda::detail
{
	TArdaRHIResult<FArdaRHIFramebufferRef> FArdaRHIDeviceImpl::CreateFramebuffer(
	    const FArdaRHIFramebufferDesc& Desc)
	{
		if (Desc.mColorAttachments.empty() && !Desc.mDepthAttachment.mTexture)
		{
			return Failure<FArdaRHIFramebufferRef>(Invalid("A framebuffer requires at least one attachment."));
		}
		FArdaProviderFramebufferCreateInfo Info{Desc};
		Info.mColors.reserve(Desc.mColorAttachments.size());
		for (const auto& Target : Desc.mColorAttachments)
		{
			auto* Texture = Cast<FArdaTexture>(Target.mTexture.Get());
			if (!Texture || !Owns(Texture))
			{
				return Failure<FArdaRHIFramebufferRef>(WrongDevice());
			}
			Info.mColors.push_back({Target, Texture->mNative});
		}
		if (Desc.mDepthAttachment.mTexture)
		{
			auto* Texture = Cast<FArdaTexture>(Desc.mDepthAttachment.mTexture.Get());
			if (!Texture || !Owns(Texture))
			{
				return Failure<FArdaRHIFramebufferRef>(WrongDevice());
			}
			Info.mDepth = {Desc.mDepthAttachment, Texture->mNative};
		}
		if (auto Status = Validate(Desc); !Status)
		{
			return Failure<FArdaRHIFramebufferRef>(Status);
		}
		const uint32_t MaxAttachments = GetCapabilities().mLimits.mMaxColorAttachments;
		if (MaxAttachments && Desc.mColorAttachments.size() > MaxAttachments)
		{
			return Failure<FArdaRHIFramebufferRef>(
			    Unsupported("Framebuffer exceeds the device color-attachment limit."));
		}
		auto Native = mDevice->CreateFramebuffer(Info);
		if (!Native)
		{
			return Failure<FArdaRHIFramebufferRef>(eastl::move(Native.mStatus));
		}
		return {
		    FArdaRHIFramebufferRef(new FArdaFramebuffer(Desc, eastl::move(Native.mValue), this, mLifetimeTracker)),
		    {}};
	}
}
