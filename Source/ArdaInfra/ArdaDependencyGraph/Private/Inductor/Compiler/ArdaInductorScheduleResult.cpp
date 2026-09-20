#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"

namespace arda::inductor_detail
{
	FArdaRHIStatus PublishSchedule(FArdaScheduleSearch& Search, FArdaInductorCompileResult& Result)
	{
		auto& G = Search.mGraph;
		Result.mSearchStatesExamined = Search.mStates;
		Result.mbSearchComplete = Search.mbComplete;
		Result.mbSearchExhausted = Search.mbExhausted;
		if (!Search.mbHasBestPlan)
		{
			if (!Search.mLastPlanFailure && Search.mLastPlanFailure.mCode != EArdaRHIResult::InvalidState)
			{
				return Search.mLastPlanFailure;
			}
			return Error(!Search.mbComplete
			        ? "No VRAM-feasible allocation schedule found within the configured search limit."
			        : "No VRAM-feasible schedule exists for the admitted resource allocation model.");
		}
		Result.mExecutionOrder = Search.mBestOrder;
		Result.mAllocatedBytes = Search.mBestPlan.mTotalBytes;
		uint64_t UniqueBytes = 0;
		for (const auto& Q : Search.mBestPlan.mRequests)
		{
			if (Q.mbUsed && !Q.mExternalBuffer && !Q.mExternalTexture && !Q.mExternalAccelerationStructure)
			{
				const uint64_t Copies = Q.mbPersistent ? 1 : G.mOptions.mFramesInFlight;
				if (Q.mRequirements.mSize > UINT64_MAX / Copies ||
				    !AddBytes(UniqueBytes, Q.mRequirements.mSize * Copies))
				{
					UniqueBytes = UINT64_MAX;
				}
			}
		}
		Result.mAliasedBytes =
		    UniqueBytes >= Search.mBestPlan.mOwnedBytes ? UniqueBytes - Search.mBestPlan.mOwnedBytes : 0;
		Result.mMemoryDependencies.clear();
		for (const auto& E : Search.mBestPlan.mAliasEdges)
		{
			auto P = Search.mNodes[Search.mIndices.at(E.mProducer)], C = Search.mNodes[Search.mIndices.at(E.mConsumer)];
			Result.mMemoryDependencies.push_back({P, C});
		}
		Result.mAllocationOrder = Result.mExecutionOrder;
		Result.mMemoryPlan = eastl::make_shared<const FArdaMemoryPlan>(eastl::move(Search.mBestPlan));
		return {};
	}
}
