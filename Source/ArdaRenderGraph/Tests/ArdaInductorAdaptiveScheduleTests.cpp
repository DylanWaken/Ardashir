#include "ArdaInductorAdaptiveSchedule.h"
#include "ArdaDependencyGraphInternal.h"

#include <gtest/gtest.h>
#include <limits>

namespace
{
	using namespace arda;

	FArdaGraphNodeHandle AddNode(FArdaDependencyGraph::FImpl& G,
	    const char* Name,
	    EArdaDependencyNodeKind Kind,
	    uint32_t Cost)
	{
		auto Definition = eastl::make_shared<FArdaDependencyNodeDefinition>();
		Definition->mName = Name;
		Definition->mKind = Kind;
		FArdaDependencyNode Node;
		Node.mName = Node.mCanonicalKey = Name;
		Node.mDefinition = Definition;
		Node.mDesc.mEstimatedCost = Cost;
		Node.mDesc.mbSideEffect = true;
		return G.mTopology.AddNode(eastl::move(Node));
	}

	void SetMeasuredCost(FArdaDependencyGraph::FImpl& G, FArdaGraphNodeHandle Handle, double Cost)
	{
		const auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
		G.mCostOverrides[Node.mName] = Cost;
		G.mCostOverrideKeys[Node.mName] = Node.mCanonicalKey;
		G.mCostOverrideDefinitions[Node.mName] = Node.mDefinition;
	}

	struct FScheduleFixture
	{
		FArdaDependencyGraph::FImpl mGraph;
		FArdaInductorMemoryPlan mPlan;
		FArdaGraphNodeHandle mIndependent, mProducer, mCompute;

		FScheduleFixture()
		{
			mGraph.mIdentity = 99;
			mGraph.mCompile.mRevision = 7;
			mGraph.mOptions.mMinimumAsyncChain = 1;
			mGraph.mOptions.mMinimumAsyncSlack = 1;
			mIndependent = AddNode(mGraph, "independent graphics", EArdaDependencyNodeKind::Graphics, 10);
			mProducer = AddNode(mGraph, "graphics producer", EArdaDependencyNodeKind::Graphics, 1);
			mCompute = AddNode(mGraph, "async consumer", EArdaDependencyNodeKind::Compute, 10);
			mGraph.mTopology.AddEdge(mProducer, mCompute, {});
			mGraph.mCompile.mExecutionOrder = {mIndependent, mProducer, mCompute};
			mGraph.mCompile.mQueues = {EArdaRHIQueueType::Graphics,
			    EArdaRHIQueueType::Graphics,
			    EArdaRHIQueueType::Compute};
			mPlan.mbBudgetAccepted = true;
		}

		void AddWorkspace()
		{
			FArdaInductorMemoryRequest Request;
			Request.mBufferDesc.mByteSize = 256;
			Request.mBufferDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::Raw;
			Request.mRequirements = {256, 256, 1};
			Request.mFirstUseNodes = Request.mLastUseNodes = {mIndependent.GetIndex()};
			mPlan.mRequests = {Request};
			FArdaInductorMemorySlot Slot;
			Slot.mResources = {0};
			Slot.mBytes = 256;
			mPlan.mSlots = {Slot};
			mPlan.mResourceSlots = {0};
			mPlan.mResourceBytes = {256};
			mPlan.mResourceHeapIndices = {UINT32_MAX};
			mPlan.mResourceOffsets = {0};
			mPlan.mTotalBytes = mPlan.mOwnedBytes = mPlan.mTransientBytes = 256;
			mGraph.mCompile.mAllocatedBytes = 256;
			mGraph.mCompile.mWorkspaceResourceIds[mIndependent.GetIndex()] = 0;
			mGraph.mTopology.TryGetNode(mIndependent)->mPayload.mDesc.mTransientWorkspaceBytes = 256;
		}
	};

