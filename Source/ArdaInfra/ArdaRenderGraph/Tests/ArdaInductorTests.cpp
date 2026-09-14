#include "ArdaDependencyNode.h"
#include "ArdaDependencyGraph.h"
#include "ArdaDependencyGraphNodes.h"
#include "ArdaDependencyHazards.h"
#include <gtest/gtest.h>

namespace
{
	using namespace arda;

	struct FArdaTestNodeParameters
	{
		FArdaDependencyResourceHandle mInput, mOutput;
		uint32_t mCost = 1, mValue = 0;
		bool mbSideEffect = false;
		uint64_t mWorkspaceBytes = 0;
		uint64_t mTransientWorkspaceBytes = 0;
	};

	template <EArdaDependencyNodeKind Kind>
	struct TArdaInductorTestNode : TArdaDependencyNode<TArdaInductorTestNode<Kind>, FArdaTestNodeParameters, Kind>
	{
		using FArdaParameters = FArdaTestNodeParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {Kind == EArdaDependencyNodeKind::Compute    ? "inductor.test.compute"
			        : Kind == EArdaDependencyNodeKind::Graphics ? "inductor.test.graphics"
			        : Kind == EArdaDependencyNodeKind::Cuda     ? "inductor.test.cuda"
			                                                    : "inductor.test.copy",
			    1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaTestNodeParameters& P)
		{
			return FArdaDependencyKeyBuilder{}
			    .Resource(P.mInput)
			    .Resource(P.mOutput)
			    .Value(P.mCost)
			    .Value(P.mValue)
			    .Value(P.mbSideEffect)
			    .Value(P.mWorkspaceBytes)
			    .Value(P.mTransientWorkspaceBytes)
			    .Build();
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaTestNodeParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc D;
			D.mEstimatedCost = P.mCost;
			D.mbSideEffect = P.mbSideEffect;
			D.mWorkspaceBytes = P.mWorkspaceBytes;
			D.mTransientWorkspaceBytes = P.mTransientWorkspaceBytes;

			// Optional inputs and outputs model independent branches and their joins.
			if (P.mInput)
			{
				D.mAccesses.push_back({P.mInput, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess});
			}
			if (P.mOutput)
			{
				D.mAccesses.push_back(
				    {P.mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess});
			}
			return D;
		}

		// Record the fixture operation and capture its observable results.
		template <EArdaDependencyNodeKind Domain = Kind,
		    std::enable_if_t<Domain != EArdaDependencyNodeKind::Cuda, int> = 0>
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return FArdaRHIStatus{};
		}

