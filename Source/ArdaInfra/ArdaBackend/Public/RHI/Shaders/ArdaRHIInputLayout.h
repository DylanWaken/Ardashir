/** @file ArdaRHIInputLayout.h
 * Declares InputLayout definitions for the RHI shaders module.
 */

#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Resources/ArdaRHIFormat.h"
#include "RHI/Resources/ArdaRHIResource.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstddef>
#include <cstdint>

namespace arda
{
	/** Describes vertex attribute desc. */
	struct FArdaRHIVertexAttributeDesc
	{
		/** Stores the semantic name. */
		eastl::string mSemanticName;
		/** Stores the format. */
		EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
		/** Number of elements represented by the attribute. */
		uint32_t mArraySize = 1;
		/** Vertex-buffer binding index. */
		uint32_t mBufferIndex = 0;
		/** Byte offset within each vertex element. */
		uint32_t mOffset = 0;
		/** Vertex element stride in bytes. */
		uint32_t mElementStride = 0;
		/** Stores the instanced. */
		bool mbInstanced = false;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIVertexAttributeDesc& O) const noexcept
		{
			return mSemanticName == O.mSemanticName && mFormat == O.mFormat && mArraySize == O.mArraySize &&
			    mBufferIndex == O.mBufferIndex && mOffset == O.mOffset && mElementStride == O.mElementStride &&
			    mbInstanced == O.mbInstanced;
		}
	};

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHIVertexAttributeDesc& Value) noexcept;

	/**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
	[[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIVertexAttributeDesc& Value) noexcept;

	/** Complete deterministic key used to cache input layouts. */
	struct FArdaRHIInputLayoutDesc
	{
		/** Stores the attributes. */
		eastl::vector<FArdaRHIVertexAttributeDesc> mAttributes;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIInputLayoutDesc& O) const noexcept
		{
			return mAttributes == O.mAttributes;
		}
	};

	/** Interface for input layout. */
	class IArdaRHIInputLayout : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIInputLayoutDesc& GetDesc() const noexcept = 0;
	};

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHIInputLayoutDesc& Value) noexcept;

	/**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
	[[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIInputLayoutDesc& Value);
}
