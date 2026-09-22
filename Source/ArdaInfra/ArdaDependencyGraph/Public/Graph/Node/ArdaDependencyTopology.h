/** Frozen graph nodes, value dependencies and overwrite ordering edges. */
#pragma once

#include "ArdaGraph.h"
#include "Graph/Node/ArdaDependencyNodeRegistry.h"

namespace arda
{
	/** Frozen node data owned by the abstract graph. */
	struct FArdaDependencyNode
	{
		eastl::string mName;
		eastl::string mCanonicalKey;
		eastl::shared_ptr<const FArdaDependencyNodeExecutable> mDefinition;
		/** Frozen execution parameters, using mDefinition->mPreparedParameterType after optional preparation.
		 * Class-authored nodes retain a private wrapper here; callers must not cast it to the public input schema.
		 */
		eastl::shared_ptr<const void> mParameters;
		FArdaDependencyNodeDesc mDesc;
		eastl::vector<FArdaDependencyNodeOutput> mOutputs;
		/** Original successful attachment order, preserved by deduplication and edit snapshots. */
		uint64_t mAttachmentOrder = 0;
		/** Canonical parameters with this graph's resource generations normalized for revision reuse. */
		eastl::string mConstructionKey;
		/** Compiler-owned traditional or meshlet stage chain recorded once for this terminal draw. */
		eastl::vector<FArdaGraphNodeHandle> mGraphicsStages;
		/** Per-draw parameter owners; immutable setup is shared but mutable stage instances are independent. */
		eastl::vector<eastl::shared_ptr<const void>> mGraphicsStageParameters;
		bool mbGraphicsLowered = false;
		bool mbRayTracingLowered = false;
		/** A successful explicit buffer materializer consumes this logical stage without requiring a pixel terminal. */
		bool mbStageMaterialized = false;
	};

	/** Value dependencies drive removal; overwrite hazards constrain execution without consuming a value. */
	struct FArdaDependencyEdge
	{
		bool mbResource = false;
		bool mbHazard = false;
	};

	using FArdaDependencyTopology = TArdaDirectedGraph<FArdaDependencyNode, FArdaDependencyEdge>;
}
