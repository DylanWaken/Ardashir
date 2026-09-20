#include "Graph/Node/ArdaDependencyNode.h"
#include <gtest/gtest.h>
#include <string>

namespace arda::dependency_node_tests
{
	using namespace arda;

	struct FArdaLifecycleNode : TArdaComputeDependencyNode<FArdaLifecycleNode, uint32_t>
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
			return FArdaDependencyKeyBuilder{}.Value(P).Build();
		}

		static FArdaRHIStatus Validate(const FArdaParameters& P)
		{
			return P ? FArdaRHIStatus{} : FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Zero is invalid.");
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
			D.mEstimatedCost = P;
			D.mbSideEffect = true;
			return D;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState& I)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, std::to_string(P + ++I.mCalls).c_str());
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaLifecycleNode, Lifecycle);
	ARDA_DEFINE_GRAPH_NODE(FArdaLifecycleNode, Lifecycle);

	struct FArdaAutoOutputParameters
	{
		FArdaDependencyResourceHandle mOutput;
		uint32_t mBytes = 64;
		bool mbFail = false;
	};

	struct FArdaAutoOutputNode : TArdaComputeDependencyNode<FArdaAutoOutputNode, FArdaAutoOutputParameters>
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
			D.mAccesses = {{P.mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess}};
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
		ASSERT_TRUE(Graph.Compile());
		EXPECT_FALSE(Graph.Execute().mStatus);
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
		ASSERT_TRUE(First.Compile());
		ASSERT_TRUE(SecondEdit.Commit());
		ASSERT_TRUE(Second.Compile());
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
		ASSERT_TRUE(Graph.Compile());
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
		EXPECT_EQ(First->mParameterType, &ArdaDependencyParameterType<uint32_t>);
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
	    : TArdaDependencyNode<FArdaNameCollisionNode, uint32_t, EArdaDependencyNodeKind::Compute>
	{
		using FArdaParameters = uint32_t;
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
		static FArdaDependencyNodeDesc Describe(uint32_t, const FArdaState&)
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
		const auto First = Lifecycle(Edit, uint32_t{7}, "first");
		const auto Second = Lifecycle(Edit, uint32_t{7});
		const auto Named = Edit.Invoke(FArdaLifecycleNode::GetMetadata().mName, uint32_t{9});
		ASSERT_TRUE(First);
		ASSERT_TRUE(Second);
		ASSERT_TRUE(Named);
		EXPECT_NE(First.GetHandle(), Second.GetHandle());
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Named.GetHandle())->mPayload.mDesc.mEstimatedCost, 9u);
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 1u);
		EXPECT_EQ(FArdaLifecycleNode::mInstances, 3u);
		EXPECT_FALSE(Lifecycle(Edit, uint32_t{7}, "first"));
		EXPECT_EQ(FArdaLifecycleNode::mInstances, 3u);
		ASSERT_TRUE(Edit.Cancel());
		FArdaGraphEdit WrongType(Graph);
		EXPECT_FALSE(WrongType.Invoke(FArdaLifecycleNode::GetMetadata().mName, int32_t{7}));
		EXPECT_EQ(FArdaLifecycleNode::mInstances, 3u);
	}

	TEST_F(FArdaDependencyNodeTest, SharesPreparationAcrossGraphsButIsolatesMutableInstances)
	{
		FArdaDependencyGraph First, Second;
		FArdaGraphEdit FirstEdit(First);
		FArdaGraphEdit SecondEdit(Second);
		uint32_t Input = 10;
		const auto A = Lifecycle(FirstEdit, Input, "a");
		Input = 200;
		const auto B = Lifecycle(SecondEdit, 20, "b");
		ASSERT_TRUE(A);
		ASSERT_TRUE(B);
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 1u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 1u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 2u);
		FArdaDependencyExecutionContext Context;
		const auto& PA = First.GetTopology().TryGetNode(A.GetHandle())->mPayload;
		const auto& PB = Second.GetTopology().TryGetNode(B.GetHandle())->mPayload;
		EXPECT_EQ(PA.mDefinition->mRecord(Context, PA.mParameters.get()).mMessage, "11");
		EXPECT_EQ(PA.mDefinition->mRecord(Context, PA.mParameters.get()).mMessage, "12");
		EXPECT_EQ(PB.mDefinition->mRecord(Context, PB.mParameters.get()).mMessage, "21");
		ASSERT_TRUE(FirstEdit.Cancel());
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 1u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 1u);
		ASSERT_TRUE(SecondEdit.Cancel());
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 0u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 0u);
		FArdaGraphEdit FreshEdit(Second);
		ASSERT_TRUE(Lifecycle(FreshEdit, 1, "fresh"));
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 2u);
	}

	TEST_F(FArdaDependencyNodeTest, PreparationFailureAndNullResultsAreRetryable)
	{

		FArdaDependencyGraph Graph;
		for (uint32_t Failure = 0; Failure < 4; ++Failure)
		{
			FArdaLifecycleNode::mbFailPrepare = Failure == 0;
			FArdaLifecycleNode::mbNullPrepare = Failure == 1;
			FArdaLifecycleNode::mbFailInstance = Failure == 2;
			FArdaLifecycleNode::mbNullInstance = Failure == 3;
			FArdaGraphEdit Edit(Graph);
			EXPECT_FALSE(Lifecycle(Edit, uint32_t{1}));
			EXPECT_EQ(Edit.GetStatus().mCode, Failure % 2 ? EArdaRHIResult::InvalidState : EArdaRHIResult::Unsupported);
			if (Failure == 0)
			{
				EXPECT_EQ(Edit.GetStatus().mMessage, "Preparation failed.");
			}
			if (Failure == 2)
			{
				EXPECT_EQ(Edit.GetStatus().mMessage, "Instance failed.");
			}
			EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
			EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 0u);
			ASSERT_TRUE(Edit.Cancel());
		}
		FArdaLifecycleNode::mbNullInstance = false;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Lifecycle(Edit, uint32_t{1}));
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 5u);
		EXPECT_EQ(FArdaLifecycleNode::mInstances, 3u);
	}

	TEST_F(FArdaDependencyNodeTest, OmittedNodeStateLivesUntilItsCachedRevisionIsInvalidated)
	{

		FArdaDependencyGraph Graph;
		FArdaGraphNodeHandle Original;
		{
			FArdaGraphEdit Edit(Graph);
			const auto Operation = Lifecycle(Edit, uint32_t{1}, "original");
			ASSERT_TRUE(Operation);
			Original = Operation.GetHandle();
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Graph.Compile());
		EXPECT_FALSE(Graph.Execute().mStatus);
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
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 1u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 1u);
		ASSERT_TRUE(Graph.InvalidatePersistentCache());
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 0u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 0u);
	}

	struct FArdaCudaNode : TArdaCudaDependencyNode<FArdaCudaNode, uint32_t>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-cuda-hook", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Value(P).Build();
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
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, std::to_string(P).c_str());
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaCudaNode, CudaSequenceProbe);
	ARDA_DEFINE_GRAPH_NODE(FArdaCudaNode, CudaSequenceProbe);

	TEST(ArdaDependencyNodeContract, RoutesCudaThroughSequenceHook)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit GraphEdit(Graph);
		const auto Node = CudaSequenceProbe(GraphEdit, 33, "cuda");
		ASSERT_TRUE(Node);
		const auto& P = Graph.GetTopology().TryGetNode(Node.GetHandle())->mPayload;
		EXPECT_FALSE(P.mDefinition->mRecord);
		ASSERT_TRUE(P.mDefinition->mPrepareCuda);
		FArdaDependencyExecutionContext Context;
		FArdaCudaSequence Sequence({});
		EXPECT_EQ(P.mDefinition->mPrepareCuda(Context, P.mParameters.get(), Sequence).mMessage, "33");
	}

	// Declarations alone suffice: invalid contracts must be rejected without instantiating their adapters.
	template <int Missing>
	struct TArdaContractProbe : TArdaComputeDependencyNode<TArdaContractProbe<Missing>, uint32_t>
	{
		using FArdaParameters = uint32_t;
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

	struct FArdaCustomStateWithoutPrepare : TArdaComputeDependencyNode<FArdaCustomStateWithoutPrepare, uint32_t>
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

	struct FArdaCustomInstanceWithoutCreate : TArdaComputeDependencyNode<FArdaCustomInstanceWithoutCreate, uint32_t>
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

	struct FArdaNativeWithCuda : TArdaComputeDependencyNode<FArdaNativeWithCuda, uint32_t>
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

	struct FArdaCudaWithBothHooks : TArdaCudaDependencyNode<FArdaCudaWithBothHooks, uint32_t>
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
