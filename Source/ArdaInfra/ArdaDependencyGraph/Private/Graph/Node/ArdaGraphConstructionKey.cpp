#include "Graph/Node/ArdaGraphSemanticFields.h"
#include "Graph/Node/ArdaGraphConstructionKey.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaDependencyKey.h"
#include <EASTL/sort.h>

namespace arda
{
	namespace
	{
		thread_local const FArdaDependencyGraph::FArdaImpl* ConstructionGraph = nullptr;

		struct FArdaConstructionKeyFields
		{
			FArdaDependencyKeyBuilder& mKey;

			template <class... T>
			void Fields(const T&... Values) const
			{
				(Append(Values), ...);
			}

			void Append(const eastl::string& Value) const
			{
				mKey.String(Value);
			}

			template <class T>
			void Append(T Value) const
			{
				mKey.Value(Value);
			}
		};

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
			InductorOptionFields(FArdaConstructionKeyFields{Key}, Options);
		}
	}

	FArdaConstructionKeyScope::FArdaConstructionKeyScope(const FArdaDependencyGraph::FArdaImpl& Graph) noexcept
	    : mPrevious(ConstructionGraph)
	{
		ConstructionGraph = &Graph;
	}

	FArdaConstructionKeyScope::~FArdaConstructionKeyScope()
	{
		ConstructionGraph = mPrevious;
	}

	FArdaDependencyKeyBuilder& FArdaDependencyKeyBuilder::Resource(FArdaDependencyResourceHandle Handle)
	{
		if (ConstructionGraph)
		{
			Handle = ConstructionGraph->ResolveResource(Handle);
			const bool bValid = Handle.mGraph == ConstructionGraph->mIdentity && ConstructionGraph->HasResource(Handle);
			Value(bValid);
			if (bValid)
			{
				return Value(uint64_t{0}).Value(Handle.mIndex).Value(uint64_t{0});
			}
		}
		return Value(Handle.mGraph).Value(Handle.mIndex).Value(Handle.mGeneration);
	}

	FArdaDependencyKeyBuilder& FArdaDependencyKeyBuilder::Stage(FArdaGraphStageValue Input)
	{
		auto Handle = Input.GetProducer();
		Value(Input.GetKind());
		if (ConstructionGraph)
		{
			Handle = ConstructionGraph->ResolveNode(Handle);
			const bool bValid = ConstructionGraph->mTopology.ContainsNode(Handle);
			Value(bValid);
			if (bValid)
			{
				return Value(uint64_t{0}).Value(Handle.GetIndex()).Value(uint32_t{0});
			}
		}
		return Value(Handle.GetGraphIdentity()).Value(Handle.GetIndex()).Value(Handle.GetGeneration());
	}

	eastl::string BuildArdaGraphConstructionKey(const FArdaDependencyGraph::FArdaImpl& Graph)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Value(uint32_t{1});
		AppendOptions(Key, Graph.mOptions);
		eastl::vector<eastl::string> ExternalNames;
		for (const auto& Entry : Graph.mExternalValues)
		{
			ExternalNames.push_back(Entry.first);
		}
		eastl::sort(ExternalNames.begin(), ExternalNames.end());
		Key.Value(static_cast<uint64_t>(ExternalNames.size()));
		for (const auto& Name : ExternalNames)
		{
			const auto& Binding = Graph.mExternalValues.at(Name);
			Key.String(Name).String(Binding.mTypeName).Value(reinterpret_cast<uintptr_t>(Binding.mValue.get()));
		}
		Key.Value(static_cast<uint64_t>(Graph.mResources.size()));
		for (const auto& Resource : Graph.mResources)
		{
			Key.String(Resource.mName)
			    .Value(Resource.mbTexture)
			    .Value(Resource.mbPersistent)
			    .Value(Resource.mbOutput)
			    .Value(Resource.mbRevisionOrdered);
			BufferDescriptorFields(FArdaConstructionKeyFields{Key}, Resource.mBuffer);
			Key.Bytes(Resource.mBufferInitializer.GetData(), Resource.mBufferInitializer.GetSize());
			TextureDescriptorFields(FArdaConstructionKeyFields{Key}, Resource.mTexture);
			// Imported storage is retained by the cached revision, preventing identity reuse.
			Key.Value(reinterpret_cast<uintptr_t>(Resource.mExternalBuffer.Get()))
			    .Value(reinterpret_cast<uintptr_t>(Resource.mExternalTexture.Get()))
			    .Value(reinterpret_cast<uintptr_t>(Resource.mExternalAccelerationStructure.Get()));
		}
		Key.Value(static_cast<uint64_t>(Graph.mCommittedEdits.size()));
		for (const auto& Edit : Graph.mCommittedEdits)
		{
			Key.Value(static_cast<uint64_t>(Edit.mNodes.size()));
			for (const auto Node : Edit.mNodes)
			{
				Key.Value(Graph.ResolveNode(Node).GetIndex());
			}
			Key.Value(static_cast<uint64_t>(Edit.mResources.size()));
			for (const auto& Revision : Edit.mResources)
			{
				Key.Value(Graph.ResolveResource(Revision.mResource).mIndex).Value(Revision.mRevision);
			}
		}
		// Explicit extensions retain persistent contents from their source revision. They must
		// not select an otherwise identical revision backed by a different native allocation.
		for (size_t Index = 0; Index < Graph.mResources.size(); ++Index)
		{
			const auto Buffer = Index < Graph.mCompile.mExecution->mPersistentBuffers.size()
			    ? Graph.mCompile.mExecution->mPersistentBuffers[Index].Get()
			    : nullptr;
			const auto Texture = Index < Graph.mCompile.mExecution->mPersistentTextures.size()
			    ? Graph.mCompile.mExecution->mPersistentTextures[Index].Get()
			    : nullptr;
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
			    .Value(Node.mbStageMaterialized)
			    .Value(reinterpret_cast<uintptr_t>(Node.mDefinition.get()))
			    .String(Node.mDefinition->mName)
			    .Value(Node.mDefinition->mVersion)
			    .Value(Node.mDefinition->mKind);
			Key.Value(static_cast<uint64_t>(Node.mOutputs.size()));
			for (const auto& Output : Node.mOutputs)
			{
				Key.String(Output.mName)
				    .Value(Output.mResource.mIndex)
				    .Value(Output.mRequiredBufferUsage)
				    .Value(Output.mRequiredTextureUsage);
			}
			const auto& Desc = Graph.GetNodeDescription(Node);
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
