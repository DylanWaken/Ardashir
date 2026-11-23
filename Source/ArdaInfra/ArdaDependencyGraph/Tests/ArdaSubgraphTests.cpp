#include "Graph/Node/ArdaNodeDefinition.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/DefaultNodes/ArdaGraphicsOperations.h"
#include "Inductor/Rules/ArdaInductorSubgraphRules.h"

#include <gtest/gtest.h>

namespace arda
{
	namespace
	{
		uint32_t SubgraphBuildCount = 0;
		uint32_t SubgraphStageMaterializeCount = 0;
		bool bRejectNestedSubgraphLeaf = false;
		FArdaDependencyGraph* CapturedSubgraphGraph = nullptr;
		FArdaGraphBuffer UndeclaredSubgraphInput;
		FArdaGraphVertices CapturedSubgraphVertices;

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaSubgraphTestParameters)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mDepth, 0)
			ARDA_NODE_BUFFER(mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaRecursiveSubgraphTestNode final : public TArdaNodeDefinition<FArdaRecursiveSubgraphTestNode,
		                                                 FArdaSubgraphTestParameters,
		                                                 EArdaNodeType::Subgraph>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.subgraph.recursive", 1};
			}

			static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& Parameters)
			{
				FArdaDependencyNodeRequirements Requirements;
				if (bRejectNestedSubgraphLeaf && !Parameters.mDepth)
				{
					Requirements.mFeatures.Require("queue.graphics");
				}
				return Requirements;
			}

			static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
			{
				FArdaRHIBufferDesc Description;
				Description.mByteSize = 16;
				return Context.Buffer(Parameters.mOutput, "Destination", Description);
			}

			static FArdaRHIStatus Build(FArdaSubgraphContext& Context, const FArdaParameters& Parameters)
			{
				++SubgraphBuildCount;
				if (Parameters.mDepth)
				{
					const auto Child = Invoke(Context.GetEdit(), FArdaParameters{Parameters.mDepth - 1}, "Child");
					return Context.DeclareOutput("Destination", Child.GetBuffer());
				}
				FArdaMemoryUploadBufferParameters Upload;
				Upload.mBytes.resize(16, 42);
				FArdaRHIBufferDesc Description;
				Description.mByteSize = 16;
				Upload.mDestination = AllocateBuffer(Context.GetEdit(), Description, "Scratch");
				return Context.DeclareOutput("Destination", UploadBuffer(Context.GetEdit(), Upload).GetBuffer());
			}
		};

		ARDA_DECLARE_GRAPH_NODE(FArdaRecursiveSubgraphTestNode, BuildRecursiveTest);
		ARDA_DEFINE_GRAPH_NODE(FArdaRecursiveSubgraphTestNode, BuildRecursiveTest);

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaSubgraphInputTestParameters)
			ARDA_NODE_BUFFER(mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::CopySource)
			ARDA_NODE_BUFFER(mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaInputSubgraphTestNode final : public TArdaNodeDefinition<FArdaInputSubgraphTestNode,
		                                             FArdaSubgraphInputTestParameters,
		                                             EArdaNodeType::Subgraph>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.subgraph.input", 1};
			}

			static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
			{
				FArdaRHIBufferDesc Description;
				Description.mByteSize = 16;
				return Context.Buffer(Parameters.mOutput, "Destination", Description);
			}

			static FArdaRHIStatus Build(FArdaSubgraphContext& Context, const FArdaParameters&)
			{
				const auto Source = Context.RequestBufferInput("mSource");
				const auto Destination = Context.GetBufferOutput();
				CopyBuffer(Context.GetEdit(), Source, Destination);
				return Context.DeclareOutput("Destination", Destination);
			}
		};

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaSubgraphPassthroughParameters)
			ARDA_NODE_BUFFER(mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::CopySource)
			ARDA_NODE_BUFFER(mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbRequireUnorderedAccess, false)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaPassthroughSubgraphTestNode final : public TArdaNodeDefinition<FArdaPassthroughSubgraphTestNode,
		                                                   FArdaSubgraphPassthroughParameters,
		                                                   EArdaNodeType::Subgraph>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.subgraph.passthrough", 1};
			}

			static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
			{
				FArdaRHIBufferDesc Description;
				Description.mByteSize = 16;
				if (Parameters.mbRequireUnorderedAccess)
				{
					Description.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
				}
				return Context.Buffer(Parameters.mOutput, "Destination", Description);
			}

			static FArdaRHIStatus Build(FArdaSubgraphContext& Context, const FArdaParameters&)
			{
				return Context.DeclareOutput("Destination", Context.RequestBufferInput("mSource"));
			}
		};

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaSubgraphCycleTestParameters)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaCycleSubgraphTestNode final : public TArdaNodeDefinition<FArdaCycleSubgraphTestNode,
		                                             FArdaSubgraphCycleTestParameters,
		                                             EArdaNodeType::Subgraph>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.subgraph.cycle", 1};
			}

			static FArdaRHIStatus Build(FArdaSubgraphContext& Context, const FArdaParameters&)
			{
				Invoke(Context.GetEdit(), FArdaParameters{});
				return Context.GetStatus();
			}
		};

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaSubgraphVertexParameters)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaSubgraphVertexTestNode final : public TArdaNodeDefinition<FArdaSubgraphVertexTestNode,
		                                              FArdaSubgraphVertexParameters,
		                                              EArdaNodeType::Vertex>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.subgraph.vertex", 1};
			}

			static FArdaGraphBuffer FetchBuffer(FArdaGraphEdit& Edit,
			    const FArdaParameters&,
			    const FArdaStageBufferRequest&)
			{
				++SubgraphStageMaterializeCount;
				FArdaMemoryUploadBufferParameters Parameters;
				Parameters.mBytes.resize(16, 42);
				return UploadBuffer(Edit, Parameters).GetBuffer();
			}
		};

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaSubgraphRasterParameters)
			ARDA_NODE_STAGE_INPUT(FArdaGraphVertices, mVertices)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaSubgraphRasterTestNode final : public TArdaNodeDefinition<FArdaSubgraphRasterTestNode,
		                                              FArdaSubgraphRasterParameters,
		                                              EArdaNodeType::Raster>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.subgraph.raster", 1};
			}
		};

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaSubgraphBoundaryTestParameters)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbOpenParentEdit, false)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbCaptureStage, false)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbKeepUndeclaredResource, false)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbMaterializeOuterStage, false)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaBoundarySubgraphTestNode final : public TArdaNodeDefinition<FArdaBoundarySubgraphTestNode,
		                                                FArdaSubgraphBoundaryTestParameters,
		                                                EArdaNodeType::Subgraph>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.subgraph.boundary", 1};
			}

			static FArdaRHIStatus Build(FArdaSubgraphContext& Context, const FArdaParameters& Parameters)
			{
				if (Parameters.mbOpenParentEdit)
				{
					FArdaGraphEdit ParentEdit(*CapturedSubgraphGraph);
					return ParentEdit.GetStatus();
				}
				if (Parameters.mbCaptureStage)
				{
					FArdaSubgraphRasterTestNode::Invoke(Context.GetEdit(),
					    FArdaSubgraphRasterParameters{CapturedSubgraphVertices});
					return Context.GetStatus();
				}
				if (Parameters.mbKeepUndeclaredResource)
				{
					Context.GetEdit().Keep(UndeclaredSubgraphInput);
					return Context.GetStatus();
				}
				if (Parameters.mbMaterializeOuterStage)
				{
					FetchBuffer(Context.GetEdit(), CapturedSubgraphVertices);
					return Context.GetStatus();
				}
				FArdaMemoryUploadBufferParameters Upload;
				Upload.mDestination = UndeclaredSubgraphInput;
				Upload.mBytes.resize(16, 42);
				UploadBuffer(Context.GetEdit(), Upload);
				return Context.GetStatus();
			}
		};

		class FArdaObserveSubgraphExpansionRule final : public FArdaGraphPreprocessRule
		{
		public:
			FArdaObserveSubgraphExpansionRule(eastl::vector<size_t>& Counts, bool bReverseRoots = false)
			    : mCounts(Counts),
			      mbReverseRoots(bReverseRoots)
			{
			}

			const char* GetName() const noexcept override
			{
				return "ObserveSubgraphExpansion";
			}

			FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const override
			{
				size_t Count = 0;
				FArdaGraphNodeHandle First;
				FArdaGraphNodeHandle Second;
				for (auto Handle : Graph.GetTopology().GetNodes())
				{
					const auto& Node = Graph.GetTopology().TryGetNode(Handle)->mPayload;
					Count += bool(Node.mDefinition->mExpand);
					if (Node.mName == "First")
					{
						First = Handle;
					}
					if (Node.mName == "Second")
					{
						Second = Handle;
					}
				}
				mCounts.push_back(Count);
				if (mbReverseRoots)
				{
					(void)Graph.GetTopology().AddEdge(Second, First, {});
				}
				return {};
			}

		private:
			eastl::vector<size_t>& mCounts;
			bool mbReverseRoots;
		};

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaSubgraphMissingOutputParameters)
			ARDA_NODE_BUFFER(mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaMissingOutputSubgraphTestNode final : public TArdaNodeDefinition<FArdaMissingOutputSubgraphTestNode,
		                                                     FArdaSubgraphMissingOutputParameters,
		                                                     EArdaNodeType::Subgraph>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.subgraph.missing-output", 1};
			}

			static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
			{
				FArdaRHIBufferDesc Description;
				Description.mByteSize = 16;
				return Context.Buffer(Parameters.mOutput, "Destination", Description);
			}

			static FArdaRHIStatus Build(FArdaSubgraphContext&, const FArdaParameters&)
			{
				return {};
			}
		};
	}

	TEST(ArdaSubgraph, ExpansionRunsAtItsConfiguredRulePositionAndPreservesCustomEdges)
	{
		SubgraphBuildCount = 0;
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		Edit.Keep(BuildRecursiveTest(Edit, FArdaSubgraphTestParameters{1}, "First").GetBuffer());
		Edit.Keep(BuildRecursiveTest(Edit, FArdaSubgraphTestParameters{}, "Second").GetBuffer());
		eastl::vector<size_t> Counts;
		FArdaInductorOptions Options;
		Options.mRules.mPreprocessRules.insert(Options.mRules.mPreprocessRules.begin(),
		    MakeArdaGraphRule<FArdaObserveSubgraphExpansionRule>(Counts, true));
		Options.mRules.mPreprocessRules.insert(Options.mRules.mPreprocessRules.begin() + 2,
		    MakeArdaGraphRule<FArdaObserveSubgraphExpansionRule>(Counts));
		Options.mRules.mPreprocessRules.insert(Options.mRules.mPreprocessRules.begin() + 3,
		    MakeArdaGraphRule<FArdaExpandSubgraphsRule>());
		ASSERT_TRUE(Edit.SetOptions(Options));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(Counts, (eastl::vector<size_t>{2, 0}));
		EXPECT_EQ(SubgraphBuildCount, 3u);
		const auto& Order = Graph.GetCompileResult().mExecutionOrder;
		ASSERT_EQ(Order.size(), 2u);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Order[0])->mPayload.mName.find("Second/"), 0u);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Order[1])->mPayload.mName.find("First/"), 0u);
	}

	TEST(ArdaSubgraph, OmittingExpansionRuleRejectsUnexpandedNodesWithoutRunningTheirBody)
	{
		SubgraphBuildCount = 0;
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		Edit.Keep(BuildRecursiveTest(Edit, FArdaSubgraphTestParameters{1}).GetBuffer());
		FArdaInductorOptions Options;
		Options.mRules.mPreprocessRules.erase(Options.mRules.mPreprocessRules.begin());
		ASSERT_TRUE(Edit.SetOptions(Options));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("require FArdaExpandSubgraphsRule"), eastl::string::npos);
		EXPECT_EQ(SubgraphBuildCount, 0u);
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 1u);
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 1u);
	}

	TEST(ArdaSubgraph, ExpansionRuleIsOptionalForGraphsWithoutSubgraphs)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaMemoryUploadBufferParameters Parameters;
		Parameters.mBytes.resize(16, 42);
		Edit.Keep(UploadBuffer(Edit, Parameters).GetBuffer());
		FArdaInductorOptions Options;
		Options.mRules.mPreprocessRules.erase(Options.mRules.mPreprocessRules.begin());
		ASSERT_TRUE(Edit.SetOptions(Options));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_TRUE(Status) << Status.mMessage.c_str();
	}

	TEST(ArdaSubgraph, DeclarationRegistersWithoutInvocationOrContentExpansion)
	{
		const auto Definition = FArdaNodeRegistry::Get().Find(FArdaRecursiveSubgraphTestNode::GetMetadata().mName);
		ASSERT_TRUE(Definition);
		EXPECT_TRUE(bool(Definition->mExpand));
		EXPECT_FALSE(bool(Definition->mRecord));
	}

	TEST(ArdaSubgraph, NestedContentExpandsOnlyAtCompilationAndUnifiesTransientOutputStorage)
	{
		SubgraphBuildCount = 0;
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Parent = BuildRecursiveTest(Edit, FArdaSubgraphTestParameters{3});
		ASSERT_TRUE(Parent) << Edit.GetStatus().mMessage.c_str();
		Edit.Keep(Parent.GetBuffer());
		EXPECT_EQ(SubgraphBuildCount, 0u);
		ASSERT_TRUE(Edit.Commit());
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 1u);
		EXPECT_EQ(SubgraphBuildCount, 0u);
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(SubgraphBuildCount, 4u);
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 1u);
		for (auto Handle : Graph.GetTopology().GetNodes())
		{
			EXPECT_FALSE(bool(Graph.GetTopology().TryGetNode(Handle)->mPayload.mDefinition->mExpand));
		}
	}

	TEST(ArdaSubgraph, StandalonePreprocessingExpandsACopyWithoutProducingACompileResult)
	{
		SubgraphBuildCount = 0;
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		Edit.Keep(BuildRecursiveTest(Edit, FArdaSubgraphTestParameters{2}).GetBuffer());
		ASSERT_TRUE(Edit.Commit());
		const auto Result = Graph.Preprocess();
		ASSERT_TRUE(Result) << Result.mStatus.mMessage.c_str();
		EXPECT_EQ(SubgraphBuildCount, 3u);
		EXPECT_FALSE(Graph.IsPreprocessed());
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 1u);
		EXPECT_TRUE(
		    Graph.GetTopology().TryGetNode(Graph.GetTopology().GetNodes().front())->mPayload.mDefinition->mExpand);
		EXPECT_TRUE(Result.mValue->IsPreprocessed());
		EXPECT_TRUE(Result.mValue->GetCompileResult().mNodes.empty());
		EXPECT_TRUE(Result.mValue->GetCompileResult().mExecutionOrder.empty());
		EXPECT_EQ(Result.mValue->GetCompileResult().mRevision, 0u);
		for (auto Handle : Result.mValue->GetTopology().GetNodes())
		{
			EXPECT_FALSE(bool(Result.mValue->GetTopology().TryGetNode(Handle)->mPayload.mDefinition->mExpand));
		}
		const auto Second = Result.mValue->Preprocess();
		ASSERT_TRUE(Second) << Second.mStatus.mMessage.c_str();
		EXPECT_EQ(SubgraphBuildCount, 3u);
	}

	TEST(ArdaSubgraph, NamedInputsConnectToNestedOutputConsumers)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Source = BuildRecursiveTest(Edit, FArdaSubgraphTestParameters{1}).GetBuffer();
		const auto Copy = FArdaInputSubgraphTestNode::Invoke(Edit, FArdaSubgraphInputTestParameters{Source});
		ASSERT_TRUE(Copy) << Edit.GetStatus().mMessage.c_str();
		Edit.Keep(Copy.GetBuffer());
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 2u);
	}

	TEST(ArdaSubgraph, ReadableInputPassthroughRewritesExistingConsumersAndSurvivesRecompilation)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Source = BuildRecursiveTest(Edit, FArdaSubgraphTestParameters{1}).GetBuffer();
		const auto Passthrough =
		    FArdaPassthroughSubgraphTestNode::Invoke(Edit, FArdaSubgraphPassthroughParameters{Source});
		const auto Copy =
		    FArdaInputSubgraphTestNode::Invoke(Edit, FArdaSubgraphInputTestParameters{Passthrough.GetBuffer()});
		Edit.Keep(Passthrough.GetBuffer());
		Edit.Keep(Copy.GetBuffer());
		ASSERT_TRUE(Edit.Commit());
		auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 2u);
		EXPECT_FALSE(Graph.ExecuteAndWait().mStatus);
		Status = Graph.CompileAndWait(true);
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 2u);
	}

	TEST(ArdaSubgraph, OutputOnlyPassthroughKeepsItsInputProducer)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Source = BuildRecursiveTest(Edit, FArdaSubgraphTestParameters{1}).GetBuffer();
		Edit.Keep(
		    FArdaPassthroughSubgraphTestNode::Invoke(Edit, FArdaSubgraphPassthroughParameters{Source}).GetBuffer());
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 1u);
	}

	TEST(ArdaSubgraph, PassthroughTransfersDeclaredOutputUsageToOwnedInputStorage)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Source = BuildRecursiveTest(Edit, FArdaSubgraphTestParameters{}).GetBuffer();
		FArdaSubgraphPassthroughParameters Parameters;
		Parameters.mSource = Source;
		Parameters.mbRequireUnorderedAccess = true;
		Edit.Keep(FArdaPassthroughSubgraphTestNode::Invoke(Edit, Parameters).GetBuffer());
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto* Resource = Graph.FindResource(Source);
		ASSERT_NE(Resource, nullptr);
		EXPECT_TRUE(HasAnyFlags(Resource->mBuffer.mUsage, EArdaRHIBufferUsage::UnorderedAccess));
	}

	TEST(ArdaSubgraph, RepeatedContentsScopeExplicitNamesAndPreserveParentOrdering)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto First = BuildRecursiveTest(Edit, FArdaSubgraphTestParameters{2}, "First");
		const auto Second = BuildRecursiveTest(Edit, FArdaSubgraphTestParameters{1}, "Second");
		Edit.Keep(First.GetBuffer());
		Edit.Keep(Second.GetBuffer());
		ASSERT_TRUE(Edit.Order(Second, First));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto& Order = Graph.GetCompileResult().mExecutionOrder;
		ASSERT_EQ(Order.size(), 2u);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Order[0])->mPayload.mName.find("Second/"), 0u);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Order[1])->mPayload.mName.find("First/"), 0u);
	}

	TEST(ArdaSubgraph, UndeclaredOuterResourceAccessFailsAtomically)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaRHIBufferDesc Description;
		Description.mByteSize = 16;
		UndeclaredSubgraphInput = AllocateBuffer(Edit, Description);
		ASSERT_TRUE(FArdaBoundarySubgraphTestNode::Invoke(Edit, FArdaSubgraphBoundaryTestParameters{}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("undeclared parent resource"), eastl::string::npos);
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 1u);
		UndeclaredSubgraphInput = {};
	}

	TEST(ArdaSubgraph, ContentCannotKeepAnUndeclaredOuterResource)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaMemoryUploadBufferParameters Parameters;
		Parameters.mBytes.resize(16, 42);
		UndeclaredSubgraphInput = UploadBuffer(Edit, Parameters).GetBuffer();
		ASSERT_TRUE(
		    FArdaBoundarySubgraphTestNode::Invoke(Edit, FArdaSubgraphBoundaryTestParameters{false, false, true}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("cannot keep an undeclared parent resource"), eastl::string::npos);
		UndeclaredSubgraphInput = {};
	}

	TEST(ArdaSubgraph, CapturedOuterStageConnectionFailsBeforePipelineAssembly)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		CapturedSubgraphVertices = FArdaSubgraphVertexTestNode::Invoke(Edit, FArdaSubgraphVertexParameters{});
		ASSERT_TRUE(CapturedSubgraphVertices);
		ASSERT_TRUE(FArdaBoundarySubgraphTestNode::Invoke(Edit, FArdaSubgraphBoundaryTestParameters{false, true}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("logical stage inputs"), eastl::string::npos);
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 2u);
		CapturedSubgraphVertices = {};
	}

	TEST(ArdaSubgraph, ContentCannotMaterializeACapturedOuterStage)
	{
		SubgraphStageMaterializeCount = 0;
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		CapturedSubgraphVertices = FArdaSubgraphVertexTestNode::Invoke(Edit, FArdaSubgraphVertexParameters{});
		ASSERT_TRUE(CapturedSubgraphVertices);
		const auto Definition = FArdaNodeRegistry::Get().Find(FArdaSubgraphVertexTestNode::GetMetadata().mName);
		ASSERT_TRUE(Definition && Definition->mFetchBuffer);
		ASSERT_TRUE(FArdaBoundarySubgraphTestNode::Invoke(Edit,
		    FArdaSubgraphBoundaryTestParameters{false, false, false, true}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("cannot access a parent logical stage value"), eastl::string::npos);
		EXPECT_EQ(SubgraphStageMaterializeCount, 0u);
		EXPECT_FALSE(
		    Graph.GetTopology().TryGetNode(CapturedSubgraphVertices.GetProducer())->mPayload.mbStageMaterialized);
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 2u);
		CapturedSubgraphVertices = {};
	}

	TEST(ArdaSubgraph, BuildCannotOpenAnEditOnItsCapturedParentGraph)
	{
		FArdaDependencyGraph Graph;
		CapturedSubgraphGraph = &Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(FArdaBoundarySubgraphTestNode::Invoke(Edit, FArdaSubgraphBoundaryTestParameters{true}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 1u);
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 1u);
		CapturedSubgraphGraph = nullptr;
	}

	TEST(ArdaSubgraph, CachedRevisionsReuseContentsAndForcedCompilationRebuildsAuthoredInputs)
	{
		SubgraphBuildCount = 0;
		FArdaDependencyGraph Graph;
		const auto Queue = [&]()
		{
			FArdaGraphEdit Edit(Graph);
			Edit.Keep(BuildRecursiveTest(Edit, FArdaSubgraphTestParameters{2}).GetBuffer());
			Edit.Keep(BuildRecursiveTest(Edit, FArdaSubgraphTestParameters{1}).GetBuffer());
			return Edit.Commit();
		};
		ASSERT_TRUE(Queue());
		auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(SubgraphBuildCount, 5u);
		EXPECT_FALSE(Graph.ExecuteAndWait().mStatus);
		ASSERT_TRUE(Queue());
		Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(SubgraphBuildCount, 5u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 1u);
		EXPECT_FALSE(Graph.ExecuteAndWait().mStatus);
		Status = Graph.CompileAndWait(true);
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(SubgraphBuildCount, 10u);
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 2u);
	}

	TEST(ArdaSubgraph, CachedCompilationRechecksRemovedNestedSubgraphAdmission)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		Edit.Keep(BuildRecursiveTest(Edit, FArdaSubgraphTestParameters{2}).GetBuffer());
		ASSERT_TRUE(Edit.Commit());
		auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		ASSERT_EQ(Graph.GetCompileResult().mSubgraphAdmissions.size(), 3u);
		EXPECT_FALSE(Graph.ExecuteAndWait().mStatus);
		bRejectNestedSubgraphLeaf = true;
		Status = Graph.CompileAndWait();
		bRejectNestedSubgraphLeaf = false;
		EXPECT_FALSE(Status);
		EXPECT_EQ(Status.mCode, EArdaRHIResult::Unsupported);
		EXPECT_NE(Status.mMessage.find("Subgraph '"), eastl::string::npos);
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 1u);
	}

	TEST(ArdaSubgraph, RecursiveCycleFailsWithoutPublishingExpandedContent)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(FArdaCycleSubgraphTestNode::Invoke(Edit, FArdaSubgraphCycleTestParameters{}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("recurs"), eastl::string::npos);
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 1u);
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 1u);
	}

	TEST(ArdaSubgraph, MissingOutputFailsWithoutPublishingExpandedContent)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(FArdaMissingOutputSubgraphTestNode::Invoke(Edit, FArdaSubgraphMissingOutputParameters{}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("declare every parent output"), eastl::string::npos);
		EXPECT_NE(Status.mMessage.find("FArdaExpandSubgraphsRule"), eastl::string::npos);
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 1u);
	}
}