	TEST(ArdaInductorAdaptiveSchedule, OneCandidateStepsContinueAndFindAnOverlappingQueueSchedule)
	{
		FScheduleFixture F;
		FArdaInductorAdaptiveScheduleState State;
		bool Improved = false;
		for (uint32_t Iteration = 0; Iteration < 7; ++Iteration)
		{
			auto Result = TuneArdaInductorSchedule(F.mGraph, F.mPlan, State, 1, .02);
			ASSERT_TRUE(Result) << Result.mStatus.mMessage.c_str();
			EXPECT_EQ(Result.mValue.mCandidatesExamined, 1u);
			EXPECT_EQ(State.mCandidatesExamined, Iteration + 1);
			EXPECT_DOUBLE_EQ(Result.mValue.mIncumbentCost, 21);
			if (Result.mValue.mbImproved)
			{
				Improved = true;
				EXPECT_DOUBLE_EQ(Result.mValue.mCandidateCost, 11);
				EXPECT_EQ(Result.mValue.mCompile.mRevision, 7u);
				EXPECT_NE(Result.mValue.mCompile.mExecutionOrder, F.mGraph.mCompile.mExecutionOrder);
				EXPECT_EQ(Result.mValue.mCompile.mAllocatedBytes, 0u);
				EXPECT_EQ(F.mGraph.mCompile.mExecutionOrder.front(), F.mIndependent);
				break;
			}
		}
		EXPECT_TRUE(Improved);
	}

	TEST(ArdaInductorAdaptiveSchedule, HysteresisKeepsTheIncumbentUntilImprovementIsLargeEnough)
	{
		FScheduleFixture F;
		FArdaInductorAdaptiveScheduleState State;
		auto Result = TuneArdaInductorSchedule(F.mGraph, F.mPlan, State, 100, .9);
		ASSERT_TRUE(Result);
		EXPECT_FALSE(Result.mValue.mbImproved);
		EXPECT_EQ(Result.mValue.mCompile.mExecutionOrder, F.mGraph.mCompile.mExecutionOrder);
		EXPECT_DOUBLE_EQ(Result.mValue.mCandidateCost, 11);
		EXPECT_LE(Result.mValue.mCandidatesExamined, 100u);
		Result = TuneArdaInductorSchedule(F.mGraph, F.mPlan, State, 1, .02);
		ASSERT_TRUE(Result);
		EXPECT_TRUE(Result.mValue.mbImproved);
		EXPECT_DOUBLE_EQ(Result.mValue.mCandidateCost, 11);
	}

	TEST(ArdaInductorAdaptiveSchedule, FixedAliasDependenciesAndBudgetsCannotBeRemovedForSpeed)
	{
		FScheduleFixture F;
		F.AddWorkspace();
		F.mPlan.mAliasEdges = {{F.mIndependent.GetIndex(), F.mProducer.GetIndex()}};
		F.mGraph.mCompile.mMemoryDependencies = {{F.mIndependent, F.mProducer}};
		F.mGraph.mOptions.mMaxVramBytes = 256;
		FArdaInductorAdaptiveScheduleState State;
		auto Result = TuneArdaInductorSchedule(F.mGraph, F.mPlan, State, 100, 0);
		ASSERT_TRUE(Result);
		EXPECT_FALSE(Result.mValue.mbImproved);
		EXPECT_EQ(Result.mValue.mCompile.mMemoryDependencies, F.mGraph.mCompile.mMemoryDependencies);
		EXPECT_FALSE(
		    EvaluateArdaInductorFixedSchedule(F.mGraph, F.mPlan, {F.mProducer, F.mCompute, F.mIndependent}, true));
		F.mGraph.mOptions.mMaxVramBytes = 255;
		EXPECT_FALSE(TuneArdaInductorSchedule(F.mGraph, F.mPlan, State, 100, 0));
		F.mGraph.mOptions.mMaxVramBytes = 0;
		EXPECT_TRUE(TuneArdaInductorSchedule(F.mGraph, F.mPlan, State, 1, 0));
	}

