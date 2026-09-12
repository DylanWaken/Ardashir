#include "ArdaDependencyNode.h"
#include <gtest/gtest.h>
#include <string>

namespace
{
	using namespace arda;

	struct FLifecycleNode : TArdaComputeDependencyNode<FLifecycleNode, uint32_t>
	{
		inline static uint32_t mPreparations = 0, mInstances = 0, mLiveStates = 0, mLiveInstances = 0;
		inline static bool mbFailPrepare = false, mbNullPrepare = false, mbFailInstance = false, mbNullInstance = false;

		struct FState
		{
			FState()
			{
				++mLiveStates;
			}

			~FState()
			{
				--mLiveStates;
			}
		};

		struct FInstanceState
		{
			uint32_t mCalls = 0;

			FInstanceState()
			{
				++mLiveInstances;
			}

			~FInstanceState()
			{
				--mLiveInstances;
			}
		};

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-lifecycle", 1};
		}

		static eastl::string GetCanonicalKey(const FParameters& P)
		{
			return eastl::string(std::to_string(P).c_str());
		}

		static FArdaRHIStatus Validate(const FParameters& P)
		{
			return P ? FArdaRHIStatus{} : FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Zero is invalid.");
		}

		static TArdaRHIResult<eastl::shared_ptr<const FState>> Prepare(FArdaRHIDeviceRef)
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
			return {eastl::make_shared<const FState>(), {}};
		}

		static TArdaRHIResult<eastl::shared_ptr<FInstanceState>> CreateInstance(FArdaRHIDeviceRef,
		    const FParameters&,
		    const FState&)
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
			return {eastl::make_shared<FInstanceState>(), {}};
		}

		static FArdaDependencyNodeDesc Describe(const FParameters& P, const FState&)
		{
			FArdaDependencyNodeDesc D;
			D.mEstimatedCost = P;
			D.mbSideEffect = true;
			return D;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FParameters& P,
		    const FState&,
		    FInstanceState& I)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, std::to_string(P + ++I.mCalls).c_str());
		}

		// Typed attachment must dispatch through the master base, even if a derived class hides Attach.
		static TArdaRHIResult<FArdaGraphNodeHandle> Attach(FArdaDependencyGraph&, eastl::string, FParameters) = delete;
	};

	class ArdaDependencyNode : public testing::Test
	{
		void SetUp() override
		{
			EXPECT_EQ(FLifecycleNode::mLiveStates, 0u);
			EXPECT_EQ(FLifecycleNode::mLiveInstances, 0u);
			FLifecycleNode::mPreparations = FLifecycleNode::mInstances = 0;
			FLifecycleNode::mbFailPrepare = FLifecycleNode::mbNullPrepare = false;
			FLifecycleNode::mbFailInstance = FLifecycleNode::mbNullInstance = false;
			(void)FArdaNodeRegistry::Get().Unregister(FLifecycleNode::GetMetadata().mName);
		}

		void TearDown() override
		{
			EXPECT_EQ(FLifecycleNode::mLiveStates, 0u);
			EXPECT_EQ(FLifecycleNode::mLiveInstances, 0u);
			(void)FArdaNodeRegistry::Get().Unregister(FLifecycleNode::GetMetadata().mName);
		}
	};

	TEST_F(ArdaDependencyNode, RegistrationIsIdempotentAndDeviceIndependent)
	{
		auto& Registry = FArdaNodeRegistry::Get();
		ASSERT_TRUE(FLifecycleNode::Register());
		const auto First = Registry.Find(FLifecycleNode::GetMetadata().mName);
		ASSERT_TRUE(First);
		ASSERT_TRUE(FLifecycleNode::Register());
		EXPECT_EQ(First, Registry.Find(First->mName));
		EXPECT_EQ(First->mParameterType, &ArdaDependencyParameterType<uint32_t>);
		EXPECT_NE(First->mPreparedParameterType, First->mParameterType);
		EXPECT_TRUE(First->mPrepare);
		EXPECT_EQ(FLifecycleNode::mPreparations, 0u);
		const auto Names = Registry.GetNames();
		EXPECT_NE(eastl::find(Names.begin(), Names.end(), First->mName), Names.end());
		ASSERT_TRUE(Registry.Unregister(First->mName));
		ASSERT_TRUE(FLifecycleNode::Register());
		EXPECT_NE(First, Registry.Find(First->mName));
	}

	TEST_F(ArdaDependencyNode, RejectsRegistryNameCollision)
	{
		TArdaDependencyNodeDefinition<uint32_t> D;
		D.mName = FLifecycleNode::GetMetadata().mName;
		D.mCanonicalKey = FLifecycleNode::GetCanonicalKey;
		D.mDescribe = [](uint32_t)
		{
			return FArdaDependencyNodeDesc{};
		};
		ASSERT_TRUE(FArdaNodeRegistry::Get().Register(eastl::move(D)));
		EXPECT_EQ(FLifecycleNode::Register().mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(FLifecycleNode::mPreparations, 0u);
	}

	TEST_F(ArdaDependencyNode, EditAndValidationGuardPreparation)
	{
		FArdaDependencyGraph Graph;
		EXPECT_EQ(Graph.AttachOrFind<FLifecycleNode>("node", 1).mStatus.mCode, EArdaRHIResult::InvalidState);
		EXPECT_FALSE(FArdaNodeRegistry::Get().Find(FLifecycleNode::GetMetadata().mName));
		ASSERT_TRUE(Graph.BeginGraphEdit());
		EXPECT_EQ(Graph.AttachOrFind<FLifecycleNode>("node", 0).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_FALSE(Graph.FindNode("node"));
		EXPECT_EQ(FLifecycleNode::mPreparations, 0u);
		EXPECT_EQ(FLifecycleNode::mInstances, 0u);
	}

	TEST_F(ArdaDependencyNode, DeduplicationPrecedesPreparationForTypedAndNamedCalls)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Node = Graph.AttachOrFind<FLifecycleNode>("node", 7);
		ASSERT_TRUE(Node);
		EXPECT_EQ(Graph.AttachOrFind<FLifecycleNode>("node", 7).mValue, Node.mValue);
		EXPECT_EQ(Graph.AttachOrFind("node", FLifecycleNode::GetMetadata().mName, uint32_t(7)).mValue, Node.mValue);
		EXPECT_FALSE(Graph.AttachOrFind<FLifecycleNode>("node", 8));
		EXPECT_FALSE(Graph.AttachOrFind("wrong-type", FLifecycleNode::GetMetadata().mName, int32_t(7)));
		EXPECT_EQ(FLifecycleNode::mPreparations, 1u);
		EXPECT_EQ(FLifecycleNode::mInstances, 1u);
		const auto Named = Graph.AttachOrFind("named", FLifecycleNode::GetMetadata().mName, uint32_t(9));
		ASSERT_TRUE(Named);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Named.mValue)->mPayload.mDesc.mEstimatedCost, 9u);
		EXPECT_EQ(FLifecycleNode::mPreparations, 1u);
		EXPECT_EQ(FLifecycleNode::mInstances, 2u);
	}

	TEST_F(ArdaDependencyNode, SharesPreparationAcrossGraphsButIsolatesMutableInstances)
	{
		FArdaDependencyGraph First, Second;
		ASSERT_TRUE(First.BeginGraphEdit());
		ASSERT_TRUE(Second.BeginGraphEdit());
		uint32_t Input = 10;
		const auto A = First.AttachOrFind<FLifecycleNode>("a", Input);
		Input = 200;
		const auto B = Second.AttachOrFind<FLifecycleNode>("b", 20);
		ASSERT_TRUE(A);
		ASSERT_TRUE(B);
		EXPECT_EQ(FLifecycleNode::mPreparations, 1u);
		EXPECT_EQ(FLifecycleNode::mLiveStates, 1u);
		EXPECT_EQ(FLifecycleNode::mLiveInstances, 2u);
		FArdaDependencyExecutionContext Context;
		const auto& PA = First.GetTopology().TryGetNode(A.mValue)->mPayload;
		const auto& PB = Second.GetTopology().TryGetNode(B.mValue)->mPayload;
		EXPECT_EQ(PA.mDefinition->mRecord(Context, PA.mParameters.get()).mMessage, "11");
		EXPECT_EQ(PA.mDefinition->mRecord(Context, PA.mParameters.get()).mMessage, "12");
		EXPECT_EQ(PB.mDefinition->mRecord(Context, PB.mParameters.get()).mMessage, "21");
		ASSERT_TRUE(First.CancelGraphEdit());
		EXPECT_EQ(FLifecycleNode::mLiveStates, 1u);
		EXPECT_EQ(FLifecycleNode::mLiveInstances, 1u);
		ASSERT_TRUE(Second.CancelGraphEdit());
		EXPECT_EQ(FLifecycleNode::mLiveStates, 0u);
		EXPECT_EQ(FLifecycleNode::mLiveInstances, 0u);
		ASSERT_TRUE(Second.BeginGraphEdit());
		ASSERT_TRUE(Second.AttachOrFind<FLifecycleNode>("fresh", 1));
		EXPECT_EQ(FLifecycleNode::mPreparations, 2u);
	}

	TEST_F(ArdaDependencyNode, PreparationFailureAndNullResultsAreRetryable)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FLifecycleNode::mbFailPrepare = true;
		auto Node = Graph.AttachOrFind<FLifecycleNode>("node", 1);
		EXPECT_EQ(Node.mStatus.mCode, EArdaRHIResult::Unsupported);
		EXPECT_EQ(Node.mStatus.mMessage, "Preparation failed.");
		FLifecycleNode::mbFailPrepare = false;
		FLifecycleNode::mbNullPrepare = true;
		EXPECT_EQ(Graph.AttachOrFind<FLifecycleNode>("node", 1).mStatus.mCode, EArdaRHIResult::InvalidState);
		EXPECT_FALSE(Graph.FindNode("node"));
		FLifecycleNode::mbNullPrepare = false;
		FLifecycleNode::mbFailInstance = true;
		EXPECT_EQ(Graph.AttachOrFind<FLifecycleNode>("node", 1).mStatus.mMessage, "Instance failed.");
		EXPECT_EQ(FLifecycleNode::mLiveStates, 0u);
		FLifecycleNode::mbFailInstance = false;
		FLifecycleNode::mbNullInstance = true;
		EXPECT_EQ(Graph.AttachOrFind<FLifecycleNode>("node", 1).mStatus.mCode, EArdaRHIResult::InvalidState);
		EXPECT_FALSE(Graph.FindNode("node"));
		EXPECT_EQ(FLifecycleNode::mLiveStates, 0u);
		FLifecycleNode::mbNullInstance = false;
		ASSERT_TRUE(Graph.AttachOrFind<FLifecycleNode>("node", 1));
		EXPECT_EQ(FLifecycleNode::mPreparations, 5u);
		EXPECT_EQ(FLifecycleNode::mInstances, 3u);
	}

	TEST_F(ArdaDependencyNode, RemovalRetainsStateUntilTransactionCommits)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Node = Graph.AttachOrFind<FLifecycleNode>("node", 1);
		ASSERT_TRUE(Node);
		ASSERT_TRUE(Graph.EndGraphEdit());
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.RemoveNode(Node.mValue));
		EXPECT_EQ(FLifecycleNode::mLiveStates, 1u);
		ASSERT_TRUE(Graph.CancelGraphEdit());
		EXPECT_EQ(Graph.FindNode("node"), Node.mValue);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.RemoveNode(Node.mValue));
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_EQ(FLifecycleNode::mLiveStates, 0u);
		EXPECT_EQ(FLifecycleNode::mLiveInstances, 0u);
	}

	struct FCudaNode : TArdaCudaDependencyNode<FCudaNode, uint32_t>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-cuda-hook", 1};
		}

		static eastl::string GetCanonicalKey(const FParameters& P)
		{
			return eastl::string(std::to_string(P).c_str());
		}

		static FArdaDependencyNodeDesc Describe(const FParameters&, const FState&)
		{
			return {};
		}

		static FArdaRHIStatus PrepareCuda(FArdaDependencyExecutionContext&,
		    const FParameters& P,
		    const FState&,
		    FInstanceState&,
		    FArdaCudaSequence&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, std::to_string(P).c_str());
		}
	};

	TEST(ArdaDependencyNodeContract, RoutesCudaThroughSequenceHook)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Node = Graph.AttachOrFind<FCudaNode>("cuda", 33);
		ASSERT_TRUE(Node);
		const auto& P = Graph.GetTopology().TryGetNode(Node.mValue)->mPayload;
		EXPECT_FALSE(P.mDefinition->mRecord);
		ASSERT_TRUE(P.mDefinition->mPrepareCuda);
		FArdaDependencyExecutionContext Context;
		FArdaCudaSequence Sequence({});
		EXPECT_EQ(P.mDefinition->mPrepareCuda(Context, P.mParameters.get(), Sequence).mMessage, "33");
	}

	TEST(ArdaDependencyNodeContract, DifferentPreparedSchemaRequiresAdapter)
	{
		TArdaDependencyNodeDefinition<int, eastl::string> D;
		D.mName = "test.invalid-prepared-schema";
		D.mCanonicalKey = [](int P)
		{
			return eastl::string(std::to_string(P).c_str());
		};
		D.mDescribe = [](const eastl::string&)
		{
			return FArdaDependencyNodeDesc{};
		};
		EXPECT_EQ(FArdaNodeRegistry::Get().Register(eastl::move(D)).mCode, EArdaRHIResult::InvalidArgument);
	}

	// Declarations alone suffice: invalid contracts must be rejected without instantiating their adapters.
	template <int Missing>
	struct TContractProbe : TArdaComputeDependencyNode<TContractProbe<Missing>, uint32_t>
	{
		using FParameters = uint32_t;
		using FState = FArdaEmptyDependencyNodeState;
		using FInstanceState = FArdaEmptyDependencyNodeState;
		template <int N = Missing, std::enable_if_t<N != 1, int> = 0>
		static FArdaDependencyNodeMetadata GetMetadata();
		template <int N = Missing, std::enable_if_t<N != 2, int> = 0>
		static eastl::string GetCanonicalKey(const FParameters&);
		template <int N = Missing, std::enable_if_t<N != 3, int> = 0>
		static FArdaDependencyNodeDesc Describe(const FParameters&, const FState&);
		template <int N = Missing, std::enable_if_t<N != 4, int> = 0>
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FParameters&,
		    const FState&,
		    FInstanceState&);
	};

	struct FCustomStateWithoutPrepare : TArdaComputeDependencyNode<FCustomStateWithoutPrepare, uint32_t>
	{
		struct FState
		{
		};

		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FParameters&);
		static FArdaDependencyNodeDesc Describe(const FParameters&, const FState&);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FParameters&,
		    const FState&,
		    FInstanceState&);
	};

	struct FCustomInstanceWithoutCreate : TArdaComputeDependencyNode<FCustomInstanceWithoutCreate, uint32_t>
	{
		struct FInstanceState
		{
		};

		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FParameters&);
		static FArdaDependencyNodeDesc Describe(const FParameters&, const FState&);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FParameters&,
		    const FState&,
		    FInstanceState&);
	};

	struct FInheritedWrongSelf : FLifecycleNode
	{
	};

	struct FNativeWithCuda : TArdaComputeDependencyNode<FNativeWithCuda, uint32_t>
	{
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FParameters&);
		static FArdaDependencyNodeDesc Describe(const FParameters&, const FState&);
		static FArdaRHIStatus PrepareCuda(FArdaDependencyExecutionContext&,
		    const FParameters&,
		    const FState&,
		    FInstanceState&,
		    FArdaCudaSequence&);
	};

	struct FCudaWithBothHooks : TArdaCudaDependencyNode<FCudaWithBothHooks, uint32_t>
	{
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FParameters&);
		static FArdaDependencyNodeDesc Describe(const FParameters&, const FState&);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FParameters&,
		    const FState&,
		    FInstanceState&);
		static FArdaRHIStatus PrepareCuda(FArdaDependencyExecutionContext&,
		    const FParameters&,
		    const FState&,
		    FInstanceState&,
		    FArdaCudaSequence&);
	};

	static_assert(TArdaDependencyNodeContract<FLifecycleNode>::value);
	static_assert(TArdaDependencyNodeContract<FCudaNode>::value);
	static_assert(TArdaDependencyNodeContract<TContractProbe<0>>::value);
	static_assert(!TArdaDependencyNodeContract<TContractProbe<1>>::value);
	static_assert(!TArdaDependencyNodeContract<TContractProbe<2>>::value);
	static_assert(!TArdaDependencyNodeContract<TContractProbe<3>>::value);
	static_assert(!TArdaDependencyNodeContract<TContractProbe<4>>::value);
	static_assert(!TArdaDependencyNodeContract<FCustomStateWithoutPrepare>::value);
	static_assert(!TArdaDependencyNodeContract<FCustomInstanceWithoutCreate>::value);
	static_assert(!TArdaDependencyNodeContract<FInheritedWrongSelf>::value);
	static_assert(!TArdaDependencyNodeContract<FNativeWithCuda>::value);
	static_assert(!TArdaDependencyNodeContract<FCudaWithBothHooks>::value);
	static_assert(!TArdaDependencyNodeContract<uint32_t>::value);
}
