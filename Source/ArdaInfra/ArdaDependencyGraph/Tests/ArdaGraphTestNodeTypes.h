#pragma once
#include "Graph/Node/ArdaNodeDefinition.h"

namespace arda
{
	constexpr EArdaNodeType ArdaTestNodeType(EArdaDependencyNodeKind Kind)
	{
		switch (Kind)
		{
		case EArdaDependencyNodeKind::Graphics:
			return EArdaNodeType::Graphics;
		case EArdaDependencyNodeKind::Compute:
			return EArdaNodeType::Compute;
		case EArdaDependencyNodeKind::Cuda:
			return EArdaNodeType::Cuda;
		case EArdaDependencyNodeKind::Copy:
			return EArdaNodeType::Copy;
		default:
			return EArdaNodeType::Synchronization;
		}
	}

	constexpr EArdaNodeType ArdaTestNodeType(EArdaGraphicsStage Stage)
	{
		switch (Stage)
		{
		case EArdaGraphicsStage::Vertex:
			return EArdaNodeType::Vertex;
		case EArdaGraphicsStage::Hull:
			return EArdaNodeType::Hull;
		case EArdaGraphicsStage::Domain:
			return EArdaNodeType::Domain;
		case EArdaGraphicsStage::Geometry:
			return EArdaNodeType::Geometry;
		case EArdaGraphicsStage::Raster:
			return EArdaNodeType::Raster;
		case EArdaGraphicsStage::Pixel:
			return EArdaNodeType::Pixel;
		case EArdaGraphicsStage::Amplification:
			return EArdaNodeType::Amplification;
		default:
			return EArdaNodeType::Mesh;
		}
	}

	constexpr EArdaNodeType ArdaTestNodeType(EArdaRHIShaderStage Stage)
	{
		switch (Stage)
		{
		case EArdaRHIShaderStage::RayGeneration:
			return EArdaNodeType::RayGeneration;
		case EArdaRHIShaderStage::Miss:
			return EArdaNodeType::RayMiss;
		case EArdaRHIShaderStage::ClosestHit:
			return EArdaNodeType::RayClosestHit;
		case EArdaRHIShaderStage::AnyHit:
			return EArdaNodeType::RayAnyHit;
		case EArdaRHIShaderStage::Intersection:
			return EArdaNodeType::RayIntersection;
		default:
			return EArdaNodeType::RayCallable;
		}
	}
}