	TEST(ArdaInductorAdaptiveSchedule, WorkspaceLifetimeChangesWithoutChangingAnyPhysicalAllocation)
	{
		FScheduleFixture F;
		F.AddWorkspace();
		FArdaInductorAdaptiveScheduleState State;
		auto Result = TuneArdaInductorSchedule(F.mGraph, F.mPlan, State, 100, .02);
		ASSERT_TRUE(Result) << Result.mStatus.mMessage.c_str();
		ASSERT_TRUE(Result.mValue.mbImproved);
		const auto& Plan = Result.mValue.mMemoryPlan;
		EXPECT_EQ(Plan.mTotalBytes, F.mPlan.mTotalBytes);
		EXPECT_EQ(Plan.mResourceOffsets, F.mPlan.mResourceOffsets);
		EXPECT_EQ(Plan.mResourceHeapIndices, F.mPlan.mResourceHeapIndices);
		EXPECT_EQ(Plan.mResourceSlots, F.mPlan.mResourceSlots);
		EXPECT_EQ(Plan.mResourceBytes, F.mPlan.mResourceBytes);
		EXPECT_EQ(Plan.mSlots.size(), F.mPlan.mSlots.size());
		EXPECT_EQ(Plan.mHeaps.size(), F.mPlan.mHeaps.size());
		EXPECT_EQ(Result.mValue.mCompile.mWorkspaceResourceIds, F.mGraph.mCompile.mWorkspaceResourceIds);
		EXPECT_GT(Plan.mRequests[0].mFirstUse, F.mPlan.mRequests[0].mFirstUse);
		EXPECT_EQ(Plan.mRequests[0].mFirstUse, Plan.mRequests[0].mLastUse);
		EXPECT_EQ(Plan.mRequests[0].mLastUseNodes, F.mPlan.mRequests[0].mLastUseNodes);
	}

	TEST(ArdaInductorAdaptiveSchedule, AliasedStorageKeepsItsOriginalFirstUseBoundary)
	{
		FScheduleFixture F;
		const auto Earlier = AddNode(F.mGraph, "prior alias occupant", EArdaDependencyNodeKind::Graphics, 1);
		F.mGraph.mCompile.mExecutionOrder.insert(F.mGraph.mCompile.mExecutionOrder.begin(), Earlier);
		F.mGraph.mCompile.mQueues.insert(F.mGraph.mCompile.mQueues.begin(), EArdaRHIQueueType::Graphics);
		F.mGraph.mCompile.mMemoryDependencies = {{Earlier, F.mIndependent}};
		F.mPlan.mAliasEdges = {{Earlier.GetIndex(), F.mIndependent.GetIndex()}};
		FArdaInductorMemoryRequest Prior, Next;
		Prior.mFirstUseNodes = Prior.mLastUseNodes = {Earlier.GetIndex()};
		Next.mIdentifier = 1;
		Next.mFirstUse = 1;
		Next.mLastUse = 2;
		Next.mFirstUseNodes = {F.mIndependent.GetIndex()};
		Next.mLastUseNodes = {F.mIndependent.GetIndex(), F.mProducer.GetIndex()};
		F.mPlan.mRequests = {Prior, Next};
		FArdaInductorMemorySlot Slot;
		Slot.mResources = {0, 1};
		F.mPlan.mSlots = {Slot};
		ASSERT_TRUE(EvaluateArdaInductorFixedSchedule(F.mGraph, F.mPlan, F.mGraph.mCompile.mExecutionOrder, true));
		// This order still respects the explicit alias edge and disjoint ordinal
		// lifetimes. Its new first user lacks a wait for the preceding occupant.
		const eastl::vector<FArdaGraphNodeHandle> UnsafeOrder = {Earlier, F.mProducer, F.mIndependent, F.mCompute};
		EXPECT_FALSE(EvaluateArdaInductorFixedSchedule(F.mGraph, F.mPlan, UnsafeOrder, true));
		// Heap placement represents the same boundary through alias activations.
		F.mPlan.mSlots.clear();
		F.mPlan.mAliasActivations = {{0, Earlier.GetIndex(), {}}, {1, F.mIndependent.GetIndex(), {0}}};
		ASSERT_TRUE(EvaluateArdaInductorFixedSchedule(F.mGraph, F.mPlan, F.mGraph.mCompile.mExecutionOrder, true));
		EXPECT_FALSE(EvaluateArdaInductorFixedSchedule(F.mGraph, F.mPlan, UnsafeOrder, true));
	}

