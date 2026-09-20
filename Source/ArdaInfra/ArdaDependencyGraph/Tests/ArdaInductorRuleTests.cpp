#include "ArdaDependencyGraph.h"
#include <gtest/gtest.h>
#include <EASTL/algorithm.h>
#include <EASTL/type_traits.h>

namespace arda::rule_tests
{
	using namespace arda;

	struct FArdaRuleParameters
	{
		bool mbSideEffect = true;
		uint64_t mWorkspaceBytes = 0;
	};

	struct FArdaRuleNode : TArdaDependencyNode<FArdaRuleNode, FArdaRuleParameters, EArdaDependencyNodeKind::Copy>
	{
		using FArdaParameters = FArdaRuleParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"inductor.rules.test", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaRuleParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Value(P.mbSideEffect).Value(P.mWorkspaceBytes).Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaRuleParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mbSideEffect = P.mbSideEffect;
			Desc.mTransientWorkspaceBytes = P.mWorkspaceBytes;
			return Desc;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaRuleNode, RuleOperation);
	ARDA_DEFINE_GRAPH_NODE(FArdaRuleNode, RuleOperation);

	template <class Existing, class Rule>
	void Replace(FArdaInductorOptions& Options, eastl::shared_ptr<const Rule> Value)
	{
		auto I = eastl::find_if(Options.mRules.mCompileRules.begin(),
		    Options.mRules.mCompileRules.end(),
		    [](const auto& R)
		    {
			    return dynamic_cast<const Existing*>(R.get()) != nullptr;
		    });
		ASSERT_NE(I, Options.mRules.mCompileRules.end());
		*I = eastl::move(Value);
	}
	enum class EArdaPolicyResult
	{
		Forward,
		Reverse,
		Duplicate,
		Foreign,
		Missing,
		Failure,
		Throw
	};

	struct FArdaOrderPolicy
	{
		EArdaPolicyResult mResult = EArdaPolicyResult::Forward;

