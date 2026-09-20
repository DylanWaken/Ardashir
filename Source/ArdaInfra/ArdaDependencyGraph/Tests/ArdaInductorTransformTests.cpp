#include "ArdaDependencyGraph.h"
#include <gtest/gtest.h>

namespace arda::transform_tests
{
	using namespace arda;

	struct FArdaStorageParameters
	{
		FArdaDependencyResourceHandle mResource;
		bool mbWrite = false;
	};

	struct FArdaStorageNode
	    : TArdaDependencyNode<FArdaStorageNode, FArdaStorageParameters, EArdaDependencyNodeKind::Compute>
	{
		using FArdaParameters = FArdaStorageParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"rules.storage", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Resource(P.mResource).Value(P.mbWrite).Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc D;
			D.mbSideEffect = !P.mbWrite;
			D.mAccesses.push_back({P.mResource,
			    P.mbWrite ? EArdaDependencyAccess::Write : EArdaDependencyAccess::Read,
			    EArdaRHIResourceState::UnorderedAccess});
			return D;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaStorageNode, StorageOperation);
	ARDA_DEFINE_GRAPH_NODE(FArdaStorageNode, StorageOperation);

	class FArdaCaptureAllocationRule final : public FArdaGraphCompileRule
	{
	public:
		explicit FArdaCaptureAllocationRule(uint64_t& Bytes)
		    : mBytes(Bytes)
		{
		}

		const char* GetName() const noexcept override
		{
			return "CaptureAllocation";
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph&, FArdaInductorCompileResult& R) const override
		{
			mBytes = R.mAllocatedBytes;
			return {};
		}

		uint64_t& mBytes;
	};