	TEST(ArdaInductorAdaptiveSchedule, UpdatedMeasuredCostsCanChangeThePreferredBranchAcrossIterations)
	{
		FArdaDependencyGraph::FImpl G;
		G.mIdentity = 44;
		G.mCompile.mRevision = 1;
		G.mOptions.mMinimumAsyncChain = G.mOptions.mMinimumAsyncSlack = 1;
		const auto A = AddNode(G, "graphics A", EArdaDependencyNodeKind::Graphics, 10);
		const auto B = AddNode(G, "graphics B", EArdaDependencyNodeKind::Graphics, 10);
		const auto C = AddNode(G, "compute A", EArdaDependencyNodeKind::Compute, 100);
		const auto D = AddNode(G, "compute B", EArdaDependencyNodeKind::Compute, 1);
		G.mTopology.AddEdge(A, C, {});
		G.mTopology.AddEdge(B, D, {});
		G.mCompile.mExecutionOrder = {A, C, B, D};
		G.mCompile.mQueues = {EArdaRHIQueueType::Graphics,
		    EArdaRHIQueueType::Compute,
		    EArdaRHIQueueType::Graphics,
		    EArdaRHIQueueType::Compute};
		FArdaInductorMemoryPlan Plan;
		Plan.mbBudgetAccepted = true;
		FArdaInductorAdaptiveScheduleState State;
		auto First = TuneArdaInductorSchedule(G, Plan, State, 100, .02);
		ASSERT_TRUE(First);
		EXPECT_FALSE(First.mValue.mbImproved);
		EXPECT_DOUBLE_EQ(First.mValue.mIncumbentCost, 111);
		const uint64_t Examined = State.mCandidatesExamined;
		SetMeasuredCost(G, C, 1);
		SetMeasuredCost(G, D, 100);
		auto Next = TuneArdaInductorSchedule(G, Plan, State, 100, .02);
		ASSERT_TRUE(Next) << Next.mStatus.mMessage.c_str();
		EXPECT_TRUE(Next.mValue.mbImproved);
		EXPECT_DOUBLE_EQ(Next.mValue.mIncumbentCost, 120);
		EXPECT_DOUBLE_EQ(Next.mValue.mCandidateCost, 111);
		EXPECT_GT(State.mCandidatesExamined, Examined);
	}

	TEST(ArdaInductorAdaptiveSchedule, ZeroWorkInvalidCostsAndRevisionChangesAreHandledExplicitly)
	{
		FScheduleFixture F;
		FArdaInductorAdaptiveScheduleState State;
		auto Zero = TuneArdaInductorSchedule(F.mGraph, F.mPlan, State, 0, .02);
		ASSERT_TRUE(Zero);
		EXPECT_EQ(Zero.mValue.mCandidatesExamined, 0u);
		EXPECT_EQ(State.mIteration, 0u);
		EXPECT_FALSE(Zero.mValue.mbImproved);
		ASSERT_TRUE(TuneArdaInductorSchedule(F.mGraph, F.mPlan, State, 2, .02));
		++F.mGraph.mCompile.mRevision;
		auto Reset = TuneArdaInductorSchedule(F.mGraph, F.mPlan, State, 1, .02);
		ASSERT_TRUE(Reset);
		EXPECT_EQ(State.mRevision, 8u);
		EXPECT_EQ(State.mCandidatesExamined, 1u);
		EXPECT_FALSE(TuneArdaInductorSchedule(F.mGraph, F.mPlan, State, 1, 1));
		SetMeasuredCost(F.mGraph, F.mCompute, std::numeric_limits<double>::quiet_NaN());
		EXPECT_FALSE(TuneArdaInductorSchedule(F.mGraph, F.mPlan, State, 1, .02));
	}

	TEST(ArdaInductorAdaptiveSchedule, AutomaticQueueEvaluationDoesNotAccumulateDuplicateCudaBatches)
	{
		FArdaDependencyGraph::FImpl G;
		G.mIdentity = 8;
		G.mCompile.mRevision = 1;
		const auto A = AddNode(G, "CUDA A", EArdaDependencyNodeKind::Cuda, 1);
		const auto B = AddNode(G, "CUDA B", EArdaDependencyNodeKind::Cuda, 1);
		G.mTopology.AddEdge(A, B, {});
		G.mCompile.mExecutionOrder = {A, B};
		G.mCompile.mQueues = {EArdaRHIQueueType::Graphics, EArdaRHIQueueType::Graphics};
		G.mCompile.mCudaBatches = {{A, B}};
		FArdaInductorMemoryPlan Plan;
		Plan.mbBudgetAccepted = true;
		for (uint32_t I = 0; I < 3; ++I)
		{
			auto Result = EvaluateArdaInductorFixedSchedule(G, Plan, G.mCompile.mExecutionOrder, true);
			ASSERT_TRUE(Result);
			EXPECT_EQ(Result.mValue.mCudaBatches, G.mCompile.mCudaBatches);
		}
	}
}
