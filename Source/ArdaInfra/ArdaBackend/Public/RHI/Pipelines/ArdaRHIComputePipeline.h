/** @file ArdaRHIComputePipeline.h
 * Declares ComputePipeline definitions for the RHI pipelines module.
 */

#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Shaders/ArdaRHIBindingLayout.h"
#include "RHI/Shaders/ArdaRHIBindingSet.h"
#include "RHI/Shaders/ArdaRHIShader.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstddef>
#include <cstdint>

namespace arda
{
	/** Describes compute pipeline desc. */
	struct FArdaRHIComputePipelineDesc
	{
		/** Stores the compute shader. */
		FArdaRHIShaderRef mComputeShader;
		/** Stores the binding layouts. */
		eastl::vector<FArdaRHIBindingLayoutRef> mBindingLayouts;
		/**
         * Stable metadata key used by backend-native persistent caches.
         * Zero disables named lookup. It is intentionally ignored by equality
         * and semantic descriptor hashing.
         */
		uint64_t mPersistentCacheKey = 0;
		/** Stores the debug name. */
		eastl::string mDebugName;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIComputePipelineDesc& O) const noexcept
		{
			return mComputeShader == O.mComputeShader && mBindingLayouts == O.mBindingLayouts;
		}
	};

	/** Interface for compute pipeline. */
	class IArdaRHIComputePipeline : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIComputePipelineDesc& GetDesc() const noexcept = 0;
	};

	/** Describes compute state. */
	struct FArdaRHIComputeState
	{
		/** Stores the pipeline. */
		FArdaRHIComputePipelineRef mPipeline;
		/** Stores the bindings. */
		eastl::vector<FArdaRHIBindingSetRef> mBindings;
	};

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHIComputePipelineDesc& Value) noexcept;

	/**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
	[[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIComputePipelineDesc& Value);
}
