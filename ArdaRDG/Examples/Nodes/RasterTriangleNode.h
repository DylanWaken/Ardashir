#pragma once
#include "Graph/Node/ArdaNodeDefinition.h"

namespace arda
{
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaRecipeTriangleVertexParameters)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mVertexCount, 3)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaRecipeTriangleVertexNode final : public TArdaNodeDefinition<FArdaRecipeTriangleVertexNode,
	                                                FArdaRecipeTriangleVertexParameters,
	                                                EArdaNodeType::Vertex>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus Validate(const FArdaParameters& P);
		static FArdaGraphBuffer FetchBuffer(FArdaGraphEdit& Edit,
		    const FArdaParameters& P,
		    const FArdaStageBufferRequest& Request);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State);
		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState& State,
		    FArdaInstanceState& Instance,
		    FArdaGraphicsWork& Work);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaRecipeTriangleVertexNode, RecipeTriangleVS);

	/** Capture a prefix of the procedural triangle's clip-space float4 positions. */
	struct FArdaRecipeVertexCaptureParameters
	{
		FArdaDependencyResourceHandle mOutput;
		uint32_t mVertexCount = 3;
	};

	class FArdaRecipeVertexCaptureNode final : public TArdaNodeDefinition<FArdaRecipeVertexCaptureNode,
	                                               FArdaRecipeVertexCaptureParameters,
	                                               EArdaNodeType::Compute>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& P);
		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P);
		static FArdaRHIStatus Validate(const FArdaParameters& P);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaRecipeVertexCaptureNode, CaptureRecipeVertices);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaRecipeTriangleRasterParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphVertices, mInput)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mWidth, 64)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mHeight, 64)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaRecipeTriangleRasterNode final : public TArdaNodeDefinition<FArdaRecipeTriangleRasterNode,
	                                                FArdaRecipeTriangleRasterParameters,
	                                                EArdaNodeType::Raster>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static FArdaRHIStatus Validate(const FArdaParameters& P);
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State);
		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState& State,
		    FArdaInstanceState& Instance,
		    FArdaGraphicsWork& Work);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaRecipeTriangleRasterNode, RecipeTriangleRasterize);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaRecipeTrianglePixelParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphFragments, mInput)
		ARDA_NODE_COLOR_TARGET(mColor)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaRecipeTrianglePixelNode final : public TArdaNodeDefinition<FArdaRecipeTrianglePixelNode,
	                                               FArdaRecipeTrianglePixelParameters,
	                                               EArdaNodeType::Pixel>
	{
	public:
		struct FArdaState;
		static FArdaDependencyNodeMetadata GetMetadata();
		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef Device);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& State);
		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState& State,
		    FArdaInstanceState& Instance,
		    FArdaGraphicsWork& Work);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaRecipeTrianglePixelNode, RecipeTrianglePS);
}
