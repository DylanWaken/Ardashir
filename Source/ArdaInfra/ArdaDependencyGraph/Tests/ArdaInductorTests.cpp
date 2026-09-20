#include "Graph/Node/ArdaDependencyNode.h"
#include "ArdaDependencyGraph.h"
#include "Graph/DefaultNodes/ArdaDependencyGraphNodes.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Inductor/Rules/ArdaInductorDependencyRules.h"
#include <gtest/gtest.h>

namespace arda::inductor_tests
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

	ARDA_DECLARE_GRAPH_NODE(TArdaInductorTestNode<EArdaDependencyNodeKind::Compute>, InductorCompute);
	ARDA_DEFINE_GRAPH_NODE(TArdaInductorTestNode<EArdaDependencyNodeKind::Compute>, InductorCompute);
	ARDA_DECLARE_GRAPH_NODE(TArdaInductorTestNode<EArdaDependencyNodeKind::Graphics>, InductorGraphics);
	ARDA_DEFINE_GRAPH_NODE(TArdaInductorTestNode<EArdaDependencyNodeKind::Graphics>, InductorGraphics);
	ARDA_DECLARE_GRAPH_NODE(TArdaInductorTestNode<EArdaDependencyNodeKind::Cuda>, InductorCuda);
	ARDA_DEFINE_GRAPH_NODE(TArdaInductorTestNode<EArdaDependencyNodeKind::Cuda>, InductorCuda);
	ARDA_DECLARE_GRAPH_NODE(TArdaInductorTestNode<EArdaDependencyNodeKind::Copy>, InductorCopy);
	ARDA_DEFINE_GRAPH_NODE(TArdaInductorTestNode<EArdaDependencyNodeKind::Copy>, InductorCopy);

	FArdaDependencyResourceHandle Buffer(FArdaGraphEdit& Edit, const char* Name, uint64_t Bytes = 256)
	{
		FArdaRHIBufferDesc D;
		D.mByteSize = Bytes;
		D.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
		auto R = AllocateBuffer(Edit, D, Name);
		EXPECT_TRUE(R);
		return R.GetHandle();
	}

	TEST(ArdaInductor, ResourceDependenciesIgnoreOperationConstructionOrder)
	{

		FArdaDependencyGraph G;
		FArdaGraphEdit GEdit(G);
		auto A = Buffer(GEdit, "a"), B = Buffer(GEdit, "b"), C = Buffer(GEdit, "c");
		const auto Consumer = InductorCompute(GEdit, {B, C}, "consumer").GetHandle();
		const auto Producer = InductorCompute(GEdit, {{}, A}, "producer").GetHandle();
		const auto Middle = InductorCompute(GEdit, {A, B}, "middle").GetHandle();
		GEdit.Keep(C);
		ASSERT_TRUE(GEdit.Commit());
		ASSERT_TRUE(G.Compile());
		EXPECT_EQ(G.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{Producer, Middle, Consumer}));
		EXPECT_TRUE(G.GetTopology().IsReachable(Producer, Consumer));
		EXPECT_EQ(G.GetCompileResult().mRevision, 1u);
		EXPECT_FALSE(AllocateBuffer(GEdit, {}));
		EXPECT_FALSE(G.Execute().mStatus);
	}

	TEST(ArdaInductor, EditCancellationDoesNotReviveAbandonedResourceOrNodeHandles)
	{

		FArdaDependencyGraph Graph;
		FArdaDependencyResourceHandle Abandoned;
		FArdaGraphNodeHandle Old;
		{
			FArdaGraphEdit GraphEdit(Graph);
			Abandoned = Buffer(GraphEdit, "abandoned");
			Old = InductorCompute(GraphEdit, {{}, Abandoned}, "abandoned").GetHandle();
			ASSERT_TRUE(GraphEdit.Cancel());
		}
		FArdaGraphEdit GraphEdit(Graph);
		const auto Replacement = Buffer(GraphEdit, "replacement");
		const auto CurrentOperation = InductorCompute(GraphEdit, {{}, Replacement}, "replacement");
		const auto Current = CurrentOperation.GetHandle();
		EXPECT_NE(Abandoned, Replacement);
		EXPECT_NE(Old, Current);
		EXPECT_FALSE(Graph.GetTopology().ContainsNode(Old));
		EXPECT_EQ(Graph.FindResource(Abandoned), nullptr);
		EXPECT_FALSE(InductorCompute(GraphEdit, {Abandoned, Replacement}));
		EXPECT_FALSE(GraphEdit.Commit());
		ASSERT_TRUE(GraphEdit.Cancel());
	}

	TEST(ArdaInductor, RejectsUnproducedReadsAndCycles)
	{

		FArdaDependencyGraph Graph;
		for (uint32_t Variant = 0; Variant < 3; ++Variant)
		{
			FArdaGraphEdit GraphEdit(Graph);
			const auto A = Buffer(GraphEdit, "a"), B = Buffer(GraphEdit, "b");
			const auto First = InductorCompute(GraphEdit, {{}, A});
			const auto Second = InductorCompute(GraphEdit, {Variant == 0 ? B : A, B});
			if (Variant == 1)
			{
				ASSERT_TRUE(GraphEdit.Order(Second, First));
			}
			GraphEdit.Keep(B);
			ASSERT_TRUE(GraphEdit.Commit());
			EXPECT_EQ(bool(Graph.Compile()), Variant == 2);
			EXPECT_FALSE(Graph.IsEditing());
			if (Variant != 2)
			{
				ASSERT_TRUE(Graph.DiscardPendingEdits());
			}
		}
	}

	TEST(ArdaInductor, FreshConstructionsOmitBranchesAndCullUnusedOperations)
	{

		FArdaDependencyGraph Graph;
		for (const bool IncludeBranch : {true, false})
		{
			FArdaGraphEdit GraphEdit(Graph);
			const auto Sync = InductorCompute(GraphEdit, {{}, {}, 1, 0, true}, "ordered-only");
			if (IncludeBranch)
			{
				const auto A = Buffer(GraphEdit, "a"), B = Buffer(GraphEdit, "b"), Dead = Buffer(GraphEdit, "dead");
				const auto Producer = InductorCompute(GraphEdit, {{}, A}, "producer");
				InductorCompute(GraphEdit, {A, B}, "consumer");
				InductorCompute(GraphEdit, {{}, Dead}, "dead");
				ASSERT_TRUE(GraphEdit.Order(Producer, Sync));
				GraphEdit.Keep(B);
			}
			ASSERT_TRUE(GraphEdit.Commit());
			ASSERT_TRUE(Graph.Compile());
			EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), IncludeBranch ? 3u : 1u);
			EXPECT_EQ(Graph.GetCompileResult().mCulledNodes.size(), IncludeBranch ? 1u : 0u);
			EXPECT_FALSE(Graph.Execute().mStatus);
		}
	}

	TEST(ArdaInductor, CoalescesCudaWhilePreservingGraphicsDependencies)
	{
		FArdaDependencyGraph G;
		FArdaGraphEdit GEdit(G);
		auto A = Buffer(GEdit, "a"), B = Buffer(GEdit, "b"), C = Buffer(GEdit, "c"), D = Buffer(GEdit, "d");
		auto P = InductorCompute(GEdit, {{}, A}, "producer").GetHandle();
		auto FirstOperation = InductorCuda(GEdit, {A, B}, "cuda1");
		const auto First = FirstOperation.GetHandle();
		auto SecondOperation = InductorCuda(GEdit, {B, C}, "cuda2");
		const auto Second = SecondOperation.GetHandle();
		auto GraphicsOperation = InductorGraphics(GEdit, {C, D}, "graphics");
		const auto Graphics = GraphicsOperation.GetHandle();
		GEdit.Keep(D);
		ASSERT_TRUE(GEdit.GetStatus());
		ASSERT_TRUE(GEdit.Commit());
		ASSERT_TRUE(G.Compile());
		ASSERT_EQ(G.GetCompileResult().mCudaBatches.size(), 1u);
		EXPECT_EQ(G.GetCompileResult().mCudaBatches[0], (eastl::vector<FArdaGraphNodeHandle>{First, Second}));
	}

	TEST(ArdaInductor, ExhaustiveSearchEvaluatesAllOrdersAndCoalescesCuda)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		FArdaInductorOptions Options;
		Options.mSearchMode = EArdaInductorSearchMode::Exhaustive;
		Options.mMaxSearchStates = 1; // Explicit exhaustive search ignores this limit.
		ASSERT_TRUE(GraphEdit.SetOptions(Options));
		InductorCuda(GraphEdit, {{}, {}, 1, 0, true}, "a-cuda").GetHandle();
		InductorGraphics(GraphEdit, {{}, {}, 3, 0, true}, "b-graphics").GetHandle();
		InductorCuda(GraphEdit, {{}, {}, 1, 0, true}, "c-cuda").GetHandle();
		InductorGraphics(GraphEdit, {{}, {}, 3, 0, true}, "d-graphics").GetHandle();
		ASSERT_TRUE(GraphEdit.Commit());
		ASSERT_TRUE(Graph.Compile());
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
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		FArdaInductorOptions Options;
		Options.mMaxSearchStates = 1;
		ASSERT_TRUE(GraphEdit.SetOptions(Options));
		InductorCompute(GraphEdit, {{}, {}, 1, 0, true}, "a").GetHandle();
		InductorCompute(GraphEdit, {{}, {}, 1, 0, true}, "b").GetHandle();
		ASSERT_TRUE(GraphEdit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 2u);
		EXPECT_TRUE(Graph.GetCompileResult().mbSearchExhausted);
		EXPECT_FALSE(Graph.GetCompileResult().mbSearchComplete);
		EXPECT_EQ(Graph.GetCompileResult().mSearchStatesExamined, 1u);
	}

	TEST(ArdaInductor, ExhaustiveSearchHasNoRecursiveDepthLimit)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		FArdaInductorOptions Options;
		Options.mSearchMode = EArdaInductorSearchMode::Exhaustive;
		ASSERT_TRUE(GraphEdit.SetOptions(Options));
		FArdaGraphOperation Previous;
		for (uint32_t Index = 0; Index < 300; ++Index)
		{
			eastl::string Name = "node-";
			Name += std::to_string(Index).c_str();
			const auto CurrentOperation = InductorCompute(GraphEdit, {{}, {}, 1, 0, true}, Name.c_str());
			const auto Current = CurrentOperation.GetHandle();
			if (Previous)
			{
				ASSERT_TRUE(GraphEdit.Order(Previous, CurrentOperation));
			}
			Previous = CurrentOperation;
		}
		ASSERT_TRUE(GraphEdit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_TRUE(Graph.GetCompileResult().mbSearchComplete);
		EXPECT_EQ(Graph.GetCompileResult().mSearchStatesExamined, 301u);
		EXPECT_EQ(Graph.GetCompileResult().mSchedulesExamined, 3u);
	}

	TEST(ArdaInductor, TemporaryWorkspacesAreAliasedAndBudgetedForEveryFrameSlot)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		FArdaInductorOptions Options;
		Options.mObjective = EArdaInductorObjective::Memory;
		Options.mFramesInFlight = 3;
		ASSERT_TRUE(GraphEdit.SetOptions(Options));
		const auto FirstOperation = InductorCompute(GraphEdit, {{}, {}, 1, 0, true, 128, 4096}, "first");
		const auto First = FirstOperation.GetHandle();
		const auto SecondOperation = InductorCompute(GraphEdit, {{}, {}, 1, 0, true, 0, 4096}, "second");
		const auto Second = SecondOperation.GetHandle();
		InductorCompute(GraphEdit, {{}, {}, 1, 0, false, 0, 16384}, "culled").GetHandle();
		ASSERT_TRUE(GraphEdit.Commit());
		ASSERT_TRUE(Graph.Compile());
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
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		InductorCompute(GraphEdit, {{}, {}, 1, 0, true}, "node").GetHandle();
		ASSERT_TRUE(GraphEdit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_FALSE(Graph.OptimizeFromTimingProfile());
		EXPECT_FALSE(Graph.IsEditing());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, 1u);
	}

	TEST(ArdaInductor, MemoryObjectiveReusesCompatibleDeadValuesAndKeepsOutputsLive)
	{
		FArdaDependencyGraph G;
		FArdaGraphEdit GEdit(G);
		FArdaInductorOptions O;
		O.mObjective = EArdaInductorObjective::Memory;
		ASSERT_TRUE(GEdit.SetOptions(O));
		auto A = Buffer(GEdit, "a"), B = Buffer(GEdit, "b"), C = Buffer(GEdit, "c"), D = Buffer(GEdit, "d");
		InductorCompute(GEdit, {{}, A}, "a").GetHandle();
		InductorCompute(GEdit, {A, B}, "b").GetHandle();
		InductorCompute(GEdit, {B, C}, "c").GetHandle();
		InductorCompute(GEdit, {C, D}, "d").GetHandle();
		GEdit.Keep(D);
		ASSERT_TRUE(GEdit.GetStatus());
		ASSERT_TRUE(GEdit.Commit());
		ASSERT_TRUE(G.Compile());
		EXPECT_GT(G.GetCompileResult().mAliasedBytes, 0u);
		EXPECT_LT(G.GetCompileResult().mAllocatedBytes, 1024u);
		EXPECT_GE(G.GetCompileResult().mAllocatedBytes, 512u);
	}

	TEST(ArdaInductor, PureSynchronizationDoesNotSplitAnOrderedCudaBatch)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		const auto A = Buffer(GraphEdit, "a"), B = Buffer(GraphEdit, "b");
		const auto FirstOperation = InductorCuda(GraphEdit, {{}, A}, "first");
		const auto First = FirstOperation.GetHandle();
		const auto SecondOperation = InductorCuda(GraphEdit, {A, B}, "second");
		const auto Second = SecondOperation.GetHandle();
		const auto Sync = SyncNode(GraphEdit, FArdaGraphSyncParameters{}, "sync");
		ASSERT_TRUE(Sync);
		ASSERT_TRUE(GraphEdit.Order(FirstOperation, Sync));
		ASSERT_TRUE(GraphEdit.Order(Sync, SecondOperation));
		GraphEdit.Keep(B);
		ASSERT_TRUE(GraphEdit.GetStatus());
		ASSERT_TRUE(GraphEdit.Commit());
		ASSERT_TRUE(Graph.Compile());
		ASSERT_EQ(Graph.GetCompileResult().mCudaBatches.size(), 1u);
		EXPECT_EQ(Graph.GetCompileResult().mCudaBatches.front(), (eastl::vector<FArdaGraphNodeHandle>{First, Second}));
	}

	TEST(ArdaInductor, EfficiencyObjectiveDoesNotSerializeIndependentLifetimesForReuse)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		const auto A = Buffer(GraphEdit, "a"), B = Buffer(GraphEdit, "b");
		InductorCompute(GraphEdit, {{}, A}, "a-producer").GetHandle();
		InductorCompute(GraphEdit, {A, {}, 1, 0, true}, "a-consumer").GetHandle();
		InductorCompute(GraphEdit, {{}, B}, "b-producer").GetHandle();
		InductorCompute(GraphEdit, {B, {}, 1, 0, true}, "b-consumer").GetHandle();
		ASSERT_TRUE(GraphEdit.Commit());
		ASSERT_TRUE(Graph.Compile());
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

	ARDA_DECLARE_GRAPH_NODE(FArdaRangeTestNode, InductorRanges);
	ARDA_DEFINE_GRAPH_NODE(FArdaRangeTestNode, InductorRanges);

	TEST(ArdaInductor, RepeatedWritesPreserveInterveningReadsAndSelectTheLatestProducer)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		const auto Resource = Buffer(GraphEdit, "versions");
		const FArdaDependencyAccess Write{Resource,
		    EArdaDependencyAccess::Write,
		    EArdaRHIResourceState::UnorderedAccess};
		const FArdaDependencyAccess Read{Resource, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess};
		const auto A = InductorRanges(GraphEdit, {{Write}}, "write first").GetHandle();
		const auto B = InductorRanges(GraphEdit, {{Read}}, "read first").GetHandle();
		const auto C = InductorRanges(GraphEdit, {{Write}}, "write second").GetHandle();
		const auto D = InductorRanges(GraphEdit, {{Read}}, "read second").GetHandle();
		ASSERT_TRUE(GraphEdit.Commit());
		const auto Status = Graph.Compile();
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
		FArdaGraphEdit GraphEdit(Graph);
		const auto Resource = Buffer(GraphEdit, "reader groups");
		const FArdaDependencyAccess Write{Resource,
		    EArdaDependencyAccess::Write,
		    EArdaRHIResourceState::UnorderedAccess};
		const FArdaDependencyAccess Read{Resource, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess};
		const auto FirstWriter = InductorRanges(GraphEdit, {{Write}}, "first writer").GetHandle();
		const auto FirstReader = InductorRanges(GraphEdit, {{Read}}, "first reader").GetHandle();
		const auto SecondReader = InductorRanges(GraphEdit, {{Read}}, "second reader").GetHandle();
		const auto SecondWriter = InductorRanges(GraphEdit, {{Write}}, "second writer").GetHandle();
		const auto ThirdReader = InductorRanges(GraphEdit, {{Read}}, "third reader").GetHandle();
		const auto FourthReader = InductorRanges(GraphEdit, {{Read}}, "fourth reader").GetHandle();
		ASSERT_TRUE(GraphEdit.Commit());
		const auto Status = Graph.Compile();
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
			FArdaGraphEdit GraphEdit(Graph);
			const auto Resource = Buffer(GraphEdit, "in place");
			const FArdaDependencyAccess Write{Resource,
			    EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::UnorderedAccess};
			const FArdaDependencyAccess Read{Resource,
			    EArdaDependencyAccess::Read,
			    EArdaRHIResourceState::UnorderedAccess};
			const FArdaDependencyAccess ReadWrite{Resource,
			    EArdaDependencyAccess::ReadWrite,
			    EArdaRHIResourceState::UnorderedAccess};
			const auto Producer = InductorRanges(GraphEdit, {{Write}}, "producer").GetHandle();
			const auto Reader = InductorRanges(GraphEdit, {{Read}}, "before update").GetHandle();
			const auto Update = InductorRanges(GraphEdit,
			    {bSeparateAccesses ? eastl::vector<FArdaDependencyAccess>{Read, Write}
			                       : eastl::vector<FArdaDependencyAccess>{ReadWrite}},
			    "update")
			                        .GetHandle();
			const auto Consumer = InductorRanges(GraphEdit, {{Read}}, "after update").GetHandle();
			ASSERT_TRUE(GraphEdit.Commit());
			const auto Status = Graph.Compile();
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
			    (eastl::vector<FArdaGraphNodeHandle>{Producer, Reader, Update, Consumer}));
			EXPECT_TRUE(Graph.GetTopology().FindEdge(Producer, Update));
			EXPECT_TRUE(Graph.GetTopology().FindEdge(Reader, Update));
			EXPECT_TRUE(Graph.GetTopology().FindEdge(Update, Consumer));
			EXPECT_FALSE(Graph.GetTopology().FindEdge(Update, Update));
		}
	}

	TEST(ArdaInductor, RepeatedWritesFollowOperationOrderAcrossCancellationAndRevisionReuse)
	{

		FArdaDependencyGraph Graph;
		FArdaGraphNodeHandle Abandoned;
		{
			FArdaGraphEdit GraphEdit(Graph);
			const auto Resource = Buffer(GraphEdit, "overwritten");
			Abandoned = InductorRanges(GraphEdit,
			    {{{Resource, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess}}},
			    "canceled")
			                .GetHandle();
			ASSERT_TRUE(GraphEdit.Cancel());
		}
		for (const bool Reverse : {false, true, false})
		{
			FArdaGraphEdit GraphEdit(Graph);
			const auto Resource = Buffer(GraphEdit, "overwritten");
			const FArdaDependencyAccess Write{Resource,
			    EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::UnorderedAccess};
			const auto First = InductorRanges(GraphEdit, {{Write}}, Reverse ? "third" : "first").GetHandle();
			const auto Second = InductorRanges(GraphEdit, {{Write}}, "second").GetHandle();
			const auto Third = InductorRanges(GraphEdit, {{Write}}, Reverse ? "first" : "third").GetHandle();
			EXPECT_NE(First, Abandoned);
			ASSERT_TRUE(GraphEdit.Commit());
			ASSERT_TRUE(Graph.Compile());
			const auto CompiledFirst = Graph.FindNode(Reverse ? "third" : "first");
			const auto CompiledSecond = Graph.FindNode("second");
			const auto CompiledThird = Graph.FindNode(Reverse ? "first" : "third");
			EXPECT_TRUE(Graph.GetTopology().IsReachable(CompiledFirst, CompiledSecond));
			EXPECT_TRUE(Graph.GetTopology().IsReachable(CompiledSecond, CompiledThird));
			EXPECT_FALSE(Graph.GetTopology().ContainsNode(Abandoned));
			EXPECT_FALSE(Graph.Execute().mStatus);
		}
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 2u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 1u);
	}

	TEST(ArdaInductor, ReaderPlacementInEachConstructionSelectsItsLatestWriter)
	{

		FArdaDependencyGraph Graph;
		for (const bool LateReader : {false, true})
		{
			FArdaGraphEdit GraphEdit(Graph);
			const auto Resource = Buffer(GraphEdit, "reader placement");
			const FArdaDependencyAccess Write{Resource,
			    EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::UnorderedAccess};
			const FArdaDependencyAccess Read{Resource,
			    EArdaDependencyAccess::Read,
			    EArdaRHIResourceState::UnorderedAccess};
			const auto FirstWriter = InductorRanges(GraphEdit, {{Write}}, "first writer").GetHandle();
			FArdaGraphNodeHandle Reader;
			if (!LateReader)
			{
				Reader = InductorRanges(GraphEdit, {{Read}}, "reader").GetHandle();
			}
			const auto SecondWriter = InductorRanges(GraphEdit, {{Write}}, "second writer").GetHandle();
			const auto LastReader = InductorRanges(GraphEdit, {{Read}}, "last reader").GetHandle();
			if (LateReader)
			{
				Reader = InductorRanges(GraphEdit, {{Read}}, "reader").GetHandle();
			}
			ASSERT_TRUE(GraphEdit.Commit());
			ASSERT_TRUE(Graph.Compile());
			EXPECT_EQ(bool(Graph.GetTopology().FindEdge(SecondWriter, Reader)), LateReader);
			EXPECT_EQ(bool(Graph.GetTopology().FindEdge(FirstWriter, Reader)), !LateReader);
			EXPECT_FALSE(Graph.GetTopology().IsReachable(LastReader, Reader));
			EXPECT_FALSE(Graph.Execute().mStatus);
		}
	}

	TEST(ArdaInductor, ConflictingOrderFailsAndAReplacementConstructionCanCompile)
	{

		FArdaDependencyGraph Graph;
		for (const bool ConflictingOrder : {true, false})
		{
			FArdaGraphEdit GraphEdit(Graph);
			const auto Resource = Buffer(GraphEdit, "ordered hazards");
			const FArdaDependencyAccess Write{Resource,
			    EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::UnorderedAccess};
			const FArdaDependencyAccess Read{Resource,
			    EArdaDependencyAccess::Read,
			    EArdaRHIResourceState::UnorderedAccess};
			const auto First = InductorRanges(GraphEdit, {{Write}});
			const auto Reader = InductorRanges(GraphEdit, {{Read}});
			const auto Second = InductorRanges(GraphEdit, {{Write}});
			if (ConflictingOrder)
			{
				ASSERT_TRUE(GraphEdit.Order(Second, Reader));
			}
			ASSERT_TRUE(GraphEdit.Commit());
			EXPECT_EQ(bool(Graph.Compile()), !ConflictingOrder);
			if (ConflictingOrder)
			{
				ASSERT_TRUE(Graph.DiscardPendingEdits());
			}
			else
			{
				EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
				    (eastl::vector<FArdaGraphNodeHandle>{First.GetHandle(), Reader.GetHandle(), Second.GetHandle()}));
			}
		}
	}

	TEST(ArdaInductor, OmittingAnInitialWriterStillRequiresCoverageForReadWriteOperations)
	{

		for (const bool ReadWrite : {false, true})
		{
			for (const bool IncludeInitialWriter : {false, true})
			{
				FArdaDependencyGraph Graph;
				FArdaGraphEdit GraphEdit(Graph);
				const auto Resource = Buffer(GraphEdit, "optional initialization");
				if (IncludeInitialWriter)
				{
					InductorRanges(GraphEdit,
					    {{{Resource, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess}}},
					    "initialize")
					    .GetHandle();
				}
				InductorRanges(GraphEdit,
				    {{{Resource,
				        ReadWrite ? EArdaDependencyAccess::ReadWrite : EArdaDependencyAccess::Write,
				        EArdaRHIResourceState::UnorderedAccess}}},
				    "update")
				    .GetHandle();
				InductorRanges(GraphEdit,
				    {{{Resource, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess}}},
				    "read")
				    .GetHandle();
				ASSERT_TRUE(GraphEdit.Commit());
				EXPECT_EQ(bool(Graph.Compile()), IncludeInitialWriter || !ReadWrite);
			}
		}
	}

	TEST(ArdaInductor, ImplicitWriteHazardsRemainWithOrWithoutExplicitOperationOrder)
	{

		for (const bool InterveningReader : {false, true})
		{
			for (const bool ExplicitOrder : {false, true})
			{
				FArdaDependencyGraph Graph;
				FArdaGraphEdit GraphEdit(Graph);
				const auto Resource = Buffer(GraphEdit, "retained hazards");
				const FArdaDependencyAccess Write{Resource,
				    EArdaDependencyAccess::Write,
				    EArdaRHIResourceState::UnorderedAccess};
				const FArdaDependencyAccess Read{Resource,
				    EArdaDependencyAccess::Read,
				    EArdaRHIResourceState::UnorderedAccess};
				const auto First = InductorRanges(GraphEdit, {{Write}});
				const auto Before = InterveningReader ? InductorRanges(GraphEdit, {{Read}}) : First;
				const auto Second = InductorRanges(GraphEdit, {{Write}});
				if (ExplicitOrder)
				{
					ASSERT_TRUE(GraphEdit.Order(Before, Second));
				}
				ASSERT_TRUE(GraphEdit.Commit());
				ASSERT_TRUE(Graph.Compile());
				const auto Edge = Graph.GetTopology().FindEdge(Before.GetHandle(), Second.GetHandle());
				ASSERT_TRUE(Edge);
				EXPECT_TRUE(Graph.GetTopology().TryGetEdge(Edge)->mPayload.mbHazard);
				EXPECT_FALSE(Graph.GetTopology().TryGetEdge(Edge)->mPayload.mbResource);
			}
		}
	}

	class FArdaPrefixHazardProbeRule final : public FArdaGraphPreprocessRule
	{
	public:
		FArdaPrefixHazardProbeRule(eastl::vector<FArdaGraphNodeHandle> Readers, FArdaGraphNodeHandle Writer)
		    : mReaders(eastl::move(Readers)),
		      mWriter(Writer)
		{
		}

		const char* GetName() const noexcept override
		{
			return "InspectPrefixDependencies";
		}

		FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const override
		{
			const auto& Topology = Graph.GetTopology();
			EXPECT_EQ(Topology.GetNodeCount(), mReaders.size() + 1);
			EXPECT_EQ(Topology.GetEdgeCount(), mReaders.size());
			EXPECT_EQ(Topology.GetOutgoingEdges(mWriter).size(), mReaders.size());
			for (const auto Reader : mReaders)
			{
				const auto Edge = Topology.FindEdge(mWriter, Reader);
				EXPECT_TRUE(Edge);
				if (Edge)
				{
					EXPECT_TRUE(Topology.TryGetEdge(Edge)->mPayload.mbResource);
					EXPECT_FALSE(Topology.TryGetEdge(Edge)->mPayload.mbHazard);
				}
				EXPECT_EQ(Topology.GetIncomingEdges(Reader).size(), 1u);
				EXPECT_TRUE(Topology.GetOutgoingEdges(Reader).empty());
			}
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Dependency inspection complete.");
		}

	private:
		eastl::vector<FArdaGraphNodeHandle> mReaders;
		FArdaGraphNodeHandle mWriter;
	};

	TEST(ArdaInductor, ManyNestedPrefixReadersKeepExactlyOneProducerEdgeEach)
	{

		constexpr uint32_t ReaderCount = 32768;
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		const auto Resource = Buffer(GraphEdit, "nested prefixes", ReaderCount);
		eastl::vector<FArdaGraphNodeHandle> Readers;
		Readers.reserve(ReaderCount);
		for (uint32_t Index = 0; Index < ReaderCount; ++Index)
		{
			const auto Reader = InductorRanges(GraphEdit,
			    {{{Resource, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess, {0, Index + 1}}}});
			ASSERT_TRUE(Reader);
			Readers.push_back(Reader.GetHandle());
		}
		const auto Writer = InductorRanges(GraphEdit,
		    {{{Resource, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess}}});
		ASSERT_TRUE(Writer);
		FArdaInductorOptions Options;
		// Inspect dependency indexing before general reachability validation and scheduling, whose
		// work on this deliberately broad fan-out is unrelated to the interval-index regression.
		Options.mRules.mPreprocessRules = {MakeArdaGraphRule<FArdaResolveDependenciesRule>(),
		    MakeArdaGraphRule<FArdaPrefixHazardProbeRule>(Readers, Writer.GetHandle())};
		ASSERT_TRUE(GraphEdit.SetOptions(Options));
		ASSERT_TRUE(GraphEdit.Commit());
		const auto Status = Graph.Compile();
		EXPECT_EQ(Status.mCode, EArdaRHIResult::InvalidState);
		EXPECT_EQ(Status.mMessage, "InspectPrefixDependencies: Dependency inspection complete.");
	}

	TEST(ArdaInductor, RejectsUnwrittenBufferBytesAndAcceptsMergedProducerRanges)
	{
		for (const bool Gap : {false, true})
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit GraphEdit(Graph);
			const auto Resource = Buffer(GraphEdit, "range", 256);
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
			ASSERT_TRUE(InductorRanges(GraphEdit, Reader, "reader"));
			ASSERT_TRUE(InductorRanges(GraphEdit, Writer, "writer"));
			ASSERT_TRUE(GraphEdit.Commit());
			const auto Status = Graph.Compile();
			EXPECT_EQ(bool(Status), !Gap) << Status.mMessage.c_str();
		}
	}

	TEST(ArdaInductor, RepeatedByteWritesDoNotImposeAttachmentOrderOnNeighboringSingleWriterRegions)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		const auto Resource = Buffer(GraphEdit, "mixed regions", 256);
		const auto Access = [&](EArdaDependencyAccess Mode, FArdaRHIBufferRange Range)
		{
			return FArdaDependencyAccess{Resource, Mode, EArdaRHIResourceState::UnorderedAccess, Range};
		};
		// Bytes [0, 64) and [128, 256) each have one writer and retain declarative producer ordering.
		const auto EarlyRead =
		    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Read, {0, 64})}}, "read unchanged prefix")
		        .GetHandle();
		const auto First =
		    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Write, {0, 128})}}, "write lower half")
		        .GetHandle();
		const auto Before =
		    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Read, {64, 64})}}, "read old overlap")
		        .GetHandle();
		const auto Second =
		    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Write, {64, 64})}}, "overwrite overlap")
		        .GetHandle();
		const auto Joined =
		    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Read, {})}}, "read all").GetHandle();
		const auto Upper =
		    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Write, {128, 128})}}, "write upper half")
		        .GetHandle();
		ASSERT_TRUE(GraphEdit.Commit());
		const auto Status = Graph.Compile();
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
		FArdaGraphEdit GraphEdit(Graph);
		const auto Resource = Buffer(GraphEdit, "partially initialized", 128);
		const auto Access = [&](EArdaDependencyAccess Mode, FArdaRHIBufferRange Range)
		{
			return FArdaDependencyAccess{Resource, Mode, EArdaRHIResourceState::UnorderedAccess, Range};
		};
		InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Write, {0, 64})}}, "initialize prefix").GetHandle();
		const auto EarlyRead =
		    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Read, {})}}, "read uninitialized suffix")
		        .GetHandle();
		InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Write, {})}}, "write all").GetHandle();
		InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Write, {64, 64})}}, "overwrite suffix").GetHandle();
		ASSERT_TRUE(GraphEdit.Commit());
		EXPECT_FALSE(Graph.Compile());
	}

	TEST(ArdaInductor, IndexedBufferProducersPreserveUnionsAndHalfOpenBoundaries)
	{
		for (const bool Reverse : {false, true})
		{
			SCOPED_TRACE(Reverse);
			FArdaDependencyGraph Graph;
			FArdaGraphEdit GraphEdit(Graph);
			const auto Resource = Buffer(GraphEdit, "fragmented", 256);
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
			const auto First = InductorRanges(GraphEdit, Reverse ? Upper : Lower, "first");
			const auto Second = InductorRanges(GraphEdit, Reverse ? Lower : Upper, "second");
			ASSERT_TRUE(First && Second);
			const auto LowerWriter = Reverse ? Second.GetHandle() : First.GetHandle();
			const auto UpperWriter = Reverse ? First.GetHandle() : Second.GetHandle();

			// Exact boundaries must not introduce a false dependency on the neighboring producer.
			const auto LowerRead =
			    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Read, {0, 80})}}, "lower read");
			const auto UpperRead =
			    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Read, {80, 176})}}, "upper read");
			const auto JoinedRead =
			    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Read, {32, 128})}}, "joined read");
			ASSERT_TRUE(LowerRead && UpperRead && JoinedRead);
			ASSERT_TRUE(GraphEdit.Commit());
			const auto Status = Graph.Compile();
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			EXPECT_TRUE(Graph.GetTopology().IsReachable(LowerWriter, LowerRead.GetHandle()));
			EXPECT_FALSE(Graph.GetTopology().IsReachable(UpperWriter, LowerRead.GetHandle()));
			EXPECT_TRUE(Graph.GetTopology().IsReachable(UpperWriter, UpperRead.GetHandle()));
			EXPECT_FALSE(Graph.GetTopology().IsReachable(LowerWriter, UpperRead.GetHandle()));
			EXPECT_TRUE(Graph.GetTopology().IsReachable(LowerWriter, JoinedRead.GetHandle()));
			EXPECT_TRUE(Graph.GetTopology().IsReachable(UpperWriter, JoinedRead.GetHandle()));
		}
	}

	TEST(ArdaInductor, IndexedBufferProducersOrderNestedWritesAndRejectSelfProducedInputs)
	{
		for (uint32_t Case = 0; Case < 3; ++Case)
		{
			SCOPED_TRACE(Case);
			FArdaDependencyGraph Graph;
			FArdaGraphEdit GraphEdit(Graph);
			const auto Resource = Buffer(GraphEdit, "range", 256);
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
			const auto FirstNode = InductorRanges(GraphEdit, First, "first");
			const auto SecondNode = InductorRanges(GraphEdit, Second, "second");
			ASSERT_TRUE(FirstNode && SecondNode);
			ASSERT_TRUE(GraphEdit.Commit());
			const auto Status = Graph.Compile();
			EXPECT_EQ(bool(Status), Case != 1) << Status.mMessage.c_str();
			if (Status && Case == 0)
			{
				EXPECT_TRUE(Graph.GetTopology().IsReachable(FirstNode.GetHandle(), SecondNode.GetHandle()));
			}
		}
	}

	TEST(ArdaInductor, ManyPitchedRowsKeepIndependentWritersAndCompleteReadCoverage)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		FArdaInductorOptions Options;
		Options.mSearchMode = EArdaInductorSearchMode::Greedy;
		ASSERT_TRUE(GraphEdit.SetOptions(Options));
		constexpr uint64_t RowCount = 32768;
		const auto Resource = Buffer(GraphEdit, "pitched rows", RowCount * 1024);
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
		const auto Read = InductorRanges(GraphEdit, Reader, "reader");
		const auto Even = InductorRanges(GraphEdit, EvenRows, "even");
		const auto Odd = InductorRanges(GraphEdit, OddRows, "odd");
		ASSERT_TRUE(Read && Even && Odd);
		ASSERT_TRUE(GraphEdit.Commit());
		const auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Even.GetHandle(), Read.GetHandle()));
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Odd.GetHandle(), Read.GetHandle()));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(Even.GetHandle(), Odd.GetHandle()));
		EXPECT_FALSE(Graph.GetTopology().IsReachable(Odd.GetHandle(), Even.GetHandle()));
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Read.GetHandle())->mPayload.mDesc.mAccesses.size(), RowCount);
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
		FArdaGraphEdit GraphEdit(Graph);
		const auto Texture = AllocateTexture(GraphEdit, RangeTexture(), "texture regions");
		ASSERT_TRUE(Texture);
		const auto Access = [&](EArdaDependencyAccess Mode, FArdaRHITextureSubresourceRange Range)
		{
			return FArdaDependencyAccess{Texture.GetHandle(), Mode, EArdaRHIResourceState::UnorderedAccess, {}, Range};
		};
		const auto EarlyRead =
		    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Read, {0, 1, 0, 1})}}, "read unchanged mip")
		        .GetHandle();
		const auto First =
		    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Write, {0, 2, 0, 2})}}, "write first two mips")
		        .GetHandle();
		const auto Before =
		    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Read, {1, 1, 1, 1})}}, "read old mip")
		        .GetHandle();
		const auto Second =
		    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Write, {1, 1, 1, 1})}}, "overwrite one slice mip")
		        .GetHandle();
		const auto Joined =
		    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Read, {0, 3, 0, 2})}}, "read all produced mips")
		        .GetHandle();
		const auto LastMip =
		    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Write, {2, 1, 0, 2})}}, "write last mip")
		        .GetHandle();
		ASSERT_TRUE(GraphEdit.Commit());
		const auto Status = Graph.Compile();
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
			FArdaGraphEdit GraphEdit(Graph);
			const auto Texture = AllocateTexture(GraphEdit, RangeTexture(), "texture update");
			ASSERT_TRUE(Texture);
			const auto Access = [&](EArdaDependencyAccess Mode, uint32_t SliceCount)
			{
				return FArdaDependencyAccess{Texture.GetHandle(),
				    Mode,
				    EArdaRHIResourceState::UnorderedAccess,
				    {},
				    {1, 1, 0, SliceCount}};
			};
			const auto First = InductorRanges(GraphEdit,
			    {{Access(EArdaDependencyAccess::Write, bInitializeBothSlices ? 2u : 1u)}},
			    "initialize")
			                       .GetHandle();
			const auto Update =
			    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::ReadWrite, 2)}}, "update both slices")
			        .GetHandle();
			const auto Read =
			    InductorRanges(GraphEdit, {{Access(EArdaDependencyAccess::Read, 2)}}, "read both slices").GetHandle();
			ASSERT_TRUE(GraphEdit.Commit());
			const auto Status = Graph.Compile();
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
		const FArdaRHITextureSubresourceRange InvalidRanges[] = {{1, 3, 0, 1, 0, 1},
		    {0, 1, 2, 2, 0, 1},
		    {0, 1, 0, 1, 0, 2},
		    {3, ArdaRHIAllSubresources, 0, 1, 0, 1},
		    {0, 0, 0, 1, 0, 1}};
		for (const auto Range : InvalidRanges)
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit GraphEdit(Graph);
			const auto Texture = AllocateTexture(GraphEdit, RangeTexture(), "texture");
			ASSERT_TRUE(Texture);
			FArdaRangeNodeParameters Writer;
			Writer.mAccesses.push_back(
			    {Texture.GetHandle(), EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess, {}, Range});
			ASSERT_TRUE(InductorRanges(GraphEdit, Writer, "writer"));
			ASSERT_TRUE(GraphEdit.Commit());
			EXPECT_FALSE(Graph.Compile());
		}
	}

	TEST(ArdaInductor, RequiresProducerCoverageForTextureMipAndArrayReads)
	{
		for (uint32_t Case = 0; Case < 3; ++Case)
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit GraphEdit(Graph);
			const auto Texture = AllocateTexture(GraphEdit, RangeTexture(), "texture");
			ASSERT_TRUE(Texture);
			FArdaRangeNodeParameters Writer, Reader;
			Writer.mAccesses.push_back({Texture.GetHandle(),
			    EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::UnorderedAccess,
			    {},
			    {1, 2, 1, 2}});
			Reader.mAccesses.push_back({Texture.GetHandle(),
			    EArdaDependencyAccess::Read,
			    EArdaRHIResourceState::UnorderedAccess,
			    {},
			    {Case == 1 ? 0u : 1u, 1, Case == 2 ? 0u : 1u, 1}});
			ASSERT_TRUE(InductorRanges(GraphEdit, Writer, "writer"));
			ASSERT_TRUE(InductorRanges(GraphEdit, Reader, "reader"));
			ASSERT_TRUE(GraphEdit.Commit());
			EXPECT_EQ(bool(Graph.Compile()), Case == 0);
		}
	}

	TEST(ArdaInductor, DisjointProducersHaveIndependentConsumers)
	{
		for (bool Texture : {false, true})
		{
			FArdaDependencyGraph G;
			FArdaGraphEdit GEdit(G);
			auto TD = RangeTexture();
			TD.mMipLevels = 1;
			TD.mArraySize = 2;
			const auto R =
			    Texture ? AllocateTexture(GEdit, TD, "partitioned").GetHandle() : Buffer(GEdit, "partitioned", 256);
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
			const auto C0 =
			    InductorRanges(GEdit, FArdaRangeNodeParameters{{Access(0, EArdaDependencyAccess::Read)}}, "read lower");
			const auto C1 =
			    InductorRanges(GEdit, FArdaRangeNodeParameters{{Access(1, EArdaDependencyAccess::Read)}}, "read upper");
			const auto All =
			    InductorRanges(GEdit, FArdaRangeNodeParameters{{Access(0, EArdaDependencyAccess::Read, true)}}, "join");
			const auto W1 = InductorRanges(GEdit,
			    FArdaRangeNodeParameters{{Access(1, EArdaDependencyAccess::Write)}},
			    "write upper");
			const auto W0 = InductorRanges(GEdit,
			    FArdaRangeNodeParameters{{Access(0, EArdaDependencyAccess::Write)}},
			    "write lower");
			ASSERT_TRUE(C0 && C1 && All && W0 && W1);
			ASSERT_TRUE(GEdit.Commit());
			const auto Compiled = G.Compile();
			ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			EXPECT_FALSE(G.GetTopology().IsReachable(W0.GetHandle(), W1.GetHandle()));
			EXPECT_FALSE(G.GetTopology().IsReachable(W1.GetHandle(), W0.GetHandle()));
			EXPECT_FALSE(G.GetTopology().IsReachable(W0.GetHandle(), C1.GetHandle()));
			EXPECT_FALSE(G.GetTopology().IsReachable(W1.GetHandle(), C0.GetHandle()));
			EXPECT_TRUE(G.GetTopology().IsReachable(W0.GetHandle(), All.GetHandle()));
			EXPECT_TRUE(G.GetTopology().IsReachable(W1.GetHandle(), All.GetHandle()));
			EXPECT_TRUE(G.GetCompileResult().mMemoryDependencies.empty());
		}
	}

	TEST(ArdaInductor, OrdersOverlappingRangeProducersAndRejectsGaps)
	{
		for (uint64_t UpperOffset : {127u, 129u})
		{
			FArdaDependencyGraph G;
			FArdaGraphEdit GEdit(G);
			const auto R = Buffer(GEdit, "partitioned", 256);
			FArdaRangeNodeParameters Lower, Upper, Read;
			Lower.mAccesses = {{R, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess, {0, 128}}};
			Upper.mAccesses = {{R,
			    EArdaDependencyAccess::Write,
			    EArdaRHIResourceState::UnorderedAccess,
			    {UpperOffset, 256 - UpperOffset}}};
			Read.mAccesses = {{R, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess}};
			const auto LowerNode = InductorRanges(GEdit, Lower, "lower");
			const auto UpperNode = InductorRanges(GEdit, Upper, "upper");
			ASSERT_TRUE(LowerNode && UpperNode);
			ASSERT_TRUE(InductorRanges(GEdit, Read, "read all"));
			ASSERT_TRUE(GEdit.Commit());
			const auto Status = G.Compile();
			EXPECT_EQ(bool(Status), UpperOffset == 127u) << Status.mMessage.c_str();
			if (Status)
			{
				EXPECT_TRUE(G.GetTopology().IsReachable(LowerNode.GetHandle(), UpperNode.GetHandle()));
			}
		}
	}

	TEST(ArdaInductor, FramebufferTargetsRequireExplicitWritableAccess)
	{
		for (uint32_t Case = 0; Case < 4; ++Case)
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit GraphEdit(Graph);
			auto Desc = RangeTexture();
			Desc.mUsage |= EArdaRHITextureUsage::RenderTarget;
			const auto Texture = AllocateTexture(GraphEdit, Desc, "target");
			ASSERT_TRUE(Texture);
			FArdaRangeNodeParameters Parameters;
			Parameters.mColorTargets.push_back(Texture.GetHandle());
			if (Case)
			{
				Parameters.mAccesses.push_back({Texture.GetHandle(),
				    EArdaDependencyAccess::Write,
				    Case >= 2 ? EArdaRHIResourceState::RenderTarget : EArdaRHIResourceState::UnorderedAccess});
				if (Case == 3)
				{
					Parameters.mAccesses.back().mTextureRange = {1, 1, 2, 1};
				}
			}
			ASSERT_TRUE(InductorRanges(GraphEdit, Parameters, "raster"));
			ASSERT_TRUE(GraphEdit.Commit());
			EXPECT_EQ(bool(Graph.Compile()), Case >= 2);
		}
	}

	TEST(ArdaInductor, AsyncChainOverlapsIndependentGraphicsScheduledBeforeItsFirstNode)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		FArdaGraphNodeHandle Previous;
		eastl::vector<FArdaGraphNodeHandle> Chain;
		FArdaDependencyResourceHandle Input;
		for (uint32_t Index = 0; Index < 4; ++Index)
		{
			eastl::string Name = "compute";
			Name.push_back(char('0' + Index));
			const auto Output = Buffer(GraphEdit, Name.c_str());
			Previous = InductorCompute(GraphEdit, {Input, Output}, Name.c_str()).GetHandle();
			Chain.push_back(Previous);
			Input = Output;
		}
		const auto GraphicsOperation = InductorGraphics(GraphEdit, {{}, {}, 100, 0, true}, "independent-graphics");
		const auto Graphics = GraphicsOperation.GetHandle();
		const auto JoinOperation = InductorGraphics(GraphEdit, {Input, {}, 1, 0, true}, "join");
		const auto Join = JoinOperation.GetHandle();
		ASSERT_TRUE(GraphEdit.Order(GraphicsOperation, JoinOperation));
		ASSERT_TRUE(GraphEdit.Commit());
		ASSERT_TRUE(Graph.Compile());
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
					FArdaGraphEdit GraphEdit(Graph);
					FArdaDependencyResourceHandle Resource;
					if (bTexture)
					{
						using FArdaTexture = TArdaInductorExternalResource<IArdaRHITexture,
						    FArdaRHITextureDesc,
						    EArdaRHIResourceType::Texture>;
						const auto Texture = bExternal ? BindTexture(GraphEdit,
						                                     FArdaRHITextureRef(new FArdaTexture(RangeTexture())),
						                                     "initial texture")
						                               : AllocateTexture(GraphEdit, RangeTexture(), "initial texture");
						ASSERT_TRUE(Texture);
						Resource = Texture.GetHandle();
					}
					else if (bExternal)
					{
						const auto Imported = BindBuffer(GraphEdit, ExternalTestBuffer(256), "initial buffer");
						ASSERT_TRUE(Imported);
						Resource = Imported.GetHandle();
					}
					else
					{
						Resource = Buffer(GraphEdit, "initial buffer");
					}
					const FArdaDependencyAccess Initial{Resource, InitialMode, EArdaRHIResourceState::UnorderedAccess};
					const FArdaDependencyAccess Write{Resource,
					    EArdaDependencyAccess::Write,
					    EArdaRHIResourceState::UnorderedAccess};
					const auto Reader = InductorRanges(GraphEdit, {{Initial}}, "initial read").GetHandle();
					const auto First = InductorRanges(GraphEdit, {{Write}}, "first overwrite").GetHandle();
					const auto Second = InductorRanges(GraphEdit, {{Write}}, "second overwrite").GetHandle();
					ASSERT_TRUE(GraphEdit.Commit());
					const auto Status = Graph.Compile();
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
		FArdaGraphEdit GraphEdit(Graph);
		const uint32_t PhysicalIdentity = 1;
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 256;
		using FArdaBuffer =
		    TArdaInductorExternalResource<IArdaRHIBuffer, FArdaRHIBufferDesc, EArdaRHIResourceType::Buffer>;
		const FArdaRHIBufferRef First(new FArdaBuffer(Desc, &PhysicalIdentity));
		const FArdaRHIBufferRef Second(new FArdaBuffer(Desc, &PhysicalIdentity));
		ASSERT_NE(First.Get(), Second.Get());
		ASSERT_TRUE(BindBuffer(GraphEdit, First, "first"));
		EXPECT_FALSE(BindBuffer(GraphEdit, Second, "second"));
	}

	TEST(ArdaInductor, RetainedImportsConsumeMemoryEvenWhenAllReadersAreCulled)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		const auto ImportedBuffer = BindBuffer(GraphEdit, ExternalTestBuffer(1024), "buffer");
		ASSERT_TRUE(ImportedBuffer);
		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = 16;
		TextureDesc.mHeight = 8;
		TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		TextureDesc.mUsage = EArdaRHITextureUsage::ShaderResource;
		using FArdaTexture =
		    TArdaInductorExternalResource<IArdaRHITexture, FArdaRHITextureDesc, EArdaRHIResourceType::Texture>;
		const auto ImportedTexture =
		    BindTexture(GraphEdit, FArdaRHITextureRef(new FArdaTexture(TextureDesc)), "texture");
		ASSERT_TRUE(ImportedTexture);
		const auto Reader = InductorCompute(GraphEdit, {ImportedBuffer.GetHandle(), {}}, "unused-reader").GetHandle();
		ASSERT_TRUE(GraphEdit.Commit());
		ASSERT_TRUE(Graph.Compile());
		const auto& Result = Graph.GetCompileResult();
		EXPECT_TRUE(Result.mExecutionOrder.empty());
		EXPECT_EQ(Result.mCulledNodes, (eastl::vector<FArdaGraphNodeHandle>{Reader}));
		EXPECT_EQ(Result.mAllocatedBytes, 1536u);
		EXPECT_EQ(Result.mPeakLiveBytes, 1536u);
	}

	TEST(ArdaInductor, CulledAdapterWorkspaceIsAbsentWhenTheNextConstructionOmitsIt)
	{

		FArdaDependencyGraph Graph;
		for (const bool IncludeAdapter : {true, false})
		{
			FArdaGraphEdit GraphEdit(Graph);
			if (IncludeAdapter)
			{
				InductorCompute(GraphEdit, {{}, {}, 1, 0, false, 8192}, "adapter").GetHandle();
			}
			ASSERT_TRUE(GraphEdit.Commit());
			ASSERT_TRUE(Graph.Compile());
			EXPECT_TRUE(Graph.GetCompileResult().mExecutionOrder.empty());
			EXPECT_EQ(Graph.GetCompileResult().mAllocatedBytes, IncludeAdapter ? 8192u : 0u);
			EXPECT_EQ(Graph.GetCompileResult().mPeakLiveBytes, IncludeAdapter ? 8192u : 0u);
			EXPECT_FALSE(Graph.Execute().mStatus);
		}
	}

	TEST(ArdaInductor, EmptyScheduleCannotHideOverflowInRetainedImportAccounting)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		ASSERT_TRUE(BindBuffer(GraphEdit, ExternalTestBuffer(UINT64_MAX), "maximum"));
		ASSERT_TRUE(BindBuffer(GraphEdit, ExternalTestBuffer(1), "one-more"));
		ASSERT_TRUE(GraphEdit.Commit());
		const auto Status = Graph.Compile();
		EXPECT_FALSE(Status);
		EXPECT_FALSE(Graph.IsEditing());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, 0u);
		EXPECT_NE(Status.mMessage.find("overflow"), eastl::string::npos);
	}

	TEST(ArdaInductor, MemorySerializationDoesNotAdvertiseFalseAsyncOverlap)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		FArdaInductorOptions Options;
		Options.mObjective = EArdaInductorObjective::Memory;
		ASSERT_TRUE(GraphEdit.SetOptions(Options));
		const auto GraphicsBuffer = Buffer(GraphEdit, "graphics-storage");
		InductorGraphics(GraphEdit, {{}, GraphicsBuffer, 100}, "a-graphics-producer").GetHandle();
		const auto GraphicsEndOperation =
		    InductorGraphics(GraphEdit, {GraphicsBuffer, {}, 100, 0, true}, "b-graphics-consumer");
		const auto GraphicsEnd = GraphicsEndOperation.GetHandle();
		eastl::vector<FArdaGraphNodeHandle> Chain;
		FArdaDependencyResourceHandle Input;
		for (uint32_t Index = 0; Index < 4; ++Index)
		{
			eastl::string Name = "compute";
			Name.push_back(char('0' + Index));
			const auto Output = Buffer(GraphEdit, Name.c_str());
			Chain.push_back(InductorCompute(GraphEdit, {Input, Output}, Name.c_str()).GetHandle());
			Input = Output;
		}
		const auto JoinOperation = InductorGraphics(GraphEdit, {Input, {}, 1, 0, true}, "join");
		const auto Join = JoinOperation.GetHandle();
		ASSERT_TRUE(GraphEdit.Order(GraphicsEndOperation, JoinOperation));
		ASSERT_TRUE(GraphEdit.Commit());
		ASSERT_TRUE(Graph.Compile());
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

		FArdaDependencyGraph Graph;
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Graph.Compile());
		EXPECT_FALSE(Graph.Execute().mStatus);
		for (const bool DuplicateSlots : {false, true})
		{
			FArdaGraphEdit Edit(Graph);
			FArdaRangeNodeParameters Parameters;
			FArdaInductorPipelineRequest Request;
			Request.mKind = EArdaPipelineStateKind::Compute;
			Parameters.mPipelines.push_back(Request);
			if (DuplicateSlots)
			{
				Request.mSlot = "default";
				Parameters.mPipelines.push_back(Request);
			}
			ASSERT_TRUE(InductorRanges(Edit, Parameters));
			ASSERT_TRUE(Edit.Commit());
			const auto Status = Graph.Compile();
			EXPECT_FALSE(Status);
			EXPECT_EQ(Graph.GetCompileResult().mRevision, 1u);
			if (DuplicateSlots)
			{
				EXPECT_NE(Status.mMessage.find("duplicate"), eastl::string::npos);
			}
			ASSERT_TRUE(Graph.DiscardPendingEdits());
		}
	}

	TEST(ArdaInductor, StageOnlyNodesCannotHidePhysicalAccessesOrPipelineRequests)
	{
		for (const bool Pipeline : {false, true})
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit GraphEdit(Graph);
			FArdaRangeNodeParameters Parameters;
			Parameters.mbPipelineStageOnly = true;
			if (Pipeline)
			{
				Parameters.mPipelines.push_back({});
			}
			else
			{
				Parameters.mAccesses.push_back({Buffer(GraphEdit, "storage"),
				    EArdaDependencyAccess::Write,
				    EArdaRHIResourceState::UnorderedAccess});
			}
			ASSERT_TRUE(InductorRanges(GraphEdit, Parameters, "stage"));
			ASSERT_TRUE(GraphEdit.Commit());
			const auto Status = Graph.Compile();
			EXPECT_FALSE(Status);
			EXPECT_NE(Status.mMessage.find("Stage-only"), eastl::string::npos);
		}
	}

	TEST(ArdaInductor, SingletonRegistryIsTypedAndLibraryIsDiscoverable)
	{

		ASSERT_TRUE(RegisterArdaBuiltinNodes());
		const auto Names = FArdaNodeRegistry::Get().GetNames();
		EXPECT_NE(eastl::find(Names.begin(), Names.end(), "arda.copy-buffer"), Names.end());
		FArdaDependencyGraph Graph;
		for (const char* Definition : {"arda.upload", "not-registered"})
		{
			FArdaGraphEdit Edit(Graph);
			EXPECT_FALSE(Edit.Invoke(Definition, uint32_t{4}));
			EXPECT_FALSE(Edit.Commit());
			ASSERT_TRUE(Edit.Cancel());
		}
	}

	TEST(ArdaInductor, DisjointBufferViewsCannotRequireIncompatibleStatesWithinOneNode)
	{
		FArdaDependencyGraph G;
		FArdaGraphEdit GEdit(G);
		const auto R = Buffer(GEdit, "shared-state");
		FArdaRangeNodeParameters P;
		P.mAccesses = {{R, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess, {0, 16}},
		    {R, EArdaDependencyAccess::Read, EArdaRHIResourceState::ShaderResource, {16, 16}}};
		ASSERT_TRUE(InductorRanges(GEdit, P, "incompatible-views"));
		ASSERT_TRUE(GEdit.Commit());
		const auto Compiled = G.Compile();
		EXPECT_FALSE(Compiled);
		EXPECT_NE(Compiled.mMessage.find("incompatible buffer states"), eastl::string::npos);
		EXPECT_FALSE(G.IsEditing());
		ASSERT_TRUE(G.DiscardPendingEdits());
	}
}
