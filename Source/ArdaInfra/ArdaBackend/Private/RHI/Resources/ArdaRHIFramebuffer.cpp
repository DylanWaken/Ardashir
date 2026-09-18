#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Pipelines/ArdaRHIFixedFunctionStates.h"
#include "RHI/Resources/ArdaRHIFormat.h"
#include "RHI/Resources/ArdaRHIFramebuffer.h"
#include "RHI/Resources/ArdaRHITexture.h"

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

	FArdaRHIStatus ResolveArdaRHIFramebufferAttachment(const FArdaRHITextureDesc& Texture,
	    const FArdaRHIFramebufferAttachment& Attachment,
	    bool bDepthAttachment,
	    FArdaRHIFramebufferAttachment& Out) noexcept
	{
		Out = {};
		if (auto Status = Validate(Texture); !Status)
		{
			return Status;
		}
		const auto RequiredUsage =
		    bDepthAttachment ? EArdaRHITextureUsage::DepthStencil : EArdaRHITextureUsage::RenderTarget;
		const auto Format = Attachment.mFormat == EArdaRHIFormat::Unknown ? Texture.mFormat : Attachment.mFormat;
		if (!HasAnyFlags(Texture.mUsage, RequiredUsage) || !IsArdaRHITextureViewFormatCompatible(Texture, Format) ||
		    GetArdaRHIFormatInfo(Format).mbDepth != bDepthAttachment || (!bDepthAttachment && Attachment.mbReadOnly))
		{
			return Invalid("Framebuffer attachment usage, format, or read-only mode is incompatible with its slot.");
		}
		auto Range = Attachment.mSubresources;
		if (Range.mMipLevelCount == ArdaRHIAllSubresources)
		{
			Range.mMipLevelCount = 1;
		}
		const auto Fits = [](uint32_t Base, uint32_t Count, uint32_t Limit)
		{
			return Base < Limit && Count && (Count == ArdaRHIAllSubresources || Count <= Limit - Base);
		};
		const uint32_t Planes = GetArdaRHIFormatPlaneCount(Texture.mFormat);
		if (Range.mMipLevelCount != 1 || !Fits(Range.mBaseMipLevel, Range.mMipLevelCount, Texture.mMipLevels) ||
		    !Fits(Range.mBaseArraySlice, Range.mArraySliceCount, Texture.mArraySize) ||
		    !Fits(Range.mBasePlane, Range.mPlaneCount, Planes))
		{
			return Invalid("Framebuffer attachment must select one mip and nonempty in-range layers and aspects.");
		}
		Range = Range.Resolve(Texture);
		if (bDepthAttachment && Range.mBasePlane != 0)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Stencil-only framebuffer attachments are unsupported.");
		}
		Out = Attachment;
		Out.mFormat = Format;
		Out.mSubresources = Range;
		return {};
	}

	FArdaRHIStatus Validate(const FArdaRHIFramebufferDesc& V)
	{
		if ((V.mColorAttachments.empty() && !V.mDepthAttachment.mTexture) ||
		    V.mColorAttachments.size() > ArdaRHIMaxRenderTargets)
		{
			return Invalid("Framebuffer requires attachments within the portable color-attachment limit.");
		}
		uint32_t SampleCount = 0;
		const auto ValidateTarget = [&](const FArdaRHIFramebufferTarget& Target, bool bDepthAttachment)
		{
			if (!Target.mTexture)
			{
				return Invalid("Framebuffer attachment texture is missing.");
			}
			const auto& Texture = Target.mTexture->GetDesc();
			FArdaRHIFramebufferAttachment Resolved;
			if (auto Status =
			        ResolveArdaRHIFramebufferAttachment(Texture, Target.mAttachment, bDepthAttachment, Resolved);
			    !Status)
			{
				return Status;
			}
			if (SampleCount && Texture.mSampleCount != SampleCount)
			{
				return Invalid("Framebuffer attachments must have matching sample counts.");
			}
			SampleCount = Texture.mSampleCount;
			return FArdaRHIStatus{};
		};
		for (const auto& Target : V.mColorAttachments)
		{
			if (auto Status = ValidateTarget(Target, false); !Status)
			{
				return Status;
			}
		}
		return V.mDepthAttachment.mTexture ? ValidateTarget(V.mDepthAttachment, true) : FArdaRHIStatus{};
	}
}
