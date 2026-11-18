#include "ArdaGraphTestNodeTypes.h"
#include "ArdaDependencyGraph.h"
#include "Inductor/Compiler/ArdaGraphRuleArchive.h"
#include "Inductor/Compiler/ArdaInductor.h"
#include <EASTL/algorithm.h>
#include <gtest/gtest.h>

namespace arda::graphics_stage_tests
{
	class FArdaRetainStageOptimizationRule final : public TArdaGraphPreprocessRule<FArdaRetainStageOptimizationRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "test.graphics.retain-stage-optimization";
		}

		FArdaRHIStatus Preprocess(FArdaInductorGraph& Graph) const
		{
			for (auto Handle : Graph.GetTopology().GetNodes())
			{
				const auto& Node = Graph.GetTopology().TryGetNode(Handle)->mPayload;
				if (Node.mName == "OriginalPixel" && Node.mbGraphicsLowered)
				{
					auto& Desc = Graph.GetNodeDescription(Handle);
					Desc.mEstimatedCost = 17;
					Desc.mWorkspaceBytes = 32;
					Desc.mTransientWorkspaceBytes = 16;
					Desc.mbSideEffect = true;
				}
			}
			return {};
		}
	};

	class FArdaReorderStageDeclarationsRule final : public TArdaGraphPreprocessRule<FArdaReorderStageDeclarationsRule>
	{
	public:
		static const char* GetRuleName() noexcept
		{
			return "test.graphics.reorder-declarations";
		}

		FArdaRHIStatus Preprocess(FArdaInductorGraph& Graph) const
		{
			for (auto Handle : Graph.GetTopology().GetNodes())
			{
				auto& Desc = Graph.GetNodeDescription(Handle);
				if (Desc.mGraphicsStage != EArdaGraphicsStage::Pixel)
				{
					continue;
				}
				eastl::reverse(Desc.mPipelineStages.begin(), Desc.mPipelineStages.end());
				auto Config =
				    eastl::make_shared<FArdaInductorPipelineConfiguration>(*Desc.mPipelines.front().mConfiguration);
				Config->mGraphics.mDesc.mSampleCount = 4;
				Desc.mPipelines.front().mConfiguration = eastl::move(Config);
			}
			return {};
		}
	};
	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaWrongStageVisibility)
		ARDA_SHADER_BUFFER_SRV(mBuffer, 0, 0, EArdaRHIShaderStage::Compute)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaEmptyTestShaderParameters)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	class FArdaStageTestShader final : public IArdaRHIShader
	{
	public:
		explicit FArdaStageTestShader(EArdaRHIShaderStage Stage)
		    : mStage(Stage)
		{
		}

		void AddRef() noexcept override
		{
			++mReferences;
		}

		void Release() noexcept override
		{
			if (--mReferences == 0)
			{
				delete this;
			}
		}

		EArdaRHIResourceType GetResourceType() const noexcept override
		{
			return EArdaRHIResourceType::Shader;
		}

		const char* GetDebugName() const noexcept override
		{
			return "Graphics stage fixture";
		}

		EArdaRHIShaderStage GetStage() const noexcept override
		{
			return mStage;
		}

		uint64_t GetPersistentCacheHash() const noexcept override
		{
			return uint64_t(mStage) + 42;
		}

		const FArdaShaderParameterMetadata* GetParameterMetadata() const noexcept override
		{
			return &FArdaEmptyTestShaderParameters::GetStaticMetadata();
		}

	private:
		EArdaRHIShaderStage mStage;
		uint32_t mReferences = 0;
	};

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaStageTestParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphStageValue, mInput)
		ARDA_NODE_PARAMETER(FArdaDependencyResourceHandle, mBuffer)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbKeep, false)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbWrongShader, false)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mIdentity, 0)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbWrongVisibility, false)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbMeshlet, false)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbTessellation, false)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbUAVWrite, false)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mBufferOffset, 0)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint64_t, mBufferSize, ArdaRHIWholeBuffer)
	ARDA_END_NODE_PARAMETER_STRUCT()

	struct FArdaStageTestInstance
	{
		uint32_t mCalls = 0;
	};

	template <EArdaGraphicsStage Stage>
	struct TArdaStageTestNode
	    : TArdaNodeDefinition<TArdaStageTestNode<Stage>, FArdaStageTestParameters, ArdaTestNodeType(Stage)>
	{
		using FArdaParameters = FArdaStageTestParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaStageTestInstance;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			switch (Stage)
			{
			case EArdaGraphicsStage::Vertex:
				return {"graphics.stages.test.vertex", 1};
			case EArdaGraphicsStage::Hull:
				return {"graphics.stages.test.hull", 1};
			case EArdaGraphicsStage::Domain:
				return {"graphics.stages.test.domain", 1};
			case EArdaGraphicsStage::Geometry:
				return {"graphics.stages.test.geometry", 1};
			case EArdaGraphicsStage::Amplification:
				return {"graphics.stages.test.amplification", 1};
			case EArdaGraphicsStage::Mesh:
				return {"graphics.stages.test.mesh", 1};
			case EArdaGraphicsStage::Raster:
				return {"graphics.stages.test.raster", 1};
			default:
				return {"graphics.stages.test.pixel", 1};
			}
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}
			    .Resource(P.mBuffer)
			    .Value(P.mbKeep)
			    .Value(P.mbWrongShader)
			    .Value(P.mIdentity)
			    .Value(P.mbWrongVisibility)
			    .Value(P.mbMeshlet)
			    .Value(P.mbTessellation)
			    .Value(P.mbUAVWrite)
			    .Value(P.mBufferOffset)
			    .Value(P.mBufferSize)
			    .Build();
		}

		static TArdaRHIResult<eastl::shared_ptr<FArdaInstanceState>> CreateInstance(FArdaRHIDeviceRef,
		    const FArdaParameters&,
		    const FArdaState&)
		{
			return {eastl::make_shared<FArdaInstanceState>(), {}};
		}

		static FArdaGraphBuffer FetchBuffer(FArdaGraphEdit& Edit,
		    const FArdaParameters& P,
		    const FArdaStageBufferRequest&)
		{
			if (P.mBuffer)
			{
				return Edit.UseBuffer(P.mBuffer);
			}
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = sizeof(uint32_t);
			const auto Buffer = AllocateBuffer(Edit, Desc);
			return Upload(Edit, Buffer, {uint8_t(P.mIdentity), 0, 0, 0});
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc D;
			D.mbSideEffect = P.mbKeep;
			if constexpr (Stage != EArdaGraphicsStage::Raster)
			{
				const auto ShaderStage = P.mbWrongShader         ? EArdaRHIShaderStage::Compute
				    : Stage == EArdaGraphicsStage::Vertex        ? EArdaRHIShaderStage::Vertex
				    : Stage == EArdaGraphicsStage::Hull          ? EArdaRHIShaderStage::Hull
				    : Stage == EArdaGraphicsStage::Domain        ? EArdaRHIShaderStage::Domain
				    : Stage == EArdaGraphicsStage::Geometry      ? EArdaRHIShaderStage::Geometry
				    : Stage == EArdaGraphicsStage::Amplification ? EArdaRHIShaderStage::Amplification
				    : Stage == EArdaGraphicsStage::Mesh          ? EArdaRHIShaderStage::Mesh
				                                                 : EArdaRHIShaderStage::Pixel;
				FArdaInductorPipelineContribution C;
				C.mShader = FArdaRHIShaderRef(new FArdaStageTestShader(ShaderStage));
				D.mPipelineStages.push_back(C);
			}
			else
			{
				auto C = eastl::make_shared<FArdaInductorPipelineConfiguration>();
				C->mKind = P.mbMeshlet ? EArdaPipelineStateKind::Meshlet : EArdaPipelineStateKind::Graphics;
				C->mGraphics.mDesc.mDepthStencilState.mbDepthTest = false;
				C->mGraphics.mDesc.mDepthStencilState.mbDepthWrite = false;
				C->mGraphics.mDesc.mRasterState.mbFrontCounterClockwise = P.mIdentity != 0;
				C->mMeshlet.mDesc.mDepthStencilState.mbDepthTest = false;
				C->mMeshlet.mDesc.mDepthStencilState.mbDepthWrite = false;
				if (P.mbTessellation)
				{
					C->mGraphics.mDesc.mTopology = EArdaRHIPrimitiveTopology::PatchList;
					C->mGraphics.mDesc.mPatchControlPoints = 3;
				}
				FArdaInductorPipelineContribution Contribution;
				Contribution.mConfiguration = C;
				D.mPipelineStages.push_back(Contribution);
			}
			if (P.mBuffer)
			{
				FArdaDependencyAccess Access;
				Access.mResource = P.mBuffer;
				Access.mAccess = Stage == EArdaGraphicsStage::Vertex && !P.mbUAVWrite ? EArdaDependencyAccess::Read
				                                                                      : EArdaDependencyAccess::Write;
				Access.mState = Stage == EArdaGraphicsStage::Vertex && !P.mbUAVWrite
				    ? EArdaRHIResourceState::VertexBuffer
				    : EArdaRHIResourceState::UnorderedAccess;
				Access.mBufferRange = {P.mBufferOffset, P.mBufferSize};
				D.mAccesses.push_back(Access);
			}
			if (P.mbWrongVisibility)
			{
				D.BindShader<FArdaWrongStageVisibility>({{"mBuffer", P.mBuffer}});
			}
			return D;
		}

		static FArdaRHIStatus ConfigureGraphics(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState& Instance,
		    FArdaGraphicsWork& Work)
		{
			Work.mDraw.mVertexCount = ++Instance.mCalls;
			return {};
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	using FArdaTestVertexStage = TArdaStageTestNode<EArdaGraphicsStage::Vertex>;
	using FArdaTestRasterStage = TArdaStageTestNode<EArdaGraphicsStage::Raster>;
	using FArdaTestPixelStage = TArdaStageTestNode<EArdaGraphicsStage::Pixel>;
	using FArdaTestHullStage = TArdaStageTestNode<EArdaGraphicsStage::Hull>;
	using FArdaTestDomainStage = TArdaStageTestNode<EArdaGraphicsStage::Domain>;
	using FArdaTestGeometryStage = TArdaStageTestNode<EArdaGraphicsStage::Geometry>;
	using FArdaTestAmplificationStage = TArdaStageTestNode<EArdaGraphicsStage::Amplification>;
	using FArdaTestMeshStage = TArdaStageTestNode<EArdaGraphicsStage::Mesh>;
	ARDA_DECLARE_GRAPH_NODE(FArdaTestVertexStage, StageTestVertex);
	ARDA_DEFINE_GRAPH_NODE(FArdaTestVertexStage, StageTestVertex);
	ARDA_DECLARE_GRAPH_NODE(FArdaTestRasterStage, StageTestRaster);
	ARDA_DEFINE_GRAPH_NODE(FArdaTestRasterStage, StageTestRaster);
	ARDA_DECLARE_GRAPH_NODE(FArdaTestPixelStage, StageTestPixel);
	ARDA_DEFINE_GRAPH_NODE(FArdaTestPixelStage, StageTestPixel);
	ARDA_DECLARE_GRAPH_NODE(FArdaTestHullStage, StageTestHull);
	ARDA_DEFINE_GRAPH_NODE(FArdaTestHullStage, StageTestHull);
	ARDA_DECLARE_GRAPH_NODE(FArdaTestDomainStage, StageTestDomain);
	ARDA_DEFINE_GRAPH_NODE(FArdaTestDomainStage, StageTestDomain);
	ARDA_DECLARE_GRAPH_NODE(FArdaTestGeometryStage, StageTestGeometry);
	ARDA_DEFINE_GRAPH_NODE(FArdaTestGeometryStage, StageTestGeometry);
	ARDA_DECLARE_GRAPH_NODE(FArdaTestAmplificationStage, StageTestAmplification);
	ARDA_DEFINE_GRAPH_NODE(FArdaTestAmplificationStage, StageTestAmplification);
	ARDA_DECLARE_GRAPH_NODE(FArdaTestMeshStage, StageTestMesh);
	ARDA_DEFINE_GRAPH_NODE(FArdaTestMeshStage, StageTestMesh);

	TEST(ArdaGraphicsStages, SharedVerticesGenerateIndependentPipelinesWithoutOrderEdges)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
		ASSERT_TRUE(Vertex);
		const auto RasterA = StageTestRaster(Edit, {Vertex, {}, false, false, 0});
		const auto RasterB = StageTestRaster(Edit, {Vertex, {}, false, false, 1});
		ASSERT_TRUE(RasterA);
		ASSERT_TRUE(RasterB);
		const auto PixelA = StageTestPixel(Edit, {RasterA, {}, true});
		const auto PixelB = StageTestPixel(Edit, {RasterB, {}, true});
		ASSERT_TRUE(PixelA);
		ASSERT_TRUE(PixelB);
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto& A = Graph.GetTopology().TryGetNode(PixelA.GetProducer())->mPayload;
		const auto& B = Graph.GetTopology().TryGetNode(PixelB.GetProducer())->mPayload;
		EXPECT_EQ(A.mGraphicsStages,
		    (eastl::vector<FArdaGraphNodeHandle>{Vertex.GetProducer(), RasterA.GetProducer(), PixelA.GetProducer()}));
		EXPECT_EQ(B.mGraphicsStages,
		    (eastl::vector<FArdaGraphNodeHandle>{Vertex.GetProducer(), RasterB.GetProducer(), PixelB.GetProducer()}));
		const auto& PreparedA = Graph.GetCompileResult().mNodes.at(PixelA.GetProducer());
		const auto& PreparedB = Graph.GetCompileResult().mNodes.at(PixelB.GetProducer());
		ASSERT_EQ(PreparedA.mGraphicsStageParameters.size(), 3u);
		ASSERT_EQ(PreparedB.mGraphicsStageParameters.size(), 3u);
		EXPECT_NE(PreparedA.mGraphicsStageParameters.front().get(), PreparedB.mGraphicsStageParameters.front().get());
		const auto& VertexDefinition = Graph.GetTopology().TryGetNode(Vertex.GetProducer())->mPayload.mDefinition;
		FArdaDependencyExecutionContext Context;
		FArdaGraphicsWork Work;
		ASSERT_TRUE(
		    VertexDefinition->mConfigureGraphics(Context, PreparedA.mGraphicsStageParameters.front().get(), Work));
		EXPECT_EQ(Work.mDraw.mVertexCount, 1u);
		ASSERT_TRUE(
		    VertexDefinition->mConfigureGraphics(Context, PreparedB.mGraphicsStageParameters.front().get(), Work));
		EXPECT_EQ(Work.mDraw.mVertexCount, 1u);
		ASSERT_TRUE(
		    VertexDefinition->mConfigureGraphics(Context, PreparedA.mGraphicsStageParameters.front().get(), Work));
		EXPECT_EQ(Work.mDraw.mVertexCount, 2u);
		ASSERT_EQ(Graph.GetNodeDescription(A.mHandle).mPipelines.size(), 1u);
		ASSERT_EQ(Graph.GetNodeDescription(B.mHandle).mPipelines.size(), 1u);
		EXPECT_FALSE(Graph.GetNodeDescription(A.mHandle)
		        .mPipelines.front()
		        .mConfiguration->mGraphics.mDesc.mRasterState.mbFrontCounterClockwise);
		EXPECT_TRUE(Graph.GetNodeDescription(B.mHandle)
		        .mPipelines.front()
		        .mConfiguration->mGraphics.mDesc.mRasterState.mbFrontCounterClockwise);
		EXPECT_EQ(Graph.GetNodeDescription(A.mHandle).mPipelineStages.size(), 3u);
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Vertex.GetProducer(), PixelA.GetProducer()));
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Vertex.GetProducer(), PixelB.GetProducer()));
		EXPECT_TRUE(Graph.GetNodeDescription(Vertex.GetProducer()).mbPipelineStageOnly);
	}

	TEST(ArdaGraphicsStages, RejectsRasterizingFragmentsEvenOnCulledBranch)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
		const auto Raster = StageTestRaster(Edit, {Vertex});
		ASSERT_TRUE(StageTestRaster(Edit, {Raster}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("incompatible producer"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, InputResourceHazardsAndLifetimeBelongToEveryAssembledDraw)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 16;
		BufferDesc.mUsage = EArdaRHIBufferUsage::Vertex;
		const auto Buffer = AllocateBuffer(Edit, BufferDesc);
		ASSERT_TRUE(Buffer);
		const auto Vertex = StageTestVertex(Edit, {{}, Buffer.GetHandle()});
		ASSERT_TRUE(Upload(Edit, Buffer, eastl::vector<uint8_t>(16, 1)));
		const auto RasterA = StageTestRaster(Edit, {Vertex, {}, false, false, 0});
		const auto RasterB = StageTestRaster(Edit, {Vertex, {}, false, false, 1});
		const auto PixelA = StageTestPixel(Edit, {RasterA, {}, true});
		const auto PixelB = StageTestPixel(Edit, {RasterB, {}, true});
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_TRUE(Graph.GetNodeDescription(Vertex.GetProducer()).mAccesses.empty());
		const auto& AccessA = Graph.GetNodeDescription(PixelA.GetProducer()).mAccesses;
		const auto& AccessB = Graph.GetNodeDescription(PixelB.GetProducer()).mAccesses;
		ASSERT_EQ(AccessA.size(), 1u);
		ASSERT_EQ(AccessB.size(), 1u);
		EXPECT_EQ(AccessA.front().mResource, Buffer.GetHandle());
		EXPECT_EQ(AccessB.front().mResource, Buffer.GetHandle());
		const auto& LastUses = Graph.GetCompileResult().mMemoryPlan->mRequests[Buffer.GetHandle().mIndex].mLastUseNodes;
		EXPECT_NE(eastl::find(LastUses.begin(), LastUses.end(), PixelA.GetProducer().GetIndex()), LastUses.end());
		EXPECT_NE(eastl::find(LastUses.begin(), LastUses.end(), PixelB.GetProducer().GetIndex()), LastUses.end());
		EXPECT_EQ(eastl::find(LastUses.begin(), LastUses.end(), Vertex.GetProducer().GetIndex()), LastUses.end());
	}

	TEST(ArdaGraphicsStages, RejectsForeignAndForgedStateProducers)
	{
		FArdaDependencyGraph Foreign;
		FArdaGraphEdit ForeignEdit(Foreign);
		const auto Other = StageTestVertex(ForeignEdit, FArdaStageTestParameters{});
		ASSERT_TRUE(Other);
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(StageTestRaster(Edit, {Other}));
		ASSERT_TRUE(Edit.Commit());
		EXPECT_FALSE(Graph.CompileAndWait());
	}

	TEST(ArdaGraphicsStages, RejectsWrongShaderStageOnDeadBranch)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(StageTestVertex(Edit, {{}, {}, false, true}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("different stage"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, RejectsWrongBindingVisibilityBeforeDeviceLayoutCreation)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaStageTestParameters Parameters;
		Parameters.mbWrongVisibility = true;
		ASSERT_TRUE(StageTestVertex(Edit, Parameters));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("visibility"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, RejectsStateFromCanceledEditAndPreviousConstruction)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphVertices Canceled;
		{
			FArdaGraphEdit Edit(Graph);
			Canceled = StageTestVertex(Edit, FArdaStageTestParameters{});
			ASSERT_TRUE(Edit.Cancel());
		}
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(StageTestRaster(Edit, {Canceled}));
			ASSERT_TRUE(Edit.Commit());
		}
		EXPECT_FALSE(Graph.CompileAndWait());
		ASSERT_TRUE(Graph.DiscardPendingEdits());
		FArdaGraphVertices Previous;
		{
			FArdaGraphEdit Edit(Graph);
			Previous = StageTestVertex(Edit, FArdaStageTestParameters{});
			const auto Raster = StageTestRaster(Edit, {Previous});
			ASSERT_TRUE(StageTestPixel(Edit, {Raster, {}, true}));
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Graph.CompileAndWait());
		(void)Graph.ExecuteAndWait();
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(StageTestRaster(Edit, {Previous}));
			ASSERT_TRUE(Edit.Commit());
		}
		EXPECT_FALSE(Graph.CompileAndWait());
	}

	TEST(ArdaGraphicsStages, StageConnectionsParticipateInPersistentRevisionIdentity)
	{
		FArdaDependencyGraph Graph;
		const auto Record = [&](bool Other)
		{
			FArdaGraphEdit Edit(Graph);
			const auto A = StageTestVertex(Edit, {{}, {}, false, false, 1});
			const auto B = StageTestVertex(Edit, {{}, {}, false, false, 2});
			const auto Raster = StageTestRaster(Edit, {(Other ? B : A)});
			EXPECT_TRUE(StageTestPixel(Edit, {Raster, {}, true}));
			const auto OtherRaster = StageTestRaster(Edit, {(Other ? A : B)});
			EXPECT_TRUE(StageTestPixel(Edit, {OtherRaster}));
			EXPECT_TRUE(Edit.Commit());
			const auto Status = Graph.CompileAndWait();
			EXPECT_TRUE(Status) << Status.mMessage.c_str();
			(void)Graph.ExecuteAndWait();
		};
		Record(false);
		const auto First = Graph.GetRevisionStats();
		Record(false);
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, First.mCompileCount);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, First.mCacheHitCount + 1);
		EXPECT_EQ(Graph.GetRevisionStats().mPersistentRevision, First.mPersistentRevision);
		Record(true);
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, First.mCompileCount + 1);
		EXPECT_NE(Graph.GetRevisionStats().mPersistentRevision, First.mPersistentRevision);
		Record(false);
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, First.mCompileCount + 1);
		EXPECT_EQ(Graph.GetRevisionStats().mPersistentRevision, First.mPersistentRevision);
	}

	TEST(ArdaGraphicsStages, CompleteUnusedPipelinesAreCulled)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
		const auto Raster = StageTestRaster(Edit, {Vertex});
		ASSERT_TRUE(StageTestPixel(Edit, {Raster}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_TRUE(Graph.GetCompileResult().mExecutionOrder.empty());
	}

	TEST(ArdaGraphicsStages, RejectsVertexWithoutRasterOrMaterialization)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(StageTestVertex(Edit, FArdaStageTestParameters{}, "unconsumed-vertex"));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("Incomplete graphics pipeline"), eastl::string::npos);
		EXPECT_NE(Status.mMessage.find("unconsumed-vertex"), eastl::string::npos);
		EXPECT_NE(Status.mMessage.find("rasterization"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, RejectsRasterWithoutPixelConsumerBeforeCulling)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
		ASSERT_TRUE(StageTestRaster(Edit, {Vertex}, "unconsumed-raster"));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("unconsumed-raster"), eastl::string::npos);
		EXPECT_NE(Status.mMessage.find("pixel-stage consumer"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, RejectsVertexToPixelWithoutRaster)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
		ASSERT_TRUE(StageTestPixel(Edit, {Vertex, {}, true}, "pixel-without-raster"));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("pixel-without-raster"), eastl::string::npos);
		EXPECT_NE(Status.mMessage.find("incompatible producer"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, RejectsExplicitOrderReversingStageDataflow)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(FArdaTestVertexStage::Register());
		ASSERT_TRUE(FArdaTestRasterStage::Register());
		ASSERT_TRUE(FArdaTestPixelStage::Register());
		const auto Vertex = Edit.Invoke(FArdaTestVertexStage::GetMetadata().mName, FArdaStageTestParameters{});
		const auto Raster =
		    Edit.Invoke(FArdaTestRasterStage::GetMetadata().mName, FArdaStageTestParameters{Vertex.GetVertices()});
		const auto Pixel = Edit.Invoke(FArdaTestPixelStage::GetMetadata().mName,
		    FArdaStageTestParameters{Raster.GetFragments(), {}, true});
		ASSERT_TRUE(Edit.Order(Pixel, Vertex));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("cycle"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, ExplicitMaterializationCompletesShaderAndRasterPrefixes)
	{
		for (bool CaptureRaster : {false, true})
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
			FArdaGraphStageValue Captured = Vertex;
			if (CaptureRaster)
			{
				Captured = StageTestRaster(Edit, {Vertex});
			}
			const auto Buffer = FetchBuffer(Edit, Captured);
			ASSERT_TRUE(Buffer);
			Edit.Keep(Buffer);
			ASSERT_TRUE(Edit.Commit());
			const auto Status = Graph.CompileAndWait();
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 1u);
			EXPECT_TRUE(Graph.GetTopology().TryGetNode(Captured.GetProducer())->mPayload.mbStageMaterialized);
		}
	}

	TEST(ArdaGraphicsStages, MaterializingVerticesDoesNotCompleteAnOrphanRaster)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
		ASSERT_TRUE(FetchBuffer(Edit, Vertex));
		ASSERT_TRUE(StageTestRaster(Edit, {Vertex}, "still-missing-pixel"));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("still-missing-pixel"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, MaterializationMarkerCannotReuseAnIncompleteConstructionFromCache)
	{
		FArdaDependencyGraph Graph;
		const auto Record = [&](bool Capture)
		{
			FArdaGraphEdit Edit(Graph);
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 4;
			Desc.mUsage = EArdaRHIBufferUsage::Vertex;
			const auto Buffer = AllocateBuffer(Edit, Desc);
			EXPECT_TRUE(Upload(Edit, Buffer, {1, 0, 0, 0}));
			const auto Vertex = StageTestVertex(Edit, {{}, Buffer.GetHandle()});
			if (Capture)
			{
				EXPECT_TRUE(FetchBuffer(Edit, Vertex));
			}
			Edit.Keep(Buffer);
			EXPECT_TRUE(Edit.Commit());
		};
		Record(true);
		ASSERT_TRUE(Graph.CompileAndWait());
		const auto Before = Graph.GetRevisionStats();
		(void)Graph.ExecuteAndWait();
		Record(false);
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("Incomplete graphics pipeline"), eastl::string::npos);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, Before.mCacheHitCount);
	}

	TEST(ArdaGraphicsStages, CustomRulesCannotSilentlyOmitStageAssembly)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaInductorOptions Options;
		Options.mRules.mPreprocessRules.erase(eastl::remove_if(Options.mRules.mPreprocessRules.begin(),
		                                          Options.mRules.mPreprocessRules.end(),
		                                          [](const auto& Rule)
		                                          {
			                                          return eastl::string(Rule->GetName()) ==
			                                              "FArdaAssembleGraphicsStagesRule";
		                                          }),
		    Options.mRules.mPreprocessRules.end());
		ASSERT_TRUE(Edit.SetOptions(Options));
		ASSERT_TRUE(StageTestVertex(Edit, FArdaStageTestParameters{}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("require FArdaAssembleGraphicsStagesRule"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, AssemblesTessellationAndGeometryIntoOneGraphicsPipeline)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
		const auto Hull = StageTestHull(Edit, {Vertex});
		const auto Domain = StageTestDomain(Edit, {Hull});
		const auto Geometry = StageTestGeometry(Edit, {Domain});
		FArdaStageTestParameters RasterParameters;
		RasterParameters.mInput = Geometry;
		RasterParameters.mbTessellation = true;
		const auto Raster = StageTestRaster(Edit, RasterParameters);
		const auto Pixel = StageTestPixel(Edit, {Raster, {}, true});
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto& Draw = Graph.GetTopology().TryGetNode(Pixel.GetProducer())->mPayload;
		EXPECT_EQ(Draw.mGraphicsStages,
		    (eastl::vector<FArdaGraphNodeHandle>{Vertex.GetProducer(),
		        Hull.GetProducer(),
		        Domain.GetProducer(),
		        Geometry.GetProducer(),
		        Raster.GetProducer(),
		        Pixel.GetProducer()}));
		EXPECT_EQ(Graph.GetCompileResult().mNodes.at(Pixel.GetProducer()).mGraphicsStageParameters.size(), 6u);
		ASSERT_EQ(Graph.GetNodeDescription(Draw.mHandle).mPipelines.size(), 1u);
		EXPECT_EQ(Graph.GetNodeDescription(Draw.mHandle).mPipelines.front().mKind, EArdaPipelineStateKind::Graphics);
		EXPECT_EQ(Graph.GetNodeDescription(Draw.mHandle).mPipelines.front().mConfiguration->mGraphics.mDesc.mTopology,
		    EArdaRHIPrimitiveTopology::PatchList);
	}

	TEST(ArdaGraphicsStages, AssemblesMeshWithOptionalAmplification)
	{
		for (bool Amplification : {false, true})
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			FArdaStageTestParameters MeshParameters;
			if (Amplification)
			{
				MeshParameters.mInput = StageTestAmplification(Edit, FArdaStageTestParameters{});
			}
			const auto Mesh = StageTestMesh(Edit, MeshParameters);
			FArdaStageTestParameters RasterParameters;
			RasterParameters.mInput = Mesh;
			RasterParameters.mbMeshlet = true;
			const auto Raster = StageTestRaster(Edit, RasterParameters);
			const auto Pixel = StageTestPixel(Edit, {Raster, {}, true});
			ASSERT_TRUE(Edit.Commit());
			const auto Status = Graph.CompileAndWait();
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			const auto& Draw = Graph.GetTopology().TryGetNode(Pixel.GetProducer())->mPayload;
			EXPECT_EQ(Draw.mGraphicsStages.size(), Amplification ? 4u : 3u);
			EXPECT_EQ(Graph.GetNodeDescription(Draw.mHandle).mPipelines.front().mKind, EArdaPipelineStateKind::Meshlet);
			EXPECT_EQ(Graph.GetNodeDescription(Draw.mHandle).mPipelines.front().mConfiguration->mKind,
			    EArdaPipelineStateKind::Meshlet);
		}
	}

	TEST(ArdaGraphicsStages, ArchivedReorderedDeclarationsKeepTheirShadersAndPortableConfiguration)
	{
		ASSERT_TRUE(RegisterArdaGraphRuleArchive<FArdaReorderStageDeclarationsRule>(
		    FArdaReorderStageDeclarationsRule::GetRuleName(),
		    1,
		    [](FArdaGraphArchiveWriter&, const FArdaReorderStageDeclarationsRule&)
		    {
			    return FArdaRHIStatus{};
		    },
		    [](FArdaGraphArchiveReader&) -> TArdaRHIResult<eastl::shared_ptr<const FArdaReorderStageDeclarationsRule>>
		    {
			    return {eastl::make_shared<FArdaReorderStageDeclarationsRule>(), {}};
		    }));
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaInductorOptions Options;
		Options.mRules.mPreprocessRules.push_back(MakeArdaGraphRule<FArdaReorderStageDeclarationsRule>());
		ASSERT_TRUE(Edit.SetOptions(Options));
		const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
		const auto Raster = StageTestRaster(Edit, {Vertex});
		const auto Pixel = StageTestPixel(Edit, {Raster, {}, true});
		ASSERT_TRUE(Edit.Commit());
		auto Processed = Graph.Preprocess();
		ASSERT_TRUE(Processed) << Processed.mStatus.mMessage.c_str();
		const auto& Expected = Processed.mValue->GetNodeDescription(Pixel.GetProducer());
		const auto Name = Processed.mValue->GetTopology().TryGetNode(Pixel.GetProducer())->mPayload.mName;
		auto Bytes = Processed.mValue->Serialize();
		ASSERT_TRUE(Bytes) << Bytes.mStatus.mMessage.c_str();
		auto Loaded = FArdaDependencyGraph::Deserialize(Bytes.mValue);
		ASSERT_TRUE(Loaded) << Loaded.mStatus.mMessage.c_str();
		const auto Result = FArdaInductor::Compile(*Loaded.mValue);
		ASSERT_TRUE(Result) << Result.mStatus.mMessage.c_str();
		const auto& Actual = Result.mValue.mNodes.at(Loaded.mValue->FindNode(Name)).mDesc;
		ASSERT_EQ(Actual.mPipelineStages.size(), Expected.mPipelineStages.size());
		for (size_t Index = 0; Index < Expected.mPipelineStages.size(); ++Index)
		{
			ASSERT_EQ(bool(Actual.mPipelineStages[Index].mShader), bool(Expected.mPipelineStages[Index].mShader));
			if (Actual.mPipelineStages[Index].mShader)
			{
				EXPECT_EQ(Actual.mPipelineStages[Index].mShader->GetStage(),
				    Expected.mPipelineStages[Index].mShader->GetStage());
			}
		}
		ASSERT_TRUE(Actual.mPipelines.front().mConfiguration);
		EXPECT_EQ(Actual.mPipelines.front().mConfiguration->mGraphics.mDesc.mSampleCount, 4u);
		EXPECT_EQ(Loaded.mValue->GetNodeDescription(Loaded.mValue->FindNode(Name))
		              .mPipelines.front()
		              .mConfiguration->mGraphics.mDesc.mSampleCount,
		    4u);
	}

	TEST(ArdaGraphicsStages, ArchivedPreprocessedGraphAcceptsNewChainsAndSharedStageConsumers)
	{
		FArdaDependencyGraph Source;
		FArdaGraphEdit Edit(Source);
		const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{}, "OriginalVertex");
		const auto Raster = StageTestRaster(Edit, FArdaStageTestParameters{Vertex}, "OriginalRaster");
		ASSERT_TRUE(StageTestPixel(Edit, FArdaStageTestParameters{Raster, {}, true}, "OriginalPixel"));
		ASSERT_TRUE(Edit.Commit());
		auto Processed = Source.Preprocess();
		ASSERT_TRUE(Processed) << Processed.mStatus.mMessage.c_str();
		auto Bytes = Processed.mValue->Serialize();
		ASSERT_TRUE(Bytes) << Bytes.mStatus.mMessage.c_str();
		auto Loaded = FArdaDependencyGraph::Deserialize(Bytes.mValue);
		ASSERT_TRUE(Loaded) << Loaded.mStatus.mMessage.c_str();

		FArdaGraphEdit Extension(*Loaded.mValue);
		FArdaInductorOptions Options;
		Options.mRules.mPreprocessRules.insert(Options.mRules.mPreprocessRules.begin(),
		    MakeArdaGraphRule<FArdaRetainStageOptimizationRule>());
		ASSERT_TRUE(Extension.SetOptions(Options));
		const FArdaGraphStageValue ExistingRaster(Loaded.mValue->FindNode("OriginalRaster"),
		    EArdaGraphStageValueKind::Fragments);
		ASSERT_TRUE(StageTestPixel(Extension, FArdaStageTestParameters{ExistingRaster, {}, true}, "SharedPixel"));
		const auto NewVertex = StageTestVertex(Extension, FArdaStageTestParameters{}, "NewVertex");
		const auto NewRaster = StageTestRaster(Extension, FArdaStageTestParameters{NewVertex}, "NewRaster");
		ASSERT_TRUE(StageTestPixel(Extension, FArdaStageTestParameters{NewRaster, {}, true}, "NewPixel"));
		ASSERT_TRUE(Extension.Commit());
		EXPECT_FALSE(Loaded.mValue->IsPreprocessed());
		auto Extended = Loaded.mValue->Preprocess();
		ASSERT_TRUE(Extended) << Extended.mStatus.mMessage.c_str();
		for (const char* Name : {"OriginalPixel", "SharedPixel", "NewPixel"})
		{
			const auto Handle = Extended.mValue->FindNode(Name);
			ASSERT_TRUE(Handle);
			const auto& Node = Extended.mValue->GetTopology().TryGetNode(Handle)->mPayload;
			EXPECT_TRUE(Node.mbGraphicsLowered);
			EXPECT_EQ(Node.mGraphicsStages.size(), 3u);
		}
		const auto& Original = Extended.mValue->GetNodeDescription(Extended.mValue->FindNode("OriginalPixel"));
		EXPECT_EQ(Original.mEstimatedCost, 17u);
		EXPECT_EQ(Original.mWorkspaceBytes, 32u);
		EXPECT_EQ(Original.mTransientWorkspaceBytes, 16u);
		EXPECT_TRUE(Original.mbSideEffect);
		EXPECT_EQ(Extended.mValue->GetNodeDescription(Extended.mValue->FindNode("SharedPixel")).mEstimatedCost, 3u);
		const auto Plan = FArdaInductor::Compile(*Extended.mValue);
		ASSERT_TRUE(Plan) << Plan.mStatus.mMessage.c_str();
	}

	TEST(ArdaGraphicsStages, InvalidTessellationConfigurationFailsBeforeCulling)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
		const auto Hull = StageTestHull(Edit, {Vertex});
		const auto Domain = StageTestDomain(Edit, {Hull});
		const auto Raster = StageTestRaster(Edit, {Domain});
		ASSERT_TRUE(StageTestPixel(Edit, {Raster}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("FArdaAssembleGraphicsStagesRule"), eastl::string::npos);
		EXPECT_NE(Status.mMessage.find("patch-list topology"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, BuiltinRasterInfersMeshFamilyFromState)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Mesh = StageTestMesh(Edit, FArdaStageTestParameters{});
		const auto Fragments = Rasterize(Edit, Mesh, FArdaRHIViewport{0, 16, 0, 16, 0, 1});
		const auto Pixel = StageTestPixel(Edit, {Fragments, {}, true});
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto& Draw = Graph.GetTopology().TryGetNode(Pixel.GetProducer())->mPayload;
		EXPECT_EQ(Graph.GetNodeDescription(Draw.mHandle).mPipelines.front().mKind, EArdaPipelineStateKind::Meshlet);
	}

	TEST(ArdaGraphicsStages, RejectsMixingTraditionalAndMeshFamilies)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Mesh = StageTestMesh(Edit, FArdaStageTestParameters{});
		const auto Raster = StageTestRaster(Edit, {Mesh});
		ASSERT_TRUE(StageTestPixel(Edit, {Raster, {}, true}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("pipeline families"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, RejectsMeshToGeometryAndMissingDomainStage)
	{
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			const auto Mesh = StageTestMesh(Edit, FArdaStageTestParameters{});
			ASSERT_TRUE(StageTestGeometry(Edit, {Mesh}));
			ASSERT_TRUE(Edit.Commit());
			EXPECT_FALSE(Graph.CompileAndWait());
		}
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
			const auto Hull = StageTestHull(Edit, {Vertex});
			ASSERT_TRUE(
			    StageTestRaster(Edit, {FArdaGraphStageValue(Hull.GetProducer(), EArdaGraphStageValueKind::Vertices)}));
			ASSERT_TRUE(Edit.Commit());
			EXPECT_FALSE(Graph.CompileAndWait());
		}
	}

	TEST(ArdaGraphicsStages, CrossStageWritesRequireDisjointDeclaredRanges)
	{
		for (bool Disjoint : {false, true})
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			FArdaRHIBufferDesc BufferDesc;
			BufferDesc.mByteSize = 32;
			BufferDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			const auto Buffer = AllocateBuffer(Edit, BufferDesc);
			FArdaStageTestParameters VertexParameters;
			VertexParameters.mBuffer = Buffer.GetHandle();
			VertexParameters.mbUAVWrite = true;
			VertexParameters.mBufferSize = 16;
			const auto Vertex = StageTestVertex(Edit, VertexParameters);
			const auto Raster = StageTestRaster(Edit, {Vertex});
			FArdaStageTestParameters PixelParameters;
			PixelParameters.mInput = Raster;
			PixelParameters.mBuffer = Buffer.GetHandle();
			PixelParameters.mbKeep = true;
			PixelParameters.mBufferOffset = Disjoint ? 16 : 0;
			PixelParameters.mBufferSize = 16;
			ASSERT_TRUE(StageTestPixel(Edit, PixelParameters));
			ASSERT_TRUE(Edit.Commit());
			const auto Status = Graph.CompileAndWait();
			EXPECT_EQ(bool(Status), Disjoint) << Status.mMessage.c_str();
			if (!Disjoint)
			{
				EXPECT_NE(Status.mMessage.find("inter-stage memory barrier"), eastl::string::npos);
			}
		}
	}
}
