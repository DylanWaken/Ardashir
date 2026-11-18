#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include "Inductor/Rules/ArdaInductorPipelineInternal.h"

namespace arda
{
	namespace
	{
		struct FArdaBatchAccess
		{
			EArdaRHIResourceState mState{};
			uint64_t mBegin = 0, mEnd = 0;
			bool mbWrite = false;
		};

		TArdaRHIResult<eastl::vector<uint8_t>> PipelineKey(const FArdaDependencyGraph::FArdaImpl& Graph,
		    FArdaGraphNodeHandle Handle)
		{
			const auto& Desc = Graph.mCompile.mNodes.at(Handle).mDesc;
			if (Desc.mPipelines.empty())
			{
				return {};
			}
			FArdaInductorPipelineNode Projection;
			Projection.mNodeId = Handle.GetIndex();
			Projection.mContributions = Desc.mPipelineStages;
			for (const auto& Request : Desc.mPipelines)
			{
				Projection.mPipelineBoundaries.push_back(Request.mKind);
			}
			pipeline_detail::FArdaPipelineKeyWriter Key;
			Key.Add(Desc.mPipelines.size());
			for (auto Request : Desc.mPipelines)
			{
				Request.mTerminalNodeId = Handle.GetIndex();
				Request.mSlot = Request.mSlot.empty() ? "default" : Request.mSlot;
				const auto Pattern = InferArdaInductorPipeline({Projection}, Request);
				if (!Pattern)
				{
					return {{}, Pattern.mStatus};
				}
				const auto Configuration = pipeline_detail::ConfigurationKey(Pattern.mValue.mConfiguration);
				Key.String(Request.mSlot);
				Key.Add(Configuration.mBytes.size());
				Key.mBytes.insert(Key.mBytes.end(), Configuration.mBytes.begin(), Configuration.mBytes.end());
			}
			// Inferred raster formats can be unspecified until physical framebuffer resolution.
			Key.Add(Desc.mColorTargets.size());
			for (auto Target : Desc.mColorTargets)
			{
				Key.Add(Graph.mResources[Target.mIndex].mTexture.mFormat);
				Key.Add(Graph.mResources[Target.mIndex].mTexture.mSampleCount);
			}
			Key.Add(bool(Desc.mDepthTarget));
			if (Desc.mDepthTarget)
			{
				Key.Add(Graph.mResources[Desc.mDepthTarget.mIndex].mTexture.mFormat);
				Key.Add(Graph.mResources[Desc.mDepthTarget.mIndex].mTexture.mSampleCount);
			}
			return {eastl::move(Key.mBytes), {}};
		}
	}

