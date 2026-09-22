/** One definition template for executable operations and typed pipeline stages. */
#pragma once

#include "Graph/Node/ArdaGraphicsStageNode.h"
#include "Graph/Node/ArdaRayTracingStageNode.h"

namespace arda
{
	/** Selects both the execution contract and, where applicable, a typed pipeline interface.
	 * Graphics, Meshlet, RayTracing and WorkGraph describe complete executable operations.
	 * Individual graphics and ray shader stages compose automatically through logical stage values.
	 */
	enum class EArdaNodeType : uint8_t
	{
		Compute,
		Cuda,
		Copy,
		Synchronization,
		Graphics,
		Meshlet,
		RayTracing,
		WorkGraph,
		Vertex,
		Hull,
		Domain,
		Geometry,
		Raster,
		Pixel,
		Amplification,
		Mesh,
		RayGeneration,
		RayMiss,
		RayClosestHit,
		RayAnyHit,
		RayIntersection,
		RayCallable
	};

	template <EArdaNodeType Type>
	struct TArdaNodeDefinitionTraits;

#define ARDA_NODE_DEFINITION_DOMAIN(Type, Domain)                                                                      \
	template <>                                                                                                        \
	struct TArdaNodeDefinitionTraits<EArdaNodeType::Type>                                                              \
	{                                                                                                                  \
		template <class Node, class Parameters>                                                                        \
		using FArdaBase = TArdaDependencyNode<Node, Parameters, EArdaDependencyNodeKind::Domain>;                      \
	}
	ARDA_NODE_DEFINITION_DOMAIN(Compute, Compute);
	ARDA_NODE_DEFINITION_DOMAIN(Cuda, Cuda);
	ARDA_NODE_DEFINITION_DOMAIN(Copy, Copy);
	ARDA_NODE_DEFINITION_DOMAIN(Synchronization, Synchronization);
	ARDA_NODE_DEFINITION_DOMAIN(Graphics, Graphics);
	ARDA_NODE_DEFINITION_DOMAIN(Meshlet, Graphics);
	ARDA_NODE_DEFINITION_DOMAIN(RayTracing, Compute);
	ARDA_NODE_DEFINITION_DOMAIN(WorkGraph, Compute);
#undef ARDA_NODE_DEFINITION_DOMAIN

#define ARDA_NODE_DEFINITION_GRAPHICS(Stage)                                                                           \
	template <>                                                                                                        \
	struct TArdaNodeDefinitionTraits<EArdaNodeType::Stage>                                                             \
	{                                                                                                                  \
		template <class Node, class Parameters>                                                                        \
		using FArdaBase = TArdaGraphicsStageNode<Node, Parameters, EArdaGraphicsStage::Stage>;                         \
	}
	ARDA_NODE_DEFINITION_GRAPHICS(Vertex);
	ARDA_NODE_DEFINITION_GRAPHICS(Hull);
	ARDA_NODE_DEFINITION_GRAPHICS(Domain);
	ARDA_NODE_DEFINITION_GRAPHICS(Geometry);
	ARDA_NODE_DEFINITION_GRAPHICS(Raster);
	ARDA_NODE_DEFINITION_GRAPHICS(Pixel);
	ARDA_NODE_DEFINITION_GRAPHICS(Amplification);
	ARDA_NODE_DEFINITION_GRAPHICS(Mesh);
#undef ARDA_NODE_DEFINITION_GRAPHICS

#define ARDA_NODE_DEFINITION_RAY(Type, Stage)                                                                          \
	template <>                                                                                                        \
	struct TArdaNodeDefinitionTraits<EArdaNodeType::Type>                                                              \
	{                                                                                                                  \
		template <class Node, class Parameters>                                                                        \
		using FArdaBase = TArdaRayTracingStageNode<Node, Parameters, EArdaRHIShaderStage::Stage>;                      \
	}
	ARDA_NODE_DEFINITION_RAY(RayGeneration, RayGeneration);
	ARDA_NODE_DEFINITION_RAY(RayMiss, Miss);
	ARDA_NODE_DEFINITION_RAY(RayClosestHit, ClosestHit);
	ARDA_NODE_DEFINITION_RAY(RayAnyHit, AnyHit);
	ARDA_NODE_DEFINITION_RAY(RayIntersection, Intersection);
	ARDA_NODE_DEFINITION_RAY(RayCallable, Callable);
#undef ARDA_NODE_DEFINITION_RAY

	/** Uniform CRTP authoring entry. All specializations share registration, parameter snapshots,
	 * structured/positional invocation and ARDA_DECLARE_GRAPH_NODE/ARDA_DEFINE_GRAPH_NODE globals.
	 * Compute/native operations implement Record; CUDA implements PrepareCuda. Graphics stages implement
	 * ConfigureGraphics and return typed state. Ray providers return typed shader references, and a
	 * ray-generation terminal implements Record to dispatch its assembled pipeline and shader table.
	 */
	template <class Node, class Parameters, EArdaNodeType Type = EArdaNodeType::Compute>
	using TArdaNodeDefinition = typename TArdaNodeDefinitionTraits<Type>::template FArdaBase<Node, Parameters>;
}
