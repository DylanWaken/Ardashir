/** @file ArdaRHIViews.h
 * Declares Views definitions for the RHI resources module.
 */

#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Resources/ArdaRHIBuffer.h"
#include "RHI/Resources/ArdaRHIFormat.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Resources/ArdaRHITexture.h"

#include <cstddef>

namespace arda
{
	/** Describes view desc. */
	struct FArdaRHIViewDesc
	{
		/** Stores the format. */
		EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
		/** Stores the dimension. */
		EArdaRHITextureDimension mDimension = EArdaRHITextureDimension::Unknown;
		/** Stores the texture range. */
		FArdaRHITextureSubresourceRange mTextureRange;
		/** Stores the buffer range. */
		FArdaRHIBufferRange mBufferRange;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIViewDesc& O) const noexcept
		{
			return mFormat == O.mFormat && mDimension == O.mDimension && mTextureRange == O.mTextureRange &&
			    mBufferRange == O.mBufferRange;
		}
	};

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHIViewDesc& Value) noexcept;

	/**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
	[[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIViewDesc& Value) noexcept;

	/** Interface for shader resource view. */
	class IArdaRHIShaderResourceView : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the resource.
         * @return The requested object pointer.
         */
		[[nodiscard]] virtual IArdaRHIResource* GetResource() const noexcept = 0;

		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIViewDesc& GetDesc() const noexcept = 0;
	};

	/** Interface for unordered access view. */
	class IArdaRHIUnorderedAccessView : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the resource.
         * @return The requested object pointer.
         */
		[[nodiscard]] virtual IArdaRHIResource* GetResource() const noexcept = 0;

		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIViewDesc& GetDesc() const noexcept = 0;
	};
}
