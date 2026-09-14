#pragma once
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	/** Parameters owned by one Cornell Geometry node instance. */
	struct FArdaCornellGeometryParameters
	{
		/** Output vertex buffer. */
		FArdaDependencyResourceHandle mVertices;
		/** Output index buffer. */
		FArdaDependencyResourceHandle mIndices;
		/** Output material buffer. */
		FArdaDependencyResourceHandle mMaterials;
		/** Number of geometry compute groups along X. */
		uint32_t mGroupCountX = 1;
		/** Number of geometry compute groups along Y. */
		uint32_t mGroupCountY = 1;
		/** Scratch memory reserved while recording this node. */
		uint64_t mWorkspaceBytes = 0;
	};

	class FArdaCornellGeometryNode final
	    : public TArdaComputeDependencyNode<FArdaCornellGeometryNode, FArdaCornellGeometryParameters>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};
}
