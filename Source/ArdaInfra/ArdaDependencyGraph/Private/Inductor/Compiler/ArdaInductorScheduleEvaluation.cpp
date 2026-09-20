#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"

namespace arda::inductor_detail
{
	void FArdaScheduleSearch::Evaluate(const eastl::vector<size_t>& Order, bool Serialize)
	{
		// Under a cap, a larger parallel pool may still fit and be faster than
		// the allocator's serialization-for-reuse candidate. Evaluate both policies.
		if (Serialize && mObjective == EArdaInductorObjective::Efficiency)
		{
			Evaluate(Order, false);
		}
		++mResult.mSchedulesExamined;
		auto Q = mRequests;
		for (auto& R : Q)
		{
			R.mFirstUse = UINT32_MAX;
			R.mLastUse = 0;
			R.mFirstUseNodes.clear();
			R.mLastUseNodes.clear();
		}
		for (uint32_t P = 0; P < Order.size(); ++P)
		{
			for (auto R : mUses[Order[P]])
			{
				auto& V = Q[R];
				V.mFirstUse = eastl::min(V.mFirstUse, P);
				V.mLastUse = P;
				// Every user participates: cross-queue lifetimes require a dependency proof, not ordinal coincidence.
				V.mLastUseNodes.push_back(mNodes[Order[P]].GetIndex());
			}
		}
		for (auto& R : Q)
		{
			if (R.mbUsed)
			{
				if (R.mFirstUse == UINT32_MAX)
				{
					R.mFirstUse = R.mLastUse = 0;
				}
				else
				{
					R.mFirstUseNodes.push_back(mNodes[Order[R.mFirstUse]].GetIndex());
				}
			}
		}
		FArdaMemoryOptions O;
		O.mFrameCount = mGraph.mOptions.mFramesInFlight;
		O.mbAllowSerialization = Serialize;
		O.mWorkspaceBytes = mWorkspace;
		O.mBudgetBytes = mGraph.mOptions.mMaxVramBytes ? mGraph.mOptions.mMaxVramBytes : UINT64_MAX;
		O.mHappensBeforeContext = this;
		O.mHappensBefore = [](void* Context, uint32_t A, uint32_t B)
		{
			auto& S = *static_cast<FArdaScheduleSearch*>(Context);
			return S.mGraph.mTopology.IsReachable(S.mNodes[S.mIndices.at(A)], S.mNodes[S.mIndices.at(B)]);
		};
		auto P = PlanArdaMemory(Q, O);
		if (!P)
		{
			mLastPlanFailure = P.mStatus;
			return;
		}
		FArdaInductorCompileResult Candidate;
		for (const auto I : Order)
		{
			Candidate.mExecutionOrder.push_back(mNodes[I]);
		}
		for (const auto& Edge : P.mValue.mAliasEdges)
		{
			Candidate.mMemoryDependencies.push_back(
			    {mNodes[mIndices.at(Edge.mProducer)], mNodes[mIndices.at(Edge.mConsumer)]});
		}
		if (auto Status = AssignQueues(mGraph, Candidate); !Status)
		{
			mLastPlanFailure = Status;
			return;
		}
		eastl::vector<double> Finish(mNodes.size(), 0);
		eastl::vector<eastl::vector<size_t>> ExtraPred(mNodes.size());
		for (const auto& Edge : P.mValue.mAliasEdges)
		{
			ExtraPred[mIndices.at(Edge.mConsumer)].push_back(mIndices.at(Edge.mProducer));
		}
		double QueueEnd[ArdaRHIQueueTypeCount]{};
		uint64_t Handoffs = 0;
		bool HavePrevious = false, PreviousCuda = false;
		for (size_t Position = 0; Position < Order.size(); ++Position)
		{
			const auto Index = Order[Position];
			const auto& Node = mGraph.mTopology.TryGetNode(mNodes[Index])->mPayload;
			double Ready = 0;
			for (const auto Pred : mPred[Index])
			{
				Ready = eastl::max(Ready, Finish[Pred]);
			}
			for (const auto Pred : ExtraPred[Index])
			{
				Ready = eastl::max(Ready, Finish[Pred]);
			}
			if (IsDeclaration(Node))
			{
				Finish[Index] = Ready;
				continue;
			}
			const auto QueueIndex = GetArdaRHIQueueIndex(Candidate.mQueues[Position]);
			Finish[Index] = eastl::max(Ready, QueueEnd[QueueIndex]) + NodeCost(mGraph, Node);
			QueueEnd[QueueIndex] = Finish[Index];
			const bool Cuda = Node.mDefinition->mKind == EArdaDependencyNodeKind::Cuda;
			if (HavePrevious && PreviousCuda != Cuda)
			{
				++Handoffs;
			}
			HavePrevious = true;
			PreviousCuda = Cuda;
		}
		double Cost = double(Handoffs) * mGraph.mOptions.mCudaHandoffCost;
		for (const auto& Activation : P.mValue.mAliasActivations)
		{
			if (Q[Activation.mResource].mKind == EArdaMemoryKind::Texture)
			{
				Cost += 2.0 * mGraph.mOptions.mAliasSubmissionCost;
			}
		}
		Cost += *eastl::max_element(QueueEnd, QueueEnd + ArdaRHIQueueTypeCount);
		const bool Memory = mObjective == EArdaInductorObjective::Memory;
		const uint64_t Bytes = P.mValue.mTotalBytes;
		const bool Better = Memory ? (Bytes < mBestBytes || (Bytes == mBestBytes && Cost < mBestCost))
		                           : (Cost < mBestCost || (Cost == mBestCost && Bytes < mBestBytes));
		if (mbHasBestPlan && !Better)
		{
			return;
		}
		mBestBytes = Bytes;
		mBestCost = Cost;
		mResult.mEstimatedExecutionCost = Cost;
		mbHasBestPlan = true;
		mBestPlan = eastl::move(P.mValue);
		mBestOrder.clear();
		for (auto I : Order)
		{
			mBestOrder.push_back(mNodes[I]);
		}
		uint64_t Peak = mWorkspace;
		if (!AddBytes(Peak, mBestPlan.mExternalBytes))
		{
			Peak = UINT64_MAX;
		}
		for (uint32_t Pos = 0; Pos < Order.size(); ++Pos)
		{
			uint64_t Bytes = mWorkspace;
			if (!AddBytes(Bytes, mBestPlan.mExternalBytes))
			{
				Bytes = UINT64_MAX;
			}
			for (const auto& R : Q)
			{
				if (R.mbUsed && !R.mExternalBuffer && !R.mExternalTexture && !R.mExternalAccelerationStructure &&
				    (R.mbPersistent || (R.mFirstUse <= Pos && Pos <= R.mLastUse)))
				{
					const uint64_t Copies = R.mbPersistent ? 1 : mGraph.mOptions.mFramesInFlight;
					if (R.mRequirements.mSize > UINT64_MAX / Copies || !AddBytes(Bytes, R.mRequirements.mSize * Copies))
					{
						Bytes = UINT64_MAX;
					}
				}
			}
			Peak = eastl::max(Peak, Bytes);
		}
		mResult.mPeakLiveBytes = Peak;
	}
}
