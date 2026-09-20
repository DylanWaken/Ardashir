#include "Graph/Node/ArdaGraphConstructionKey.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaDependencyKey.h"
#include <EASTL/sort.h>

namespace arda
{
	namespace
	{
		void AppendBuffer(FArdaDependencyKeyBuilder& Key, const FArdaRHIBufferDesc& Buffer)
		{
			Key.Value(Buffer.mByteSize)
			    .Value(Buffer.mStructureStride)
			    .Value(Buffer.mMaxVersions)
			    .Value(Buffer.mFormat)
			    .Value(Buffer.mUsage)
			    .Value(Buffer.mCpuAccess)
			    .Value(Buffer.mInitialState)
			    .Value(Buffer.mbKeepInitialState)
			    .Value(Buffer.mbVirtual)
			    .Value(Buffer.mbTiled)
			    .String(Buffer.mDebugName)
			    .Value(Buffer.mbCudaInterop);
		}

		void AppendTexture(FArdaDependencyKeyBuilder& Key, const FArdaRHITextureDesc& Texture)
		{
			Key.Value(Texture.mWidth)
			    .Value(Texture.mHeight)
			    .Value(Texture.mDepth)
			    .Value(Texture.mArraySize)
			    .Value(Texture.mMipLevels)
			    .Value(Texture.mSampleCount)
			    .Value(Texture.mFormat)
			    .Value(Texture.mDimension)
			    .Value(Texture.mUsage)
			    .Value(Texture.mInitialState)
			    .Value(Texture.mbKeepInitialState)
			    .Value(Texture.mbVirtual)
			    .Value(Texture.mbTiled)
			    .Value(Texture.mClearValue.mR)
			    .Value(Texture.mClearValue.mG)
			    .Value(Texture.mClearValue.mB)
			    .Value(Texture.mClearValue.mA)
			    .Value(Texture.mbUseClearValue)
			    .String(Texture.mDebugName)
			    .Value(Texture.mbCudaInterop);
		}

		void AppendOptions(FArdaDependencyKeyBuilder& Key, const FArdaInductorOptions& Options)
		{

			const auto AppendRules = [&](const auto& Rules)
			{
				Key.Value(static_cast<uint64_t>(Rules.size()));
				for (const auto& Rule : Rules)
				{
					Key.Value(reinterpret_cast<uintptr_t>(Rule.get()));
				}
			};
			AppendRules(Options.mRules.mPreprocessRules);
			AppendRules(Options.mRules.mCompileRules);
			Key.Value(Options.mObjective)
			    .Value(Options.mSearchMode)
			    .Value(Options.mMaxVramBytes)
			    .Value(Options.mMinimumAsyncChain)
			    .Value(Options.mMinimumAsyncSlack)
			    .Value(Options.mMaxSearchStates)
			    .Value(Options.mFramesInFlight)
			    .Value(Options.mCudaGraphMode)
			    .Value(Options.mCudaHandoffCost)
			    .Value(Options.mAliasSubmissionCost)
			    .Value(Options.mbEnableGpuTiming)
			    .Value(Options.mGpuTimingEmaAlpha)
			    .Value(Options.mGpuTimingSampleInterval)
			    .Value(Options.mGpuTimingHistoryCapacity)
			    .Value(Options.mbEnableAdaptiveScheduling)
			    .Value(Options.mAdaptiveSchedulingInterval)
			    .Value(Options.mAdaptiveSchedulingMinSamples)
			    .Value(Options.mAdaptiveSchedulingSearchBudget)
			    .Value(Options.mAdaptiveSchedulingMinImprovement)
			    .Value(Options.mbEnableCopyQueue)
			    .Value(Options.mbEnableAsyncCompute);
		}
	}

