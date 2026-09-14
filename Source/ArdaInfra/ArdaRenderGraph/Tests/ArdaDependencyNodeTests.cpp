#include "ArdaDependencyNode.h"
#include <gtest/gtest.h>
#include <string>

namespace
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

		// Typed attachment must dispatch through the master base, even if a derived class hides Attach.
		static TArdaRHIResult<FArdaGraphNodeHandle> Attach(FArdaDependencyGraph&,
		    eastl::string,
		    FArdaParameters) = delete;
	};

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

	TEST(ArdaNodeResources, DeclaresOutputsTransactionallyAndReusesThemOnAttach)
	{
		ASSERT_TRUE(FArdaAutoOutputNode::Register());
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		EXPECT_FALSE(G.AttachOrFind<FArdaAutoOutputNode>("failed", {{}, 64, true}));
		const auto N = G.AttachOrFind<FArdaAutoOutputNode>("output", {});
		ASSERT_TRUE(N);
		const auto R = G.FindOutput(N.mValue, "Result");
		ASSERT_TRUE(R);
		EXPECT_EQ(R.mIndex, 0u); // Failed attachment left no allocation declaration.
		EXPECT_EQ(G.FindResource(R)->mBuffer.mByteSize, 64u);
		const auto Again = G.AttachOrFind<FArdaAutoOutputNode>("output", {});
		ASSERT_TRUE(Again);
		EXPECT_EQ(Again.mValue, N.mValue);
		EXPECT_EQ(G.FindOutput(Again.mValue, "Result"), R);
		EXPECT_FALSE(G.AttachOrFind<FArdaAutoOutputNode>("output", {{}, 128}));
		EXPECT_FALSE(G.AttachOrFind<FArdaAutoOutputNode>("too small", {R, 128}));
		ASSERT_TRUE(G.EndGraphEdit());
		ASSERT_TRUE(G.BeginGraphEdit());
		const auto Temporary = G.AttachOrFind<FArdaAutoOutputNode>("temporary", {});
		ASSERT_TRUE(Temporary);
		const auto Abandoned = G.FindOutput(Temporary.mValue, "Result");
		ASSERT_TRUE(G.CancelGraphEdit());
		EXPECT_EQ(G.FindResource(Abandoned), nullptr);
		EXPECT_EQ(G.FindOutput(N.mValue, "Result"), R);
	}

	TEST(ArdaNodeResources, PrefilledAndInlineParametersStayIndependentAcrossNodesAndGraphs)
	{
		FArdaDependencyGraph First;
		FArdaDependencyGraph Second;
		ASSERT_TRUE(First.BeginGraphEdit());
		ASSERT_TRUE(Second.BeginGraphEdit());
		FArdaAutoOutputNode::FArdaParameters Parameters;
		const auto A = First.AttachOrFind<FArdaAutoOutputNode>("prefilled", Parameters);
		ASSERT_TRUE(A);
		EXPECT_FALSE(Parameters.mOutput);
		Parameters.mBytes = 128;
		const auto B = First.AttachOrFind<FArdaAutoOutputNode>("another", Parameters);
		ASSERT_TRUE(B);
		const auto C = Second.AttachOrFind<FArdaAutoOutputNode>("prefilled", Parameters);
		ASSERT_TRUE(C);
		Parameters.mBytes = 512;
		const auto D = First.AttachOrFind<FArdaAutoOutputNode>("inline",
		    [](FArdaAutoOutputNode::FArdaParameters& P)
		    {
			    EXPECT_FALSE(P.mOutput);
			    EXPECT_EQ(P.mBytes, 64u);
			    P.mBytes = 256;
		    });
		ASSERT_TRUE(D);
		const auto OutputA = First.FindOutput(A.mValue, "Result");
		const auto OutputB = First.FindOutput(B.mValue, "Result");
		const auto OutputC = Second.FindOutput(C.mValue, "Result");
		const auto OutputD = First.FindOutput(D.mValue, "Result");
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
		ASSERT_TRUE(First.EndGraphEdit());
		ASSERT_TRUE(Second.EndGraphEdit());
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
		EXPECT_EQ(Graph.AttachOrFind<FArdaAutoOutputNode>("failed", Initialize).mStatus.mCode,
		    EArdaRHIResult::InvalidState);
		EXPECT_EQ(FArdaAutoOutputNode::Attach(Graph, "failed", Initialize).mStatus.mCode, EArdaRHIResult::InvalidState);
		EXPECT_EQ(Calls, 0u);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Failed = Graph.AttachOrFind<FArdaAutoOutputNode>("failed", Initialize);
		EXPECT_EQ(Failed.mStatus.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(Failed.mStatus.mMessage, "Invalid initializer input.");
		EXPECT_EQ(Calls, 1u);
		EXPECT_FALSE(Graph.FindNode("failed"));
		const auto Retry = FArdaAutoOutputNode::Attach(Graph,
		    "failed",
		    [](FArdaAutoOutputNode::FArdaParameters&)
		    {
			    return FArdaRHIStatus{};
		    });
		ASSERT_TRUE(Retry);
		EXPECT_EQ(Graph.FindOutput(Retry.mValue, "Result").mIndex, 0u);
		const auto Again = Graph.AttachOrFind<FArdaAutoOutputNode>("failed",
		    [](auto&)
		    {
		    });
		ASSERT_TRUE(Again);
		EXPECT_EQ(Again.mValue, Retry.mValue);
		EXPECT_FALSE(Graph.AttachOrFind<FArdaAutoOutputNode>("failed",
		    [](auto& P)
		    {
			    P.mBytes = 256;
		    }));
		ASSERT_TRUE(Graph.EndGraphEdit());
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
		EXPECT_EQ(Graph.AttachOrFind<FArdaLifecycleNode>("node", 1).mStatus.mCode, EArdaRHIResult::InvalidState);
		EXPECT_FALSE(FArdaNodeRegistry::Get().Find(FArdaLifecycleNode::GetMetadata().mName));
		ASSERT_TRUE(Graph.BeginGraphEdit());
		EXPECT_EQ(Graph.AttachOrFind<FArdaLifecycleNode>("node", 0).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_FALSE(Graph.FindNode("node"));
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 0u);
		EXPECT_EQ(FArdaLifecycleNode::mInstances, 0u);
	}

	TEST_F(FArdaDependencyNodeTest, DeduplicationPrecedesPreparationForTypedAndNamedCalls)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Node = Graph.AttachOrFind<FArdaLifecycleNode>("node", 7);
		ASSERT_TRUE(Node);
		EXPECT_EQ(Graph.AttachOrFind<FArdaLifecycleNode>("node", 7).mValue, Node.mValue);
		EXPECT_EQ(Graph.AttachOrFind("node", FArdaLifecycleNode::GetMetadata().mName, uint32_t(7)).mValue, Node.mValue);
		EXPECT_FALSE(Graph.AttachOrFind<FArdaLifecycleNode>("node", 8));
		EXPECT_FALSE(Graph.AttachOrFind("wrong-type", FArdaLifecycleNode::GetMetadata().mName, int32_t(7)));
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 1u);
		EXPECT_EQ(FArdaLifecycleNode::mInstances, 1u);
		const auto Named = Graph.AttachOrFind("named", FArdaLifecycleNode::GetMetadata().mName, uint32_t(9));
		ASSERT_TRUE(Named);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Named.mValue)->mPayload.mDesc.mEstimatedCost, 9u);
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 1u);
		EXPECT_EQ(FArdaLifecycleNode::mInstances, 2u);
	}

	TEST_F(FArdaDependencyNodeTest, SharesPreparationAcrossGraphsButIsolatesMutableInstances)
	{
		FArdaDependencyGraph First, Second;
		ASSERT_TRUE(First.BeginGraphEdit());
		ASSERT_TRUE(Second.BeginGraphEdit());
		uint32_t Input = 10;
		const auto A = First.AttachOrFind<FArdaLifecycleNode>("a", Input);
		Input = 200;
		const auto B = Second.AttachOrFind<FArdaLifecycleNode>("b", 20);
		ASSERT_TRUE(A);
		ASSERT_TRUE(B);
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 1u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 1u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 2u);
		FArdaDependencyExecutionContext Context;
		const auto& PA = First.GetTopology().TryGetNode(A.mValue)->mPayload;
		const auto& PB = Second.GetTopology().TryGetNode(B.mValue)->mPayload;
		EXPECT_EQ(PA.mDefinition->mRecord(Context, PA.mParameters.get()).mMessage, "11");
		EXPECT_EQ(PA.mDefinition->mRecord(Context, PA.mParameters.get()).mMessage, "12");
		EXPECT_EQ(PB.mDefinition->mRecord(Context, PB.mParameters.get()).mMessage, "21");
		ASSERT_TRUE(First.CancelGraphEdit());
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 1u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 1u);
		ASSERT_TRUE(Second.CancelGraphEdit());
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 0u);
		EXPECT_EQ(FArdaLifecycleNode::mLiveInstances, 0u);
		ASSERT_TRUE(Second.BeginGraphEdit());
		ASSERT_TRUE(Second.AttachOrFind<FArdaLifecycleNode>("fresh", 1));
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 2u);
	}

	TEST_F(FArdaDependencyNodeTest, PreparationFailureAndNullResultsAreRetryable)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaLifecycleNode::mbFailPrepare = true;
		auto Node = Graph.AttachOrFind<FArdaLifecycleNode>("node", 1);
		EXPECT_EQ(Node.mStatus.mCode, EArdaRHIResult::Unsupported);
		EXPECT_EQ(Node.mStatus.mMessage, "Preparation failed.");
		FArdaLifecycleNode::mbFailPrepare = false;
		FArdaLifecycleNode::mbNullPrepare = true;
		EXPECT_EQ(Graph.AttachOrFind<FArdaLifecycleNode>("node", 1).mStatus.mCode, EArdaRHIResult::InvalidState);
		EXPECT_FALSE(Graph.FindNode("node"));
		FArdaLifecycleNode::mbNullPrepare = false;
		FArdaLifecycleNode::mbFailInstance = true;
		EXPECT_EQ(Graph.AttachOrFind<FArdaLifecycleNode>("node", 1).mStatus.mMessage, "Instance failed.");
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 0u);
		FArdaLifecycleNode::mbFailInstance = false;
		FArdaLifecycleNode::mbNullInstance = true;
		EXPECT_EQ(Graph.AttachOrFind<FArdaLifecycleNode>("node", 1).mStatus.mCode, EArdaRHIResult::InvalidState);
		EXPECT_FALSE(Graph.FindNode("node"));
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 0u);
		FArdaLifecycleNode::mbNullInstance = false;
		ASSERT_TRUE(Graph.AttachOrFind<FArdaLifecycleNode>("node", 1));
		EXPECT_EQ(FArdaLifecycleNode::mPreparations, 5u);
		EXPECT_EQ(FArdaLifecycleNode::mInstances, 3u);
	}

	TEST_F(FArdaDependencyNodeTest, RemovalRetainsStateUntilTransactionCommits)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Node = Graph.AttachOrFind<FArdaLifecycleNode>("node", 1);
		ASSERT_TRUE(Node);
		ASSERT_TRUE(Graph.EndGraphEdit());
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.RemoveNode(Node.mValue));
		EXPECT_EQ(FArdaLifecycleNode::mLiveStates, 1u);
		ASSERT_TRUE(Graph.CancelGraphEdit());
		EXPECT_EQ(Graph.FindNode("node"), Node.mValue);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.RemoveNode(Node.mValue));
		ASSERT_TRUE(Graph.EndGraphEdit());
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

	TEST(ArdaDependencyNodeContract, RoutesCudaThroughSequenceHook)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Node = Graph.AttachOrFind<FArdaCudaNode>("cuda", 33);
		ASSERT_TRUE(Node);
		const auto& P = Graph.GetTopology().TryGetNode(Node.mValue)->mPayload;
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
