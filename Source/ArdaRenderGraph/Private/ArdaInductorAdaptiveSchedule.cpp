#include "ArdaInductorAdaptiveSchedule.h"
#include "ArdaDependencyGraphInternal.h"

#include <EASTL/algorithm.h>
#include <cmath>

namespace arda
{
	namespace
	{
		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}

		void Increment(uint64_t& Value)
		{
			if (Value != UINT64_MAX)
			{
				++Value;
			}
		}

		eastl::vector<FArdaGraphNodeHandle> CriticalOrder(const FArdaDependencyGraph::FImpl& Graph,
		    const FArdaInductorMemoryPlan& Plan)
		{
			const auto& Nodes = Graph.mCompile.mExecutionOrder;
			eastl::unordered_map<uint32_t, size_t> Indices;
			for (size_t I = 0; I < Nodes.size(); ++I)
			{
				Indices.emplace(Nodes[I].GetIndex(), I);
			}
			eastl::vector<eastl::vector<size_t>> Next(Nodes.size());
			eastl::vector<size_t> Degree(Nodes.size(), 0);
			const auto Add = [&](uint32_t From, uint32_t To)
			{
				const auto P = Indices.at(From), C = Indices.at(To);
				if (eastl::find(Next[P].begin(), Next[P].end(), C) == Next[P].end())
				{
					Next[P].push_back(C);
					++Degree[C];
				}
			};
			for (const auto H : Nodes)
			{
				for (const auto E : Graph.mTopology.GetIncomingEdges(H))
				{
					Add(Graph.mTopology.TryGetEdge(E)->mFrom.GetIndex(), H.GetIndex());
				}
			}
			for (const auto& Edge : Graph.mCompile.mMemoryDependencies)
			{
				Add(Edge.first.GetIndex(), Edge.second.GetIndex());
			}
			for (const auto& Edge : Plan.mAliasEdges)
			{
				Add(Edge.mProducer, Edge.mConsumer);
			}
			eastl::vector<double> Critical(Nodes.size(), 0);
			for (size_t I = Nodes.size(); I-- > 0;)
			{
				double Tail = 0;
				for (const auto J : Next[I])
				{
					Tail = eastl::max(Tail, Critical[J]);
				}
				Critical[I] =
				    Tail + GetArdaInductorScheduleNodeCost(Graph, Graph.mTopology.TryGetNode(Nodes[I])->mPayload);
			}
			eastl::vector<bool> Done(Nodes.size(), false);
			eastl::vector<FArdaGraphNodeHandle> Order;
			while (Order.size() < Nodes.size())
			{
				size_t Best = Nodes.size();
				for (size_t I = 0; I < Nodes.size(); ++I)
				{
					if (!Done[I] && !Degree[I] && (Best == Nodes.size() || Critical[I] > Critical[Best]))
					{
						Best = I;
					}
				}
				if (Best == Nodes.size())
				{
					return {};
				}
				Done[Best] = true;
				Order.push_back(Nodes[Best]);
				for (const auto J : Next[Best])
				{
					--Degree[J];
				}
			}
			return Order;
		}