	eastl::string BuildArdaGraphConstructionKey(const FArdaDependencyGraph::FArdaImpl& Graph)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Value(uint32_t{1});
		AppendOptions(Key, Graph.mOptions);
		Key.Value(static_cast<uint64_t>(Graph.mResources.size()));
		for (const auto& Resource : Graph.mResources)
		{
			Key.String(Resource.mName).Value(Resource.mbTexture).Value(Resource.mbPersistent).Value(Resource.mbOutput);
			AppendBuffer(Key, Resource.mBuffer);
			AppendTexture(Key, Resource.mTexture);
			// Imported storage is retained by the cached revision, preventing identity reuse.
			Key.Value(reinterpret_cast<uintptr_t>(Resource.mExternalBuffer.Get()))
			    .Value(reinterpret_cast<uintptr_t>(Resource.mExternalTexture.Get()))
			    .Value(reinterpret_cast<uintptr_t>(Resource.mExternalAccelerationStructure.Get()));
		}
		// Explicit extensions retain persistent contents from their source revision. They must
		// not select an otherwise identical revision backed by a different native allocation.
		for (size_t Index = 0; Index < Graph.mResources.size(); ++Index)
		{
			const auto Buffer =
			    Index < Graph.mPersistentBuffers.size() ? Graph.mPersistentBuffers[Index].Get() : nullptr;
			const auto Texture =
			    Index < Graph.mPersistentTextures.size() ? Graph.mPersistentTextures[Index].Get() : nullptr;
			Key.Value(reinterpret_cast<uintptr_t>(Buffer)).Value(reinterpret_cast<uintptr_t>(Texture));
		}
		auto Nodes = Graph.mTopology.GetNodes();
		eastl::sort(Nodes.begin(),
		    Nodes.end(),
		    [&](auto Left, auto Right)
		    {
			    return Graph.mTopology.TryGetNode(Left)->mPayload.mAttachmentOrder <
			        Graph.mTopology.TryGetNode(Right)->mPayload.mAttachmentOrder;
		    });
		Key.Value(static_cast<uint64_t>(Nodes.size()));
		for (const auto Handle : Nodes)
		{
			const auto& Node = Graph.mTopology.TryGetNode(Handle)->mPayload;
			// Traversal records relative attachment order without monotonic frame counters.
			Key.Value(Handle.GetIndex())
			    .String(Node.mName)
			    .String(Node.mConstructionKey)
			    .Value(reinterpret_cast<uintptr_t>(Node.mDefinition.get()))
			    .String(Node.mDefinition->mName)
			    .Value(Node.mDefinition->mVersion)
			    .Value(Node.mDefinition->mKind);
			Key.Value(static_cast<uint64_t>(Node.mOutputs.size()));
			for (const auto& Output : Node.mOutputs)
			{
				Key.String(Output.mName).Value(Output.mResource.mIndex);
			}
			const auto& Desc = Node.mDesc;
			Key.Value(Desc.mEstimatedCost)
			    .Value(Desc.mWorkspaceBytes)
			    .Value(Desc.mTransientWorkspaceBytes)
			    .Value(Desc.mbSideEffect)
			    .Value(Desc.mbPipelineStageOnly)
			    .Value(static_cast<uint64_t>(Desc.mAccesses.size()));
			for (const auto& Access : Desc.mAccesses)
			{
				Key.Value(Access.mResource.mIndex)
				    .Value(Access.mAccess)
				    .Value(Access.mState)
				    .BufferRange(Access.mBufferRange)
				    .TextureRange(Access.mTextureRange);
			}
			Key.Value(static_cast<uint64_t>(Desc.mColorTargets.size()));
			for (const auto Target : Desc.mColorTargets)
			{
				Key.Value(Target.mIndex);
			}
			Key.Value(Desc.mDepthTarget.mIndex);
		}
		Key.Value(static_cast<uint64_t>(Graph.mManualEdges.size()));
		for (const auto& Edge : Graph.mManualEdges)
		{
			Key.Value(Edge.first.GetIndex()).Value(Edge.second.GetIndex());
		}
		return Key.Build();
	}

	uint64_t HashArdaGraphConstructionKey(const eastl::string& Key)
	{
		uint64_t Hash = 14695981039346656037ull;
		for (const auto Byte : Key)
		{
			Hash ^= static_cast<uint8_t>(Byte);
			Hash *= 1099511628211ull;
		}
		return Hash;
	}
}
