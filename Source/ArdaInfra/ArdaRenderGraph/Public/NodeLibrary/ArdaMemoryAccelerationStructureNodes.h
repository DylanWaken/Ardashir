#pragma once

#include "ArdaDependencyNode.h"

namespace arda
{
	/** GPU clone of a complete BLAS or TLAS; import both objects before attachment. */
	struct FArdaMemoryCopyAccelerationStructureParameters
	{
		/** Source, built before execution or by an earlier graph node. */
		FArdaDependencyResourceHandle mSource;
		/** Distinct destination of the same kind and build flags, with at least the source allocation size.
		 * Create it from the source descriptor. TLAS clones keep the original BLAS addresses;
		 * retain those BLAS objects for as long as the cloned TLAS is used.
		 */
		FArdaDependencyResourceHandle mDestination;
	};

	/** GPU compaction of a completed BLAS or TLAS build that allowed compaction. */
	struct FArdaMemoryCompactAccelerationStructureParameters
	{
		/** Source whose build was successfully submitted before attachment. Its contents must remain
		 * unchanged until compaction completes; build/update this source in a separate graph execution.
		 */
		FArdaDependencyResourceHandle mSource;
		/** Distinct imported destination of the same kind and build flags. Set mResultSizeOverride
		 * from GetAccelStructCompactedSize. Execution validates that size using the completed source
		 * query, which can wait for prior GPU work. TLAS BLAS references remain unchanged.
		 */
		FArdaDependencyResourceHandle mDestination;
	};

	/** Clones BLAS/TLAS storage with native acceleration-structure copy commands on the graphics queue. */
	class FArdaMemoryCopyAccelerationStructureNode final
	    : public TArdaGraphicsDependencyNode<FArdaMemoryCopyAccelerationStructureNode,
	          FArdaMemoryCopyAccelerationStructureParameters>
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
		/** Declares a whole-object source read and destination write. */
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		/** Validates build state/capacity and records the native clone. */
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	/** Compacts BLAS/TLAS storage with native commands on the graphics queue. */
	class FArdaMemoryCompactAccelerationStructureNode final
	    : public TArdaGraphicsDependencyNode<FArdaMemoryCompactAccelerationStructureNode,
	          FArdaMemoryCompactAccelerationStructureParameters>
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
		/** Declares a whole-object source read and destination write. */
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		/** Validates the completed compact-size query before recording native compaction. */
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	/** Registers both acceleration-structure Memory nodes; safe to repeat. */
	FArdaRHIStatus RegisterArdaMemoryAccelerationStructureNodes();
}
