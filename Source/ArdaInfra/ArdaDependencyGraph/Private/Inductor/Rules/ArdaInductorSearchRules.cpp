#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"

namespace arda::inductor_detail
{
	void FArdaScheduleSearch::Greedy(bool Memory)
	{
		eastl::vector<size_t> Degree;
		for (const auto& P : mPred)
		{
			Degree.push_back(P.size());
		}
		eastl::vector<uint32_t> Remaining(mRequests.size());
		for (const auto& Uses : mUses)
		{
			for (auto R : Uses)
			{
				++Remaining[R];
			}
		}
		eastl::vector<bool> Seen(mRequests.size(), false), Done(mNodes.size(), false);
		eastl::vector<size_t> Order;
		while (Order.size() < mNodes.size())
		{
			size_t Best = SIZE_MAX;
			long double BestScore = -eastl::numeric_limits<long double>::infinity();
			for (size_t I = 0; I < mNodes.size(); ++I)
			{
				if (!Done[I] && !Degree[I])
				{
					long double Score = static_cast<long double>(mCritical[I]);
					if (Memory)
					{
						Score = 0;
						for (auto R : mUses[I])
						{
							const auto& Q = mRequests[R];
							if (Q.mbPersistent || Q.mExternalBuffer || Q.mExternalTexture ||
							    Q.mExternalAccelerationStructure)
							{
								continue;
							}
							if (!Seen[R])
							{
								Score -= Q.mRequirements.mSize;
							}
							if (Remaining[R] == 1)
							{
								Score += Q.mRequirements.mSize;
							}
						}
					}
					else if (!Order.empty() &&
					    mGraph.mTopology.TryGetNode(mNodes[Order.back()])->mPayload.mDefinition->mKind ==
					        EArdaDependencyNodeKind::Cuda &&
					    mGraph.mTopology.TryGetNode(mNodes[I])->mPayload.mDefinition->mKind ==
					        EArdaDependencyNodeKind::Cuda)
					{
						Score += double(mGraph.mOptions.mCudaHandoffCost) * 2;
					}
					if (Best == SIZE_MAX || Score > BestScore)
					{
						Best = I;
						BestScore = Score;
					}
				}
			}
			if (Best == SIZE_MAX)
			{
				return;
			}
			Done[Best] = true;
			Order.push_back(Best);
			for (auto J : mNext[Best])
			{
				--Degree[J];
			}
			for (auto R : mUses[Best])
			{
				Seen[R] = true;
				--Remaining[R];
			}
		}
		Evaluate(Order, mGraph.mOptions.mMaxVramBytes || mObjective == EArdaInductorObjective::Memory);
	}

	void FArdaScheduleSearch::ImproveIncumbent()
	{
		if (!mbHasBestPlan || mSearchMode != EArdaInductorSearchMode::Bounded)
		{
			return;
		}
		// Spend at most a quarter of the search budget on whole-order neighborhoods.
		// This visits early decisions even when a large suffix would exhaust DFS.
		const uint64_t Limit = mMaxSearchStates / 4;
		mStates = 1;
		for (uint32_t Pass = 0; Pass < 4 && mStates < Limit; ++Pass)
		{
			bool Improved = false;
			for (size_t Position = 1; Position < mBestOrder.size() && mStates < Limit; ++Position)
			{
				eastl::vector<size_t> Order;
				for (const auto H : mBestOrder)
				{
					Order.push_back(mIndices.at(H.GetIndex()));
				}
				const auto Before = Order[Position - 1], After = Order[Position];
				// Adjacent vertices in a topological order cannot have an indirect path
				// between them. A direct edge is the only reason their swap is illegal.
				if (eastl::find(mPred[After].begin(), mPred[After].end(), Before) != mPred[After].end())
				{
					continue;
				}
				eastl::swap(Order[Position - 1], Order[Position]);
				const auto Previous = mBestOrder;
				++mStates;
				Evaluate(Order, mGraph.mOptions.mMaxVramBytes || mObjective == EArdaInductorObjective::Memory);
				Improved |= Previous != mBestOrder;
			}
			if (!Improved)
			{
				break;
			}
		}
	}

	void FArdaScheduleSearch::Explore()
	{
		// Iterative Kahn backtracking does not impose a graph-depth or call-stack limit.
		// Explore near the incumbent first, retaining deterministic name order for ties.
		eastl::vector<size_t> Priority, Order, Degree, Cursor(mNodes.size() + 1, 0);
		eastl::vector<bool> Done(mNodes.size(), false);
		for (const auto H : mBestOrder)
		{
			Priority.push_back(mIndices.at(H.GetIndex()));
		}
		if (Priority.empty())
		{
			for (size_t I = 0; I < mNodes.size(); ++I)
			{
				Priority.push_back(I);
			}
		}
		for (const auto& Pred : mPred)
		{
			Degree.push_back(Pred.size());
		}
		const bool Serialize = mGraph.mOptions.mMaxVramBytes || mObjective == EArdaInductorObjective::Memory;
		mStates = eastl::max(uint64_t(1), mStates);
		for (;;)
		{
			if (Order.size() == mNodes.size())
			{
				Evaluate(Order, Serialize);
			}
			else
			{
				auto& Next = Cursor[Order.size()];
				while (Next < Priority.size() && (Done[Priority[Next]] || Degree[Priority[Next]]))
				{
					++Next;
				}
				if (Next < Priority.size())
				{
					if (mSearchMode != EArdaInductorSearchMode::Exhaustive && mStates >= mMaxSearchStates)
					{
						mbExhausted = true;
						return;
					}
					const size_t I = Priority[Next++];
					Done[I] = true;
					Order.push_back(I);
					for (const auto N : mNext[I])
					{
						--Degree[N];
					}
					Cursor[Order.size()] = 0;
					++mStates;
					continue;
				}
			}
			if (Order.empty())
			{
				mbComplete = true;
				return;
			}
			const auto Previous = Order.back();
			Order.pop_back();
			Done[Previous] = false;
			for (const auto N : mNext[Previous])
			{
				++Degree[N];
			}
		}
	}
}
