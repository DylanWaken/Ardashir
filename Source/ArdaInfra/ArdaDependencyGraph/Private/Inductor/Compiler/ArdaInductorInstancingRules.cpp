#include "Inductor/Compiler/ArdaInductorInstancingRules.h"
#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"

namespace arda
{
	namespace
	{
		bool SameBindings(const FArdaDependencyNodeDesc& A, const FArdaDependencyNodeDesc& B)
		{
			if (A.mShaderBindings.size() != B.mShaderBindings.size() || A.mAccesses.size() != B.mAccesses.size() ||
			    A.mColorTargets != B.mColorTargets || A.mDepthTarget != B.mDepthTarget)
			{
				return false;
			}
			for (size_t Index = 0; Index < A.mAccesses.size(); ++Index)
			{
				const auto& Left = A.mAccesses[Index];
				const auto& Right = B.mAccesses[Index];
				if (Left.mResource != Right.mResource || Left.mAccess != Right.mAccess || Left.mState != Right.mState ||
				    !(Left.mBufferRange == Right.mBufferRange) || !(Left.mTextureRange == Right.mTextureRange))
				{
					return false;
				}
			}
			for (size_t Index = 0; Index < A.mShaderBindings.size(); ++Index)
			{
				const auto& Left = A.mShaderBindings[Index];
				const auto& Right = B.mShaderBindings[Index];
				if (Left.mMetadata != Right.mMetadata || Left.mPipelineSlot != Right.mPipelineSlot ||
				    Left.mResources.size() != Right.mResources.size() || Left.mValues.size() != Right.mValues.size())
				{
					return false;
				}
				for (size_t Resource = 0; Resource < Left.mResources.size(); ++Resource)
				{
					const auto& LeftResource = Left.mResources[Resource];
					const auto& RightResource = Right.mResources[Resource];
					if (LeftResource.mMember != RightResource.mMember ||
					    LeftResource.mResource != RightResource.mResource ||
					    LeftResource.mArrayElement != RightResource.mArrayElement ||
					    LeftResource.mAccess != RightResource.mAccess || !(LeftResource.mView == RightResource.mView) ||
					    LeftResource.mSampler != RightResource.mSampler)
					{
						return false;
					}
				}
				for (size_t Value = 0; Value < Left.mValues.size(); ++Value)
				{
					if (Left.mValues[Value].mMember != Right.mValues[Value].mMember ||
					    Left.mValues[Value].mBytes != Right.mValues[Value].mBytes)
					{
						return false;
					}
				}
			}
			return true;
		}

		bool CanInstance(const FArdaDependencyGraph::FArdaImpl& Graph, const FArdaDependencyNode& Node)
		{
			const auto& Desc = Graph.GetNodeDescription(Node);
			if (Node.mGraphicsStages.empty() || Desc.mPipelines.size() != 1 ||
			    Desc.mPipelines.front().mKind != EArdaPipelineStateKind::Graphics || !Desc.mBindlessTables.empty() ||
			    !Desc.mShaderTables.empty() || Desc.mWorkspaceBytes || Desc.mTransientWorkspaceBytes)
			{
				return false;
			}
			for (auto StageHandle : Node.mGraphicsStages)
			{
				const auto* Stage = Graph.mTopology.TryGetNode(StageHandle);
				if (!Stage || !Stage->mPayload.mDefinition ||
				    Stage->mPayload.mDefinition->mGraphicsInstancing != EArdaGraphicsInstancing::ContiguousInstances)
				{
					return false;
				}
			}
			for (const auto& Access : Desc.mAccesses)
			{
				if (Access.mAccess == EArdaDependencyAccess::Read)
				{
					continue;
				}
				const bool bColor = Access.mState == EArdaRHIResourceState::RenderTarget &&
				    eastl::find(Desc.mColorTargets.begin(), Desc.mColorTargets.end(), Access.mResource) !=
				        Desc.mColorTargets.end();
				const bool bDepth =
				    Access.mState == EArdaRHIResourceState::DepthWrite && Desc.mDepthTarget == Access.mResource;
				if ((!bColor && !bDepth) || Access.mAccess == EArdaDependencyAccess::AutoDeduction)
				{
					return false;
				}
			}
			for (const auto& Bindings : Desc.mShaderBindings)
			{
				for (const auto& Value : Bindings.mValues)
				{
					// Descriptor indices are patched per node. Equal declarations do not establish equal bytes.
					if (!Value.mDescriptorIndices.empty())
					{
						return false;
					}
				}
			}
			return true;
		}
	}

	FArdaRHIStatus FArdaMergeDrawInstancesRule::Compile(const FArdaInductorGraph& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		const auto& State = FArdaInductorGraphAccess::State(Graph);
		if (auto Status = inductor_detail::ValidateNodeOrder(State, Result); !Status)
		{
			return Status;
		}
		Result.mInstancedDraws.clear();
		eastl::unordered_set<uint32_t> Batched;
		for (const auto& Batch : Result.mPipelineBatches)
		{
			eastl::vector<FArdaGraphNodeHandle> Candidates;
			const auto Flush = [&]
			{
				if (Candidates.size() > 1)
				{
					Result.mInstancedDraws.push_back(eastl::move(Candidates));
				}
				Candidates.clear();
			};
			for (auto Handle : Batch)
			{
				const auto* Record = State.mTopology.TryGetNode(Handle);
				if (!Record || !Batched.insert(Handle.GetIndex()).second)
				{
					return inductor_detail::Error(
					    "Instancing requires pipeline batches without stale or duplicate nodes.");
				}
				const auto& Node = Record->mPayload;
				if (!CanInstance(State, Node))
				{
					Flush();
					continue;
				}
				if (!Candidates.empty() &&
				    !SameBindings(State.mCompile.mNodes.at(Candidates.back()).mDesc, State.GetNodeDescription(Node)))
				{
					Flush();
				}
				Candidates.push_back(Handle);
			}
			Flush();
		}
		for (auto Handle : Result.mExecutionOrder)
		{
			if (!inductor_detail::IsDeclaration(State, State.mTopology.TryGetNode(Handle)->mPayload) &&
			    !Batched.count(Handle.GetIndex()))
			{
				return inductor_detail::Error("Batch pipelines before merging draw instances.");
			}
		}
		return {};
	}
}
