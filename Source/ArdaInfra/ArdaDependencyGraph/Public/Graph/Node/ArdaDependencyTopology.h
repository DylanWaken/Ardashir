/** Frozen graph nodes, value dependencies and overwrite ordering edges. */
#pragma once

#include "ArdaGraph.h"
#include "Graph/Node/ArdaDependencyNodeRegistry.h"

namespace arda
{
	/** One vectorized facade invocation. Lane nodes share this immutable object so compiler rules can
	 * retain array provenance while resolving their ordinary resource and pipeline dependencies.
	 */
	struct FArdaNodeArrayInvocation
	{
		size_t mLaneCount = 0;
	};

	/** Frozen node data owned by the abstract graph. */
	struct FArdaDependencyNode
	{
		eastl::string mName;
		eastl::string mCanonicalKey;
		eastl::shared_ptr<const FArdaDependencyNodeExecutable> mDefinition;
		eastl::vector<FArdaDependencyNodeOutput> mOutputs;
		/** Frozen public inputs retained for semantic identity after concurrent-edit slot remapping. */
		eastl::shared_ptr<const void> mSourceParameters;
		/** Original successful attachment order, preserved by deduplication and edit snapshots. */
		uint64_t mAttachmentOrder = 0;
		/** Canonical parameters with this graph's resource generations normalized for revision reuse. */
		eastl::string mConstructionKey;
		/** Compiler-owned traditional or meshlet stage chain recorded once for this terminal draw. */
		eastl::vector<FArdaGraphNodeHandle> mGraphicsStages;
		bool mbGraphicsLowered = false;
		bool mbRayTracingLowered = false;
		/** A successful explicit buffer materializer consumes this logical stage without requiring a pixel terminal. */
		bool mbStageMaterialized = false;
		/** Optional array invocation family; scalar calls have no family. */
		eastl::shared_ptr<const FArdaNodeArrayInvocation> mArrayInvocation;
		size_t mArrayLane = 0;
		/** This payload's complete topology identity; rebased when private edits are merged. */
		FArdaGraphNodeHandle mHandle;
		/** Structural optimization annotation produced by graph preprocessing. */
		bool mbCulled = false;
		/** Original portable payload retained while contextual references are unresolved. */
		eastl::string mArchivedParameters;
		bool mbUnboundExternalParameters = false;
		eastl::vector<eastl::string> mExternalParameterNames;
		/** Declarative effects and stage contributions transformed by graph preprocessing. */
		FArdaDependencyNodeDesc mDesc;
		/** Original stage declaration retained so later edits can assemble additional consumers. */
		eastl::shared_ptr<const FArdaDependencyNodeDesc> mStageDeclaration;
	};

	/** Value dependencies drive removal; overwrite hazards constrain execution without consuming a value. */
	struct FArdaDependencyEdge
	{
		bool mbResource = false;
		bool mbHazard = false;
	};

	using FArdaDependencyTopology = TArdaDirectedGraph<FArdaDependencyNode, FArdaDependencyEdge>;
}