	TEST(ArdaInductorTransforms, MemoryRuleReordersExistingResultAndReplanningReducesLiveStorage)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaRHIBufferDesc D;
		D.mByteSize = 1024;
		D.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		const auto A = AllocateBuffer(Edit, D, "A"), B = AllocateBuffer(Edit, D, "B");
		ASSERT_TRUE(A);
		ASSERT_TRUE(B);
		ASSERT_TRUE(StorageOperation(Edit, {A.GetHandle(), true}, "a.write A"));
		ASSERT_TRUE(StorageOperation(Edit, {B.GetHandle(), true}, "b.write B"));
		ASSERT_TRUE(StorageOperation(Edit, {A.GetHandle(), false}, "c.read A"));
		ASSERT_TRUE(StorageOperation(Edit, {B.GetHandle(), false}, "d.read B"));
		FArdaInductorOptions O;
		uint64_t Before = 0;
		O.mRules.mCompileRules = {MakeArdaGraphRule<FArdaFlattenGraphRule>(),
		    MakeArdaGraphRule<FArdaStableOrderRule>(),
		    MakeArdaGraphRule<FArdaPlanMemoryRule>(),
		    MakeArdaGraphRule<FArdaCaptureAllocationRule>(Before),
		    MakeArdaGraphRule<FArdaMinimizeMemoryRule>(128),
		    MakeArdaGraphRule<FArdaPlanMemoryRule>(true),
		    MakeArdaGraphRule<FArdaAssignQueuesRule>(),
		    MakeArdaGraphRule<FArdaBatchCudaRule>(),
		    MakeArdaGraphRule<FArdaFlattenDispatchesRule>(),
		    MakeArdaGraphRule<FArdaResolveFencesRule>()};
		ASSERT_TRUE(Edit.SetOptions(O));
		ASSERT_TRUE(Edit.Commit());
		const auto S = Graph.Compile();
		ASSERT_TRUE(S) << S.mMessage.c_str();
		EXPECT_LT(Graph.GetCompileResult().mAllocatedBytes, Before);
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder[1], Graph.FindNode("c.read A"));
		EXPECT_EQ(Graph.GetCompileResult().mAllocationOrder, Graph.GetCompileResult().mExecutionOrder);
	}

	template <EArdaDependencyNodeKind Kind>
	struct TArdaDispatchParameters
	{
	};

	template <EArdaDependencyNodeKind Kind>
	struct TArdaDispatchNode : TArdaDependencyNode<TArdaDispatchNode<Kind>, TArdaDispatchParameters<Kind>, Kind>
	{
		using FArdaParameters = TArdaDispatchParameters<Kind>;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {Kind == EArdaDependencyNodeKind::Cuda      ? "rules.dispatch.cuda"
			        : Kind == EArdaDependencyNodeKind::Copy    ? "rules.dispatch.copy"
			        : Kind == EArdaDependencyNodeKind::Compute ? "rules.dispatch.compute"
			                                                   : "rules.dispatch.graphics",
			    1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters&)
		{
			return "dispatch";
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			FArdaDependencyNodeDesc D;
			D.mbSideEffect = true;
			D.mEstimatedCost = 1;
			return D;
		}

		template <EArdaDependencyNodeKind K = Kind, eastl::enable_if_t<K != EArdaDependencyNodeKind::Cuda, int> = 0>
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}

		template <EArdaDependencyNodeKind K = Kind, eastl::enable_if_t<K == EArdaDependencyNodeKind::Cuda, int> = 0>
		static FArdaRHIStatus PrepareCuda(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaCudaSequence&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(TArdaDispatchNode<EArdaDependencyNodeKind::Copy>, DispatchCopy);
	ARDA_DEFINE_GRAPH_NODE(TArdaDispatchNode<EArdaDependencyNodeKind::Copy>, DispatchCopy);

	ARDA_DECLARE_GRAPH_NODE(TArdaDispatchNode<EArdaDependencyNodeKind::Compute>, DispatchCompute);
	ARDA_DEFINE_GRAPH_NODE(TArdaDispatchNode<EArdaDependencyNodeKind::Compute>, DispatchCompute);

	ARDA_DECLARE_GRAPH_NODE(TArdaDispatchNode<EArdaDependencyNodeKind::Cuda>, DispatchCuda);
	ARDA_DEFINE_GRAPH_NODE(TArdaDispatchNode<EArdaDependencyNodeKind::Cuda>, DispatchCuda);

	ARDA_DECLARE_GRAPH_NODE(TArdaDispatchNode<EArdaDependencyNodeKind::Graphics>, DispatchGraphics);
	ARDA_DEFINE_GRAPH_NODE(TArdaDispatchNode<EArdaDependencyNodeKind::Graphics>, DispatchGraphics);

	class FArdaIncreaseCostRule final : public FArdaGraphPreprocessRule
	{
	public:
		explicit FArdaIncreaseCostRule(uint32_t Increment)
		    : mIncrement(Increment)
		{
		}

		const char* GetName() const noexcept override
		{
			return "IncreaseCost";
		}

		FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const override
		{
			for (auto N : Graph.GetTopology().GetNodes())
			{
				Graph.GetTopology().TryGetNode(N)->mPayload.mDesc.mEstimatedCost += mIncrement;
			}
			return {};
		}

		uint32_t mIncrement;
	};

	TEST(ArdaInductorTransforms, ForcedRecompilationRestartsFromUnprocessedConstruction)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		auto N = DispatchCopy(Edit, {}, "node");
		ASSERT_TRUE(N);
		FArdaInductorOptions O;
		O.mRules.mPreprocessRules.push_back(MakeArdaGraphRule<FArdaIncreaseCostRule>(7));
		ASSERT_TRUE(Edit.SetOptions(O));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Graph.FindNode("node"))->mPayload.mDesc.mEstimatedCost, 8u);
		(void)Graph.Execute();
		ASSERT_TRUE(Graph.Compile(true));
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Graph.FindNode("node"))->mPayload.mDesc.mEstimatedCost, 8u);
	}

	TEST(ArdaInductorTransforms, ReplacingConfiguredInstancesInvalidatesCachedRevisions)
	{
		FArdaDependencyGraph Graph;
		FArdaInductorOptions First, Second;
		First.mRules.mPreprocessRules.push_back(MakeArdaGraphRule<FArdaIncreaseCostRule>(1));
		Second.mRules.mPreprocessRules.push_back(MakeArdaGraphRule<FArdaIncreaseCostRule>(2));
		uint32_t Frame = 0;
		for (const auto* Options : {&First, &First, &Second})
		{
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetOptions(*Options));
			ASSERT_TRUE(DispatchCopy(Edit, {}, "node"));
			ASSERT_TRUE(Edit.Commit());
			ASSERT_TRUE(Graph.Compile());
			EXPECT_EQ(Graph.GetTopology().TryGetNode(Graph.FindNode("node"))->mPayload.mDesc.mEstimatedCost,
			    Frame++ < 2 ? 2u : 3u);
			(void)Graph.Execute();
		}
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 2u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 1u);
	}

	class FArdaClearFencesRule final : public FArdaGraphCompileRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "ClearFences";
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph&, FArdaInductorCompileResult& R) const override
		{
			R.mFences.clear();
			return {};
		}
	};

	TEST(ArdaInductorTransforms, ResultContainsQueueDispatchesCudaStreamsAndCrossDomainFences)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		auto A = DispatchCopy(Edit, {}, "copy");
		auto B = DispatchCompute(Edit, {}, "compute");
		auto C = DispatchCuda(Edit, {}, "cuda");
		auto D = DispatchGraphics(Edit, {}, "graphics");
		ASSERT_TRUE(A);
		ASSERT_TRUE(B);
		ASSERT_TRUE(C);
		ASSERT_TRUE(D);
		ASSERT_TRUE(Edit.Order(A, B));
		ASSERT_TRUE(Edit.Order(B, C));
		ASSERT_TRUE(Edit.Order(C, D));
		FArdaInductorOptions O;
		O.mMinimumAsyncChain = 1;
		O.mMinimumAsyncSlack = 0;
		ASSERT_TRUE(Edit.SetOptions(O));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		const auto& R = Graph.GetCompileResult();
		ASSERT_EQ(R.mDispatches.size(), 4u);
		ASSERT_EQ(R.mCudaStreams.size(), 1u);
		EXPECT_EQ(R.mCudaStreams[0], (eastl::vector<uint32_t>{2}));
		EXPECT_EQ(R.mQueueDispatches[GetArdaRHIQueueIndex(EArdaRHIQueueType::Copy)], (eastl::vector<uint32_t>{0}));
		EXPECT_EQ(R.mQueueDispatches[GetArdaRHIQueueIndex(EArdaRHIQueueType::Compute)], (eastl::vector<uint32_t>{1}));
		ASSERT_EQ(R.mFences.size(), 3u);
		for (uint32_t I = 0; I < 3; ++I)
		{
			EXPECT_EQ(R.mFences[I].mProducer, I);
			EXPECT_EQ(R.mFences[I].mConsumer, I + 1);
		}
		const auto Revision = R.mRevision;
		(void)Graph.Execute();
		FArdaGraphEdit Rejected(Graph);
		const auto NextA = DispatchCopy(Rejected, {}, "copy");
		const auto NextB = DispatchCompute(Rejected, {}, "compute");
		const auto NextC = DispatchCuda(Rejected, {}, "cuda");
		const auto NextD = DispatchGraphics(Rejected, {}, "graphics");
		ASSERT_TRUE(Rejected.Order(NextA, NextB));
		ASSERT_TRUE(Rejected.Order(NextB, NextC));
		ASSERT_TRUE(Rejected.Order(NextC, NextD));
		O.mRules.mCompileRules.push_back(MakeArdaGraphRule<FArdaClearFencesRule>());
		ASSERT_TRUE(Rejected.SetOptions(O));
		ASSERT_TRUE(Rejected.Commit());
		EXPECT_FALSE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
		ASSERT_TRUE(Graph.DiscardPendingEdits());
	}
}
