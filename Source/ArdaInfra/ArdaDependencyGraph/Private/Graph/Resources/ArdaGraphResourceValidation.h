/** Validate graph resource shape before compilation resolves native usage. */
#pragma once
#include "Graph/Resources/ArdaDependencyResource.h"

namespace arda
{
	template <class Usage>
	constexpr Usage ArdaExplicitResourceUsage(Usage Value)
	{
		return static_cast<Usage>(static_cast<uint16_t>(Value) & ~static_cast<uint16_t>(Usage::AutoDeduction));
	}

	/** Temporary descriptor for shape/transfer validation, never for native allocation.
	 * Initial-state/usage compatibility is checked against the resolved descriptor at compile time.
	 */
	template <class Descriptor>
	Descriptor ArdaGraphShapeDescription(Descriptor Description)
	{
		using EArdaUsage = decltype(Description.mUsage);
		if (HasAnyFlags(Description.mUsage, EArdaUsage::AutoDeduction))
		{
			Description.mUsage = ArdaExplicitResourceUsage(Description.mUsage);
			Description.mInitialState = EArdaRHIResourceState::Unknown;
		}
		return Description;
	}

	inline FArdaRHIStatus ValidateArdaGraphResourceDescription(const FArdaDependencyResourceDesc& Description)
	{
		if (Description.mExternalAccelerationStructure)
		{
			return {};
		}
		// Native imports are already allocated and cannot request deferred deduction.
		if (Description.mbTexture)
		{
			return Validate(
			    Description.mExternalTexture ? Description.mTexture : ArdaGraphShapeDescription(Description.mTexture));
		}
		return Validate(
		    Description.mExternalBuffer ? Description.mBuffer : ArdaGraphShapeDescription(Description.mBuffer));
	}
}
