/** @file ArdaRHISampler.h
 * Declares Sampler definitions for the RHI resources module.
 */

#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Resources/ArdaRHIColor.h"
#include "RHI/Resources/ArdaRHIResource.h"

#include <EASTL/string.h>
#include <cstddef>
#include <cstdint>

namespace arda
{
	/** Enumerates sampler address mode values. */
	enum class EArdaRHISamplerAddressMode : uint8_t
	{
		Clamp,
		Wrap,
		Border,
		Mirror,
		MirrorOnce
	};

	/** Enumerates sampler reduction values. */
	enum class EArdaRHISamplerReduction : uint8_t
	{
		Standard,
		Comparison,
		Minimum,
		Maximum
	};

	/** Describes sampler desc. */
	struct FArdaRHISamplerDesc
	{
		/** Stores the border color. */
		FArdaRHIColor mBorderColor{1.f, 1.f, 1.f, 1.f};
		/** Maximum anisotropy level. */
		float mMaxAnisotropy = 1.f;
		/** Mip-level-of-detail bias. */
		float mMipBias = 0.f;
		/** Whether minification filtering is enabled. */
		bool mbMinFilter = true;
		/** Whether magnification filtering is enabled. */
		bool mbMagFilter = true;
		/** Whether mip filtering is enabled. */
		bool mbMipFilter = true;
		/** Stores the address u. */
		EArdaRHISamplerAddressMode mAddressU = EArdaRHISamplerAddressMode::Clamp;
		/** Stores the address v. */
		EArdaRHISamplerAddressMode mAddressV = EArdaRHISamplerAddressMode::Clamp;
		/** Stores the address w. */
		EArdaRHISamplerAddressMode mAddressW = EArdaRHISamplerAddressMode::Clamp;
		/** Stores the reduction. */
		EArdaRHISamplerReduction mReduction = EArdaRHISamplerReduction::Standard;
		/** Stores the debug name. */
		eastl::string mDebugName;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHISamplerDesc& O) const noexcept;
	};

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHISamplerDesc& Value) noexcept;

	/**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
	[[nodiscard]] FArdaRHIStatus Validate(const FArdaRHISamplerDesc& Value) noexcept;

	/** Interface for sampler. */
	class IArdaRHISampler : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHISamplerDesc& GetDesc() const noexcept = 0;
	};
}
