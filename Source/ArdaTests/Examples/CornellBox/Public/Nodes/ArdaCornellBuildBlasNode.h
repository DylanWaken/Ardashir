#pragma once
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	/** Parameters owned by one Cornell BuildBlas node instance. */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaCornellBuildBlasParameters)
		/** Input vertex buffer. */
		ARDA_NODE_BUFFER(mVertices, EArdaDependencyAccess::Read, EArdaRHIResourceState::AccelStructBuildInput)
		/** Input index buffer. */
		ARDA_NODE_BUFFER(mIndices, EArdaDependencyAccess::Read, EArdaRHIResourceState::AccelStructBuildInput)
		/** Destination bottom-level acceleration structure. */
		ARDA_NODE_ACCELERATION_STRUCTURE(mBlas, EArdaDependencyAccess::Write, EArdaRHIResourceState::AccelStructWrite)
		/** Number of vertices in the geometry. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mVertexCount, 0)
		/** Number of triangle indices in the geometry. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mIndexCount, 0)
		/** Size of each vertex in bytes. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mVertexStride, 0)
		/** Build policy for the bottom-level acceleration structure. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(EArdaRHIAccelStructBuildFlags,
		    mBuildFlags,
		    EArdaRHIAccelStructBuildFlags::PreferFastTrace)
		/** Required acceleration-structure build scratch size in bytes. */
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mWorkspaceBytes, 0)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaCornellBuildBlasNode final
	    : public TArdaNodeDefinition<FArdaCornellBuildBlasNode, FArdaCornellBuildBlasParameters, EArdaNodeType::Compute>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Hardware admission is checked before output declaration or device preparation. */
		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaCornellBuildBlasNode, CornellBuildBlas);
}
