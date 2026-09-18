/** @file ArdaRHIFramebuffer.h
 * Declares Framebuffer definitions for the RHI resources module.
 */

#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Resources/ArdaRHIFormat.h"
#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Resources/ArdaRHITexture.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace arda
{
	/** Describes framebuffer attachment. */
	struct FArdaRHIFramebufferAttachment
	{
		/** Stores the subresources. */
		FArdaRHITextureSubresourceRange mSubresources;
		/** Stores the format. */
		EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
		/** Stores the read only. */
		bool mbReadOnly = false;
	};

	/** Validates and resolves an attachment's format and subresource selection. The default mip sentinel
	 * selects one mip at the base; default array and plane counts select all remaining layers/aspects.
	 * Explicit stencil-only attachments are unsupported. Ownership and device limits are checked by the facade.
	 */
	[[nodiscard]] FArdaRHIStatus ResolveArdaRHIFramebufferAttachment(const FArdaRHITextureDesc& Texture,
	    const FArdaRHIFramebufferAttachment& Attachment,
	    bool bDepthAttachment,
	    FArdaRHIFramebufferAttachment& Out) noexcept;

	/** Describes framebuffer target. */
	struct FArdaRHIFramebufferTarget
	{
		/** Stores the texture. */
		FArdaRHITextureRef mTexture;
		/** Stores the attachment. */
		FArdaRHIFramebufferAttachment mAttachment;
	};

	/** Describes framebuffer desc. */
	struct FArdaRHIFramebufferDesc
	{
		/** Stores the color attachments. */
		eastl::vector<FArdaRHIFramebufferTarget> mColorAttachments;
		/** Stores the depth attachment. */
		FArdaRHIFramebufferTarget mDepthAttachment;
		/** Stores the debug name. */
		eastl::string mDebugName;
	};

	/** Interface for framebuffer. */
	class IArdaRHIFramebuffer : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIFramebufferDesc& GetDesc() const noexcept = 0;
	};

	/** Validates attachment usage, format, ranges, and matching sample counts. Different attachment extents
	 * are permitted; native rendering uses their common minimum extent. Device ownership/limits are separate.
	 */
	[[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIFramebufferDesc& Value);
}