		// Provide the CUDA sequence hook without submitting during compilation.
		template <EArdaDependencyNodeKind Domain = Kind,
		    std::enable_if_t<Domain == EArdaDependencyNodeKind::Cuda, int> = 0>
		static FArdaRHIStatus PrepareCuda(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaCudaSequence&)
		{
			return FArdaRHIStatus{};
		}
	};

	void RegisterTestNodes()
	{
		ASSERT_TRUE(TArdaInductorTestNode<EArdaDependencyNodeKind::Compute>::Register());
		ASSERT_TRUE(TArdaInductorTestNode<EArdaDependencyNodeKind::Graphics>::Register());
		ASSERT_TRUE(TArdaInductorTestNode<EArdaDependencyNodeKind::Cuda>::Register());
		ASSERT_TRUE(TArdaInductorTestNode<EArdaDependencyNodeKind::Copy>::Register());
	}

	FArdaDependencyResourceHandle Buffer(FArdaDependencyGraph& G, const char* Name, uint64_t Bytes = 256)
	{
		FArdaRHIBufferDesc D;
		D.mByteSize = Bytes;
		D.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
		auto R = G.CreateBuffer(Name, D);
		EXPECT_TRUE(R);
		return R.mValue;
	}

	FArdaGraphNodeHandle Node(FArdaDependencyGraph& G,
	    const char* Name,
	    FArdaTestNodeParameters P,
	    const char* Definition = "inductor.test.compute")
	{
		auto N = G.AttachOrFind(Name, Definition, P);
		EXPECT_TRUE(N) << N.mStatus.mMessage.c_str();
		return N.mValue;
	}

	TEST(ArdaInductor, ResourceDependenciesIgnoreAttachmentOrderAndDeduplicate)
	{
		RegisterTestNodes();
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		auto A = Buffer(G, "a"), B = Buffer(G, "b"), C = Buffer(G, "c");

		// Reverse authoring order must still produce the resource-defined execution order.
		auto Consumer = Node(G, "consumer", {B, C});
		auto Producer = Node(G, "producer", {{}, A});
		auto Middle = Node(G, "middle", {A, B});
		EXPECT_EQ(Node(G, "middle", {A, B}), Middle);
		EXPECT_FALSE(G.AttachOrFind("middle", "inductor.test.compute", FArdaTestNodeParameters{A, B, 1, 99}));

		// Compile the live output chain and verify that duplicate attachment added no work.
		ASSERT_TRUE(G.MarkOutput(C));
		ASSERT_TRUE(G.EndGraphEdit());
		EXPECT_EQ(G.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{Producer, Middle, Consumer}));
		EXPECT_TRUE(G.GetTopology().IsReachable(Producer, Consumer));
		EXPECT_EQ(G.GetCompileResult().mRevision, 1u);
		EXPECT_FALSE(G.CreateBuffer("outside", {}));
		EXPECT_FALSE(G.Execute().mStatus);
	}

	TEST(ArdaInductor, EditCancellationDoesNotReviveAbandonedResourceOrNodeHandles)
	{
		RegisterTestNodes();
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		auto A = Buffer(G, "a");
		Node(G, "a", {{}, A});
		ASSERT_TRUE(G.MarkOutput(A));
		ASSERT_TRUE(G.EndGraphEdit());
		ASSERT_TRUE(G.BeginGraphEdit());
		auto Abandoned = Buffer(G, "abandoned");
		auto N = Node(G, "abandoned", {A, Abandoned});
		ASSERT_TRUE(G.CancelGraphEdit());
		ASSERT_TRUE(G.BeginGraphEdit());
		auto B = Buffer(G, "replacement");
		auto New = Node(G, "replacement", {A, B});
		EXPECT_NE(B, Abandoned);
		EXPECT_NE(N, New);
		EXPECT_FALSE(G.GetTopology().ContainsNode(N));
		EXPECT_EQ(G.FindResource(Abandoned), nullptr);
		EXPECT_FALSE(G.AttachOrFind("stale", "inductor.test.compute", FArdaTestNodeParameters{Abandoned, B}));
		ASSERT_TRUE(G.CancelGraphEdit());
		EXPECT_EQ(G.GetCompileResult().mRevision, 1u);
	}

	TEST(ArdaInductor, RejectsUnproducedReadsAndCycles)
	{
		RegisterTestNodes();
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		auto A = Buffer(G, "a"), B = Buffer(G, "b");
		Node(G, "one", {{}, A});
		auto Missing = Node(G, "missing", {B, {}});
		EXPECT_FALSE(G.EndGraphEdit());
		EXPECT_TRUE(G.IsEditing());
		ASSERT_TRUE(G.RemoveNode(Missing));
		auto First = G.FindNode("one"), Second = Node(G, "second", {A, B});
		ASSERT_TRUE(G.AddDependency(Second, First));
		EXPECT_FALSE(G.EndGraphEdit());
		ASSERT_TRUE(G.RemoveDependency(Second, First));
		ASSERT_TRUE(G.MarkOutput(B));
		ASSERT_TRUE(G.EndGraphEdit());
	}

	TEST(ArdaInductor, RemovalCascadesOnlyResourceConsumersAndCullsUnusedBranches)
	{
		RegisterTestNodes();
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		auto A = Buffer(G, "a"), B = Buffer(G, "b"), Dead = Buffer(G, "dead");
		auto P = Node(G, "producer", {{}, A});
		auto C = Node(G, "consumer", {A, B});
		auto D = Node(G, "dead", {{}, Dead});
		auto Sync = Node(G, "ordered-only", {{}, {}, 1, 0, true});
		ASSERT_TRUE(G.AddDependency(P, Sync));
		ASSERT_TRUE(G.MarkOutput(B));
		ASSERT_TRUE(G.EndGraphEdit());
		EXPECT_NE(eastl::find(G.GetCompileResult().mCulledNodes.begin(), G.GetCompileResult().mCulledNodes.end(), D),
		    G.GetCompileResult().mCulledNodes.end());
		ASSERT_TRUE(G.BeginGraphEdit());
		ASSERT_TRUE(G.RemoveNode(P));
		EXPECT_FALSE(G.GetTopology().ContainsNode(C));
		EXPECT_TRUE(G.GetTopology().ContainsNode(Sync));
		ASSERT_TRUE(G.EndGraphEdit());
	}

	TEST(ArdaInductor, CoalescesCudaWhilePreservingGraphicsDependencies)
	{
		RegisterTestNodes();
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		auto A = Buffer(G, "a"), B = Buffer(G, "b"), C = Buffer(G, "c"), D = Buffer(G, "d");
		auto P = Node(G, "producer", {{}, A});
		auto First = Node(G, "cuda1", {A, B}, "inductor.test.cuda");
		auto Second = Node(G, "cuda2", {B, C}, "inductor.test.cuda");
		auto Graphics = Node(G, "graphics", {C, D}, "inductor.test.graphics");
		ASSERT_TRUE(G.MarkOutput(D));
		ASSERT_TRUE(G.EndGraphEdit());
		ASSERT_EQ(G.GetCompileResult().mCudaBatches.size(), 1u);
		EXPECT_EQ(G.GetCompileResult().mCudaBatches[0], (eastl::vector<FArdaGraphNodeHandle>{First, Second}));
		ASSERT_TRUE(G.BeginGraphEdit());
		ASSERT_TRUE(G.AddDependency(Graphics, First));
		EXPECT_FALSE(G.EndGraphEdit());
		ASSERT_TRUE(G.CancelGraphEdit());
		EXPECT_TRUE(G.GetTopology().IsReachable(P, Graphics));
	}

	TEST(ArdaInductor, ExhaustiveSearchEvaluatesAllOrdersAndCoalescesCuda)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mSearchMode = EArdaInductorSearchMode::Exhaustive;
		Options.mMaxSearchStates = 1; // Explicit exhaustive search ignores this limit.
		ASSERT_TRUE(Graph.SetOptions(Options));
		Node(Graph, "a-cuda", {{}, {}, 1, 0, true}, "inductor.test.cuda");
		Node(Graph, "b-graphics", {{}, {}, 3, 0, true}, "inductor.test.graphics");
		Node(Graph, "c-cuda", {{}, {}, 1, 0, true}, "inductor.test.cuda");
		Node(Graph, "d-graphics", {{}, {}, 3, 0, true}, "inductor.test.graphics");
		ASSERT_TRUE(Graph.EndGraphEdit());
		const auto& Result = Graph.GetCompileResult();
		EXPECT_TRUE(Result.mbSearchComplete);
		EXPECT_FALSE(Result.mbSearchExhausted);
		EXPECT_EQ(Result.mSchedulesExamined, 26u); // 4! orders and two initial candidates.
		EXPECT_EQ(Result.mSearchStatesExamined, 65u);
		ASSERT_EQ(Result.mCudaBatches.size(), 1u);
		EXPECT_EQ(Result.mCudaBatches.front().size(), 2u);
		EXPECT_DOUBLE_EQ(Result.mEstimatedExecutionCost, 8.0 + Options.mCudaHandoffCost);
	}

	TEST(ArdaInductor, BoundedSearchKeepsFeasiblePlanWhenItsStateLimitIsReached)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mMaxSearchStates = 1;
		ASSERT_TRUE(Graph.SetOptions(Options));
		Node(Graph, "a", {{}, {}, 1, 0, true});
		Node(Graph, "b", {{}, {}, 1, 0, true});
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 2u);
		EXPECT_TRUE(Graph.GetCompileResult().mbSearchExhausted);
		EXPECT_FALSE(Graph.GetCompileResult().mbSearchComplete);
		EXPECT_EQ(Graph.GetCompileResult().mSearchStatesExamined, 1u);
	}

	TEST(ArdaInductor, ExhaustiveSearchHasNoRecursiveDepthLimit)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mSearchMode = EArdaInductorSearchMode::Exhaustive;
		ASSERT_TRUE(Graph.SetOptions(Options));
		FArdaGraphNodeHandle Previous;
		for (uint32_t Index = 0; Index < 300; ++Index)
		{
			eastl::string Name = "node-";
			Name += std::to_string(Index).c_str();
			const auto Current = Node(Graph, Name.c_str(), {{}, {}, 1, 0, true});
			if (Previous)
			{
				ASSERT_TRUE(Graph.AddDependency(Previous, Current));
			}
			Previous = Current;
		}
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_TRUE(Graph.GetCompileResult().mbSearchComplete);
		EXPECT_EQ(Graph.GetCompileResult().mSearchStatesExamined, 301u);
		EXPECT_EQ(Graph.GetCompileResult().mSchedulesExamined, 3u);
	}

	TEST(ArdaInductor, TemporaryWorkspacesAreAliasedAndBudgetedForEveryFrameSlot)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mObjective = EArdaInductorObjective::Memory;
		Options.mFramesInFlight = 3;
		ASSERT_TRUE(Graph.SetOptions(Options));
		const auto First = Node(Graph, "first", {{}, {}, 1, 0, true, 128, 4096});
		const auto Second = Node(Graph, "second", {{}, {}, 1, 0, true, 0, 4096});
		Node(Graph, "culled", {{}, {}, 1, 0, false, 0, 16384});
		ASSERT_TRUE(Graph.EndGraphEdit());
		const auto& Result = Graph.GetCompileResult();
		EXPECT_EQ(Result.mWorkspaceResourceIds.size(), 2u);
		EXPECT_NE(Result.mWorkspaceResourceIds.at(First.GetIndex()),
		    Result.mWorkspaceResourceIds.at(Second.GetIndex()));
		EXPECT_EQ(Result.mAllocatedBytes, 3u * 4096 + 128);
		EXPECT_EQ(Result.mAliasedBytes, 3u * 4096);
		EXPECT_FALSE(Result.mMemoryDependencies.empty());
	}

	TEST(ArdaInductor, TimingOptimizationWithoutSamplesPreservesTheCompiledGraph)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		Node(Graph, "node", {{}, {}, 1, 0, true});
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_FALSE(Graph.OptimizeFromTimingProfile());
		EXPECT_FALSE(Graph.IsEditing());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, 1u);
	}

	TEST(ArdaInductor, MemoryObjectiveReusesCompatibleDeadValuesAndKeepsOutputsLive)
	{
		RegisterTestNodes();
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		FArdaInductorOptions O;
		O.mObjective = EArdaInductorObjective::Memory;
		ASSERT_TRUE(G.SetOptions(O));
		auto A = Buffer(G, "a"), B = Buffer(G, "b"), C = Buffer(G, "c"), D = Buffer(G, "d");
		Node(G, "a", {{}, A});
		Node(G, "b", {A, B});
		Node(G, "c", {B, C});
		Node(G, "d", {C, D});
		ASSERT_TRUE(G.MarkOutput(D));
		ASSERT_TRUE(G.EndGraphEdit());
		EXPECT_GT(G.GetCompileResult().mAliasedBytes, 0u);
		EXPECT_LT(G.GetCompileResult().mAllocatedBytes, 1024u);
		EXPECT_GE(G.GetCompileResult().mAllocatedBytes, 512u);
	}

	TEST(ArdaInductor, PureSynchronizationDoesNotSplitAnOrderedCudaBatch)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto A = Buffer(Graph, "a"), B = Buffer(Graph, "b");
		const auto First = Node(Graph, "first", {{}, A}, "inductor.test.cuda");
		const auto Second = Node(Graph, "second", {A, B}, "inductor.test.cuda");
		const auto Sync = Graph.AttachOrFind("sync", "arda.sync", FArdaGraphSyncParameters{});
		ASSERT_TRUE(Sync);
		ASSERT_TRUE(Graph.AddDependency(First, Sync.mValue));
		ASSERT_TRUE(Graph.AddDependency(Sync.mValue, Second));
		ASSERT_TRUE(Graph.MarkOutput(B));
		ASSERT_TRUE(Graph.EndGraphEdit());
		ASSERT_EQ(Graph.GetCompileResult().mCudaBatches.size(), 1u);
		EXPECT_EQ(Graph.GetCompileResult().mCudaBatches.front(), (eastl::vector<FArdaGraphNodeHandle>{First, Second}));
	}

	TEST(ArdaInductor, EfficiencyObjectiveDoesNotSerializeIndependentLifetimesForReuse)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto A = Buffer(Graph, "a"), B = Buffer(Graph, "b");
		Node(Graph, "a-producer", {{}, A});
		Node(Graph, "a-consumer", {A, {}, 1, 0, true});
		Node(Graph, "b-producer", {{}, B});
		Node(Graph, "b-consumer", {B, {}, 1, 0, true});
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_TRUE(Graph.GetCompileResult().mMemoryDependencies.empty());
		EXPECT_EQ(Graph.GetCompileResult().mAllocatedBytes, 512u);
	}

	struct FArdaRangeNodeParameters
	{
		eastl::vector<FArdaDependencyAccess> mAccesses;
		eastl::vector<FArdaDependencyResourceHandle> mColorTargets;
		eastl::vector<FArdaInductorPipelineRequest> mPipelines;
		bool mbPipelineStageOnly = false;
	};

	struct FArdaRangeTestNode
	    : TArdaDependencyNode<FArdaRangeTestNode, FArdaRangeNodeParameters, EArdaDependencyNodeKind::Graphics>
	{
		using FArdaParameters = FArdaRangeNodeParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"inductor.test.ranges", 1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaRangeNodeParameters& P)
		{

			FArdaDependencyKeyBuilder Key;

			// Preserve complete resource views, including disjoint buffer and texture subranges.
			Key.Value(P.mAccesses.size());
			for (const auto& Access : P.mAccesses)
			{
				Key.Resource(Access.mResource)
				    .Value(Access.mAccess)
				    .Value(Access.mState)
				    .BufferRange(Access.mBufferRange)
				    .TextureRange(Access.mTextureRange);
			}

			// Attachments and pipeline selections also distinguish otherwise identical resource access lists.
			Key.Value(P.mColorTargets.size());
			for (const auto Target : P.mColorTargets)
			{
				Key.Resource(Target);
			}
			Key.Value(P.mbPipelineStageOnly).Value(P.mPipelines.size());
			for (const auto& Request : P.mPipelines)
			{
				Key.Value(Request.mKind).Value(Request.mTerminalNodeId).String(Request.mSlot).String(Request.mGroup);
			}
			return Key.Build();
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaRangeNodeParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mAccesses = P.mAccesses;
			Desc.mColorTargets = P.mColorTargets;
			Desc.mPipelines = P.mPipelines;
			Desc.mbPipelineStageOnly = P.mbPipelineStageOnly;
			Desc.mbSideEffect = true;
			return Desc;
		}

		// Record the fixture operation and capture its observable results.
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaRangeNodeParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return FArdaRHIStatus{};
		}
	};

	FArdaGraphNodeHandle RangeNode(FArdaDependencyGraph& Graph,
	    const char* Name,
	    eastl::vector<FArdaDependencyAccess> Accesses)
	{
		auto Result = Graph.AttachOrFind<FArdaRangeTestNode>(Name, {eastl::move(Accesses)});
		EXPECT_TRUE(Result) << Result.mStatus.mMessage.c_str();
		return Result.mValue;
	}

	TEST(ArdaInductor, RepeatedWritesPreserveInterveningReadsAndSelectTheLatestProducer)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Resource = Buffer(Graph, "versions");
		const FArdaDependencyAccess Write{Resource,
		    EArdaDependencyAccess::Write,
		    EArdaRHIResourceState::UnorderedAccess};
		const FArdaDependencyAccess Read{Resource, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess};
		const auto A = RangeNode(Graph, "write first", {Write});
		const auto B = RangeNode(Graph, "read first", {Read});
		const auto C = RangeNode(Graph, "write second", {Write});
		const auto D = RangeNode(Graph, "read second", {Read});
		const auto Status = Graph.EndGraphEdit();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder, (eastl::vector<FArdaGraphNodeHandle>{A, B, C, D}));
		EXPECT_TRUE(Graph.GetTopology().FindEdge(A, B));
		EXPECT_TRUE(Graph.GetTopology().FindEdge(B, C));
		EXPECT_TRUE(Graph.GetTopology().FindEdge(C, D));
		EXPECT_FALSE(Graph.GetTopology().FindEdge(A, D));
	}

	TEST(ArdaInductor, ReadersOfTheSameWrittenValueRemainIndependent)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Resource = Buffer(Graph, "reader groups");
		const FArdaDependencyAccess Write{Resource,
		    EArdaDependencyAccess::Write,
		    EArdaRHIResourceState::UnorderedAccess};
		const FArdaDependencyAccess Read{Resource, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess};
		const auto FirstWriter = RangeNode(Graph, "first writer", {Write});
		const auto FirstReader = RangeNode(Graph, "first reader", {Read});
		const auto SecondReader = RangeNode(Graph, "second reader", {Read});
		const auto SecondWriter = RangeNode(Graph, "second writer", {Write});
		const auto ThirdReader = RangeNode(Graph, "third reader", {Read});
		const auto FourthReader = RangeNode(Graph, "fourth reader", {Read});
		const auto Status = Graph.EndGraphEdit();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		for (const auto Reader : {FirstReader, SecondReader})
		{
			EXPECT_TRUE(Graph.GetTopology().FindEdge(FirstWriter, Reader));
			EXPECT_TRUE(Graph.GetTopology().FindEdge(Reader, SecondWriter));
		}
		for (const auto Reader : {ThirdReader, FourthReader})
		{
			EXPECT_TRUE(Graph.GetTopology().FindEdge(SecondWriter, Reader));
			EXPECT_FALSE(Graph.GetTopology().IsReachable(Reader, SecondWriter));
		}
		EXPECT_FALSE(Graph.GetTopology().IsReachable(FirstReader, SecondReader));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(SecondReader, FirstReader));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(ThirdReader, FourthReader));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(FourthReader, ThirdReader));
	}

	TEST(ArdaInductor, ReadWriteConsumesThePriorValueAndProducesTheNextValue)
	{
		for (const bool bSeparateAccesses : {false, true})
		{
			SCOPED_TRACE(bSeparateAccesses);
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			const auto Resource = Buffer(Graph, "in place");
			const FArdaDependencyAccess Write{Resource,
			    EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::UnorderedAccess};
			const FArdaDependencyAccess Read{Resource,
			    EArdaDependencyAccess::Read,
			    EArdaRHIResourceState::UnorderedAccess};
			const FArdaDependencyAccess ReadWrite{Resource,
			    EArdaDependencyAccess::ReadWrite,
			    EArdaRHIResourceState::UnorderedAccess};
			const auto Producer = RangeNode(Graph, "producer", {Write});
			const auto Reader = RangeNode(Graph, "before update", {Read});
			const auto Update = RangeNode(Graph,
			    "update",
			    bSeparateAccesses ? eastl::vector<FArdaDependencyAccess>{Read, Write}
			                      : eastl::vector<FArdaDependencyAccess>{ReadWrite});
			const auto Consumer = RangeNode(Graph, "after update", {Read});
			const auto Status = Graph.EndGraphEdit();
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
			    (eastl::vector<FArdaGraphNodeHandle>{Producer, Reader, Update, Consumer}));
			EXPECT_TRUE(Graph.GetTopology().FindEdge(Producer, Update));
			EXPECT_TRUE(Graph.GetTopology().FindEdge(Reader, Update));
			EXPECT_TRUE(Graph.GetTopology().FindEdge(Update, Consumer));
			EXPECT_FALSE(Graph.GetTopology().FindEdge(Update, Update));
		}
	}

	TEST(ArdaInductor, RepeatedWritesUseAttachmentOrderAcrossDeduplicationSlotReuseAndCancellation)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Resource = Buffer(Graph, "overwritten");
		const FArdaDependencyAccess Write{Resource,
		    EArdaDependencyAccess::Write,
		    EArdaRHIResourceState::UnorderedAccess};
		const auto First = RangeNode(Graph, "first", {Write});
		const auto Second = RangeNode(Graph, "second", {Write});
		const auto Third = RangeNode(Graph, "third", {Write});
		EXPECT_EQ(RangeNode(Graph, "first", {Write}), First);
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 3u);
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{First, Second, Third}));

		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.RemoveNode(First));
		const auto Abandoned = RangeNode(Graph, "first", {Write});
		EXPECT_EQ(Abandoned.GetIndex(), First.GetIndex());
		EXPECT_NE(Abandoned, First);
		ASSERT_TRUE(Graph.CancelGraphEdit());
		EXPECT_EQ(Graph.FindNode("first"), First);
		EXPECT_FALSE(Graph.GetTopology().ContainsNode(Abandoned));
		EXPECT_EQ(Graph.GetCompileResult().mRevision, 1u);
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{First, Second, Third}));

		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.RemoveNode(First));
		const auto Replacement = RangeNode(Graph, "first", {Write});
		EXPECT_EQ(Replacement.GetIndex(), First.GetIndex());
		EXPECT_NE(Replacement, Abandoned);
		EXPECT_EQ(RangeNode(Graph, "second", {Write}), Second);
		const auto Status = Graph.EndGraphEdit();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{Second, Third, Replacement}));
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Second, Third));
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Third, Replacement));
	}

	TEST(ArdaInductor, ReattachedReaderObservesTheLatestWriterDespiteReusingAnEarlierSlot)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Resource = Buffer(Graph, "reader replacement");
		const FArdaDependencyAccess Write{Resource,
		    EArdaDependencyAccess::Write,
		    EArdaRHIResourceState::UnorderedAccess};
		const FArdaDependencyAccess Read{Resource, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess};
		const auto FirstWriter = RangeNode(Graph, "first writer", {Write});
		const auto OldReader = RangeNode(Graph, "reader", {Read});
		const auto SecondWriter = RangeNode(Graph, "second writer", {Write});
		const auto LastReader = RangeNode(Graph, "last reader", {Read});
		ASSERT_TRUE(Graph.EndGraphEdit());
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.RemoveNode(OldReader));
		const auto NewReader = RangeNode(Graph, "reader", {Read});
		EXPECT_EQ(NewReader.GetIndex(), OldReader.GetIndex());
		EXPECT_NE(NewReader, OldReader);
		const auto Status = Graph.EndGraphEdit();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_TRUE(Graph.GetTopology().FindEdge(SecondWriter, NewReader));
		EXPECT_FALSE(Graph.GetTopology().FindEdge(FirstWriter, NewReader));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(NewReader, SecondWriter));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(NewReader, LastReader));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(LastReader, NewReader));
	}

	TEST(ArdaInductor, ExplicitEdgesConflictingWithAnInterveningReadProduceARepairableCycle)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Resource = Buffer(Graph, "ordered hazards");
		const FArdaDependencyAccess Write{Resource,
		    EArdaDependencyAccess::Write,
		    EArdaRHIResourceState::UnorderedAccess};
		const FArdaDependencyAccess Read{Resource, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess};
		const auto First = RangeNode(Graph, "first", {Write});
		const auto Reader = RangeNode(Graph, "reader", {Read});
		const auto Second = RangeNode(Graph, "second", {Write});
		ASSERT_TRUE(Graph.AddDependency(Second, Reader));
		EXPECT_FALSE(Graph.EndGraphEdit());
		EXPECT_TRUE(Graph.IsEditing());
		ASSERT_TRUE(Graph.RemoveDependency(Second, Reader));
		const auto Status = Graph.EndGraphEdit();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{First, Reader, Second}));
	}

	TEST(ArdaInductor, RemovingAWriterCascadesOnlyReadersOfItsValue)
	{
		for (const bool bCompileBeforeRemoval : {false, true})
		{
			for (const bool bRemoveFirst : {false, true})
			{
				for (const bool bReadWrite : {false, true})
				{
					SCOPED_TRACE(bCompileBeforeRemoval);
					SCOPED_TRACE(bRemoveFirst);
					SCOPED_TRACE(bReadWrite);
					FArdaDependencyGraph Graph;
					ASSERT_TRUE(Graph.BeginGraphEdit());
					const auto Resource = Buffer(Graph, "removal");
					const FArdaDependencyAccess Write{Resource,
					    EArdaDependencyAccess::Write,
					    EArdaRHIResourceState::UnorderedAccess};
					const FArdaDependencyAccess Read{Resource,
					    EArdaDependencyAccess::Read,
					    EArdaRHIResourceState::UnorderedAccess};
					const FArdaDependencyAccess Update{Resource,
					    bReadWrite ? EArdaDependencyAccess::ReadWrite : EArdaDependencyAccess::Write,
					    EArdaRHIResourceState::UnorderedAccess};
					const auto A = RangeNode(Graph, "write first", {Write});
					const auto B = RangeNode(Graph, "read first", {Read});
					const auto C = RangeNode(Graph, "write second", {Update});
					const auto D = RangeNode(Graph, "read second", {Read});
					if (bCompileBeforeRemoval)
					{
						ASSERT_TRUE(Graph.EndGraphEdit());
						ASSERT_TRUE(Graph.BeginGraphEdit());
					}
					ASSERT_TRUE(Graph.RemoveNode(bRemoveFirst ? A : C));
					EXPECT_EQ(Graph.GetTopology().ContainsNode(A), !bRemoveFirst);
					EXPECT_EQ(Graph.GetTopology().ContainsNode(B), !bRemoveFirst);
					EXPECT_EQ(Graph.GetTopology().ContainsNode(C), bRemoveFirst && !bReadWrite);
					EXPECT_EQ(Graph.GetTopology().ContainsNode(D), bRemoveFirst && !bReadWrite);
					const auto Status = Graph.EndGraphEdit();
					ASSERT_TRUE(Status) << Status.mMessage.c_str();
				}
			}
		}
	}

	TEST(ArdaInductor, RemovingAnExplicitDependencyPreservesTheMatchingWriteHazard)
	{
		for (const bool bInterveningReader : {false, true})
		{
			SCOPED_TRACE(bInterveningReader);
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			const auto Resource = Buffer(Graph, "retained hazards");
			const FArdaDependencyAccess Write{Resource,
			    EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::UnorderedAccess};
			const FArdaDependencyAccess Read{Resource,
			    EArdaDependencyAccess::Read,
			    EArdaRHIResourceState::UnorderedAccess};
			const auto First = RangeNode(Graph, "first writer", {Write});
			const auto Before = bInterveningReader ? RangeNode(Graph, "intervening reader", {Read}) : First;
			const auto Second = RangeNode(Graph, "second writer", {Write});
			ASSERT_TRUE(Graph.AddDependency(Before, Second));
			ASSERT_TRUE(Graph.EndGraphEdit());
			ASSERT_TRUE(Graph.BeginGraphEdit());
			ASSERT_TRUE(Graph.RemoveDependency(Before, Second));
			const auto Edge = Graph.GetTopology().FindEdge(Before, Second);
			ASSERT_TRUE(Edge);
			EXPECT_TRUE(Graph.GetTopology().TryGetEdge(Edge)->mPayload.mbHazard);
			EXPECT_FALSE(Graph.GetTopology().TryGetEdge(Edge)->mPayload.mbResource);
			EXPECT_FALSE(Graph.RemoveDependency(Before, Second));
			const auto Status = Graph.EndGraphEdit();
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			EXPECT_TRUE(Graph.GetTopology().FindEdge(Before, Second));
			EXPECT_TRUE(Graph.GetTopology().IsReachable(First, Second));
		}
	}

	TEST(ArdaInductor, ManyNestedPrefixReadersKeepExactlyOneProducerEdgeEach)
	{
		// Exercise dependency indexing directly so scheduling work does not dominate this regression.
		constexpr uint32_t ReaderCount = 32768;
		FArdaDependencyGraph::FArdaImpl Graph;
		Graph.mIdentity = AllocateArdaGraphIdentity();
		FArdaDependencyResourceDesc Desc;
		Desc.mName = "nested prefixes";
		Desc.mBuffer.mByteSize = ReaderCount;
		Graph.mResources.push_back(Desc);
		Graph.mResourceGenerations.push_back(1);
		const FArdaDependencyResourceHandle Resource{Graph.mIdentity, 0, 1};
		eastl::vector<FArdaGraphNodeHandle> Readers;
		Readers.reserve(ReaderCount);
		for (uint32_t Index = 0; Index < ReaderCount; ++Index)
		{
			FArdaDependencyNode Node;
			Node.mAttachmentOrder = Index + 1;
			Node.mDesc.mAccesses.push_back(
			    {Resource, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess, {0, Index + 1}});
			const auto Reader = Graph.mTopology.AddNode(eastl::move(Node));
			ASSERT_TRUE(Reader);
			Readers.push_back(Reader);
		}
		FArdaDependencyNode Producer;
		Producer.mAttachmentOrder = ReaderCount + 1;
		Producer.mDesc.mAccesses.push_back(
		    {Resource, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess});
		const auto Writer = Graph.mTopology.AddNode(eastl::move(Producer));
		ASSERT_TRUE(Writer);
		const auto Status = ResolveArdaDependencyResourceEdges(Graph, Graph.mTopology);
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(Graph.mTopology.GetNodeCount(), ReaderCount + 1u);
		EXPECT_EQ(Graph.mTopology.GetEdgeCount(), ReaderCount);
		EXPECT_EQ(Graph.mTopology.GetOutgoingEdges(Writer).size(), ReaderCount);
		for (const auto Reader : Readers)
		{
			const auto Edge = Graph.mTopology.FindEdge(Writer, Reader);
			ASSERT_TRUE(Edge);
			EXPECT_TRUE(Graph.mTopology.TryGetEdge(Edge)->mPayload.mbResource);
			EXPECT_FALSE(Graph.mTopology.TryGetEdge(Edge)->mPayload.mbHazard);
			EXPECT_EQ(Graph.mTopology.GetIncomingEdges(Reader).size(), 1u);
			EXPECT_TRUE(Graph.mTopology.GetOutgoingEdges(Reader).empty());
		}
	}

	TEST(ArdaInductor, RejectsUnwrittenBufferBytesAndAcceptsMergedProducerRanges)
	{
		ASSERT_TRUE(FArdaRangeTestNode::Register());
		for (const bool Gap : {false, true})
		{
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			const auto Resource = Buffer(Graph, "range", 256);
			FArdaRangeNodeParameters Writer;
			for (const FArdaRHIBufferRange Range :
			    {FArdaRHIBufferRange{128, 64}, FArdaRHIBufferRange{0, 64}, FArdaRHIBufferRange{48, Gap ? 64u : 80u}})
			{
				Writer.mAccesses.push_back(
				    {Resource, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess, Range});
			}
			FArdaRangeNodeParameters Reader;
			Reader.mAccesses.push_back(
			    {Resource, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess, {0, 192}});
			ASSERT_TRUE(Graph.AttachOrFind("reader", "inductor.test.ranges", Reader));
			ASSERT_TRUE(Graph.AttachOrFind("writer", "inductor.test.ranges", Writer));
			const auto Status = Graph.EndGraphEdit();
			EXPECT_EQ(bool(Status), !Gap) << Status.mMessage.c_str();
		}
	}

	TEST(ArdaInductor, RepeatedByteWritesDoNotImposeAttachmentOrderOnNeighboringSingleWriterRegions)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Resource = Buffer(Graph, "mixed regions", 256);
		const auto Access = [&](EArdaDependencyAccess Mode, FArdaRHIBufferRange Range)
		{
			return FArdaDependencyAccess{Resource, Mode, EArdaRHIResourceState::UnorderedAccess, Range};
		};
		// Bytes [0, 64) and [128, 256) each have one writer and retain declarative producer ordering.
		const auto EarlyRead =
		    RangeNode(Graph, "read unchanged prefix", {Access(EArdaDependencyAccess::Read, {0, 64})});
		const auto First = RangeNode(Graph, "write lower half", {Access(EArdaDependencyAccess::Write, {0, 128})});
		const auto Before = RangeNode(Graph, "read old overlap", {Access(EArdaDependencyAccess::Read, {64, 64})});
		const auto Second = RangeNode(Graph, "overwrite overlap", {Access(EArdaDependencyAccess::Write, {64, 64})});
		const auto Joined = RangeNode(Graph, "read all", {Access(EArdaDependencyAccess::Read, {})});
		const auto Upper = RangeNode(Graph, "write upper half", {Access(EArdaDependencyAccess::Write, {128, 128})});
		const auto Status = Graph.EndGraphEdit();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_TRUE(Graph.GetTopology().FindEdge(First, EarlyRead));
		EXPECT_TRUE(Graph.GetTopology().FindEdge(First, Before));
		EXPECT_TRUE(Graph.GetTopology().FindEdge(Before, Second));
		EXPECT_TRUE(Graph.GetTopology().FindEdge(First, Joined));
		EXPECT_TRUE(Graph.GetTopology().FindEdge(Second, Joined));
		EXPECT_TRUE(Graph.GetTopology().FindEdge(Upper, Joined));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(EarlyRead, Second));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(Second, EarlyRead));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(First, Upper));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(Upper, First));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(Joined, Upper));
	}

	TEST(ArdaInductor, FutureRepeatedWritesCannotFillAnEarlierOwnedReadCoverageGap)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Resource = Buffer(Graph, "partially initialized", 128);
		const auto Access = [&](EArdaDependencyAccess Mode, FArdaRHIBufferRange Range)
		{
			return FArdaDependencyAccess{Resource, Mode, EArdaRHIResourceState::UnorderedAccess, Range};
		};
		RangeNode(Graph, "initialize prefix", {Access(EArdaDependencyAccess::Write, {0, 64})});
		const auto EarlyRead = RangeNode(Graph, "read uninitialized suffix", {Access(EArdaDependencyAccess::Read, {})});
		RangeNode(Graph, "write all", {Access(EArdaDependencyAccess::Write, {})});
		RangeNode(Graph, "overwrite suffix", {Access(EArdaDependencyAccess::Write, {64, 64})});
		EXPECT_FALSE(Graph.EndGraphEdit());
		EXPECT_TRUE(Graph.IsEditing());
		ASSERT_TRUE(Graph.RemoveNode(EarlyRead));
		const auto Status = Graph.EndGraphEdit();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
	}

	TEST(ArdaInductor, IndexedBufferProducersPreserveUnionsAndHalfOpenBoundaries)
	{
		for (const bool Reverse : {false, true})
		{
			SCOPED_TRACE(Reverse);
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			const auto Resource = Buffer(Graph, "fragmented", 256);
			const auto Access = [&](EArdaDependencyAccess Mode, FArdaRHIBufferRange Range)
			{
				return FArdaDependencyAccess{Resource, Mode, EArdaRHIResourceState::UnorderedAccess, Range};
			};

			// One producer's nested/adjacent ranges form a union; different producers only touch at byte 80.
			FArdaRangeNodeParameters Lower, Upper;
			for (const FArdaRHIBufferRange Range : {FArdaRHIBufferRange{64, 16}, {0, 32}, {16, 64}, {24, 8}})
			{
				Lower.mAccesses.push_back(Access(EArdaDependencyAccess::Write, Range));
			}
			Upper.mAccesses = {Access(EArdaDependencyAccess::Write, {96, 160}),
			    Access(EArdaDependencyAccess::Write, {80, 16})};
			const auto First = Graph.AttachOrFind<FArdaRangeTestNode>("first", Reverse ? Upper : Lower);
			const auto Second = Graph.AttachOrFind<FArdaRangeTestNode>("second", Reverse ? Lower : Upper);
			ASSERT_TRUE(First && Second);
			const auto LowerWriter = Reverse ? Second.mValue : First.mValue;
			const auto UpperWriter = Reverse ? First.mValue : Second.mValue;

			// Exact boundaries must not introduce a false dependency on the neighboring producer.
			const auto LowerRead =
			    Graph.AttachOrFind<FArdaRangeTestNode>("lower read", {{Access(EArdaDependencyAccess::Read, {0, 80})}});
			const auto UpperRead = Graph.AttachOrFind<FArdaRangeTestNode>("upper read",
			    {{Access(EArdaDependencyAccess::Read, {80, 176})}});
			const auto JoinedRead = Graph.AttachOrFind<FArdaRangeTestNode>("joined read",
			    {{Access(EArdaDependencyAccess::Read, {32, 128})}});
			ASSERT_TRUE(LowerRead && UpperRead && JoinedRead);
			const auto Status = Graph.EndGraphEdit();
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			EXPECT_TRUE(Graph.GetTopology().IsReachable(LowerWriter, LowerRead.mValue));
			EXPECT_FALSE(Graph.GetTopology().IsReachable(UpperWriter, LowerRead.mValue));
			EXPECT_TRUE(Graph.GetTopology().IsReachable(UpperWriter, UpperRead.mValue));
			EXPECT_FALSE(Graph.GetTopology().IsReachable(LowerWriter, UpperRead.mValue));
			EXPECT_TRUE(Graph.GetTopology().IsReachable(LowerWriter, JoinedRead.mValue));
			EXPECT_TRUE(Graph.GetTopology().IsReachable(UpperWriter, JoinedRead.mValue));
		}
	}

	TEST(ArdaInductor, IndexedBufferProducersOrderNestedWritesAndRejectSelfProducedInputs)
	{
		for (uint32_t Case = 0; Case < 3; ++Case)
		{
			SCOPED_TRACE(Case);
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			const auto Resource = Buffer(Graph, "range", 256);
			const auto Access = [&](EArdaDependencyAccess Mode, FArdaRHIBufferRange Range)
			{
				return FArdaDependencyAccess{Resource, Mode, EArdaRHIResourceState::UnorderedAccess, Range};
			};
			FArdaRangeNodeParameters First, Second;
			if (Case == 0)
			{
				// Redundant ranges from one node do not hide a later nested overwrite.
				First.mAccesses = {Access(EArdaDependencyAccess::Write, {}),
				    Access(EArdaDependencyAccess::Write, {16, 32})};
				Second.mAccesses = {Access(EArdaDependencyAccess::Write, {24, 8})};
			}
			else
			{
				// A read cannot use its own writes to fill a gap in another producer's coverage.
				First.mAccesses = {Access(EArdaDependencyAccess::Write, {0, 64}),
				    Access(EArdaDependencyAccess::Read, {0, 128})};
				Second.mAccesses = {Access(EArdaDependencyAccess::Write, {64, 64})};
				if (Case == 2)
				{
					First.mAccesses.back().mBufferRange = {64, 64};
				}
			}
			const auto FirstNode = Graph.AttachOrFind<FArdaRangeTestNode>("first", First);
			const auto SecondNode = Graph.AttachOrFind<FArdaRangeTestNode>("second", Second);
			ASSERT_TRUE(FirstNode && SecondNode);
			const auto Status = Graph.EndGraphEdit();
			EXPECT_EQ(bool(Status), Case != 1) << Status.mMessage.c_str();
			if (Status && Case == 0)
			{
				EXPECT_TRUE(Graph.GetTopology().IsReachable(FirstNode.mValue, SecondNode.mValue));
			}
		}
	}

	TEST(ArdaInductor, ManyPitchedRowsKeepIndependentWritersAndCompleteReadCoverage)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mSearchMode = EArdaInductorSearchMode::Greedy;
		ASSERT_TRUE(Graph.SetOptions(Options));
		constexpr uint64_t RowCount = 32768;
		const auto Resource = Buffer(Graph, "pitched rows", RowCount * 1024);
		FArdaRangeNodeParameters EvenRows, OddRows, Reader;
		for (uint64_t Row = RowCount; Row-- > 0;)
		{
			// Reverse insertion and alternating owners exercise indexing without treating padding as produced.
			FArdaDependencyAccess Access{Resource,
			    EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::UnorderedAccess,
			    {Row * 1024, 1020}};
			(Row % 2 ? OddRows : EvenRows).mAccesses.push_back(Access);
			Access.mAccess = EArdaDependencyAccess::Read;
			Reader.mAccesses.push_back(Access);
		}
		const auto Read = Graph.AttachOrFind<FArdaRangeTestNode>("reader", Reader);
		const auto Even = Graph.AttachOrFind<FArdaRangeTestNode>("even", EvenRows);
		const auto Odd = Graph.AttachOrFind<FArdaRangeTestNode>("odd", OddRows);
		ASSERT_TRUE(Read && Even && Odd);
		const auto Status = Graph.EndGraphEdit();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Even.mValue, Read.mValue));
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Odd.mValue, Read.mValue));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(Even.mValue, Odd.mValue));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(Odd.mValue, Even.mValue));
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Read.mValue)->mPayload.mDesc.mAccesses.size(), RowCount);
	}

	FArdaRHITextureDesc RangeTexture()
	{
		FArdaRHITextureDesc Desc;
		Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
		Desc.mWidth = Desc.mHeight = 16;
		Desc.mMipLevels = 3;
		Desc.mArraySize = 3;
		Desc.mFormat = EArdaRHIFormat::R32Float;
		Desc.mUsage = EArdaRHITextureUsage::UnorderedAccess | EArdaRHITextureUsage::ShaderResource;
		return Desc;
	}

	TEST(ArdaInductor, RepeatedTextureWritesOrderOnlyOverlappingMipsAndArraySlices)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Texture = Graph.CreateTexture("texture regions", RangeTexture());
		ASSERT_TRUE(Texture);
		const auto Access = [&](EArdaDependencyAccess Mode, FArdaRHITextureSubresourceRange Range)
		{
			return FArdaDependencyAccess{Texture.mValue, Mode, EArdaRHIResourceState::UnorderedAccess, {}, Range};
		};
		const auto EarlyRead =
		    RangeNode(Graph, "read unchanged mip", {Access(EArdaDependencyAccess::Read, {0, 1, 0, 1})});
		const auto First =
		    RangeNode(Graph, "write first two mips", {Access(EArdaDependencyAccess::Write, {0, 2, 0, 2})});
		const auto Before = RangeNode(Graph, "read old mip", {Access(EArdaDependencyAccess::Read, {1, 1, 1, 1})});
		const auto Second =
		    RangeNode(Graph, "overwrite one slice mip", {Access(EArdaDependencyAccess::Write, {1, 1, 1, 1})});
		const auto Joined =
		    RangeNode(Graph, "read all produced mips", {Access(EArdaDependencyAccess::Read, {0, 3, 0, 2})});
		const auto LastMip = RangeNode(Graph, "write last mip", {Access(EArdaDependencyAccess::Write, {2, 1, 0, 2})});
		const auto Status = Graph.EndGraphEdit();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_TRUE(Graph.GetTopology().FindEdge(First, EarlyRead));
		EXPECT_TRUE(Graph.GetTopology().FindEdge(First, Before));
		EXPECT_TRUE(Graph.GetTopology().FindEdge(Before, Second));
		EXPECT_TRUE(Graph.GetTopology().FindEdge(First, Joined));
		EXPECT_TRUE(Graph.GetTopology().FindEdge(Second, Joined));
		EXPECT_TRUE(Graph.GetTopology().FindEdge(LastMip, Joined));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(EarlyRead, Second));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(Second, EarlyRead));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(LastMip, First));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(First, LastMip));
	}

	TEST(ArdaInductor, TextureReadWriteRequiresPriorCoverageOfEveryReadSubresource)
	{
		for (const bool bInitializeBothSlices : {false, true})
		{
			SCOPED_TRACE(bInitializeBothSlices);
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			const auto Texture = Graph.CreateTexture("texture update", RangeTexture());
			ASSERT_TRUE(Texture);
			const auto Access = [&](EArdaDependencyAccess Mode, uint32_t SliceCount)
			{
				return FArdaDependencyAccess{Texture.mValue,
				    Mode,
				    EArdaRHIResourceState::UnorderedAccess,
				    {},
				    {1, 1, 0, SliceCount}};
			};
			const auto First =
			    RangeNode(Graph, "initialize", {Access(EArdaDependencyAccess::Write, bInitializeBothSlices ? 2u : 1u)});
			const auto Update = RangeNode(Graph, "update both slices", {Access(EArdaDependencyAccess::ReadWrite, 2)});
			const auto Read = RangeNode(Graph, "read both slices", {Access(EArdaDependencyAccess::Read, 2)});
			const auto Status = Graph.EndGraphEdit();
			EXPECT_EQ(bool(Status), bInitializeBothSlices) << Status.mMessage.c_str();
			if (Status)
			{
				EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
				    (eastl::vector<FArdaGraphNodeHandle>{First, Update, Read}));
			}
		}
	}

	TEST(ArdaInductor, RejectsExplicitTextureRangesInsteadOfClampingThem)
	{
		ASSERT_TRUE(FArdaRangeTestNode::Register());
		const FArdaRHITextureSubresourceRange InvalidRanges[] = {{1, 3, 0, 1, 0, 1},
		    {0, 1, 2, 2, 0, 1},
		    {0, 1, 0, 1, 0, 2},
		    {3, ArdaRHIAllSubresources, 0, 1, 0, 1},
		    {0, 0, 0, 1, 0, 1}};
		for (const auto Range : InvalidRanges)
		{
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			const auto Texture = Graph.CreateTexture("texture", RangeTexture());
			ASSERT_TRUE(Texture);
			FArdaRangeNodeParameters Writer;
			Writer.mAccesses.push_back(
			    {Texture.mValue, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess, {}, Range});
			ASSERT_TRUE(Graph.AttachOrFind("writer", "inductor.test.ranges", Writer));
			EXPECT_FALSE(Graph.EndGraphEdit());
		}
	}

	TEST(ArdaInductor, RequiresProducerCoverageForTextureMipAndArrayReads)
	{
		ASSERT_TRUE(FArdaRangeTestNode::Register());
		for (uint32_t Case = 0; Case < 3; ++Case)
		{
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			const auto Texture = Graph.CreateTexture("texture", RangeTexture());
			ASSERT_TRUE(Texture);
			FArdaRangeNodeParameters Writer, Reader;
			Writer.mAccesses.push_back({Texture.mValue,
			    EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::UnorderedAccess,
			    {},
			    {1, 2, 1, 2}});
			Reader.mAccesses.push_back({Texture.mValue,
			    EArdaDependencyAccess::Read,
			    EArdaRHIResourceState::UnorderedAccess,
			    {},
			    {Case == 1 ? 0u : 1u, 1, Case == 2 ? 0u : 1u, 1}});
			ASSERT_TRUE(Graph.AttachOrFind("writer", "inductor.test.ranges", Writer));
			ASSERT_TRUE(Graph.AttachOrFind("reader", "inductor.test.ranges", Reader));
			EXPECT_EQ(bool(Graph.EndGraphEdit()), Case == 0);
		}
	}

	TEST(ArdaInductor, DisjointProducersHaveIndependentConsumersAndRemoval)
	{
		ASSERT_TRUE(FArdaRangeTestNode::Register());
		for (bool Texture : {false, true})
		{
			FArdaDependencyGraph G;
			ASSERT_TRUE(G.BeginGraphEdit());
			auto TD = RangeTexture();
			TD.mMipLevels = 1;
			TD.mArraySize = 2;
			const auto R = Texture ? G.CreateTexture("partitioned", TD).mValue : Buffer(G, "partitioned", 256);
			auto Access = [&](uint32_t Half, EArdaDependencyAccess Mode, bool Whole = false)
			{
				FArdaDependencyAccess A;
				A.mResource = R;
				A.mAccess = Mode;
				A.mState = EArdaRHIResourceState::UnorderedAccess;
				if (Texture)
				{
					A.mTextureRange = {0, 1, Half, Whole ? 2u : 1u};
				}
				else
				{
					A.mBufferRange = {Half * 128u, Whole ? 256u : 128u};
				}
				return A;
			};
			const auto C0 = G.AttachOrFind("read lower",
			    "inductor.test.ranges",
			    FArdaRangeNodeParameters{{Access(0, EArdaDependencyAccess::Read)}});
			const auto C1 = G.AttachOrFind("read upper",
			    "inductor.test.ranges",
			    FArdaRangeNodeParameters{{Access(1, EArdaDependencyAccess::Read)}});
			const auto All = G.AttachOrFind("join",
			    "inductor.test.ranges",
			    FArdaRangeNodeParameters{{Access(0, EArdaDependencyAccess::Read, true)}});
			const auto W1 = G.AttachOrFind("write upper",
			    "inductor.test.ranges",
			    FArdaRangeNodeParameters{{Access(1, EArdaDependencyAccess::Write)}});
			const auto W0 = G.AttachOrFind("write lower",
			    "inductor.test.ranges",
			    FArdaRangeNodeParameters{{Access(0, EArdaDependencyAccess::Write)}});
			ASSERT_TRUE(C0 && C1 && All && W0 && W1);
			const auto Compiled = G.EndGraphEdit();
			ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			EXPECT_FALSE(G.GetTopology().IsReachable(W0.mValue, W1.mValue));
			EXPECT_FALSE(G.GetTopology().IsReachable(W1.mValue, W0.mValue));
			EXPECT_FALSE(G.GetTopology().IsReachable(W0.mValue, C1.mValue));
			EXPECT_FALSE(G.GetTopology().IsReachable(W1.mValue, C0.mValue));
			EXPECT_TRUE(G.GetTopology().IsReachable(W0.mValue, All.mValue));
			EXPECT_TRUE(G.GetTopology().IsReachable(W1.mValue, All.mValue));
			EXPECT_TRUE(G.GetCompileResult().mMemoryDependencies.empty());
			ASSERT_TRUE(G.BeginGraphEdit());
			ASSERT_TRUE(G.RemoveNode(W0.mValue));
			EXPECT_FALSE(G.GetTopology().ContainsNode(C0.mValue));
			EXPECT_FALSE(G.GetTopology().ContainsNode(All.mValue));
			EXPECT_TRUE(G.GetTopology().ContainsNode(C1.mValue));
			EXPECT_TRUE(G.GetTopology().ContainsNode(W1.mValue));
			ASSERT_TRUE(G.EndGraphEdit());
		}
	}

	TEST(ArdaInductor, OrdersOverlappingRangeProducersAndRejectsGaps)
	{
		ASSERT_TRUE(FArdaRangeTestNode::Register());
		for (uint64_t UpperOffset : {127u, 129u})
		{
			FArdaDependencyGraph G;
			ASSERT_TRUE(G.BeginGraphEdit());
			const auto R = Buffer(G, "partitioned", 256);
			FArdaRangeNodeParameters Lower, Upper, Read;
			Lower.mAccesses = {{R, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess, {0, 128}}};
			Upper.mAccesses = {{R,
			    EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::UnorderedAccess,
			    {UpperOffset, 256 - UpperOffset}}};
			Read.mAccesses = {{R, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess}};
			const auto LowerNode = G.AttachOrFind("lower", "inductor.test.ranges", Lower);
			const auto UpperNode = G.AttachOrFind("upper", "inductor.test.ranges", Upper);
			ASSERT_TRUE(LowerNode && UpperNode);
			ASSERT_TRUE(G.AttachOrFind("read all", "inductor.test.ranges", Read));
			const auto Status = G.EndGraphEdit();
			EXPECT_EQ(bool(Status), UpperOffset == 127u) << Status.mMessage.c_str();
			if (Status)
			{
				EXPECT_TRUE(G.GetTopology().IsReachable(LowerNode.mValue, UpperNode.mValue));
			}
		}
	}

	TEST(ArdaInductor, FramebufferTargetsRequireExplicitWritableAccess)
	{
		ASSERT_TRUE(FArdaRangeTestNode::Register());
		for (uint32_t Case = 0; Case < 4; ++Case)
		{
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			auto Desc = RangeTexture();
			Desc.mUsage |= EArdaRHITextureUsage::RenderTarget;
			const auto Texture = Graph.CreateTexture("target", Desc);
			ASSERT_TRUE(Texture);
			FArdaRangeNodeParameters Parameters;
			Parameters.mColorTargets.push_back(Texture.mValue);
			if (Case)
			{
				Parameters.mAccesses.push_back({Texture.mValue,
				    EArdaDependencyAccess::Write,
				    Case >= 2 ? EArdaRHIResourceState::RenderTarget : EArdaRHIResourceState::UnorderedAccess});
				if (Case == 3)
				{
					Parameters.mAccesses.back().mTextureRange = {1, 1, 2, 1};
				}
			}
			ASSERT_TRUE(Graph.AttachOrFind("raster", "inductor.test.ranges", Parameters));
			EXPECT_EQ(bool(Graph.EndGraphEdit()), Case >= 2);
		}
	}

	TEST(ArdaInductor, AsyncChainOverlapsIndependentGraphicsScheduledBeforeItsFirstNode)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaGraphNodeHandle Previous;
		eastl::vector<FArdaGraphNodeHandle> Chain;
		FArdaDependencyResourceHandle Input;
		for (uint32_t Index = 0; Index < 4; ++Index)
		{
			eastl::string Name = "compute";
			Name.push_back(char('0' + Index));
			const auto Output = Buffer(Graph, Name.c_str());
			Previous = Node(Graph, Name.c_str(), {Input, Output});
			Chain.push_back(Previous);
			Input = Output;
		}
		const auto Graphics = Node(Graph, "independent-graphics", {{}, {}, 100, 0, true}, "inductor.test.graphics");
		const auto Join = Node(Graph, "join", {Input, {}, 1, 0, true}, "inductor.test.graphics");
		ASSERT_TRUE(Graph.AddDependency(Graphics, Join));
		ASSERT_TRUE(Graph.EndGraphEdit());
		const auto& Result = Graph.GetCompileResult();
		ASSERT_EQ(Result.mExecutionOrder.front(), Graphics);
		for (const auto Handle : Chain)
		{
			const auto Position = eastl::find(Result.mExecutionOrder.begin(), Result.mExecutionOrder.end(), Handle);
			ASSERT_NE(Position, Result.mExecutionOrder.end());
			EXPECT_EQ(Result.mQueues[size_t(Position - Result.mExecutionOrder.begin())], EArdaRHIQueueType::Compute);
		}
	}

	template <typename Interface, typename Descriptor, EArdaRHIResourceType Kind>
	class TArdaInductorExternalResource final : public Interface
	{
	public:
		explicit TArdaInductorExternalResource(Descriptor Desc, const void* Identity = nullptr)
		    : mDesc(eastl::move(Desc)),
		      mIdentity(Identity)
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
			return Kind;
		}

		const char* GetDebugName() const noexcept override
		{
			return "inductor external test";
		}

		const Descriptor& GetDesc() const noexcept override
		{
			return mDesc;
		}

		const void* GetPhysicalIdentity() const noexcept override
		{
			return mIdentity ? mIdentity : this;
		}

	private:
		Descriptor mDesc;
		const void* mIdentity;
		uint32_t mReferences = 0;
	};

	FArdaRHIBufferRef ExternalTestBuffer(uint64_t Bytes)
	{
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = Bytes;
		Desc.mUsage = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::UnorderedAccess;
		using FArdaBuffer =
		    TArdaInductorExternalResource<IArdaRHIBuffer, FArdaRHIBufferDesc, EArdaRHIResourceType::Buffer>;
		return FArdaRHIBufferRef(new FArdaBuffer(Desc));
	}

	TEST(ArdaInductor, InitialReadsOfRepeatedlyWrittenRegionsRequireImportedStorage)
	{
		for (const bool bTexture : {false, true})
		{
			for (const bool bExternal : {false, true})
			{
				for (const auto InitialMode : {EArdaDependencyAccess::Read, EArdaDependencyAccess::ReadWrite})
				{
					SCOPED_TRACE(bTexture);
					SCOPED_TRACE(bExternal);
					SCOPED_TRACE(int(InitialMode));
					FArdaDependencyGraph Graph;
					ASSERT_TRUE(Graph.BeginGraphEdit());
					FArdaDependencyResourceHandle Resource;
					if (bTexture)
					{
						using FArdaTexture = TArdaInductorExternalResource<IArdaRHITexture,
						    FArdaRHITextureDesc,
						    EArdaRHIResourceType::Texture>;
						const auto Texture = bExternal ? Graph.ImportTexture("initial texture",
						                                     FArdaRHITextureRef(new FArdaTexture(RangeTexture())))
						                               : Graph.CreateTexture("initial texture", RangeTexture());
						ASSERT_TRUE(Texture);
						Resource = Texture.mValue;
					}
					else if (bExternal)
					{
						const auto Imported = Graph.ImportBuffer("initial buffer", ExternalTestBuffer(256));
						ASSERT_TRUE(Imported);
						Resource = Imported.mValue;
					}
					else
					{
						Resource = Buffer(Graph, "initial buffer");
					}
					const FArdaDependencyAccess Initial{Resource, InitialMode, EArdaRHIResourceState::UnorderedAccess};
					const FArdaDependencyAccess Write{Resource,
					    EArdaDependencyAccess::Write,
					    EArdaRHIResourceState::UnorderedAccess};
					const auto Reader = RangeNode(Graph, "initial read", {Initial});
					const auto First = RangeNode(Graph, "first overwrite", {Write});
					const auto Second = RangeNode(Graph, "second overwrite", {Write});
					const auto Status = Graph.EndGraphEdit();
					EXPECT_EQ(bool(Status), bExternal) << Status.mMessage.c_str();
					if (Status)
					{
						EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
						    (eastl::vector<FArdaGraphNodeHandle>{Reader, First, Second}));
						EXPECT_TRUE(Graph.GetTopology().FindEdge(Reader, First));
						EXPECT_TRUE(Graph.GetTopology().FindEdge(First, Second));
					}
				}
			}
		}
	}

	TEST(ArdaInductor, RejectsDistinctImportWrappersWithTheSamePhysicalIdentity)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const uint32_t PhysicalIdentity = 1;
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 256;
		using FArdaBuffer =
		    TArdaInductorExternalResource<IArdaRHIBuffer, FArdaRHIBufferDesc, EArdaRHIResourceType::Buffer>;
		const FArdaRHIBufferRef First(new FArdaBuffer(Desc, &PhysicalIdentity));
		const FArdaRHIBufferRef Second(new FArdaBuffer(Desc, &PhysicalIdentity));
		ASSERT_NE(First.Get(), Second.Get());
		ASSERT_TRUE(Graph.ImportBuffer("first", First));
		EXPECT_FALSE(Graph.ImportBuffer("second", Second));
	}

	TEST(ArdaInductor, RetainedImportsConsumeMemoryEvenWhenAllReadersAreCulled)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto ImportedBuffer = Graph.ImportBuffer("buffer", ExternalTestBuffer(1024));
		ASSERT_TRUE(ImportedBuffer);
		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = 16;
		TextureDesc.mHeight = 8;
		TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		TextureDesc.mUsage = EArdaRHITextureUsage::ShaderResource;
		using FArdaTexture =
		    TArdaInductorExternalResource<IArdaRHITexture, FArdaRHITextureDesc, EArdaRHIResourceType::Texture>;
		const auto ImportedTexture = Graph.ImportTexture("texture", FArdaRHITextureRef(new FArdaTexture(TextureDesc)));
		ASSERT_TRUE(ImportedTexture);
		const auto Reader = Node(Graph, "unused-reader", {ImportedBuffer.mValue, {}});
		ASSERT_TRUE(Graph.EndGraphEdit());
		const auto& Result = Graph.GetCompileResult();
		EXPECT_TRUE(Result.mExecutionOrder.empty());
		EXPECT_EQ(Result.mCulledNodes, (eastl::vector<FArdaGraphNodeHandle>{Reader}));
		EXPECT_EQ(Result.mAllocatedBytes, 1536u);
		EXPECT_EQ(Result.mPeakLiveBytes, 1536u);
	}

	TEST(ArdaInductor, CulledAdaptersRetainTheirDeclaredWorkspaceUntilRemoved)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Adapter = Node(Graph, "adapter", {{}, {}, 1, 0, false, 8192});
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_TRUE(Graph.GetCompileResult().mExecutionOrder.empty());
		EXPECT_EQ(Graph.GetCompileResult().mAllocatedBytes, 8192u);
		EXPECT_EQ(Graph.GetCompileResult().mPeakLiveBytes, 8192u);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.RemoveNode(Adapter));
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_EQ(Graph.GetCompileResult().mAllocatedBytes, 0u);
	}

	TEST(ArdaInductor, EmptyScheduleCannotHideOverflowInRetainedImportAccounting)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.ImportBuffer("maximum", ExternalTestBuffer(UINT64_MAX)));
		ASSERT_TRUE(Graph.ImportBuffer("one-more", ExternalTestBuffer(1)));
		const auto Status = Graph.EndGraphEdit();
		EXPECT_FALSE(Status);
		EXPECT_TRUE(Graph.IsEditing());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, 0u);
		EXPECT_NE(Status.mMessage.find("overflow"), eastl::string::npos);
	}

	TEST(ArdaInductor, MemorySerializationDoesNotAdvertiseFalseAsyncOverlap)
	{
		RegisterTestNodes();
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mObjective = EArdaInductorObjective::Memory;
		ASSERT_TRUE(Graph.SetOptions(Options));
		const auto GraphicsBuffer = Buffer(Graph, "graphics-storage");
		Node(Graph, "a-graphics-producer", {{}, GraphicsBuffer, 100}, "inductor.test.graphics");
		const auto GraphicsEnd =
		    Node(Graph, "b-graphics-consumer", {GraphicsBuffer, {}, 100, 0, true}, "inductor.test.graphics");
		eastl::vector<FArdaGraphNodeHandle> Chain;
		FArdaDependencyResourceHandle Input;
		for (uint32_t Index = 0; Index < 4; ++Index)
		{
			eastl::string Name = "compute";
			Name.push_back(char('0' + Index));
			const auto Output = Buffer(Graph, Name.c_str());
			Chain.push_back(Node(Graph, Name.c_str(), {Input, Output}));
			Input = Output;
		}
		const auto Join = Node(Graph, "join", {Input, {}, 1, 0, true}, "inductor.test.graphics");
		ASSERT_TRUE(Graph.AddDependency(GraphicsEnd, Join));
		ASSERT_TRUE(Graph.EndGraphEdit());
		const auto& Result = Graph.GetCompileResult();
		EXPECT_FALSE(Result.mMemoryDependencies.empty());
		for (const auto Handle : Chain)
		{
			const auto Position = eastl::find(Result.mExecutionOrder.begin(), Result.mExecutionOrder.end(), Handle);
			ASSERT_NE(Position, Result.mExecutionOrder.end());
			EXPECT_EQ(Result.mQueues[size_t(Position - Result.mExecutionOrder.begin())], EArdaRHIQueueType::Graphics);
		}
	}

	TEST(ArdaInductor, RejectsInvalidPipelineRequestsBeforePublishingADeviceIndependentEdit)
	{
		ASSERT_TRUE(FArdaRangeTestNode::Register());
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.EndGraphEdit());
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaRangeNodeParameters Parameters;
		FArdaInductorPipelineRequest Request;
		Request.mKind = EArdaPipelineStateKind::Compute;
		Parameters.mPipelines.push_back(Request);
		ASSERT_TRUE(Graph.AttachOrFind("missing-shader", "inductor.test.ranges", Parameters));
		EXPECT_FALSE(Graph.EndGraphEdit());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, 1u);
		ASSERT_TRUE(Graph.CancelGraphEdit());
		ASSERT_TRUE(Graph.BeginGraphEdit());
		Request.mSlot = "default";
		Parameters.mPipelines.push_back(Request);
		ASSERT_TRUE(Graph.AttachOrFind("duplicate-slots", "inductor.test.ranges", Parameters));
		const auto Status = Graph.EndGraphEdit();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("duplicate"), eastl::string::npos);
	}

	TEST(ArdaInductor, StageOnlyNodesCannotHidePhysicalAccessesOrPipelineRequests)
	{
		ASSERT_TRUE(FArdaRangeTestNode::Register());
		for (const bool Pipeline : {false, true})
		{
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			FArdaRangeNodeParameters Parameters;
			Parameters.mbPipelineStageOnly = true;
			if (Pipeline)
			{
				Parameters.mPipelines.push_back({});
			}
			else
			{
				Parameters.mAccesses.push_back(
				    {Buffer(Graph, "storage"), EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess});
			}
			ASSERT_TRUE(Graph.AttachOrFind("stage", "inductor.test.ranges", Parameters));
			const auto Status = Graph.EndGraphEdit();
			EXPECT_FALSE(Status);
			EXPECT_NE(Status.mMessage.find("Stage-only"), eastl::string::npos);
		}
	}

	TEST(ArdaInductor, SingletonRegistryIsTypedAndLibraryIsDiscoverable)
	{
		ASSERT_TRUE(RegisterArdaBuiltinNodes());
		auto Names = FArdaNodeRegistry::Get().GetNames();
		EXPECT_NE(eastl::find(Names.begin(), Names.end(), "arda.copy-buffer"), Names.end());
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		EXPECT_FALSE(G.AttachOrFind("wrong-type", "arda.upload", uint32_t(4)));
		EXPECT_FALSE(G.AttachOrFind("missing", "not-registered", uint32_t(4)));
		ASSERT_TRUE(G.CancelGraphEdit());
	}

	TEST(ArdaInductor, DisjointBufferViewsCannotRequireIncompatibleStatesWithinOneNode)
	{
		ASSERT_TRUE(FArdaRangeTestNode::Register());
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		const auto R = Buffer(G, "shared-state");
		FArdaRangeNodeParameters P;
		P.mAccesses = {{R, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess, {0, 16}},
		    {R, EArdaDependencyAccess::Read, EArdaRHIResourceState::ShaderResource, {16, 16}}};
		ASSERT_TRUE(G.AttachOrFind("incompatible-views", "inductor.test.ranges", P));
		const auto Compiled = G.EndGraphEdit();
		EXPECT_FALSE(Compiled);
		EXPECT_NE(Compiled.mMessage.find("incompatible buffer states"), eastl::string::npos);
		EXPECT_TRUE(G.IsEditing());
		ASSERT_TRUE(G.CancelGraphEdit());
	}
}
