#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include "Inductor/Rules/ArdaInductorAdaptiveSchedule.h"
#include <cmath>

namespace arda
{
	using namespace inductor_detail;

	double GetArdaInductorScheduleNodeCost(const FArdaDependencyGraph::FArdaImpl& Snapshot,
	    const FArdaDependencyNode& Node)
	{
		return NodeCost(Snapshot, Node);
	}

	TArdaRHIResult<FArdaInductorCompileResult> EvaluateArdaInductorFixedSchedule(
	    const FArdaDependencyGraph::FArdaImpl& G,
	    const FArdaMemoryPlan& FixedPlan,
	    const eastl::vector<FArdaGraphNodeHandle>& Order,
	    bool AssignAutomaticQueues)
	{
		if (!FixedPlan.mbBudgetAccepted || FixedPlan.mFrameCount != G.mOptions.mFramesInFlight ||
		    FixedPlan.mTotalBytes != G.mCompile.mAllocatedBytes ||
		    (G.mOptions.mMaxVramBytes && FixedPlan.mTotalBytes > G.mOptions.mMaxVramBytes))
		{
			return {{},
			    Error(
			        "Adaptive scheduling requires the incumbent accepted allocation plan within the current budget.")};
		}
		if (Order.size() != G.mCompile.mExecutionOrder.size() || G.mCompile.mQueues.size() != Order.size())
		{
			return {{},
			    Error("Adaptive scheduling must preserve every incumbent node and its queue assignment domain.")};
		}
		eastl::unordered_map<uint32_t, size_t> Position;
		eastl::unordered_set<FArdaGraphNodeHandle, FArdaGraphNodeHandleHash> Incumbent;
		for (auto H : G.mCompile.mExecutionOrder)
		{
			Incumbent.insert(H);
		}
		for (size_t I = 0; I < Order.size(); ++I)
		{
			const auto H = Order[I];
			const auto* Record = G.mTopology.TryGetNode(H);
			if (!Record || !Record->mPayload.mDefinition || !Incumbent.count(H) ||
			    !Position.emplace(H.GetIndex(), I).second)
			{
				return {{}, Error("Adaptive candidate contains a duplicate, foreign or stale semantic node.")};
			}
			const double Cost = NodeCost(G, Record->mPayload);
			if (!std::isfinite(Cost) || Cost < 0)
			{
				return {{}, Error("Adaptive timing costs must be finite and nonnegative.")};
			}
			for (const auto& Access : Record->mPayload.mDesc.mAccesses)
			{
				if (!G.HasResource(Access.mResource))
				{
					return {{}, Error("Adaptive snapshot contains a stale resource access.")};
				}
			}
		}
		FArdaInductorCompileResult Candidate = G.mCompile;
		Candidate.mExecutionOrder = Order;
		eastl::vector<eastl::vector<size_t>> Predecessors(Order.size());
		const auto AddPredecessor = [&](FArdaGraphNodeHandle From, FArdaGraphNodeHandle To) -> bool
		{
			const auto P = Position.find(From.GetIndex()), C = Position.find(To.GetIndex());
			if (P == Position.end() || C == Position.end() || Order[P->second] != From || Order[C->second] != To ||
			    P->second >= C->second)
			{
				return false;
			}
			auto& Pred = Predecessors[C->second];
			if (eastl::find(Pred.begin(), Pred.end(), P->second) == Pred.end())
			{
				Pred.push_back(P->second);
			}
			return true;
		};
		for (const auto H : Order)
		{
			for (const auto E : G.mTopology.GetIncomingEdges(H))
			{
				if (!AddPredecessor(G.mTopology.TryGetEdge(E)->mFrom, H))
				{
					return {{}, Error("Adaptive candidate violates semantic dependencies.")};
				}
			}
		}
		for (const auto& Edge : Candidate.mMemoryDependencies)
		{
			if (!AddPredecessor(Edge.first, Edge.second))
			{
				return {{}, Error("Adaptive candidate violates a fixed memory dependency.")};
			}
		}
		for (const auto& Edge : FixedPlan.mAliasEdges)
		{
			const auto P = Position.find(Edge.mProducer), C = Position.find(Edge.mConsumer);
			if (P == Position.end() || C == Position.end() || !AddPredecessor(Order[P->second], Order[C->second]))
			{
				return {{}, Error("Adaptive candidate violates the accepted allocation's alias order.")};
			}
			const auto Pair = eastl::make_pair(Order[P->second], Order[C->second]);
			if (eastl::find(Candidate.mMemoryDependencies.begin(), Candidate.mMemoryDependencies.end(), Pair) ==
			    Candidate.mMemoryDependencies.end())
			{
				return {{}, Error("The incumbent compile is missing a fixed native alias dependency.")};
			}
		}
		// Native alias edges terminate at the accepted first user. Keep that boundary
		// fixed: moving a different independent user ahead of it would preserve a
		// linear lifetime interval while losing the cross-queue happens-before proof.
		eastl::unordered_set<uint32_t> FixedFirstUsers;
		for (const auto& Activation : FixedPlan.mAliasActivations)
		{
			FixedFirstUsers.insert(Activation.mResource);
			FixedFirstUsers.insert(Activation.mPreviousResources.begin(), Activation.mPreviousResources.end());
		}
		for (const auto& Slot : FixedPlan.mSlots)
		{
			if (Slot.mResources.size() > 1)
			{
				FixedFirstUsers.insert(Slot.mResources.begin(), Slot.mResources.end());
			}
		}
		for (const auto Identifier : FixedFirstUsers)
		{
			if (Identifier >= FixedPlan.mRequests.size())
			{
				return {{}, Error("Adaptive alias boundary references missing storage.")};
			}
			const auto& Request = FixedPlan.mRequests[Identifier];
			if (Request.mFirstUseNodes.size() != 1 || !Position.count(Request.mFirstUseNodes.front()))
			{
				return {{}, Error("Adaptive alias storage must retain its accepted first-use boundary.")};
			}
			const auto FirstPosition = Position.at(Request.mFirstUseNodes.front());
			for (const auto User : Request.mLastUseNodes)
			{
				if (!Position.count(User) || Position.at(User) < FirstPosition)
				{
					return {{}, Error("Adaptive candidate moves an alias user ahead of its activation boundary.")};
				}
			}
		}
		if (AssignAutomaticQueues)
		{
			if (auto Status = AssignQueues(G, Candidate); !Status)
			{
				return {{}, Status};
			}
		}
		else if (Order != G.mCompile.mExecutionOrder)
		{
			return {{}, Error("An incumbent queue cost can only be evaluated in its original order.")};
		}
		eastl::vector<double> Finish(Order.size(), 0);
		double QueueEnd[ArdaRHIQueueTypeCount]{};
		uint64_t Handoffs = 0;
		bool HavePrevious = false, PreviousCuda = false;
		for (size_t I = 0; I < Order.size(); ++I)
		{
			double Ready = 0;
			for (const auto P : Predecessors[I])
			{
				Ready = eastl::max(Ready, Finish[P]);
			}
			const auto& Node = G.mTopology.TryGetNode(Order[I])->mPayload;
			if (IsDeclaration(Node))
			{
				Finish[I] = Ready;
				continue;
			}
			const auto Queue = GetArdaRHIQueueIndex(Candidate.mQueues[I]);
			if (Queue >= ArdaRHIQueueTypeCount)
			{
				return {{}, Error("Adaptive schedule has an invalid queue.")};
			}
			Finish[I] = eastl::max(Ready, QueueEnd[Queue]) + NodeCost(G, Node);
			if (!std::isfinite(Finish[I]))
			{
				return {{}, Error("Adaptive critical-path cost overflowed.")};
			}
			QueueEnd[Queue] = Finish[I];
			const bool Cuda = Node.mDefinition->mKind == EArdaDependencyNodeKind::Cuda;
			if (HavePrevious && PreviousCuda != Cuda)
			{
				++Handoffs;
			}
			HavePrevious = true;
			PreviousCuda = Cuda;
		}
		double Cost = *eastl::max_element(QueueEnd, QueueEnd + ArdaRHIQueueTypeCount) +
		    double(Handoffs) * G.mOptions.mCudaHandoffCost;
		for (const auto& Activation : FixedPlan.mAliasActivations)
		{
			if (Activation.mResource >= FixedPlan.mRequests.size())
			{
				return {{}, Error("Adaptive alias activation references missing storage.")};
			}
			if (FixedPlan.mRequests[Activation.mResource].mKind == EArdaMemoryKind::Texture)
			{
				Cost += 2.0 * G.mOptions.mAliasSubmissionCost;
			}
		}
		if (!std::isfinite(Cost))
		{
			return {{}, Error("Adaptive schedule cost overflowed.")};
		}
		auto View = FArdaInductorGraphAccess::View(const_cast<FArdaDependencyGraph::FArdaImpl&>(G));
		if (auto S = FArdaFlattenDispatchesRule{}.Apply(View, Candidate); !S)
		{
			return {{}, S};
		}
		if (auto S = FArdaResolveFencesRule{}.Apply(View, Candidate); !S)
		{
			return {{}, S};
		}
		Candidate.mAllocationOrder = Order;
		Candidate.mEstimatedExecutionCost = Cost;
		return {eastl::move(Candidate), {}};
	}
}
