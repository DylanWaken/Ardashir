/** @file ArdaRHIRayTracingPipeline.h
 * Declares RayTracingPipeline definitions for the RHI pipelines module.
 */

#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Shaders/ArdaRHIBindingLayout.h"
#include "RHI/Shaders/ArdaRHIBindingSet.h"
#include "RHI/Shaders/ArdaRHIShader.h"
#include "RHI/Shaders/ArdaRHIShaderTable.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstddef>
#include <cstdint>

namespace arda
{
	/** Describes ray tracing pipeline shader desc. */
	struct FArdaRHIRayTracingPipelineShaderDesc
	{
		/** Stores the export name. */
		eastl::string mExportName;
		/** Stores the shader. */
		FArdaRHIShaderRef mShader;
		/** Stores the local binding layout. */
		FArdaRHIBindingLayoutRef mLocalBindingLayout;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIRayTracingPipelineShaderDesc& O) const noexcept
		{
			return mExportName == O.mExportName && mShader == O.mShader && mLocalBindingLayout == O.mLocalBindingLayout;
		}
	};

	/** Describes ray tracing hit group desc. */
	struct FArdaRHIRayTracingHitGroupDesc
	{
		/** Stores the export name. */
		eastl::string mExportName;
		/** Closest-hit shader exported by the hit group. */
		FArdaRHIShaderRef mClosestHitShader;
		/** Any-hit shader exported by the hit group. */
		FArdaRHIShaderRef mAnyHitShader;
		/** Intersection shader exported by the hit group. */
		FArdaRHIShaderRef mIntersectionShader;
		/** Stores the local binding layout. */
		FArdaRHIBindingLayoutRef mLocalBindingLayout;
		/** Stores the procedural primitive. */
		bool mbProceduralPrimitive = false;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIRayTracingHitGroupDesc& O) const noexcept
		{
			return mExportName == O.mExportName && mClosestHitShader == O.mClosestHitShader &&
			    mAnyHitShader == O.mAnyHitShader && mIntersectionShader == O.mIntersectionShader &&
			    mLocalBindingLayout == O.mLocalBindingLayout && mbProceduralPrimitive == O.mbProceduralPrimitive;
		}
	};

	/** Describes ray tracing pipeline desc. */
	struct FArdaRHIRayTracingPipelineDesc
	{
		/** Stores the shaders. */
		eastl::vector<FArdaRHIRayTracingPipelineShaderDesc> mShaders;
		/** Stores the hit groups. */
		eastl::vector<FArdaRHIRayTracingHitGroupDesc> mHitGroups;
		/** Stores the global binding layouts. */
		eastl::vector<FArdaRHIBindingLayoutRef> mGlobalBindingLayouts;
		/** Maximum ray payload size in bytes. */
		uint32_t mMaxPayloadSize = 0;

		/** Maximum intersection attribute size in bytes. */
		uint32_t mMaxAttributeSize = sizeof(float) * 2;
		/** Stores the max recursion depth. */
		uint32_t mMaxRecursionDepth = 1;
		/** Stores the allow opacity micromaps. */
		bool mbAllowOpacityMicromaps = false;
		/** Stable renderer-supplied semantic key for native cache integration. */
		uint64_t mPersistentCacheKey = 0;
		/** Stores the debug name. */
		eastl::string mDebugName;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIRayTracingPipelineDesc& O) const noexcept
		{
			return mShaders == O.mShaders && mHitGroups == O.mHitGroups &&
			    mGlobalBindingLayouts == O.mGlobalBindingLayouts && mMaxPayloadSize == O.mMaxPayloadSize &&
			    mMaxAttributeSize == O.mMaxAttributeSize && mMaxRecursionDepth == O.mMaxRecursionDepth &&
			    mbAllowOpacityMicromaps == O.mbAllowOpacityMicromaps;
		}
	};

	/** Interface for ray tracing pipeline. */
	class IArdaRHIRayTracingPipeline : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIRayTracingPipelineDesc& GetDesc() const noexcept = 0;
	};

	/** Describes ray tracing state. */
	struct FArdaRHIRayTracingState
	{
		/** Stores the shader table. */
		FArdaRHIShaderTableRef mShaderTable;
		/** Stores the bindings. */
		eastl::vector<FArdaRHIBindingSetRef> mBindings;
	};

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHIRayTracingPipelineShaderDesc& Value) noexcept;

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHIRayTracingHitGroupDesc& Value) noexcept;

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHIRayTracingPipelineDesc& Value) noexcept;

	/**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
	[[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIRayTracingPipelineDesc& Value);
}
