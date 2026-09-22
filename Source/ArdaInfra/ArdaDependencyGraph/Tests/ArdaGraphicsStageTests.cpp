#include "ArdaDependencyGraph.h"
#include <EASTL/algorithm.h>
#include <gtest/gtest.h>

namespace arda::graphics_stage_tests
{
	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaWrongStageVisibility)
		ARDA_SHADER_BUFFER_SRV(mBuffer, 0, 0, EArdaRHIShaderStage::Compute)
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

	private:
		EArdaRHIShaderStage mStage;
		uint32_t mReferences = 0;
	};

	struct FArdaStageTestParameters
	{
		FArdaGraphStageValue mInput;
		FArdaDependencyResourceHandle mBuffer;
		bool mbKeep = false;
		bool mbWrongShader = false;
		uint32_t mIdentity = 0;
		bool mbWrongVisibility = false;
		bool mbMeshlet = false;
		bool mbTessellation = false;
		bool mbUAVWrite = false;
		uint64_t mBufferOffset = 0;
		uint64_t mBufferSize = ArdaRHIWholeBuffer;
	};

	struct FArdaStageTestInstance
	{
		uint32_t mCalls = 0;
	};

	template <EArdaGraphicsStage Stage>
	struct TArdaStageTestNode : TArdaGraphicsDependencyNode<TArdaStageTestNode<Stage>, FArdaStageTestParameters>
	{
		using FArdaParameters = FArdaStageTestParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaStageTestInstance;
		static constexpr EArdaGraphicsStage mGraphicsStage = Stage;

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

		static FArdaGraphStageValue GetStageInput(const FArdaParameters& P)
		{
			return P.mInput;
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
		const auto RasterA = StageTestRaster(Edit, {Vertex.GetVertices(), {}, false, false, 0});
		const auto RasterB = StageTestRaster(Edit, {Vertex.GetVertices(), {}, false, false, 1});
		ASSERT_TRUE(RasterA);
		ASSERT_TRUE(RasterB);
		const auto PixelA = StageTestPixel(Edit, {RasterA.GetFragments(), {}, true});
		const auto PixelB = StageTestPixel(Edit, {RasterB.GetFragments(), {}, true});
		ASSERT_TRUE(PixelA);
		ASSERT_TRUE(PixelB);
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto& A = Graph.GetTopology().TryGetNode(PixelA.GetHandle())->mPayload;
		const auto& B = Graph.GetTopology().TryGetNode(PixelB.GetHandle())->mPayload;
		EXPECT_EQ(A.mGraphicsStages,
		    (eastl::vector<FArdaGraphNodeHandle>{Vertex.GetHandle(), RasterA.GetHandle(), PixelA.GetHandle()}));
		EXPECT_EQ(B.mGraphicsStages,
		    (eastl::vector<FArdaGraphNodeHandle>{Vertex.GetHandle(), RasterB.GetHandle(), PixelB.GetHandle()}));
		ASSERT_EQ(A.mGraphicsStageParameters.size(), 3u);
		ASSERT_EQ(B.mGraphicsStageParameters.size(), 3u);
		EXPECT_NE(A.mGraphicsStageParameters.front().get(), B.mGraphicsStageParameters.front().get());
		const auto& VertexDefinition = Graph.GetTopology().TryGetNode(Vertex.GetHandle())->mPayload.mDefinition;
		FArdaDependencyExecutionContext Context;
		FArdaGraphicsWork Work;
		ASSERT_TRUE(VertexDefinition->mConfigureGraphics(Context, A.mGraphicsStageParameters.front().get(), Work));
		EXPECT_EQ(Work.mDraw.mVertexCount, 1u);
		ASSERT_TRUE(VertexDefinition->mConfigureGraphics(Context, B.mGraphicsStageParameters.front().get(), Work));
		EXPECT_EQ(Work.mDraw.mVertexCount, 1u);
		ASSERT_TRUE(VertexDefinition->mConfigureGraphics(Context, A.mGraphicsStageParameters.front().get(), Work));
		EXPECT_EQ(Work.mDraw.mVertexCount, 2u);
		ASSERT_EQ(A.mDesc.mPipelines.size(), 1u);
		ASSERT_EQ(B.mDesc.mPipelines.size(), 1u);
		EXPECT_FALSE(A.mDesc.mPipelines.front().mConfiguration->mGraphics.mDesc.mRasterState.mbFrontCounterClockwise);
		EXPECT_TRUE(B.mDesc.mPipelines.front().mConfiguration->mGraphics.mDesc.mRasterState.mbFrontCounterClockwise);
		EXPECT_EQ(A.mDesc.mPipelineStages.size(), 3u);
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Vertex.GetHandle(), PixelA.GetHandle()));
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Vertex.GetHandle(), PixelB.GetHandle()));
		EXPECT_TRUE(Graph.GetTopology().TryGetNode(Vertex.GetHandle())->mPayload.mDesc.mbPipelineStageOnly);
	}

	TEST(ArdaGraphicsStages, RejectsRasterizingFragmentsEvenOnCulledBranch)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
		const auto Raster = StageTestRaster(Edit, {Vertex.GetVertices()});
		ASSERT_TRUE(StageTestRaster(Edit, {Raster.GetFragments()}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
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
		const auto RasterA = StageTestRaster(Edit, {Vertex.GetVertices(), {}, false, false, 0});
		const auto RasterB = StageTestRaster(Edit, {Vertex.GetVertices(), {}, false, false, 1});
		const auto PixelA = StageTestPixel(Edit, {RasterA.GetFragments(), {}, true});
		const auto PixelB = StageTestPixel(Edit, {RasterB.GetFragments(), {}, true});
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_TRUE(Graph.GetTopology().TryGetNode(Vertex.GetHandle())->mPayload.mDesc.mAccesses.empty());
		const auto& AccessA = Graph.GetTopology().TryGetNode(PixelA.GetHandle())->mPayload.mDesc.mAccesses;
		const auto& AccessB = Graph.GetTopology().TryGetNode(PixelB.GetHandle())->mPayload.mDesc.mAccesses;
		ASSERT_EQ(AccessA.size(), 1u);
		ASSERT_EQ(AccessB.size(), 1u);
		EXPECT_EQ(AccessA.front().mResource, Buffer.GetHandle());
		EXPECT_EQ(AccessB.front().mResource, Buffer.GetHandle());
		const auto& LastUses = Graph.GetCompileResult().mMemoryPlan->mRequests[Buffer.GetHandle().mIndex].mLastUseNodes;
		EXPECT_NE(eastl::find(LastUses.begin(), LastUses.end(), PixelA.GetHandle().GetIndex()), LastUses.end());
		EXPECT_NE(eastl::find(LastUses.begin(), LastUses.end(), PixelB.GetHandle().GetIndex()), LastUses.end());
		EXPECT_EQ(eastl::find(LastUses.begin(), LastUses.end(), Vertex.GetHandle().GetIndex()), LastUses.end());
	}

	TEST(ArdaGraphicsStages, RejectsForeignAndForgedStateProducers)
	{
		FArdaDependencyGraph Foreign;
		FArdaGraphEdit ForeignEdit(Foreign);
		const auto Other = StageTestVertex(ForeignEdit, FArdaStageTestParameters{});
		ASSERT_TRUE(Other);
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(StageTestRaster(Edit, {Other.GetVertices()}));
		ASSERT_TRUE(Edit.Commit());
		EXPECT_FALSE(Graph.Compile());
	}

	TEST(ArdaGraphicsStages, RejectsWrongShaderStageOnDeadBranch)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(StageTestVertex(Edit, {{}, {}, false, true}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
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
		const auto Status = Graph.Compile();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("visibility"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, RejectsStateFromCanceledEditAndPreviousConstruction)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphVertices Canceled;
		{
			FArdaGraphEdit Edit(Graph);
			Canceled = StageTestVertex(Edit, FArdaStageTestParameters{}).GetVertices();
			ASSERT_TRUE(Edit.Cancel());
		}
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(StageTestRaster(Edit, {Canceled}));
			ASSERT_TRUE(Edit.Commit());
		}
		EXPECT_FALSE(Graph.Compile());
		ASSERT_TRUE(Graph.DiscardPendingEdits());
		FArdaGraphVertices Previous;
		{
			FArdaGraphEdit Edit(Graph);
			Previous = StageTestVertex(Edit, FArdaStageTestParameters{}).GetVertices();
			const auto Raster = StageTestRaster(Edit, {Previous});
			ASSERT_TRUE(StageTestPixel(Edit, {Raster.GetFragments(), {}, true}));
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Graph.Compile());
		(void)Graph.Execute();
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(StageTestRaster(Edit, {Previous}));
			ASSERT_TRUE(Edit.Commit());
		}
		EXPECT_FALSE(Graph.Compile());
	}

	TEST(ArdaGraphicsStages, StageConnectionsParticipateInPersistentRevisionIdentity)
	{
		FArdaDependencyGraph Graph;
		const auto Record = [&](bool Other)
		{
			FArdaGraphEdit Edit(Graph);
			const auto A = StageTestVertex(Edit, {{}, {}, false, false, 1});
			const auto B = StageTestVertex(Edit, {{}, {}, false, false, 2});
			const auto Raster = StageTestRaster(Edit, {(Other ? B : A).GetVertices()});
			EXPECT_TRUE(StageTestPixel(Edit, {Raster.GetFragments(), {}, true}));
			const auto OtherRaster = StageTestRaster(Edit, {(Other ? A : B).GetVertices()});
			EXPECT_TRUE(StageTestPixel(Edit, {OtherRaster.GetFragments()}));
			EXPECT_TRUE(Edit.Commit());
			const auto Status = Graph.Compile();
			EXPECT_TRUE(Status) << Status.mMessage.c_str();
			(void)Graph.Execute();
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
		const auto Raster = StageTestRaster(Edit, {Vertex.GetVertices()});
		ASSERT_TRUE(StageTestPixel(Edit, {Raster.GetFragments()}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_TRUE(Graph.GetCompileResult().mExecutionOrder.empty());
	}

	TEST(ArdaGraphicsStages, RejectsVertexWithoutRasterOrMaterialization)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(StageTestVertex(Edit, FArdaStageTestParameters{}, "unconsumed-vertex"));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
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
		ASSERT_TRUE(StageTestRaster(Edit, {Vertex.GetVertices()}, "unconsumed-raster"));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("unconsumed-raster"), eastl::string::npos);
		EXPECT_NE(Status.mMessage.find("pixel-stage consumer"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, RejectsVertexToPixelWithoutRaster)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
		ASSERT_TRUE(StageTestPixel(Edit, {Vertex.GetVertices(), {}, true}, "pixel-without-raster"));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("pixel-without-raster"), eastl::string::npos);
		EXPECT_NE(Status.mMessage.find("incompatible producer"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, RejectsExplicitOrderReversingStageDataflow)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
		const auto Raster = StageTestRaster(Edit, {Vertex.GetVertices()});
		const auto Pixel = StageTestPixel(Edit, {Raster.GetFragments(), {}, true});
		ASSERT_TRUE(Edit.Order(Pixel, Vertex));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
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
			FArdaGraphStageValue Captured = Vertex.GetVertices();
			if (CaptureRaster)
			{
				Captured = StageTestRaster(Edit, {Vertex.GetVertices()}).GetFragments();
			}
			const auto Buffer = FetchBuffer(Edit, Captured);
			ASSERT_TRUE(Buffer);
			Edit.Keep(Buffer);
			ASSERT_TRUE(Edit.Commit());
			const auto Status = Graph.Compile();
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
		ASSERT_TRUE(FetchBuffer(Edit, Vertex.GetVertices()));
		ASSERT_TRUE(StageTestRaster(Edit, {Vertex.GetVertices()}, "still-missing-pixel"));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
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
				EXPECT_TRUE(FetchBuffer(Edit, Vertex.GetVertices()));
			}
			Edit.Keep(Buffer);
			EXPECT_TRUE(Edit.Commit());
		};
		Record(true);
		ASSERT_TRUE(Graph.Compile());
		const auto Before = Graph.GetRevisionStats();
		(void)Graph.Execute();
		Record(false);
		const auto Status = Graph.Compile();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("Incomplete graphics pipeline"), eastl::string::npos);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, Before.mCacheHitCount);
	}

	TEST(ArdaGraphicsStages, CustomRulesCannotSilentlyOmitStageAssembly)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaInductorOptions Options;
		Options.mRules.mPreprocessRules.erase(Options.mRules.mPreprocessRules.begin());
		ASSERT_TRUE(Edit.SetOptions(Options));
		ASSERT_TRUE(StageTestVertex(Edit, FArdaStageTestParameters{}));
		ASSERT_TRUE(Edit.Commit());
		EXPECT_FALSE(Graph.Compile());
	}

	TEST(ArdaGraphicsStages, AssemblesTessellationAndGeometryIntoOneGraphicsPipeline)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
		const auto Hull = StageTestHull(Edit, {Vertex.GetVertices()});
		const auto Domain = StageTestDomain(Edit, {Hull.GetControlPoints()});
		const auto Geometry = StageTestGeometry(Edit, {Domain.GetVertices()});
		FArdaStageTestParameters RasterParameters;
		RasterParameters.mInput = Geometry.GetVertices();
		RasterParameters.mbTessellation = true;
		const auto Raster = StageTestRaster(Edit, RasterParameters);
		const auto Pixel = StageTestPixel(Edit, {Raster.GetFragments(), {}, true});
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto& Draw = Graph.GetTopology().TryGetNode(Pixel.GetHandle())->mPayload;
		EXPECT_EQ(Draw.mGraphicsStages,
		    (eastl::vector<FArdaGraphNodeHandle>{Vertex.GetHandle(),
		        Hull.GetHandle(),
		        Domain.GetHandle(),
		        Geometry.GetHandle(),
		        Raster.GetHandle(),
		        Pixel.GetHandle()}));
		EXPECT_EQ(Draw.mGraphicsStageParameters.size(), 6u);
		ASSERT_EQ(Draw.mDesc.mPipelines.size(), 1u);
		EXPECT_EQ(Draw.mDesc.mPipelines.front().mKind, EArdaPipelineStateKind::Graphics);
		EXPECT_EQ(Draw.mDesc.mPipelines.front().mConfiguration->mGraphics.mDesc.mTopology,
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
				MeshParameters.mInput = StageTestAmplification(Edit, FArdaStageTestParameters{}).GetMeshPayload();
			}
			const auto Mesh = StageTestMesh(Edit, MeshParameters);
			FArdaStageTestParameters RasterParameters;
			RasterParameters.mInput = Mesh.GetVertices();
			RasterParameters.mbMeshlet = true;
			const auto Raster = StageTestRaster(Edit, RasterParameters);
			const auto Pixel = StageTestPixel(Edit, {Raster.GetFragments(), {}, true});
			ASSERT_TRUE(Edit.Commit());
			const auto Status = Graph.Compile();
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			const auto& Draw = Graph.GetTopology().TryGetNode(Pixel.GetHandle())->mPayload;
			EXPECT_EQ(Draw.mGraphicsStages.size(), Amplification ? 4u : 3u);
			EXPECT_EQ(Draw.mDesc.mPipelines.front().mKind, EArdaPipelineStateKind::Meshlet);
			EXPECT_EQ(Draw.mDesc.mPipelines.front().mConfiguration->mKind, EArdaPipelineStateKind::Meshlet);
		}
	}

	TEST(ArdaGraphicsStages, InvalidTessellationConfigurationFailsBeforeCulling)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
		const auto Hull = StageTestHull(Edit, {Vertex.GetVertices()});
		const auto Domain = StageTestDomain(Edit, {Hull.GetControlPoints()});
		const auto Raster = StageTestRaster(Edit, {Domain.GetVertices()});
		ASSERT_TRUE(StageTestPixel(Edit, {Raster.GetFragments()}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("FArdaAssembleGraphicsStagesRule"), eastl::string::npos);
		EXPECT_NE(Status.mMessage.find("patch-list topology"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, BuiltinRasterInfersMeshFamilyFromState)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Mesh = StageTestMesh(Edit, FArdaStageTestParameters{});
		const auto Fragments = Rasterize(Edit, Mesh.GetVertices(), FArdaRHIViewport{0, 16, 0, 16, 0, 1});
		const auto Pixel = StageTestPixel(Edit, {Fragments, {}, true});
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto& Draw = Graph.GetTopology().TryGetNode(Pixel.GetHandle())->mPayload;
		EXPECT_EQ(Draw.mDesc.mPipelines.front().mKind, EArdaPipelineStateKind::Meshlet);
	}

	TEST(ArdaGraphicsStages, RejectsMixingTraditionalAndMeshFamilies)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Mesh = StageTestMesh(Edit, FArdaStageTestParameters{});
		const auto Raster = StageTestRaster(Edit, {Mesh.GetVertices()});
		ASSERT_TRUE(StageTestPixel(Edit, {Raster.GetFragments(), {}, true}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("pipeline families"), eastl::string::npos);
	}

	TEST(ArdaGraphicsStages, RejectsMeshToGeometryAndMissingDomainStage)
	{
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			const auto Mesh = StageTestMesh(Edit, FArdaStageTestParameters{});
			ASSERT_TRUE(StageTestGeometry(Edit, {Mesh.GetVertices()}));
			ASSERT_TRUE(Edit.Commit());
			EXPECT_FALSE(Graph.Compile());
		}
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			const auto Vertex = StageTestVertex(Edit, FArdaStageTestParameters{});
			const auto Hull = StageTestHull(Edit, {Vertex.GetVertices()});
			ASSERT_TRUE(
			    StageTestRaster(Edit, {FArdaGraphStageValue(Hull.GetHandle(), EArdaGraphStageValueKind::Vertices)}));
			ASSERT_TRUE(Edit.Commit());
			EXPECT_FALSE(Graph.Compile());
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
			const auto Raster = StageTestRaster(Edit, {Vertex.GetVertices()});
			FArdaStageTestParameters PixelParameters;
			PixelParameters.mInput = Raster.GetFragments();
			PixelParameters.mBuffer = Buffer.GetHandle();
			PixelParameters.mbKeep = true;
			PixelParameters.mBufferOffset = Disjoint ? 16 : 0;
			PixelParameters.mBufferSize = 16;
			ASSERT_TRUE(StageTestPixel(Edit, PixelParameters));
			ASSERT_TRUE(Edit.Commit());
			const auto Status = Graph.Compile();
			EXPECT_EQ(bool(Status), Disjoint) << Status.mMessage.c_str();
			if (!Disjoint)
			{
				EXPECT_NE(Status.mMessage.find("inter-stage memory barrier"), eastl::string::npos);
			}
		}
	}
}
