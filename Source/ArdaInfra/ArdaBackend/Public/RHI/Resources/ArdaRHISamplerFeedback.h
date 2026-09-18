/** @file ArdaRHISamplerFeedback.h
 * Declares SamplerFeedback definitions for the RHI resources module.
 */

#pragma once

#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Resources/ArdaRHITexture.h"
#include "RHI/Scheduling/ArdaRHIResourceStates.h"

#include <EASTL/string.h>
#include <cstdint>

namespace arda
{
	/** Enumerates sampler feedback format values. */
	enum class EArdaRHISamplerFeedbackFormat : uint8_t
	{
		MinMipOpaque,
		MipRegionUsedOpaque
	};

	/** Describes sampler feedback texture desc. */
	struct FArdaRHISamplerFeedbackTextureDesc
	{
		/** Stores the format. */
		EArdaRHISamplerFeedbackFormat mFormat = EArdaRHISamplerFeedbackFormat::MinMipOpaque;
		/** Feedback mip-region width. */
		uint32_t mMipRegionX = 0;
		/** Feedback mip-region height. */
		uint32_t mMipRegionY = 0;
		/** Feedback mip-region depth. */
		uint32_t mMipRegionZ = 0;
		/** Stores the initial state. */
		EArdaRHIResourceState mInitialState = EArdaRHIResourceState::Unknown;
		/** Stores the keep initial state. */
		bool mbKeepInitialState = false;
		/** Stores the debug name. */
		eastl::string mDebugName;
	};

	/** Interface for sampler feedback texture. */
	class IArdaRHISamplerFeedbackTexture : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHISamplerFeedbackTextureDesc& GetDesc() const noexcept = 0;

		/**
         * Returns the paired texture.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHITextureRef& GetPairedTexture() const noexcept = 0;

		/** Native feedback-map identity used by state-conformance diagnostics. */
		[[nodiscard]] virtual const void* GetPhysicalIdentity() const noexcept = 0;
	};
}
