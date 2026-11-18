/** Reusable rasterization and explicit stage materialization operations. */
#pragma once

#include "Graph/Node/ArdaNodeDefinition.h"

namespace arda
{
	/** Reusable raster configuration, independent of the vertices being rasterized.
	 * Positional invocation follows this declaration order; omitted trailing fields use their defaults.
	 * Zero draw count preserves the vertex node's draw arguments.
	 */
	struct FArdaRasterConfig
	{
		FArdaRHIViewport mViewport;
		FArdaRHIRasterState mRasterState;
		FArdaRHIDepthStencilState mDepthStencilState{false, false};
		FArdaRHIBlendState mBlendState;
		FArdaRHIRect mScissor;
		FArdaRHIDrawArguments mDraw;
		EArdaRHIPrimitiveTopology mTopology = EArdaRHIPrimitiveTopology::TriangleList;
		uint32_t mPatchControlPoints = 0;
	};

	/** Invocation snapshot. The normal node facade accepts (Edit, Vertices, Config) or
	 * (Edit, Vertices, ConfigFields...) through these parameter constructors.
	 */
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaRasterizeParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphVertices, mInput)
		ARDA_NODE_PARAMETER(FArdaRasterConfig, mConfig)
	ARDA_END_NODE_PARAMETER_STRUCT(

	    FArdaRasterizeParameters() = default;

	    FArdaRasterizeParameters(FArdaGraphVertices Input, FArdaRasterConfig Config) : mInput(Input),
	    mConfig(eastl::move(Config)) {}

	    template <class... Arguments,
	        eastl::enable_if_t<TArdaGraphPositionalArguments<FArdaRasterConfig, Arguments...>::value, int> = 0,
	        class = decltype(FArdaRasterConfig{eastl::declval<Arguments>()...})>
	    FArdaRasterizeParameters(FArdaGraphVertices Input, Arguments&&... Values) : FArdaRasterizeParameters(Input,
	        FArdaRasterConfig{eastl::forward<Arguments>(Values)...}){})

	/** Fixed-function raster stage. Shader libraries may derive their own raster node to add a capture adapter. */
	class FArdaRasterizeNode final
	    : public TArdaNodeDefinition<FArdaRasterizeNode, FArdaRasterizeParameters, EArdaNodeType::Raster>
	{
	public:
		/** Fixed raster configuration does not depend on individual draw boundaries. */
		static constexpr EArdaGraphicsInstancing mGraphicsInstancing = EArdaGraphicsInstancing::ContiguousInstances;
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& P);
		static FArdaRHIStatus Validate(const FArdaParameters& P);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&);
		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext&,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaGraphicsWork& Work);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaRasterizeNode, Rasterize);

	/** Materialize the producer's declared capture representation into GPU-addressable storage.
	 * Calls its registered FetchBuffer adapter, which supplies the record layout and capture shaders.
	 * Logical values without an adapter fail explicitly. Use ReadBuffer for subsequent CPU readback.
	 */
	FArdaGraphBuffer FetchBuffer(FArdaGraphEdit& Edit,
	    FArdaGraphStageValue Value,
	    const FArdaStageBufferRequest& Request = {});
}
