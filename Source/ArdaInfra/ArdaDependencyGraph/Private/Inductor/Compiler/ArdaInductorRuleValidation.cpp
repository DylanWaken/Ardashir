#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"

namespace arda::inductor_detail
{
	namespace
	{
		bool IsBatchRefinement(const eastl::vector<eastl::vector<FArdaGraphNodeHandle>>& Safe,
		    const eastl::vector<eastl::vector<FArdaGraphNodeHandle>>& Proposed,
		    size_t MinimumSize)
		{
			eastl::unordered_map<uint32_t, eastl::pair<size_t, size_t>> Positions;
			for (size_t Batch = 0; Batch < Safe.size(); ++Batch)
			{
				for (size_t Lane = 0; Lane < Safe[Batch].size(); ++Lane)
				{
					Positions.emplace(Safe[Batch][Lane].GetIndex(), eastl::make_pair(Batch, Lane));
				}
			}
			eastl::unordered_set<FArdaGraphNodeHandle, FArdaGraphNodeHandleHash> Seen;
			for (const auto& Batch : Proposed)
			{
				if (Batch.size() < MinimumSize)
				{
					return false;
				}
				const auto First = Positions.find(Batch.front().GetIndex());
				if (First == Positions.end())
				{
					return false;
				}
				const auto& Original = Safe[First->second.first];
				const auto Offset = First->second.second;
				if (Batch.size() > Original.size() - Offset)
				{
					return false;
				}
				for (size_t Lane = 0; Lane < Batch.size(); ++Lane)
				{
					if (Batch[Lane] != Original[Offset + Lane] || !Seen.insert(Batch[Lane]).second)
					{
						return false;
					}
				}
			}
			return true;
		}
	}

	FArdaRHIStatus ValidateNodeOrder(const FArdaDependencyGraph::FArdaImpl& G, const FArdaInductorCompileResult& R)
	{
		if (!R.mStatus)
		{
			return R.mStatus;
		}
		eastl::unordered_map<uint32_t, size_t> Position;
		eastl::unordered_set<FArdaGraphNodeHandle, FArdaGraphNodeHandleHash> Partition;
		for (const auto* List : {&R.mExecutionOrder, &R.mCulledNodes})
		{
			for (auto N : *List)
			{
				if (!G.mTopology.ContainsNode(N) || !Partition.insert(N).second)
				{
					return Error("Compilation contains foreign, stale or repeated nodes.");
				}
			}
		}
		if (Partition.size() != G.mTopology.GetNodeCount())
		{
			return Error("Flattened and culled nodes must account for the working graph.");
		}
		for (size_t I = 0; I < R.mExecutionOrder.size(); ++I)
		{
			Position.emplace(R.mExecutionOrder[I].GetIndex(), I);
		}
		for (auto C : R.mExecutionOrder)
		{
			for (auto E : G.mTopology.GetIncomingEdges(C))
			{
				const auto P = G.mTopology.TryGetEdge(E)->mFrom;
				const auto Found = Position.find(P.GetIndex());
				if (Found == Position.end() || Found->second >= Position.at(C.GetIndex()))
				{
					return Error("A live consumer precedes or omits its dependency.");
				}
			}
		}
		return {};
	}

