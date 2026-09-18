/** @file ArdaRHIMeshletPipeline.h
 * Declares MeshletPipeline definitions for the RHI pipelines module.
 */

#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Pipelines/ArdaRHIFixedFunctionStates.h"
#include "RHI/Resources/ArdaRHIFormat.h"
#include "RHI/Resources/ArdaRHIFramebuffer.h"
#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Scheduling/ArdaRHIDrawTypes.h"
#include "RHI/Shaders/ArdaRHIBindingLayout.h"
#include "RHI/Shaders/ArdaRHIBindingSet.h"
#include "RHI/Shaders/ArdaRHIShader.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstddef>
#include <cstdint>

namespace arda
{
	/** Describes meshlet pipeline desc. */
	struct FArdaRHIMeshletPipelineDesc
	{
		/** Stores the topology. */
		EArdaRHIPrimitiveTopology mTopology = EArdaRHIPrimitiveTopology::TriangleList;
		/** Stores the amplification shader. */
		FArdaRHIShaderRef mAmplificationShader;
		/** Stores the mesh shader. */
		FArdaRHIShaderRef mMeshShader;
		/** Stores the pixel shader. */
		FArdaRHIShaderRef mPixelShader;
		/** Stores the binding layouts. */
		eastl::vector<FArdaRHIBindingLayoutRef> mBindingLayouts;
		/** Stores the blend state. */
		FArdaRHIBlendState mBlendState;
		/** Stores the raster state. */
		FArdaRHIRasterState mRasterState;
		/** Stores the depth stencil state. */
		FArdaRHIDepthStencilState mDepthStencilState;
		/** Stores the color formats. */
		eastl::vector<EArdaRHIFormat> mColorFormats;
		/** Stores the depth format. */
		EArdaRHIFormat mDepthFormat = EArdaRHIFormat::Unknown;
		/** Stores the sample count. */
		uint32_t mSampleCount = 1;
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
		bool operator==(const FArdaRHIMeshletPipelineDesc& O) const noexcept
		{
			return mTopology == O.mTopology && mAmplificationShader == O.mAmplificationShader &&
			    mMeshShader == O.mMeshShader && mPixelShader == O.mPixelShader &&
			    mBindingLayouts == O.mBindingLayouts && mBlendState == O.mBlendState &&
			    mRasterState == O.mRasterState && mDepthStencilState == O.mDepthStencilState &&
			    mColorFormats == O.mColorFormats && mDepthFormat == O.mDepthFormat && mSampleCount == O.mSampleCount;
		}
	};

	/** Interface for meshlet pipeline. */
	class IArdaRHIMeshletPipeline : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIMeshletPipelineDesc& GetDesc() const noexcept = 0;
	};

	/** Describes meshlet state. */
	struct FArdaRHIMeshletState
	{
		/** Stores the pipeline. */
		FArdaRHIMeshletPipelineRef mPipeline;
		/** Stores the framebuffer. */
		FArdaRHIFramebufferRef mFramebuffer;
		/** Stores the bindings. */
		eastl::vector<FArdaRHIBindingSetRef> mBindings;
		/** Stores the viewports. */
		eastl::vector<FArdaRHIViewport> mViewports;
		/** Stores the scissors. */
		eastl::vector<FArdaRHIRect> mScissors;
	};

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHIMeshletPipelineDesc& Value) noexcept;

	/**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
	[[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIMeshletPipelineDesc& Value);
}
