/** Validate graph resource shape before compilation resolves native usage. */
#pragma once
#include "Graph/Resources/ArdaDependencyResource.h"

namespace arda
{
	/** Retain only the shape required to rebind an external acceleration structure. */
	inline FArdaRHIAccelStructDesc ArdaGraphAccelerationStructureShape(FArdaRHIAccelStructDesc Description)
	{
		for (auto& Geometry : Description.mBottomLevelGeometries)
		{
			Geometry.mIndexBuffer = {};
			Geometry.mVertexOrAABBBuffer = {};
			Geometry.mOpacityMicromap = {};
			Geometry.mOpacityMicromapIndexBuffer = {};
		}
		return Description;
	}

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
		if (!Description.mBufferInitializer.IsEmpty() &&
		    (Description.mbTexture || Description.IsExternal() || !Description.mbPersistent ||
		        Description.mBuffer.mCpuAccess != EArdaRHICpuAccess::None ||
		        Description.mBufferInitializer.GetSize() > Description.mBuffer.mByteSize))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Initial contents require a graph-owned persistent GPU buffer and a contained byte prefix.");
		}
		if (Description.mExternalKind == EArdaGraphExternalResourceKind::AccelerationStructure)
		{
			return {};
		}
		// Native imports are already allocated and cannot request deferred deduction.
		if (Description.mbTexture)
		{
			return Validate(
			    Description.IsExternal() ? Description.mTexture : ArdaGraphShapeDescription(Description.mTexture));
		}
		return Validate(
		    Description.IsExternal() ? Description.mBuffer : ArdaGraphShapeDescription(Description.mBuffer));
	}
}
