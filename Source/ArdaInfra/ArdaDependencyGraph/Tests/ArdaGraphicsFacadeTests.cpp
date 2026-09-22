#include "ArdaDependencyGraph.h"
#include <gtest/gtest.h>

namespace arda::graphics_facade_tests
{
	struct FArdaFacadeVertexParameters
	{
		uint32_t mMarker = 7;
	};

	class FArdaFacadeVertexNode final : public TArdaVertexStageNode<FArdaFacadeVertexNode, FArdaFacadeVertexParameters>
	{
	public:
		inline static uint32_t mLastCaptureMarker = 0;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.graphics-facade.vertex", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder().Value(P.mMarker).Build();
		}

		// Facade tests deliberately stop before compilation, which validates the shader contribution.
		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			return {};
		}

		static FArdaGraphBuffer FetchBuffer(FArdaGraphEdit& Edit,
		    const FArdaParameters& P,
		    const FArdaStageBufferRequest& Request)
		{
			mLastCaptureMarker = P.mMarker;
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = (Request.mElementCount ? Request.mElementCount : 1) * sizeof(uint32_t);
			return AllocateBuffer(Edit, Desc);
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaFacadeVertexNode, FacadeVertices);
	ARDA_DEFINE_GRAPH_NODE(FArdaFacadeVertexNode, FacadeVertices);

	struct FArdaFacadeNoCaptureParameters
	{
	};

	class FArdaFacadeNoCaptureNode final
	    : public TArdaVertexStageNode<FArdaFacadeNoCaptureNode, FArdaFacadeNoCaptureParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.graphics-facade.no-capture", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters&)
		{
			return {};
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaFacadeNoCaptureNode, FacadeNoCapture);
	ARDA_DEFINE_GRAPH_NODE(FArdaFacadeNoCaptureNode, FacadeNoCapture);

	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaStageOutputBindings)
		ARDA_SHADER_BUFFER_UAV(mOutput, 0, 0, EArdaRHIShaderStage::Pixel)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	struct FArdaFacadeOutputParameters
	{
		FArdaGraphFragments mInput;
		FArdaDependencyResourceHandle mOutput;
		bool mbReadOnly = false;
	};

	class FArdaFacadeOutputNode final : public TArdaPixelStageNode<FArdaFacadeOutputNode, FArdaFacadeOutputParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.graphics-facade.output", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder().Resource(P.mOutput).Value(P.mbReadOnly).Build();
		}

		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& P)
		{
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 16;
			Desc.mStructureStride = 4;
			Desc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
			return Context.Buffer(P.mOutput, "Destination", Desc);
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.BindShader<FArdaStageOutputBindings>(
			    {{"mOutput", P.mOutput, 0, P.mbReadOnly ? EArdaDependencyAccess::Read : EArdaDependencyAccess::Write}});
			return Desc;
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaFacadeOutputNode, FacadeStageOutput);
	ARDA_DEFINE_GRAPH_NODE(FArdaFacadeOutputNode, FacadeStageOutput);

	static_assert(!eastl::is_convertible_v<FArdaGraphFragments, FArdaGraphVertices>);
	static_assert(!eastl::is_convertible_v<FArdaGraphVertices, FArdaDependencyResourceHandle>);
	static_assert(
	    eastl::is_same_v<decltype(FacadeVertices(eastl::declval<FArdaGraphEdit&>(), 1u)), FArdaGraphVertices>);
	static_assert(eastl::is_same_v<decltype(Rasterize(eastl::declval<FArdaGraphEdit&>(),
	                                   eastl::declval<FArdaRasterizeParameters>())),
	    FArdaGraphFragments>);
	static_assert(!eastl::is_invocable_v<const FArdaRasterizeNode&, FArdaGraphEdit&, FArdaGraphVertices, int>);
	static_assert(
	    !eastl::is_invocable_v<const FArdaRasterizeNode&, FArdaGraphEdit&, FArdaGraphFragments, FArdaRasterConfig>);

	TEST(ArdaGraphicsFacade, RasterConfigurationAndPositionalCallsShareFrozenSemantics)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Vertices = FacadeVertices(Edit);
		FArdaRasterConfig Config;
		Config.mViewport = {0, 32, 0, 16, 0, 1};
		Config.mRasterState.mCullMode = EArdaRHICullMode::None;
		Config.mRasterState.mbScissor = true;
		Config.mDepthStencilState = {true, true, EArdaRHIComparisonFunc::Less};
		Config.mScissor = {1, 31, 2, 14};
		Config.mDraw.mVertexCount = 9;
		Config.mTopology = EArdaRHIPrimitiveTopology::PatchList;
		Config.mPatchControlPoints = 3;
		const auto Structured = Rasterize(Edit, Vertices, Config);
		const auto Positional = Rasterize(Edit,
		    Vertices,
		    Config.mViewport,
		    Config.mRasterState,
		    Config.mDepthStencilState,
		    Config.mBlendState,
		    Config.mScissor,
		    Config.mDraw,
		    Config.mTopology,
		    Config.mPatchControlPoints);
		const auto Invoked = FArdaRasterizeNode::Invoke(Edit, Vertices, Config);
		const auto Packaged = Rasterize(Edit, FArdaRasterizeParameters{Vertices, Config});
		ASSERT_TRUE(Structured && Positional && Invoked && Packaged) << Edit.GetStatus().mMessage.c_str();
		const auto& Node = Graph.GetTopology().TryGetNode(Structured.GetProducer())->mPayload;
		for (auto Value : {Positional, Invoked, Packaged})
		{
			EXPECT_EQ(Node.mCanonicalKey, Graph.GetTopology().TryGetNode(Value.GetProducer())->mPayload.mCanonicalKey);
		}
		Config.mDepthStencilState.mbDepthWrite = false;
		const auto Changed = Rasterize(Edit, Vertices, Config);
		ASSERT_TRUE(Changed);
		// Recording another operation can reallocate topology storage: reacquire both nodes.
		const auto& Frozen = Graph.GetTopology().TryGetNode(Structured.GetProducer())->mPayload;
		EXPECT_TRUE(
		    Frozen.mDesc.mPipelineStages.front().mConfiguration->mGraphics.mDesc.mDepthStencilState.mbDepthWrite);
		EXPECT_NE(Frozen.mCanonicalKey, Graph.GetTopology().TryGetNode(Changed.GetProducer())->mPayload.mCanonicalKey);
		const auto Short = Rasterize(Edit, Vertices, Config.mViewport);
		const auto Defaults = Rasterize(Edit, Vertices, FArdaRasterConfig{Config.mViewport});
		ASSERT_TRUE(Short && Defaults);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Short.GetProducer())->mPayload.mCanonicalKey,
		    Graph.GetTopology().TryGetNode(Defaults.GetProducer())->mPayload.mCanonicalKey);
	}

	TEST(ArdaGraphicsFacade, BothRasterCallFormsPreserveValidation)
	{
		for (bool Structured : {false, true})
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			const auto Vertices = FacadeVertices(Edit);
			FArdaRasterConfig Config;
			const auto Fragments =
			    Structured ? Rasterize(Edit, Vertices, Config) : Rasterize(Edit, Vertices, Config.mViewport);
			EXPECT_FALSE(Fragments);
			EXPECT_NE(Edit.GetStatus().mMessage.find("viewport"), eastl::string::npos);
			EXPECT_FALSE(Edit.Commit());
		}
	}

	TEST(ArdaGraphicsFacade, TypedCallsAndMaterializersRetainParameterSnapshotsAcrossQueuedEdits)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphVertices Saved;
		{
			FArdaGraphEdit Edit(Graph);
			FArdaFacadeVertexParameters P{42};
			Saved = FacadeVertices(Edit, P);
			P.mMarker = 99;
			ASSERT_TRUE(Saved);
			ASSERT_TRUE(FacadeVertices(Edit, 8u));
			ASSERT_TRUE(FacadeVertices(Edit,
			    [](auto& Value)
			    {
				    Value.mMarker = 13;
			    }));
			ASSERT_TRUE(Edit.Commit());
		}
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = FetchBuffer(Edit, Saved, {4});
		ASSERT_TRUE(Buffer) << Edit.GetStatus().mMessage.c_str();
		EXPECT_EQ(FArdaFacadeVertexNode::mLastCaptureMarker, 42u);
		ASSERT_NE(Graph.FindResource(Buffer), nullptr);
		EXPECT_EQ(Graph.FindResource(Buffer)->mBuffer.mByteSize, 16u);
		EXPECT_TRUE(Graph.GetTopology().TryGetNode(Saved.GetProducer())->mPayload.mbStageMaterialized);
		ASSERT_TRUE(Edit.Cancel());
		EXPECT_FALSE(Graph.GetTopology().TryGetNode(Saved.GetProducer())->mPayload.mbStageMaterialized);
	}

	TEST(ArdaGraphicsFacade, FetchWithoutRegisteredMaterializerReportsUnsupported)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		EXPECT_FALSE(FetchBuffer(Edit, FacadeNoCapture(Edit)));
		EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::Unsupported);
		EXPECT_FALSE(Edit.Commit());
	}

	TEST(ArdaGraphicsFacade, FetchRejectsErasedTypeMismatchAndCanceledState)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphVertices Saved;
		{
			FArdaGraphEdit Edit(Graph);
			Saved = FacadeVertices(Edit);
			EXPECT_FALSE(FetchBuffer(Edit, {Saved.GetProducer(), EArdaGraphStageValueKind::Fragments}));
			EXPECT_FALSE(Edit.Commit());
		}
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(FacadeVertices(Edit));
		EXPECT_FALSE(FetchBuffer(Edit, Saved));
		EXPECT_FALSE(Edit.Commit());
	}

	TEST(ArdaGraphicsFacade, StageOutputDeclarationsUseShaderWritesBeforePipelineAssembly)
	{
		FArdaDependencyGraph Graph;
		{
			FArdaGraphEdit Edit(Graph);
			auto Pixels = FacadeStageOutput(Edit);
			ASSERT_TRUE(Pixels) << Edit.GetStatus().mMessage.c_str();
			EXPECT_TRUE(Edit.GetStageBuffer(Pixels));
		}
		FArdaGraphEdit Edit(Graph);
		EXPECT_FALSE(FacadeStageOutput(Edit, FArdaFacadeOutputParameters{{}, {}, true}));
		// The canonical shader schema rejects the missing write before stage-output validation.
		EXPECT_NE(Edit.GetStatus().mMessage.find("shader parameter requirement"), eastl::string::npos);
	}
}