		TArdaRHIResult<eastl::vector<uint32_t>> Order(const FArdaInductorGraph&,
		    const eastl::vector<FArdaInductorOrderingNode>& Nodes) const
		{
			if (mResult == EArdaPolicyResult::Throw)
			{
				throw 7;
			}
			if (mResult == EArdaPolicyResult::Failure)
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Configured rejection")};
			}
			eastl::vector<uint32_t> Ids;
			for (const auto& N : Nodes)
			{
				Ids.push_back(N.mNodeId);
			}
			if (mResult == EArdaPolicyResult::Reverse)
			{
				eastl::reverse(Ids.begin(), Ids.end());
			}
			if (mResult == EArdaPolicyResult::Duplicate && Ids.size() > 1)
			{
				Ids.back() = Ids.front();
			}
			if (mResult == EArdaPolicyResult::Foreign && !Ids.empty())
			{
				Ids.front() = UINT32_MAX;
			}
			if (mResult == EArdaPolicyResult::Missing && !Ids.empty())
			{
				Ids.pop_back();
			}
			return {eastl::move(Ids), {}};
		}
	};

	FArdaInductorOptions CustomOptions(EArdaPolicyResult Result)
	{
		FArdaInductorOptions O;
		Replace<FArdaSearchOrderRule>(O,
		    MakeArdaGraphRule<TArdaInductorOrderingRule<FArdaOrderPolicy>>(FArdaOrderPolicy{Result}));
		return O;
	}

	struct FArdaRuleLog
	{
		eastl::vector<int> mValues;
		const FArdaInductorCompileResult* mResult = nullptr;
	};

	class FArdaObserveRule final : public FArdaGraphCompileRule
	{
	public:
		FArdaObserveRule(FArdaRuleLog& Log, int Value)
		    : mLog(Log),
		      mValue(Value)
		{
		}

		const char* GetName() const noexcept override
		{
			return "Observe";
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph&, FArdaInductorCompileResult& R) const override
		{
			if (!mLog.mResult)
			{
				mLog.mResult = &R;
			}
			EXPECT_EQ(mLog.mResult, &R);
			mLog.mValues.push_back(mValue);
			return {};
		}

		FArdaRuleLog& mLog;
		int mValue;
	};

	class FArdaOrderPreprocessRule final : public FArdaGraphPreprocessRule
	{
	public:
		FArdaOrderPreprocessRule(FArdaGraphOperation A, FArdaGraphOperation B, bool Fail = false)
		    : mA(A.GetHandle()),
		      mB(B.GetHandle()),
		      mbFail(Fail)
		{
		}

		const char* GetName() const noexcept override
		{
			return "PreprocessOrder";
		}

		FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const override
		{
			(void)Graph.GetTopology().AddEdge(mA, mB, {});
			return mbFail ? FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "After mutation") : FArdaRHIStatus{};
		}

		FArdaGraphNodeHandle mA, mB;
		bool mbFail;
	};

	class FArdaFailureRule final : public FArdaGraphCompileRule
	{
	public:
		explicit FArdaFailureRule(bool Throw)
		    : mbThrow(Throw)
		{
		}

		const char* GetName() const noexcept override
		{
			return "Reject";
		}

		FArdaRHIStatus Apply(const FArdaInductorGraph&, FArdaInductorCompileResult&) const override
		{
			if (mbThrow)
			{
				throw 1;
			}
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Rejected");
		}

		bool mbThrow;
	};

	TEST(ArdaInductorRules, ConfiguredInstancesRunInListOrderAndMayRepeat)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		RuleOperation(Edit, {}, "node");
		FArdaRuleLog Log;
		FArdaInductorOptions O;
		const auto One = MakeArdaGraphRule<FArdaObserveRule>(Log, 1);
		O.mRules.mCompileRules.insert(O.mRules.mCompileRules.begin(), One);
		O.mRules.mCompileRules.insert(O.mRules.mCompileRules.begin() + 3, MakeArdaGraphRule<FArdaObserveRule>(Log, 2));
		O.mRules.mCompileRules.push_back(One);
		ASSERT_TRUE(Edit.SetOptions(O));
		ASSERT_TRUE(Edit.Commit());
		const auto S = Graph.Compile();
		ASSERT_TRUE(S) << S.mMessage.c_str();
		EXPECT_EQ(Log.mValues, (eastl::vector<int>{1, 2, 1}));
	}

	TEST(ArdaInductorRules, PreprocessingChangesTheGraphBeforeFlattening)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto A = RuleOperation(Edit, {}, "a");
		const auto B = RuleOperation(Edit, {}, "b");
		FArdaInductorOptions O;
		// Also exercise a preprocess mutation before the default dependency resolver.
		O.mRules.mPreprocessRules.insert(O.mRules.mPreprocessRules.begin(),
		    MakeArdaGraphRule<FArdaOrderPreprocessRule>(B, A));
		ASSERT_TRUE(Edit.SetOptions(O));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_TRUE(Graph.GetTopology().IsReachable(B.GetHandle(), A.GetHandle()));
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{B.GetHandle(), A.GetHandle()}));
	}

	TEST(ArdaInductorRules, FailedPreprocessingDiscardsItsMutationsAndPreservesQueuedGraph)
	{

		FArdaDependencyGraph Graph;
		FArdaGraphEdit Initial(Graph);
		const auto A = RuleOperation(Initial, {}, "a");
		const auto B = RuleOperation(Initial, {}, "b");
		ASSERT_TRUE(Initial.Commit());
		ASSERT_TRUE(Graph.Compile());
		const auto Before = Graph.GetCompileResult();
		(void)Graph.Execute();
		FArdaGraphEdit Next(Graph);
		const auto NextA = RuleOperation(Next, {}, "a");
		const auto NextB = RuleOperation(Next, {}, "b");
		FArdaInductorOptions O;
		O.mRules.mPreprocessRules.push_back(MakeArdaGraphRule<FArdaOrderPreprocessRule>(NextB, NextA, true));
		ASSERT_TRUE(Next.SetOptions(O));
		ASSERT_TRUE(Next.Commit());
		EXPECT_FALSE(Graph.Compile());
		EXPECT_FALSE(Graph.GetTopology().IsReachable(NextB.GetHandle(), NextA.GetHandle()));
		EXPECT_EQ(Graph.GetCompileResult().mRevision, Before.mRevision);
		ASSERT_TRUE(Graph.DiscardPendingEdits());
		EXPECT_FALSE(Graph.GetTopology().IsReachable(B.GetHandle(), A.GetHandle()));
	}

	TEST(ArdaInductorRules, DefaultProgramPreservesDependenciesAndCulling)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto C = RuleOperation(Edit, {}, "a consumer");
		const auto P = RuleOperation(Edit, {false}, "z producer");
		const auto D = RuleOperation(Edit, {false}, "dead");
		ASSERT_TRUE(Edit.Order(P, C));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{P.GetHandle(), C.GetHandle()}));
		EXPECT_EQ(Graph.GetCompileResult().mCulledNodes, (eastl::vector<FArdaGraphNodeHandle>{D.GetHandle()}));
	}

	TEST(ArdaInductorRules, OptionalRulesMayBeOmittedWithoutFixedSlots)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto A = RuleOperation(Edit, {false}, "a");
		FArdaInductorOptions O;
		auto& Rules = O.mRules.mCompileRules;
		Rules.erase(eastl::remove_if(Rules.begin(),
		                Rules.end(),
		                [](const auto& R)
		                {
			                return dynamic_cast<const FArdaCullUnreachableRule*>(R.get()) ||
			                    dynamic_cast<const FArdaSearchOrderRule*>(R.get());
		                }),
		    Rules.end());
		ASSERT_TRUE(Edit.SetOptions(O));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder, (eastl::vector<FArdaGraphNodeHandle>{A.GetHandle()}));
		EXPECT_TRUE(Graph.GetCompileResult().mCulledNodes.empty());
	}

	TEST(ArdaInductorRules, InvalidCustomOrdersAndExceptionsDoNotPublish)
	{

		FArdaDependencyGraph Graph;
		FArdaGraphEdit Initial(Graph);
		const auto A = RuleOperation(Initial, {}, "a"), B = RuleOperation(Initial, {}, "b");
		ASSERT_TRUE(Initial.Order(A, B));
		ASSERT_TRUE(Initial.Commit());
		ASSERT_TRUE(Graph.Compile());
		const auto Before = Graph.GetCompileResult();
		(void)Graph.Execute();
		for (auto Result : {EArdaPolicyResult::Reverse,
		         EArdaPolicyResult::Duplicate,
		         EArdaPolicyResult::Foreign,
		         EArdaPolicyResult::Missing,
		         EArdaPolicyResult::Failure,
		         EArdaPolicyResult::Throw})
		{
			FArdaGraphEdit Edit(Graph);
			const auto NextA = RuleOperation(Edit, {}, "a"), NextB = RuleOperation(Edit, {}, "b");
			ASSERT_TRUE(Edit.Order(NextA, NextB));
			ASSERT_TRUE(Edit.SetOptions(CustomOptions(Result)));
			ASSERT_TRUE(Edit.Commit());
			const auto S = Graph.Compile();
			EXPECT_FALSE(S);
			EXPECT_FALSE(S.mMessage.empty());
			EXPECT_EQ(Graph.GetCompileResult().mRevision, Before.mRevision);
			EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder, Before.mExecutionOrder);
			ASSERT_TRUE(Graph.DiscardPendingEdits());
		}
	}

	TEST(ArdaInductorRules, FailureAtAnyListPositionPreservesCommittedCompilation)
	{

		FArdaDependencyGraph Graph;
		FArdaGraphEdit Initial(Graph);
		ASSERT_TRUE(RuleOperation(Initial, {}, "node"));
		ASSERT_TRUE(Initial.Commit());
		ASSERT_TRUE(Graph.Compile());
		const auto Before = Graph.GetCompileResult();
		(void)Graph.Execute();
		for (size_t I = 0; I <= FArdaInductorOptions{}.mRules.mCompileRules.size(); ++I)
		{
			for (bool Throw : {false, true})
			{
				FArdaGraphEdit Edit(Graph);
				ASSERT_TRUE(RuleOperation(Edit, {}, "node"));
				FArdaInductorOptions O;
				O.mRules.mCompileRules.insert(O.mRules.mCompileRules.begin() + I,
				    MakeArdaGraphRule<FArdaFailureRule>(Throw));
				ASSERT_TRUE(Edit.SetOptions(O));
				ASSERT_TRUE(Edit.Commit());
				EXPECT_FALSE(Graph.Compile());
				EXPECT_EQ(Graph.GetCompileResult().mRevision, Before.mRevision);
				ASSERT_TRUE(Graph.DiscardPendingEdits());
			}
		}
	}

	TEST(ArdaInductorRules, RuleInstancesKeepIndependentConfiguration)
	{
		for (auto Policy : {EArdaPolicyResult::Forward, EArdaPolicyResult::Reverse})
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			const auto A = RuleOperation(Edit, {}, "a"), B = RuleOperation(Edit, {}, "b");
			ASSERT_TRUE(Edit.SetOptions(CustomOptions(Policy)));
			ASSERT_TRUE(Edit.Commit());
			ASSERT_TRUE(Graph.Compile());
			EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
			    Policy == EArdaPolicyResult::Forward
			        ? (eastl::vector<FArdaGraphNodeHandle>{A.GetHandle(), B.GetHandle()})
			        : (eastl::vector<FArdaGraphNodeHandle>{B.GetHandle(), A.GetHandle()}));
		}
	}

	TEST(ArdaInductorRules, StableOrderingAndConfiguredQueuePlacement)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Z = RuleOperation(Edit, {}, "z"), A = RuleOperation(Edit, {}, "a"),
		           M = RuleOperation(Edit, {false}, "m");
		ASSERT_TRUE(Edit.Order(Z, A));
		FArdaInductorOptions O;
		Replace<FArdaCullUnreachableRule>(O, MakeArdaGraphRule<FArdaKeepAllNodesRule>());
		Replace<FArdaSearchOrderRule>(O, MakeArdaGraphRule<FArdaStableOrderRule>());
		Replace<FArdaAssignQueuesRule>(O, MakeArdaGraphRule<FArdaAssignQueuesRule>(false, false));
		ASSERT_TRUE(Edit.SetOptions(O));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{M.GetHandle(), Z.GetHandle(), A.GetHandle()}));
		EXPECT_TRUE(eastl::all_of(Graph.GetCompileResult().mQueues.begin(),
		    Graph.GetCompileResult().mQueues.end(),
		    [](auto Q)
		    {
			    return Q == EArdaRHIQueueType::Graphics;
		    }));
	}

	TEST(ArdaInductorRules, MissingOrStaleOutputFailsByDataInvariant)
	{
		for (int Mode = 0; Mode < 3; ++Mode)
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			RuleOperation(Edit, {}, "node");
			FArdaInductorOptions O;
			auto& Rules = O.mRules.mCompileRules;
			Rules.erase(eastl::remove_if(Rules.begin(),
			                Rules.end(),
			                [&](const auto& R)
			                {
				                return Mode == 0 ? dynamic_cast<const FArdaPlanMemoryRule*>(R.get()) != nullptr
				                    : Mode == 1  ? dynamic_cast<const FArdaFlattenDispatchesRule*>(R.get()) != nullptr
				                                 : dynamic_cast<const FArdaFlattenGraphRule*>(R.get()) != nullptr;
			                }),
			    Rules.end());
			ASSERT_TRUE(Edit.SetOptions(O));
			ASSERT_TRUE(Edit.Commit());
			EXPECT_FALSE(Graph.Compile());
			EXPECT_EQ(Graph.GetCompileResult().mRevision, 0u);
		}
	}

	TEST(ArdaInductorRules, NullInstancesAreRejectedButListLengthAndRepeatedRulesAreUnrestricted)
	{
		FArdaInductorRuleSet R;
		ASSERT_TRUE(ValidateArdaInductorRules(R));
		R.mPreprocessRules.push_back(R.mPreprocessRules.back());
		R.mCompileRules.push_back(R.mCompileRules.back());
		EXPECT_TRUE(ValidateArdaInductorRules(R));
		R.mCompileRules.push_back(nullptr);
		EXPECT_FALSE(ValidateArdaInductorRules(R));
		R.mCompileRules.clear();
		R.mPreprocessRules.clear();
		EXPECT_TRUE(ValidateArdaInductorRules(R));
		R.mPreprocessRules.push_back(nullptr);
		EXPECT_FALSE(ValidateArdaInductorRules(R));
	}

	TEST(ArdaInductorRules, CustomOrderAndMemoryRuleRespectWorkspaceAndBudget)
	{

		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto N = RuleOperation(Edit, {true, 4096}, "workspace");
		auto O = CustomOptions(EArdaPolicyResult::Forward);
		ASSERT_TRUE(Edit.SetOptions(O));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mAllocatedBytes, 4096u);
		EXPECT_NE(Graph.GetCompileResult().mWorkspaceResourceIds.find(N.GetHandle().GetIndex()),
		    Graph.GetCompileResult().mWorkspaceResourceIds.end());
		(void)Graph.Execute();
		FArdaGraphEdit Rejected(Graph);
		ASSERT_TRUE(RuleOperation(Rejected, {true, 4096}, "workspace"));
		O.mMaxVramBytes = 1024;
		ASSERT_TRUE(Rejected.SetOptions(O));
		ASSERT_TRUE(Rejected.Commit());
		EXPECT_FALSE(Graph.Compile());
		ASSERT_TRUE(Graph.DiscardPendingEdits());
	}

	TEST(ArdaInductorRules, CustomRulesRequireExplicitAdaptiveOptIn)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		auto O = CustomOptions(EArdaPolicyResult::Forward);
		O.mbEnableGpuTiming = O.mbEnableAdaptiveScheduling = true;
		EXPECT_FALSE(Edit.SetOptions(O));
	}
}
