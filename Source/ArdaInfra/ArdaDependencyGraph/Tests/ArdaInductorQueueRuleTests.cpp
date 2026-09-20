#include "ArdaDependencyGraph.h"
#include <gtest/gtest.h>

namespace arda::queue_rule_tests
{
	using namespace arda;

	struct FArdaQueueRuleParameters
	{
	};

	struct FArdaQueueRuleNode
	    : TArdaDependencyNode<FArdaQueueRuleNode, FArdaQueueRuleParameters, EArdaDependencyNodeKind::Copy>
	{
		using FArdaParameters = FArdaQueueRuleParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"inductor.queue.rules.test", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters&)
		{
			return "copy";
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			FArdaDependencyNodeDesc Result;
			Result.mbSideEffect = true;
			return Result;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaQueueRuleNode, QueueRuleOperation);
	ARDA_DEFINE_GRAPH_NODE(FArdaQueueRuleNode, QueueRuleOperation);

	enum class EArdaQueuePolicyResult
	{
		Graphics,
		MissingQueue,
		InvalidQueue,
		ForeignNode,
		InvalidCudaBatch,
		Throw
	};

	class FArdaTestQueueRule final : public FArdaInductorQueueRule
	{
	public:
		FArdaTestQueueRule(EArdaQueuePolicyResult Result, uint32_t* Invocations)
		    : mResult(Result),
		      mInvocations(Invocations)
		{
		}

		EArdaQueuePolicyResult mResult;
		uint32_t* mInvocations;

		const char* GetName() const noexcept override
		{
			return "TestQueuePolicy";
		}

		FArdaRHIStatus Assign(const FArdaInductorQueueContext& Graph, FArdaInductorCompileResult& Result) const override
		{
			if (mInvocations)
			{
				++*mInvocations;
			}
			EXPECT_EQ(Graph.mTopology.GetNodeCount(), 1u);
			if (mResult == EArdaQueuePolicyResult::Throw)
			{
				throw 1;
			}
			if (auto Status = FArdaGraphicsQueueRule{}.Assign(Graph, Result); !Status)
			{
				return Status;
			}
			if (mResult == EArdaQueuePolicyResult::MissingQueue)
			{
				Result.mQueues.clear();
			}
			if (mResult == EArdaQueuePolicyResult::InvalidQueue)
			{
				Result.mQueues.front() = EArdaRHIQueueType::Count;
			}
			if (mResult == EArdaQueuePolicyResult::ForeignNode)
			{
				Result.mExecutionOrder.front() = {};
			}
			if (mResult == EArdaQueuePolicyResult::InvalidCudaBatch)
			{
				Result.mCudaBatches.push_back(Result.mExecutionOrder);
			}
			return {};
		}
	};

	FArdaInductorOptions QueueOptions(EArdaQueuePolicyResult Result, uint32_t* Invocations = nullptr)
	{
		FArdaInductorOptions Options;

		for (auto& Rule : Options.mRules.mCompileRules)
		{
			if (dynamic_cast<const FArdaInductorQueueRule*>(Rule.get()))
			{
				Rule = MakeArdaGraphRule<FArdaTestQueueRule>(Result, Invocations);
			}
		}
		return Options;
	}

	TEST(ArdaInductorQueueRules, CustomPolicyAppliesDuringSearchAndFinalCompilation)
	{

		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		uint32_t Invocations = 0;
		ASSERT_TRUE(Edit.SetOptions(QueueOptions(EArdaQueuePolicyResult::Graphics, &Invocations)));
		ASSERT_TRUE(QueueRuleOperation(Edit, {}, "copy"));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_GT(Invocations, 1u);
		EXPECT_EQ(Graph.GetCompileResult().mQueues, (eastl::vector<EArdaRHIQueueType>{EArdaRHIQueueType::Graphics}));
		EXPECT_GT(Graph.GetCompileResult().mSchedulesExamined, 0u);
	}

	TEST(ArdaInductorQueueRules, InvalidPoliciesPreserveTheCommittedSchedule)
	{

		FArdaDependencyGraph Graph;
		FArdaGraphEdit Initial(Graph);
		ASSERT_TRUE(QueueRuleOperation(Initial, {}, "copy"));
		ASSERT_TRUE(Initial.Commit());
		ASSERT_TRUE(Graph.Compile());
		const auto Before = Graph.GetCompileResult();
		// A device-less execution attempt releases the compiled stage for the next construction.
		(void)Graph.Execute();
		for (const auto Invalid : {EArdaQueuePolicyResult::MissingQueue,
		         EArdaQueuePolicyResult::InvalidQueue,
		         EArdaQueuePolicyResult::ForeignNode,
		         EArdaQueuePolicyResult::InvalidCudaBatch,
		         EArdaQueuePolicyResult::Throw})
		{
			SCOPED_TRACE(static_cast<int>(Invalid));
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.SetOptions(QueueOptions(Invalid)));
			ASSERT_TRUE(QueueRuleOperation(Edit, {}, "copy"));
			ASSERT_TRUE(Edit.Commit());
			const auto Status = Graph.Compile();
			EXPECT_FALSE(Status);
			EXPECT_FALSE(Status.mMessage.empty());
			EXPECT_EQ(Graph.GetCompileResult().mRevision, Before.mRevision);
			EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder, Before.mExecutionOrder);
			EXPECT_EQ(Graph.GetCompileResult().mQueues, Before.mQueues);
			ASSERT_TRUE(Graph.DiscardPendingEdits());
		}
	}

	TEST(ArdaInductorQueueRules, CustomQueuePolicyRequiresExplicitAdaptiveOptIn)
	{

		FArdaDependencyGraph Graph;
		auto Options = QueueOptions(EArdaQueuePolicyResult::Graphics);
		EXPECT_FALSE(MakeArdaGraphRule<FArdaTestQueueRule>(EArdaQueuePolicyResult::Graphics, nullptr)
		        ->AllowsAdaptiveScheduling());
		Options.mbEnableGpuTiming = true;
		Options.mbEnableAdaptiveScheduling = true;
		{
			FArdaGraphEdit Rejected(Graph);
			EXPECT_FALSE(Rejected.SetOptions(Options));
			EXPECT_FALSE(Rejected.Commit());
		}
		for (auto& Rule : Options.mRules.mCompileRules)
		{
			if (dynamic_cast<const FArdaInductorQueueRule*>(Rule.get()))
			{
				Rule = MakeArdaGraphRule<FArdaGraphicsQueueRule>();
			}
		}
		FArdaGraphEdit Accepted(Graph);
		EXPECT_TRUE(Accepted.SetOptions(Options));
		EXPECT_TRUE(QueueRuleOperation(Accepted));
		EXPECT_TRUE(Accepted.Commit());
	}
}
