#pragma once
#include "ArdaCornellNodeParameters.h"

namespace arda
{
	/** Parameters owned by one Cornell BuildBlas node instance. */
	struct FArdaCornellBuildBlasParameters
	{
		/** Input vertex buffer. */
		FArdaDependencyResourceHandle mVertices;
		/** Input index buffer. */
		FArdaDependencyResourceHandle mIndices;
		/** Destination bottom-level acceleration structure. */
		FArdaDependencyResourceHandle mBlas;
		/** Number of vertices in the geometry. */
		uint32_t mVertexCount = 0;
		/** Number of triangle indices in the geometry. */
		uint32_t mIndexCount = 0;
		/** Size of each vertex in bytes. */
		uint32_t mVertexStride = 0;
		/** Build policy for the bottom-level acceleration structure. */
		EArdaRHIAccelStructBuildFlags mBuildFlags = EArdaRHIAccelStructBuildFlags::PreferFastTrace;
		/** Required acceleration-structure build scratch size in bytes. */
		uint64_t mWorkspaceBytes = 0;
	};

	class FArdaCornellBuildBlasNode final
	    : public TArdaComputeDependencyNode<FArdaCornellBuildBlasNode, FArdaCornellBuildBlasParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Hardware admission is checked before output declaration or device preparation. */
		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& Parameters);
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};
}
