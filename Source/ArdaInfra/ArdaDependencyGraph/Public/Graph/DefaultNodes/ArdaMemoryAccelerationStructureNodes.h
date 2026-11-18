#pragma once

#include "Graph/Node/ArdaNodeDefinition.h"

namespace arda
{
	/** GPU clone of a complete BLAS or TLAS; import both objects before attachment. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaMemoryCopyAccelerationStructureParameters)
		/** Source, built before execution or by an earlier graph node. */
		ARDA_NODE_ACCELERATION_STRUCTURE(mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::AccelStructRead)
		/** Distinct destination of the same kind and build flags, with at least the source allocation size.
		 * Create it from the source descriptor. TLAS clones keep the original BLAS addresses;
		 * retain those BLAS objects for as long as the cloned TLAS is used.
		 */
		ARDA_NODE_ACCELERATION_STRUCTURE(mDestination,
		    EArdaDependencyAccess::Write,
		    EArdaRHIResourceState::AccelStructWrite)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** GPU compaction of a completed BLAS or TLAS build that allowed compaction. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaMemoryCompactAccelerationStructureParameters)
		/** Source whose build was successfully submitted before attachment. Its contents must remain
		 * unchanged until compaction completes; build/update this source in a separate graph execution.
		 */
		ARDA_NODE_ACCELERATION_STRUCTURE(mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::AccelStructRead)
		/** Distinct imported destination of the same kind and build flags. Set mResultSizeOverride
		 * from GetAccelStructCompactedSize. Execution validates that size using the completed source
		 * query, which can wait for prior GPU work. TLAS BLAS references remain unchanged.
		 */
		ARDA_NODE_ACCELERATION_STRUCTURE(mDestination,
		    EArdaDependencyAccess::Write,
		    EArdaRHIResourceState::AccelStructWrite)
	ARDA_END_NODE_PARAMETER_STRUCT()

	/** Clones BLAS/TLAS storage with native acceleration-structure copy commands on the graphics queue. */
	class FArdaMemoryCopyAccelerationStructureNode final
	    : public TArdaNodeDefinition<FArdaMemoryCopyAccelerationStructureNode,
	          FArdaMemoryCopyAccelerationStructureParameters,
	          EArdaNodeType::Graphics>
	{
	public:
		/** Stable registry identity and implementation revision. */
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Requires native acceleration structures before device preparation. */
		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& Parameters);
		/** Validates imported identities, distinct objects, matching kinds and build flags. */
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
		/** Encodes both full graph resource identities. */
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		/** Validates build state/capacity and records the native clone. */
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaMemoryCopyAccelerationStructureNode, CopyAccelerationStructure);

	/** Compacts BLAS/TLAS storage with native commands on the graphics queue. */
	class FArdaMemoryCompactAccelerationStructureNode final
	    : public TArdaNodeDefinition<FArdaMemoryCompactAccelerationStructureNode,
	          FArdaMemoryCompactAccelerationStructureParameters,
	          EArdaNodeType::Graphics>
	{
	public:
		/** Stable registry identity and implementation revision. */
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Requires native acceleration structures and compaction support. */
		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& Parameters);
		/** Validates matching imported objects, completed source build and compaction descriptors. */
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
		/** Encodes both full graph resource identities. */
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		/** Validates the completed compact-size query before recording native compaction. */
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaMemoryCompactAccelerationStructureNode, CompactAccelerationStructure);

	/** Registers both acceleration-structure Memory nodes; safe to repeat. */
	FArdaRHIStatus RegisterArdaMemoryAccelerationStructureNodes();
}