	FArdaRHIStatus FArdaBatchPipelinesRule::Compile(const FArdaInductorGraph& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		const auto& State = FArdaInductorGraphAccess::State(Graph);
		if (auto Status = inductor_detail::ValidateNodeOrder(State, Result); !Status)
		{
			return Status;
		}
		if (Result.mQueues.size() != Result.mExecutionOrder.size())
		{
			return inductor_detail::Error("Assign queues before batching pipelines.");
		}
		Result.mPipelineBatches.clear();
		eastl::unordered_set<uint32_t> Boundaries;
		for (const auto& Dependency : Result.mMemoryDependencies)
		{
			Boundaries.insert(Dependency.second.GetIndex());
		}
		if (Result.mMemoryPlan)
		{
			for (const auto& Activation : Result.mMemoryPlan->mAliasActivations)
			{
				// First occupants of buffer slots can all activate at the batch head. Reused slots
				// must retain their predecessor boundary; textures have explicit image lifetime passes.
				if (!Activation.mPreviousResources.empty() ||
				    Result.mMemoryPlan->mRequests[Activation.mResource].mKind == EArdaMemoryKind::Texture)
				{
					Boundaries.insert(Activation.mConsumer);
				}
			}
		}
		eastl::unordered_map<uint32_t, const eastl::vector<FArdaGraphNodeHandle>*> CudaHeads;
		for (const auto& Batch : Result.mCudaBatches)
		{
			if (!Batch.empty())
			{
				CudaHeads.emplace(Batch.front().GetIndex(), &Batch);
			}
		}
		eastl::unordered_set<uint32_t> Emitted;
		eastl::unordered_map<uint32_t, FArdaBatchAccess> Accesses;
		eastl::vector<uint8_t> PreviousKey;
		EArdaRHIQueueType PreviousQueue{};
		for (size_t Position = 0; Position < Result.mExecutionOrder.size(); ++Position)
		{
			const auto Handle = Result.mExecutionOrder[Position];
			const auto& Node = State.mTopology.TryGetNode(Handle)->mPayload;
			if (inductor_detail::IsDeclaration(State, Node) || Emitted.count(Handle.GetIndex()))
			{
				continue;
			}
			if (Node.mDefinition->mKind == EArdaDependencyNodeKind::Cuda)
			{
				const auto Found = CudaHeads.find(Handle.GetIndex());
				if (Found == CudaHeads.end())
				{
					return inductor_detail::Error("Batch CUDA before batching pipelines.");
				}
				Result.mPipelineBatches.push_back(*Found->second);
				for (auto Member : *Found->second)
				{
					Emitted.insert(Member.GetIndex());
				}
				PreviousKey.clear();
				Accesses.clear();
				continue;
			}
			auto Key = PipelineKey(State, Handle);
			if (!Key)
			{
				return Key.mStatus;
			}
			bool bJoin = !Key.mValue.empty() && Key.mValue == PreviousKey &&
			    Result.mQueues[Position] == PreviousQueue && !Boundaries.count(Handle.GetIndex());
			for (const auto& Access : State.GetNodeDescription(Node).mAccesses)
			{
				const auto& Resource = State.mResources[Access.mResource.mIndex];
				// Acceleration-structure build state is published at submission boundaries.
				if (Resource.mExternalAccelerationStructure && Access.mAccess != EArdaDependencyAccess::Read)
				{
					bJoin = false;
					Key.mValue.clear();
				}
				const auto Existing = Accesses.find(Access.mResource.mIndex);
				if (!bJoin || Existing == Accesses.end())
				{
					continue;
				}
				const auto& Prior = Existing->second;
				if (Prior.mState != Access.mState)
				{
					bJoin = false;
					continue;
				}
				if (!Prior.mbWrite && Access.mAccess == EArdaDependencyAccess::Read)
				{
					continue;
				}
				// Ordered framebuffer writes need no intervening transition/UAV barrier.
				if (Resource.mbTexture &&
				    (Access.mState == EArdaRHIResourceState::RenderTarget ||
				        Access.mState == EArdaRHIResourceState::DepthWrite))
				{
					continue;
				}
				if (Resource.mbTexture)
				{
					bJoin = false;
					continue;
				}
				const auto Range = Access.mBufferRange.Resolve(Resource.mBuffer);
				if (Range.mByteOffset < Prior.mEnd && Prior.mBegin < Range.mByteOffset + Range.mByteSize)
				{
					bJoin = false;
				}
			}
			if (!bJoin)
			{
				Result.mPipelineBatches.push_back({});
				Accesses.clear();
			}
			Result.mPipelineBatches.back().push_back(Handle);
			for (const auto& Access : State.GetNodeDescription(Node).mAccesses)
			{
				const auto& Resource = State.mResources[Access.mResource.mIndex];
				const auto Range = Resource.mExternalAccelerationStructure ? FArdaRHIBufferRange{0, 1}
				    : Resource.mbTexture                                   ? FArdaRHIBufferRange{0, UINT64_MAX}
				                         : Access.mBufferRange.Resolve(Resource.mBuffer);
				const auto Added = Accesses.emplace(Access.mResource.mIndex,
				    FArdaBatchAccess{Access.mState,
				        Range.mByteOffset,
				        Range.mByteOffset + Range.mByteSize,
				        Access.mAccess != EArdaDependencyAccess::Read});
				if (!Added.second)
				{
					auto& Value = Added.first->second;
					Value.mBegin = eastl::min(Value.mBegin, Range.mByteOffset);
					Value.mEnd = eastl::max(Value.mEnd, Range.mByteOffset + Range.mByteSize);
					Value.mbWrite |= Access.mAccess != EArdaDependencyAccess::Read;
				}
			}
			PreviousKey = eastl::move(Key.mValue);
			PreviousQueue = Result.mQueues[Position];
		}
		return {};
	}
}
