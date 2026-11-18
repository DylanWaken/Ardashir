#include "Graph/Node/ArdaNodeDefinition.h"
#include <gtest/gtest.h>
#include <string>

namespace arda::dependency_node_tests
{
	using namespace arda;

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaNodeValueParameters)
		ARDA_NODE_PARAMETER(uint32_t, mValue)
	ARDA_END_NODE_PARAMETER_STRUCT()

	struct FArdaLifecycleNode
	    : TArdaNodeDefinition<FArdaLifecycleNode, FArdaNodeValueParameters, EArdaNodeType::Compute>
	{
		inline static uint32_t mPreparations = 0, mInstances = 0, mLiveStates = 0, mLiveInstances = 0;
		inline static bool mbFailPrepare = false, mbNullPrepare = false, mbFailInstance = false, mbNullInstance = false;

		struct FArdaState
		{
			FArdaState()
			{
				++mLiveStates;
			}

			~FArdaState()
			{
				--mLiveStates;
			}
		};

		struct FArdaInstanceState
		{
			uint32_t mCalls = 0;

			FArdaInstanceState()
			{
				++mLiveInstances;
			}

			~FArdaInstanceState()
			{
				--mLiveInstances;
			}
		};

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-lifecycle", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Value(P.mValue).Build();
		}

		static FArdaRHIStatus Validate(const FArdaParameters& P)
		{
			return P.mValue ? FArdaRHIStatus{}
			                : FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Zero is invalid.");
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef)
		{
			++mPreparations;
			if (mbFailPrepare)
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Preparation failed.")};
			}
			if (mbNullPrepare)
			{
				return {};
			}
			return {eastl::make_shared<const FArdaState>(), {}};
		}

		static TArdaRHIResult<eastl::shared_ptr<FArdaInstanceState>> CreateInstance(FArdaRHIDeviceRef,
		    const FArdaParameters&,
		    const FArdaState&)
		{
			++mInstances;
			if (mbFailInstance)
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Instance failed.")};
			}
			if (mbNullInstance)
			{
				return {};
			}
			return {eastl::make_shared<FArdaInstanceState>(), {}};
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc D;
			D.mEstimatedCost = P.mValue;
			D.mbSideEffect = true;
			return D;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState& I)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, std::to_string(P.mValue + ++I.mCalls).c_str());
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaLifecycleNode, Lifecycle);
	ARDA_DEFINE_GRAPH_NODE(FArdaLifecycleNode, Lifecycle);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaAutoOutputParameters)
		ARDA_NODE_BUFFER(mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mBytes, 64)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbFail, false)
	ARDA_END_NODE_PARAMETER_STRUCT()

	struct FArdaAutoOutputNode
	    : TArdaNodeDefinition<FArdaAutoOutputNode, FArdaAutoOutputParameters, EArdaNodeType::Compute>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-auto-output", 1};
		}

		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
		{
			FArdaRHIBufferDesc D;
			D.mByteSize = P.mBytes;
			D.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			const auto S = C.Buffer(P.mOutput, "Result", D);
			return !S ? S
			    : P.mbFail
			    ? FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Intentional failure after declaration.")
			    : S;
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Resource(P.mOutput).Value(P.mBytes).Value(P.mbFail).Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc D;
			D.mbSideEffect = true;
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

	ARDA_DECLARE_GRAPH_NODE(FArdaAutoOutputNode, AutoOutput);
	ARDA_DEFINE_GRAPH_NODE(FArdaAutoOutputNode, AutoOutput);

	TEST(ArdaNodeResources, DeclaresDistinctOutputsAndCancelsResourcesTransactionally)
	{

		FArdaDependencyGraph Graph;
		{
			FArdaGraphEdit FailedEdit(Graph);
			EXPECT_FALSE(AutoOutput(FailedEdit, {{}, 64, true}));
			EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
			EXPECT_FALSE(FailedEdit.Commit());
		}
		FArdaDependencyResourceHandle Original;
		{
			FArdaGraphEdit Edit(Graph);
			const auto First = AutoOutput(Edit);
			const auto Second = AutoOutput(Edit);
			ASSERT_TRUE(First);
			ASSERT_TRUE(Second);
			Original = First.GetBuffer("Result");
			EXPECT_NE(Original, Second.GetBuffer("Result").GetHandle());
			EXPECT_EQ(Graph.FindResource(Original)->mBuffer.mByteSize, 64u);
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Graph.CompileAndWait());
		EXPECT_FALSE(Graph.ExecuteAndWait().mStatus);
		FArdaGraphEdit Next(Graph);
		const auto Temporary = AutoOutput(Next);
		ASSERT_TRUE(Temporary);
		const auto Abandoned = Temporary.GetBuffer("Result").GetHandle();
		ASSERT_TRUE(Next.Cancel());
		EXPECT_EQ(Graph.FindResource(Abandoned), nullptr);
		EXPECT_NE(Graph.FindResource(Original), nullptr);
	}

	TEST(ArdaNodeResources, PrefilledAndInlineParametersStayIndependentAcrossNodesAndGraphs)
	{
		FArdaDependencyGraph First;
		FArdaDependencyGraph Second;
		FArdaGraphEdit FirstEdit(First);
		FArdaGraphEdit SecondEdit(Second);
		FArdaAutoOutputNode::FArdaParameters Parameters;
		const auto A = AutoOutput(FirstEdit, Parameters, "prefilled");
		ASSERT_TRUE(A);
		EXPECT_FALSE(Parameters.mOutput);
		Parameters.mBytes = 128;
		const auto B = AutoOutput(FirstEdit, Parameters, "another");
		ASSERT_TRUE(B);
		const auto C = AutoOutput(SecondEdit, Parameters, "prefilled");
		ASSERT_TRUE(C);
		Parameters.mBytes = 512;
		const auto D = AutoOutput(
		    FirstEdit,
		    [](FArdaAutoOutputNode::FArdaParameters& P)
		    {
			    EXPECT_FALSE(P.mOutput);
			    EXPECT_EQ(P.mBytes, 64u);
			    P.mBytes = 256;
		    },
		    "inline");
		ASSERT_TRUE(D);
		const auto OutputA = First.FindOutput(A.GetHandle(), "Result");
		const auto OutputB = First.FindOutput(B.GetHandle(), "Result");
		const auto OutputC = Second.FindOutput(C.GetHandle(), "Result");
		const auto OutputD = First.FindOutput(D.GetHandle(), "Result");
		ASSERT_NE(First.FindResource(OutputA), nullptr);
		ASSERT_NE(First.FindResource(OutputB), nullptr);
		ASSERT_NE(Second.FindResource(OutputC), nullptr);
		ASSERT_NE(First.FindResource(OutputD), nullptr);
		EXPECT_EQ(First.FindResource(OutputA)->mBuffer.mByteSize, 64u);
		EXPECT_EQ(First.FindResource(OutputB)->mBuffer.mByteSize, 128u);
		EXPECT_EQ(Second.FindResource(OutputC)->mBuffer.mByteSize, 128u);
		EXPECT_EQ(First.FindResource(OutputD)->mBuffer.mByteSize, 256u);
		EXPECT_NE(OutputA, OutputB);
		EXPECT_NE(OutputB, OutputC);
		ASSERT_TRUE(FirstEdit.Commit());
		ASSERT_TRUE(First.CompileAndWait());
		ASSERT_TRUE(SecondEdit.Commit());
		ASSERT_TRUE(Second.CompileAndWait());
	}

	TEST(ArdaNodeResources, InlineInitializationRequiresAnEditAndPropagatesFailure)
	{

		FArdaDependencyGraph Graph;
		uint32_t Calls = 0;
		auto Initialize = [&](FArdaAutoOutputNode::FArdaParameters& P)
		{
			++Calls;
			P.mBytes = 128;
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Invalid initializer input.");
		};
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.Cancel());
		EXPECT_FALSE(AutoOutput(Edit, Initialize));
		EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::InvalidState);
		EXPECT_EQ(Calls, 0u);
		FArdaGraphEdit Failed(Graph);
		EXPECT_FALSE(AutoOutput(Failed, Initialize));
		EXPECT_EQ(Failed.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(Failed.GetStatus().mMessage, "Invalid initializer input.");
		EXPECT_EQ(Calls, 1u);
		EXPECT_FALSE(AutoOutput(Failed, Initialize));
		EXPECT_EQ(Calls, 1u);
		ASSERT_TRUE(Failed.Cancel());
		FArdaGraphEdit Retry(Graph);
		const auto Result = AutoOutput(Retry,
		    [](auto& P)
		    {
			    P.mBytes = 256;
		    });
		ASSERT_TRUE(Result);
		EXPECT_EQ(Graph.FindResource(Result.GetBuffer("Result"))->mBuffer.mByteSize, 256u);
		ASSERT_TRUE(Retry.Commit());
		ASSERT_TRUE(Graph.CompileAndWait());
	}

	class FArdaDependencyNodeTest : public testing::Test
	{
		void SetUp() override
		{
			EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 0u);
			EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 0u);
			FArdaLifecycleNode::mPreparations = FArdaLifecycleNode::mInstances = 0;
			FArdaLifecycleNode::mbFailPrepare = FArdaLifecycleNode::mbNullPrepare = false;
			FArdaLifecycleNode::mbFailInstance = FArdaLifecycleNode::mbNullInstance = false;
			(void)FArdaNodeRegistry::Get().Unregister(FArdaLifecycleNode::GetMetadata().mName);
		}

		void TearDown() override
		{
			EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 0u);
			EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 0u);
			(void)FArdaNodeRegistry::Get().Unregister(FArdaLifecycleNode::GetMetadata().mName);
		}
	};

	TEST_F(FArdaDependencyNodeTest, RegistrationIsIdempotentAndDeviceIndependent)
	{
		auto& Registry = FArdaNodeRegistry::Get();
		ASSERT_TRUE(FArdaLifecycleNode::Register());
		const auto First = Registry.Find(FArdaLifecycleNode::GetMetadata().mName);
		ASSERT_TRUE(First);
		ASSERT_TRUE(FArdaLifecycleNode::Register());
		EXPECT_EQ(First, Registry.Find(First->mName));
		EXPECT_EQ(First->mParameterType, &ArdaDependencyParameterType<FArdaNodeValueParameters>);
		EXPECT_NE(First->mPreparedParameterType, First->mParameterType);
		EXPECT_TRUE(First->mPrepare);
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 0u);
		const auto Names = Registry.GetNames();
		EXPECT_NE(eastl::find(Names.begin(), Names.end(), First->mName), Names.end());
		ASSERT_TRUE(Registry.Unregister(First->mName));
		ASSERT_TRUE(FArdaLifecycleNode::Register());
		EXPECT_NE(First, Registry.Find(First->mName));
	}

	struct FArdaNameCollisionNode
	    : TArdaNodeDefinition<FArdaNameCollisionNode, FArdaNodeValueParameters, EArdaNodeType::Compute>
	{
		using FArdaParameters = FArdaNodeValueParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {FArdaLifecycleNode::GetMetadata().mName, 1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaLifecycleNode::GetCanonicalKey(P);
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			return FArdaDependencyNodeDesc{};
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaNameCollisionNode, CollidingLifecycle);
	ARDA_DEFINE_GRAPH_NODE(FArdaNameCollisionNode, CollidingLifecycle);

	TEST_F(FArdaDependencyNodeTest, RejectsRegistryNameCollision)
	{
		// Another class owns the same public identity, so registration must reject the collision.
		ASSERT_TRUE(FArdaNameCollisionNode::Register());
		EXPECT_EQ(FArdaLifecycleNode::Register().mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 0u);
	}

	TEST_F(FArdaDependencyNodeTest, EditAndValidationGuardPreparation)
	{

		FArdaDependencyGraph Graph;
		FArdaGraphEdit Closed(Graph);
		ASSERT_TRUE(Closed.Cancel());
		EXPECT_FALSE(Lifecycle(Closed, uint32_t{1}));
		EXPECT_EQ(Closed.GetStatus().mCode, EArdaRHIResult::InvalidState);
		EXPECT_FALSE(FArdaNodeRegistry::Get().Find(FArdaLifecycleNode::GetMetadata().mName));
		FArdaGraphEdit Edit(Graph);
		EXPECT_FALSE(Lifecycle(Edit, uint32_t{0}));
		EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 0u);
		EXPECT_EQ(FArdaLifecycleNode::mInstances, 0u);
	}

	TEST_F(FArdaDependencyNodeTest, TypedAndRegistryOperationsSharePreparationButHaveDistinctInstances)
	{

		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto First = Lifecycle(Edit, FArdaNodeValueParameters{uint32_t{7}}, "first");
		const auto Second = Lifecycle(Edit, uint32_t{7});
		const auto Named = Edit.Invoke(FArdaLifecycleNode::GetMetadata().mName, FArdaNodeValueParameters{9});
		ASSERT_TRUE(First);
		ASSERT_TRUE(Second);
		ASSERT_TRUE(Named);
		EXPECT_NE(First.GetHandle(), Second.GetHandle());
		EXPECT_EQ(Graph.GetNodeDescription(Named.GetHandle()).mEstimatedCost, 9u);
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 1u);
		EXPECT_EQ(FArdaLifecycleNode::mInstances, 0u);
		EXPECT_TRUE(Edit.GetGraph().GetCompileResult().mNodes.empty());
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.CompileAndWait());
		EXPECT_EQ(FArdaLifecycleNode::mInstances, 3u);
		FArdaDependencyGraph InvalidGraph;
		FArdaGraphEdit Duplicate(InvalidGraph);
		ASSERT_TRUE(Lifecycle(Duplicate, FArdaNodeValueParameters{uint32_t{7}}, "first"));
		EXPECT_FALSE(Lifecycle(Duplicate, FArdaNodeValueParameters{uint32_t{7}}, "first"));
		EXPECT_EQ(FArdaLifecycleNode::mInstances, 3u);
		ASSERT_TRUE(Duplicate.Cancel());
		FArdaGraphEdit WrongType(InvalidGraph);
		EXPECT_FALSE(WrongType.Invoke(FArdaLifecycleNode::GetMetadata().mName, int32_t{7}));
		EXPECT_EQ(FArdaLifecycleNode::mInstances, 3u);
	}

	TEST_F(FArdaDependencyNodeTest, SharesPreparationAcrossGraphsButIsolatesMutableInstances)
	{
		FArdaDependencyGraph First, Second;
		FArdaGraphEdit FirstEdit(First);
		FArdaGraphEdit SecondEdit(Second);
		uint32_t Input = 10;
		const auto A = Lifecycle(FirstEdit, FArdaNodeValueParameters{Input}, "a");
		Input = 200;
		const auto B = Lifecycle(SecondEdit, FArdaNodeValueParameters{20}, "b");
		ASSERT_TRUE(A);
		ASSERT_TRUE(B);
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 1u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 1u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 0u);
		FArdaDependencyExecutionContext Context;
		const auto& PA = First.GetTopology().TryGetNode(A.GetHandle())->mPayload;
		const auto& PB = Second.GetTopology().TryGetNode(B.GetHandle())->mPayload;
		auto PreparedA = PA.mDefinition->mPrepare({}, PA.mSourceParameters);
		auto PreparedB = PB.mDefinition->mPrepare({}, PB.mSourceParameters);
		ASSERT_TRUE(PreparedA && PreparedB);
		EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 2u);
		EXPECT_EQ(PA.mDefinition->mRecord(Context, PreparedA.mValue.get()).mMessage, "11");
		EXPECT_EQ(PA.mDefinition->mRecord(Context, PreparedA.mValue.get()).mMessage, "12");
		EXPECT_EQ(PB.mDefinition->mRecord(Context, PreparedB.mValue.get()).mMessage, "21");
		ASSERT_TRUE(FirstEdit.Cancel());
		PreparedA.mValue.reset();
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 1u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 1u);
		ASSERT_TRUE(SecondEdit.Cancel());
		PreparedB.mValue.reset();
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 0u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 0u);
		FArdaGraphEdit FreshEdit(Second);
		ASSERT_TRUE(Lifecycle(FreshEdit, FArdaNodeValueParameters{1}, "fresh"));
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 2u);
	}

	TEST_F(FArdaDependencyNodeTest, PreparationFailureAndNullResultsAreRetryable)
	{

		FArdaDependencyGraph Graph;
		for (uint32_t Failure = 0; Failure < 2; ++Failure)
		{
			FArdaLifecycleNode::mbFailPrepare = Failure == 0;
			FArdaLifecycleNode::mbNullPrepare = Failure == 1;
			FArdaGraphEdit Edit(Graph);
			EXPECT_FALSE(Lifecycle(Edit, uint32_t{1}));
			EXPECT_EQ(Edit.GetStatus().mCode, Failure % 2 ? EArdaRHIResult::InvalidState : EArdaRHIResult::Unsupported);
			if (Failure == 0)
			{
				EXPECT_EQ(Edit.GetStatus().mMessage, "Preparation failed.");
			}
			EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
			EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 0u);
			ASSERT_TRUE(Edit.Cancel());
		}
		FArdaLifecycleNode::mbNullPrepare = false;
		FArdaGraphEdit Edit(Graph);
		const auto Operation = Lifecycle(Edit, uint32_t{1});
		ASSERT_TRUE(Operation);
		const auto& Node = Graph.GetTopology().TryGetNode(Operation.GetHandle())->mPayload;
		EXPECT_EQ(FArdaLifecycleNode::mInstances, 0u);
		for (uint32_t Failure = 0; Failure < 2; ++Failure)
		{
			FArdaLifecycleNode::mbFailInstance = Failure == 0;
			FArdaLifecycleNode::mbNullInstance = Failure == 1;
			const auto Prepared = Node.mDefinition->mPrepare({}, Node.mSourceParameters);
			EXPECT_FALSE(Prepared);
			EXPECT_EQ(Prepared.mStatus.mCode, Failure ? EArdaRHIResult::InvalidState : EArdaRHIResult::Unsupported);
			if (!Failure)
			{
				EXPECT_EQ(Prepared.mStatus.mMessage, "Instance failed.");
			}
			EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 1u);
			EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 0u);
		}
		FArdaLifecycleNode::mbNullInstance = false;
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.CompileAndWait());
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 3u);
		EXPECT_EQ(FArdaLifecycleNode::mInstances, 3u);
	}

	TEST_F(FArdaDependencyNodeTest, OmittedNodeStateLivesUntilItsCachedRevisionIsInvalidated)
	{

		FArdaDependencyGraph Graph;
		FArdaGraphNodeHandle Original;
		{
			FArdaGraphEdit Edit(Graph);
			const auto Operation = Lifecycle(Edit, FArdaNodeValueParameters{uint32_t{1}}, "original");
			ASSERT_TRUE(Operation);
			Original = Operation.GetHandle();
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Graph.CompileAndWait());
		EXPECT_FALSE(Graph.ExecuteAndWait().mStatus);
		{
			FArdaGraphEdit Canceled(Graph);
			EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
			EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 1u);
			ASSERT_TRUE(Canceled.Cancel());
			EXPECT_EQ(Graph.FindNode("original"), Original);
		}
		{
			FArdaGraphEdit Empty(Graph);
			ASSERT_TRUE(Empty.Commit());
		}
		ASSERT_TRUE(Graph.CompileAndWait());
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 1u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 1u);
		ASSERT_TRUE(Graph.InvalidatePersistentCache());
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 0u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 0u);
	}

	struct FArdaCudaNode : TArdaNodeDefinition<FArdaCudaNode, FArdaNodeValueParameters, EArdaNodeType::Cuda>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-cuda-hook", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Value(P.mValue).Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			return {};
		}

		static FArdaRHIStatus PrepareCuda(FArdaDependencyExecutionContext&,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaCudaSequence&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, std::to_string(P.mValue).c_str());
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaCudaNode, CudaSequenceProbe);
	ARDA_DEFINE_GRAPH_NODE(FArdaCudaNode, CudaSequenceProbe);

	TEST(ArdaDependencyNodeContract, RoutesCudaThroughSequenceHook)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		const auto Node = CudaSequenceProbe(GraphEdit, FArdaNodeValueParameters{33}, "cuda");
		ASSERT_TRUE(Node);
		const auto& P = Graph.GetTopology().TryGetNode(Node.GetHandle())->mPayload;
		EXPECT_FALSE(P.mDefinition->mRecord);
		ASSERT_TRUE(P.mDefinition->mPrepareCuda);
		FArdaDependencyExecutionContext Context;
		FArdaCudaSequence Sequence({});
		const auto Prepared = P.mDefinition->mPrepare(Graph.GetDevice(), P.mSourceParameters);
		ASSERT_TRUE(Prepared);
		EXPECT_EQ(P.mDefinition->mPrepareCuda(Context, Prepared.mValue.get(), Sequence).mMessage, "33");
	}

	// Declarations alone suffice: invalid contracts must be rejected without instantiating their adapters.
	template <int Missing>
	struct TArdaContractProbe
	    : TArdaNodeDefinition<TArdaContractProbe<Missing>, FArdaNodeValueParameters, EArdaNodeType::Compute>
	{
		using FArdaParameters = FArdaNodeValueParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;
		template <int N = Missing, std::enable_if_t<N != 1, int> = 0>
		static FArdaDependencyNodeMetadata GetMetadata();
		template <int N = Missing, std::enable_if_t<N != 2, int> = 0>
		static eastl::string GetCanonicalKey(const FArdaParameters&);
		static std::conditional_t<Missing == 6, bool, FArdaRHIStatus> DeclareResources(
		    std::conditional_t<Missing == 5, uint32_t, FArdaDependencyResourceContext>&,
		    FArdaParameters&);
		template <int N = Missing, std::enable_if_t<N != 3, int> = 0>
		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&);
		template <int N = Missing, std::enable_if_t<N != 4, int> = 0>
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&);
	};

	struct FArdaCustomStateWithoutPrepare
	    : TArdaNodeDefinition<FArdaCustomStateWithoutPrepare, FArdaNodeValueParameters, EArdaNodeType::Compute>
	{
		struct FArdaState
		{
		};

		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters&);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&);
	};

	struct FArdaCustomInstanceWithoutCreate
	    : TArdaNodeDefinition<FArdaCustomInstanceWithoutCreate, FArdaNodeValueParameters, EArdaNodeType::Compute>
	{
		struct FArdaInstanceState
		{
		};

		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters&);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&);
	};

	struct FArdaInheritedWrongSelf : FArdaLifecycleNode
	{
	};

	struct FArdaNativeWithCuda
	    : TArdaNodeDefinition<FArdaNativeWithCuda, FArdaNodeValueParameters, EArdaNodeType::Compute>
	{
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters&);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&);
		static FArdaRHIStatus PrepareCuda(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaCudaSequence&);
	};

	struct FArdaCudaWithBothHooks
	    : TArdaNodeDefinition<FArdaCudaWithBothHooks, FArdaNodeValueParameters, EArdaNodeType::Cuda>
	{
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters&);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&);
		static FArdaRHIStatus PrepareCuda(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaCudaSequence&);
	};

	TEST(ArdaDependencyNodeContract, CustomStateRequiresItsClassPreparationHook)
	{
		// Missing setup is rejected by the one node contract before an adapter can be instantiated.
		EXPECT_FALSE(TArdaDependencyNodeContract<FArdaCustomStateWithoutPrepare>::value);
		EXPECT_FALSE(TArdaDependencyNodeContract<FArdaCustomInstanceWithoutCreate>::value);
	}

	// Even a complete manually described node cannot bypass the parameter schema contract.
	struct FArdaUndeclaredParameters
	{
		uint32_t mValue = 0;
	};

	struct FArdaUndeclaredParameterNode
	    : TArdaNodeDefinition<FArdaUndeclaredParameterNode, FArdaUndeclaredParameters, EArdaNodeType::Compute>
	{
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters&);
		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&);
	};

	static_assert(!TArdaDependencyNodeContract<FArdaUndeclaredParameterNode>::value);

	static_assert(TArdaDependencyNodeContract<FArdaLifecycleNode>::value);
	static_assert(TArdaDependencyNodeContract<FArdaCudaNode>::value);
	static_assert(TArdaDependencyNodeContract<TArdaContractProbe<0>>::value);
	static_assert(!TArdaDependencyNodeContract<TArdaContractProbe<1>>::value);
	static_assert(!TArdaDependencyNodeContract<TArdaContractProbe<2>>::value);
	static_assert(!TArdaDependencyNodeContract<TArdaContractProbe<3>>::value);
	static_assert(!TArdaDependencyNodeContract<TArdaContractProbe<4>>::value);
	static_assert(!TArdaDependencyNodeContract<TArdaContractProbe<5>>::value);
	static_assert(!TArdaDependencyNodeContract<TArdaContractProbe<6>>::value);
	static_assert(!TArdaDependencyNodeContract<FArdaCustomStateWithoutPrepare>::value);
	static_assert(!TArdaDependencyNodeContract<FArdaCustomInstanceWithoutCreate>::value);
	static_assert(!TArdaDependencyNodeContract<FArdaInheritedWrongSelf>::value);
	static_assert(!TArdaDependencyNodeContract<FArdaNativeWithCuda>::value);
	static_assert(!TArdaDependencyNodeContract<FArdaCudaWithBothHooks>::value);
	static_assert(!TArdaDependencyNodeContract<uint32_t>::value);
}
