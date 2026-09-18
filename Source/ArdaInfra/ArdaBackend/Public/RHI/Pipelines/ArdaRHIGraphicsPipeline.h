/** @file ArdaRHIGraphicsPipeline.h
 * Declares GraphicsPipeline definitions for the RHI pipelines module.
 */

#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Pipelines/ArdaRHIFixedFunctionStates.h"
#include "RHI/Resources/ArdaRHIBuffer.h"
#include "RHI/Resources/ArdaRHIFormat.h"
#include "RHI/Resources/ArdaRHIFramebuffer.h"
#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Scheduling/ArdaRHIDrawTypes.h"
#include "RHI/Shaders/ArdaRHIBindingLayout.h"
#include "RHI/Shaders/ArdaRHIBindingSet.h"
#include "RHI/Shaders/ArdaRHIInputLayout.h"
#include "RHI/Shaders/ArdaRHIShader.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstddef>
#include <cstdint>

namespace arda
{
	/** Describes graphics pipeline desc. */
	struct FArdaRHIGraphicsPipelineDesc
	{
		/** Stores the topology. */
		EArdaRHIPrimitiveTopology mTopology = EArdaRHIPrimitiveTopology::TriangleList;
		/** Stores the patch control points. */
		uint32_t mPatchControlPoints = 0;
		/** Stores the input layout. */
		FArdaRHIInputLayoutRef mInputLayout;
		/** Vertex shader used by the pipeline. */
		FArdaRHIShaderRef mVertexShader;
		/** Hull shader used by the pipeline. */
		FArdaRHIShaderRef mHullShader;
		/** Domain shader used by the pipeline. */
		FArdaRHIShaderRef mDomainShader;
		/** Geometry shader used by the pipeline. */
		FArdaRHIShaderRef mGeometryShader;
		/** Pixel shader used by the pipeline. */
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
		bool operator==(const FArdaRHIGraphicsPipelineDesc& O) const noexcept
		{
			return mTopology == O.mTopology && mPatchControlPoints == O.mPatchControlPoints &&
			    mInputLayout == O.mInputLayout && mVertexShader == O.mVertexShader && mHullShader == O.mHullShader &&
			    mDomainShader == O.mDomainShader && mGeometryShader == O.mGeometryShader &&
			    mPixelShader == O.mPixelShader && mBindingLayouts == O.mBindingLayouts &&
			    mBlendState == O.mBlendState && mRasterState == O.mRasterState &&
			    mDepthStencilState == O.mDepthStencilState && mColorFormats == O.mColorFormats &&
			    mDepthFormat == O.mDepthFormat && mSampleCount == O.mSampleCount;
		}
	};

	/** Interface for graphics pipeline. */
	class IArdaRHIGraphicsPipeline : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIGraphicsPipelineDesc& GetDesc() const noexcept = 0;
	};

	/** Describes graphics state. */
	struct FArdaRHIGraphicsState
	{
		/** Stores the pipeline. */
		FArdaRHIGraphicsPipelineRef mPipeline;
		/** Stores the framebuffer. */
		FArdaRHIFramebufferRef mFramebuffer;
		/** Stores the bindings. */
		eastl::vector<FArdaRHIBindingSetRef> mBindings;
		/** Stores the vertex buffers. */
		eastl::vector<FArdaRHIVertexBufferBinding> mVertexBuffers;
		/** Stores the index buffer. */
		FArdaRHIBufferRef mIndexBuffer;
		/** Stores the index format. */
		EArdaRHIFormat mIndexFormat = EArdaRHIFormat::Unknown;
		/** Stores the index offset. */
		uint32_t mIndexOffset = 0;
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
	[[nodiscard]] size_t HashValue(const FArdaRHIGraphicsPipelineDesc& Value) noexcept;

	/**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
	[[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIGraphicsPipelineDesc& Value);
}