	FArdaRHIStatus ValidateCompiledResult(const FArdaDependencyGraph::FArdaImpl& G, const FArdaInductorCompileResult& R)
	{
		if (auto Status = CheckArdaGraphRequiredFeatures(G); !Status)
		{
			return Status;
		}
		if (auto Status = ValidateResourceAccesses(G); !Status)
		{
			return Status;
		}
		if (auto S = ValidateNodeOrder(G, R); !S)
		{
			return S;
		}
		if (auto S = ValidatePipelines(G, R.mExecutionOrder); !S)
		{
			return S;
		}
		if (!R.mMemoryPlan || !R.mMemoryPlan->mbBudgetAccepted || R.mAllocationOrder != R.mExecutionOrder ||
		    R.mMemoryPlan->mFrameCount != G.mOptions.mFramesInFlight ||
		    R.mAllocatedBytes != R.mMemoryPlan->mTotalBytes ||
		    (G.mOptions.mMaxVramBytes && R.mAllocatedBytes > G.mOptions.mMaxVramBytes))
		{
			return Error("The result requires an accepted allocation plan for its current node order and budget.");
		}
		if (R.mMemoryPlan->mRequests.size() < G.mResources.size())
		{
			return Error("The allocation plan omits graph resource descriptions.");
		}
		for (const auto& Request : R.mMemoryPlan->mRequests)
		{
			if (HasAnyFlags(Request.mBufferDesc.mUsage, EArdaRHIBufferUsage::AutoDeduction) ||
			    HasAnyFlags(Request.mTextureDesc.mUsage, EArdaRHITextureUsage::AutoDeduction))
			{
				return Error("Allocation plans must contain concrete resource usages, never AutoDeduction.");
			}
			if (Request.mIdentifier < G.mResources.size())
			{
				const auto& Resource = G.mResources[Request.mIdentifier];
				if (Resource.mExternalAccelerationStructure)
				{
					continue;
				}
				const bool Matches = Resource.mbTexture ? Request.mKind == EArdaMemoryKind::Texture &&
				        (Request.mTextureDesc.mUsage & Resource.mTexture.mUsage) == Resource.mTexture.mUsage
				                                        : Request.mKind == EArdaMemoryKind::Buffer &&
				        (Request.mBufferDesc.mUsage & Resource.mBuffer.mUsage) == Resource.mBuffer.mUsage;
				if (!Matches)
				{
					return Error("Allocation resource usages no longer satisfy the compiled graph requirements.");
				}
			}
		}
		eastl::vector<eastl::pair<FArdaGraphNodeHandle, FArdaGraphNodeHandle>> MemoryEdges;
		eastl::unordered_map<uint32_t, FArdaGraphNodeHandle> LiveNodes;
		for (auto Handle : R.mExecutionOrder)
		{
			LiveNodes.emplace(Handle.GetIndex(), Handle);
		}
		const auto Find = [&](uint32_t Id)
		{
			const auto I = LiveNodes.find(Id);
			return I == LiveNodes.end() ? FArdaGraphNodeHandle{} : I->second;
		};
		for (const auto& E : R.mMemoryPlan->mAliasEdges)
		{
			const auto P = Find(E.mProducer), C = Find(E.mConsumer);
			if (!P || !C)
			{
				return Error("Allocation references an absent node.");
			}
			MemoryEdges.push_back({P, C});
		}
		if (MemoryEdges != R.mMemoryDependencies)
		{
			return Error("Allocation dependencies were changed without rebuilding the plan.");
		}
		const FArdaInductorQueueContext Input{G.mTopology,
		    G.mResources,
		    G.mOptions,
		    G.mCompile.mExecution->mDevice,
		    {},
		    G.mCompile};
		if (auto S = ValidateQueueAssignment(Input, R); !S)
		{
			return S;
		}
		// Check the shape and required synchronization of the executable data, not which rules produced it.
		auto Expected = R;
		BuildCudaBatches(G, Expected);
		if (Expected.mCudaBatches != R.mCudaBatches)
		{
			return Error("CUDA batches do not cover the selected CUDA operations.");
		}
		auto View = FArdaInductorGraphAccess::View(const_cast<FArdaDependencyGraph::FArdaImpl&>(G));
		if (auto S = FArdaBatchPipelinesRule{}.Apply(View, Expected); !S)
		{
			return S;
		}
		if (!IsBatchRefinement(Expected.mPipelineBatches, R.mPipelineBatches, 1))
		{
			return Error("Pipeline batches cross compatible pipeline, hazard or alias boundaries.");
		}
		Expected.mPipelineBatches = R.mPipelineBatches;
		if (auto S = FArdaMergeDrawInstancesRule{}.Apply(View, Expected); !S)
		{
			return S;
		}
		if (!IsBatchRefinement(Expected.mInstancedDraws, R.mInstancedDraws, 2))
		{
			return Error("Instanced draws disagree with compatible bindings and stage contracts.");
		}
		Expected.mInstancedDraws = R.mInstancedDraws;
		if (auto S = FArdaFlattenDispatchesRule{}.Apply(View, Expected); !S)
		{
			return S;
		}
		if (Expected.mDispatches.size() != R.mDispatches.size() || Expected.mQueueDispatches != R.mQueueDispatches ||
		    Expected.mCudaStreams != R.mCudaStreams)
		{
			return Error("Queue/stream dispatch ordering is missing or stale.");
		}
		for (size_t I = 0; I < R.mDispatches.size(); ++I)
		{
			if (Expected.mDispatches[I].mNodes != R.mDispatches[I].mNodes ||
			    Expected.mDispatches[I].mQueue != R.mDispatches[I].mQueue ||
			    Expected.mDispatches[I].mCudaStream != R.mDispatches[I].mCudaStream)
			{
				return Error("Dispatch contents disagree with node ordering, queues or CUDA batches.");
			}
		}
		if (auto S = FArdaResolveFencesRule{}.Apply(View, Expected); !S)
		{
			return S;
		}
		const auto EdgeKey = [](uint32_t From, uint32_t To)
		{
			return (uint64_t(From) << 32) | To;
		};
		eastl::unordered_set<uint64_t> DependencyKeys, FenceKeys;
		for (const auto& E : R.mDispatchDependencies)
		{
			DependencyKeys.insert(EdgeKey(E.first, E.second));
		}
		for (const auto& F : R.mFences)
		{
			FenceKeys.insert(EdgeKey(F.mProducer, F.mConsumer));
		}
		for (const auto& E : Expected.mDispatchDependencies)
		{
			if (!DependencyKeys.count(EdgeKey(E.first, E.second)))
			{
				return Error("A required dispatch dependency is missing.");
			}
		}
		for (const auto& E : R.mDispatchDependencies)
		{
			if (E.first >= E.second || E.second >= R.mDispatches.size())
			{
				return Error("A dispatch dependency is invalid or cyclic.");
			}
		}
		for (const auto& E : R.mDispatchDependencies)
		{
			const auto& P = R.mDispatches[E.first];
			const auto& C = R.mDispatches[E.second];
			if ((P.mQueue != C.mQueue || P.mCudaStream != C.mCudaStream) &&
			    !FenceKeys.count(EdgeKey(E.first, E.second)))
			{
				return Error("A cross-domain dispatch dependency has no fence.");
			}
		}
		for (const auto& F : R.mFences)
		{
			if (F.mProducer >= F.mConsumer || F.mConsumer >= R.mDispatches.size() ||
			    !DependencyKeys.count(EdgeKey(F.mProducer, F.mConsumer)))
			{
				return Error("A fence references a missing or invalid dispatch dependency.");
			}
		}
		return {};
	}
}