		FArdaRHIStatus UpdateLifetimes(const FArdaDependencyGraph::FImpl& Graph,
		    FArdaInductorCompileResult& Compile,
		    FArdaInductorMemoryPlan& Plan)
		{
			for (auto& Request : Plan.mRequests)
			{
				Request.mFirstUse = UINT32_MAX;
				Request.mLastUse = 0;
				Request.mFirstUseNodes.clear();
				Request.mLastUseNodes.clear();
			}
			const auto Mark = [&](uint32_t Resource, uint32_t Position, FArdaGraphNodeHandle Node) -> bool
			{
				if (Resource >= Plan.mRequests.size() || !Plan.mRequests[Resource].mbUsed ||
				    Plan.mRequests[Resource].mIdentifier != Resource)
				{
					return false;
				}
				auto& Request = Plan.mRequests[Resource];
				if (Request.mFirstUse == UINT32_MAX)
				{
					Request.mFirstUse = Position;
					Request.mFirstUseNodes = {Node.GetIndex()};
				}
				Request.mLastUse = Position;
				if (Request.mLastUseNodes.empty() || Request.mLastUseNodes.back() != Node.GetIndex())
				{
					Request.mLastUseNodes.push_back(Node.GetIndex());
				}
				return true;
			};
			for (uint32_t Position = 0; Position < Compile.mExecutionOrder.size(); ++Position)
			{
				const auto Handle = Compile.mExecutionOrder[Position];
				for (const auto& Access : Graph.mTopology.TryGetNode(Handle)->mPayload.mDesc.mAccesses)
				{
					if (!Mark(Access.mResource.mIndex, Position, Handle))
					{
						return Invalid("Adaptive schedule uses storage outside its accepted physical pool.");
					}
				}
				const auto Scratch = Compile.mWorkspaceResourceIds.find(Handle.GetIndex());
				if (Scratch != Compile.mWorkspaceResourceIds.end() && !Mark(Scratch->second, Position, Handle))
				{
					return Invalid("Adaptive schedule lost a declared workspace allocation.");
				}
			}
			for (auto& Request : Plan.mRequests)
			{
				if (Request.mbUsed && Request.mFirstUse == UINT32_MAX)
				{
					Request.mFirstUse = Request.mLastUse = 0;
				}
			}
			for (auto& Activation : Plan.mAliasActivations)
			{
				if (Activation.mResource >= Plan.mRequests.size() ||
				    Plan.mRequests[Activation.mResource].mFirstUseNodes.empty())
				{
					return Invalid("Adaptive alias activation lost its first user.");
				}
				Activation.mConsumer = Plan.mRequests[Activation.mResource].mFirstUseNodes.front();
			}
			const auto Disjoint = [&](uint32_t Left, uint32_t Right)
			{
				const auto& A = Plan.mRequests[Left];
				const auto& B = Plan.mRequests[Right];
				return A.mLastUse < B.mFirstUse || B.mLastUse < A.mFirstUse;
			};
			for (const auto& Slot : Plan.mSlots)
			{
				if (!Slot.mbExternal && !Slot.mbPersistent)
				{
					for (size_t I = 0; I < Slot.mResources.size(); ++I)
					{
						for (size_t J = I + 1; J < Slot.mResources.size(); ++J)
						{
							if (!Disjoint(Slot.mResources[I], Slot.mResources[J]))
							{
								return Invalid(
								    "Adaptive order overlaps two logical users of one committed allocation.");
							}
						}
					}
				}
			}
			for (const auto& Heap : Plan.mHeaps)
			{
				for (size_t I = 0; I < Heap.mResources.size(); ++I)
				{
					for (size_t J = I + 1; J < Heap.mResources.size(); ++J)
					{
						const auto A = Heap.mResources[I], B = Heap.mResources[J];
						const uint64_t AStart = Plan.mResourceOffsets[A], BStart = Plan.mResourceOffsets[B];
						const uint64_t ASize = Plan.mResourceBytes[A], BSize = Plan.mResourceBytes[B];
						// Subtraction avoids overflow at the upper end of the native allocation domain.
						const bool Overlap = AStart <= BStart ? BStart - AStart < ASize : AStart - BStart < BSize;
						if (Overlap && !Disjoint(A, B))
						{
							return Invalid("Adaptive order overlaps aliased native heap storage.");
						}
					}
				}
			}
			// Allocation capacity remains unchanged; peak live bytes is an ordinal diagnostic only.
			uint64_t Peak = Plan.mExternalBytes + Plan.mWorkspaceBytes;
			for (uint32_t Position = 0; Position < Compile.mExecutionOrder.size(); ++Position)
			{
				uint64_t Bytes = Plan.mExternalBytes + Plan.mWorkspaceBytes;
				for (const auto& Request : Plan.mRequests)
				{
					if (Request.mbUsed && !Request.mExternalBuffer && !Request.mExternalTexture &&
					    !Request.mExternalAccelerationStructure &&
					    (Request.mbPersistent || (Request.mFirstUse <= Position && Position <= Request.mLastUse)))
					{
						const uint64_t Copies = Request.mbPersistent ? 1 : Plan.mFrameCount;
						if (Request.mRequirements.mSize > UINT64_MAX / Copies ||
						    Request.mRequirements.mSize * Copies > UINT64_MAX - Bytes)
						{
							Bytes = UINT64_MAX;
						}
						else
						{
							Bytes += Request.mRequirements.mSize * Copies;
						}
					}
				}
				Peak = eastl::max(Peak, Bytes);
			}
			Compile.mPeakLiveBytes = Peak;
			return {};
		}
	}

	TArdaRHIResult<FArdaInductorAdaptiveScheduleResult> TuneArdaInductorSchedule(
	    const FArdaDependencyGraph::FImpl& Snapshot,
	    const FArdaInductorMemoryPlan& FixedPlan,
	    FArdaInductorAdaptiveScheduleState& State,
	    uint32_t CandidateBudget,
	    double MinimumRelativeImprovement)
	{
		if (!std::isfinite(MinimumRelativeImprovement) || MinimumRelativeImprovement < 0 ||
		    MinimumRelativeImprovement >= 1)
		{
			return {{}, Invalid("Adaptive minimum relative improvement must be finite and in [0,1).")};
		}
		const auto Baseline =
		    EvaluateArdaInductorFixedSchedule(Snapshot, FixedPlan, Snapshot.mCompile.mExecutionOrder, false);
		if (!Baseline)
		{
			return {{}, Baseline.mStatus};
		}
		FArdaInductorAdaptiveScheduleResult Result;
		Result.mCompile = Baseline.mValue;
		Result.mMemoryPlan = FixedPlan;
		Result.mIncumbentCost = Result.mCandidateCost = Baseline.mValue.mEstimatedExecutionCost;
		if (!CandidateBudget || Snapshot.mCompile.mExecutionOrder.empty())
		{
			return {eastl::move(Result), {}};
		}
		if (State.mGraphIdentity != Snapshot.mIdentity || State.mRevision != Snapshot.mCompile.mRevision ||
		    State.mIncumbentOrder != Snapshot.mCompile.mExecutionOrder)
		{
			State = {};
			State.mGraphIdentity = Snapshot.mIdentity;
			State.mRevision = Snapshot.mCompile.mRevision;
			State.mIncumbentOrder = State.mSeedOrder = State.mBestOrder = Snapshot.mCompile.mExecutionOrder;
		}
		Increment(State.mIteration);
		auto Best = EvaluateArdaInductorFixedSchedule(Snapshot, FixedPlan, State.mBestOrder, true);
		if (!Best)
		{
			State.mCandidateCursor = 0;
			State.mSeedOrder = State.mBestOrder = Snapshot.mCompile.mExecutionOrder;
			Best = EvaluateArdaInductorFixedSchedule(Snapshot, FixedPlan, State.mBestOrder, true);
		}
		if (!Best)
		{
			return {{}, Best.mStatus};
		}
		if (Best.mValue.mEstimatedExecutionCost > Baseline.mValue.mEstimatedExecutionCost)
		{
			Best = Baseline;
			State.mBestOrder = Snapshot.mCompile.mExecutionOrder;
		}
		const uint64_t Count = State.mSeedOrder.size();
		const uint64_t NeighborhoodSize = 1 + Count * (Count - 1);
		// The critical-path candidate crosses local insertion minima when EMA costs change branches.
		const auto Priority = CriticalOrder(Snapshot, FixedPlan);
		State.mCandidateCursor %= NeighborhoodSize;
		while (Result.mCandidatesExamined < CandidateBudget)
		{
			auto Order = State.mSeedOrder;
			const uint64_t Cursor = State.mCandidateCursor++;
			if (Cursor)
			{
				const size_t From = size_t((Cursor - 1) / (Count - 1));
				size_t To = size_t((Cursor - 1) % (Count - 1));
				if (To >= From)
				{
					++To;
				}
				const auto Moved = Order[From];
				Order.erase(Order.begin() + From);
				Order.insert(Order.begin() + To, Moved);
			}
			else
			{
				Order = Priority;
			}
			++Result.mCandidatesExamined;
			Increment(State.mCandidatesExamined);
			auto Candidate = EvaluateArdaInductorFixedSchedule(Snapshot, FixedPlan, Order, true);
			if (Candidate && Candidate.mValue.mEstimatedExecutionCost < Best.mValue.mEstimatedExecutionCost)
			{
				State.mBestOrder = Order;
				Best = eastl::move(Candidate);
			}
			if (State.mCandidateCursor == NeighborhoodSize)
			{
				State.mCandidateCursor = 0;
				Result.mbNeighborhoodComplete = true;
				const bool BetterSeed = State.mSeedOrder != State.mBestOrder;
				State.mSeedOrder = State.mBestOrder;
				// Revisit this neighborhood when a later timing sample changes the cost model.
				if (!BetterSeed)
				{
					break;
				}
			}
		}
		Result.mCandidateCost = Best.mValue.mEstimatedExecutionCost;
		const double Gain = Result.mIncumbentCost - Result.mCandidateCost;
		if (Gain > 0 && Gain >= Result.mIncumbentCost * MinimumRelativeImprovement)
		{
			Result.mCompile = eastl::move(Best.mValue);
			if (auto Status = UpdateLifetimes(Snapshot, Result.mCompile, Result.mMemoryPlan); !Status)
			{
				return {{}, Status};
			}
			Result.mbImproved = true;
		}
		return {eastl::move(Result), {}};
	}
}
