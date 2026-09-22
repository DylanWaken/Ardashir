#pragma once
#include "ArdaTriangleNodeParameters.h"

namespace arda
{
	class FArdaTriangleVertexNode final
	    : public TArdaNodeDefinition<FArdaTriangleVertexNode, FArdaTriangleVertexParameters, EArdaNodeType::Vertex>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance,
		    FArdaGraphicsWork& Work);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaTriangleVertexNode, TriangleVS);

	class FArdaTriangleRasterNode final
	    : public TArdaNodeDefinition<FArdaTriangleRasterNode, FArdaTriangleRasterParameters, EArdaNodeType::Raster>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance,
		    FArdaGraphicsWork& Work);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaTriangleRasterNode, TriangleRasterize);

	class FArdaTrianglePixelNode final
	    : public TArdaNodeDefinition<FArdaTrianglePixelNode, FArdaTrianglePixelParameters, EArdaNodeType::Pixel>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State);
		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance,
		    FArdaGraphicsWork& Work);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaTrianglePixelNode, TrianglePS);
}
