/** @file ArdaRHIDrawTypes.h
 * Declares DrawTypes definitions for the RHI scheduling module.
 */

#pragma once

#include "RHI/Resources/ArdaRHIBuffer.h"
#include "RHI/Resources/ArdaRHIRef.h"

#include <cstdint>

namespace arda
{
	/** Describes viewport. */
	struct FArdaRHIViewport
	{
		/** Minimum X coordinate. */
		float mMinX = 0.f;
		/** Maximum X coordinate. */
		float mMaxX = 0.f;
		/** Minimum Y coordinate. */
		float mMinY = 0.f;
		/** Maximum Y coordinate. */
		float mMaxY = 0.f;
		/** Minimum depth value. */
		float mMinZ = 0.f;
		/** Maximum depth value. */
		float mMaxZ = 1.f;
	};

	/** Describes rect. */
	struct FArdaRHIRect
	{
		/** Minimum X coordinate. */
		int32_t mMinX = 0;
		/** Maximum X coordinate. */
		int32_t mMaxX = 0;
		/** Minimum Y coordinate. */
		int32_t mMinY = 0;
		/** Maximum Y coordinate. */
		int32_t mMaxY = 0;
	};

	/** Describes draw arguments. */
	struct FArdaRHIDrawArguments
	{
		/** Number of vertices or indices to draw. */
		uint32_t mVertexCount = 0;
		/** Number of instances to draw. */
		uint32_t mInstanceCount = 1;
		/** First index for indexed draws. */
		uint32_t mStartIndex = 0;
		/** First vertex or base-vertex offset. */
		uint32_t mStartVertex = 0;
		/** First instance identifier. */
		uint32_t mStartInstance = 0;
	};

	/** Describes vertex buffer binding. */
	struct FArdaRHIVertexBufferBinding
	{
		/** Stores the buffer. */
		FArdaRHIBufferRef mBuffer;
		/** Stores the slot. */
		uint32_t mSlot = 0;
		/** Stores the offset. */
		uint64_t mOffset = 0;
	};
}
