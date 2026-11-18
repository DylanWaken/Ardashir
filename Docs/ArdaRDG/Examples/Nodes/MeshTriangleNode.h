#pragma once
#include "Graph/Node/ArdaNodeDefinition.h"

namespace arda
{
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaMeshTriangleParameters)
		ARDA_NODE_COLOR_TARGET(mColor)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mWidth, 64)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mHeight, 64)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaMeshTriangleNode final
	    : public TArdaNodeDefinition<FArdaMeshTriangleNode, FArdaMeshTriangleParameters, EArdaNodeType::Meshlet>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		/** Hardware admission is checked before output declaration or device preparation. */
		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& Parameters);
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P);
		static FArdaRHIStatus Validate(const FArdaParameters& P);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaMeshTriangleNode, MeshTriangle);
}
