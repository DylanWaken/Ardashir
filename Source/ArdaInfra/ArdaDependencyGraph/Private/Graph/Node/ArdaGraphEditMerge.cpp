#include "Graph/Node/ArdaGraphEdit.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaDependencyGraphStatus.h"
#include "Graph/Node/ArdaGraphConstructionKey.h"
#include "Graph/Resources/ArdaGraphResourceIdentity.h"

namespace arda
{
	FArdaRHIStatus FArdaDependencyGraph::MergeGraphEdit(FArdaGraphEdit& Edit, FArdaDependencyGraph& Destination)
	{
		const auto& Source = *Edit.mRecordingGraph->mImpl;
		const auto& Base = *Edit.mBase;
		auto& Combined = *Destination.mImpl;
		eastl::vector<FArdaGraphNodeHandle> Added;
		eastl::unordered_map<FArdaDependencyResourceHandle, eastl::string, FArdaDependencyResourceHandleHash>
		    OutputNames;
		// Rebase generated names in recording order, not node/resource storage order. This gives
		// identical scripts the same names whether workers opened before or after prior commits.
		eastl::vector<eastl::string> Generated;
		for (const auto& Name : Source.mGeneratedNames)
		{
			if (!Base.mGeneratedNames.count(Name))
			{
				Generated.push_back(Name);
			}
		}
		const auto NameSequence = [](const eastl::string& Name)
		{
			uint64_t Sequence = 0;
			for (size_t Index = Name.find_last_of('#') + 1; Index < Name.size(); ++Index)
			{
				Sequence = Sequence * 10 + uint64_t(Name[Index] - '0');
			}
			return Sequence;
		};
		eastl::sort(Generated.begin(),
		    Generated.end(),
		    [&](const auto& Left, const auto& Right)
		    {
			    return NameSequence(Left) < NameSequence(Right);
		    });
		eastl::unordered_map<eastl::string, eastl::string> Names;
		for (const auto& Original : Generated)
		{
			const auto Prefix = Original.substr(0, Original.find_last_of('#'));
			eastl::string Name;
			do
			{
				Name = Prefix + "#" + eastl::to_string(Combined.mNextOperationName++);
			} while (Combined.mNames.count(Name) || Combined.mResourceNames.count(Name) ||
			    Combined.mGeneratedNames.count(Name));
			Combined.mGeneratedNames.insert(Name);
			Names.emplace(Original, eastl::move(Name));
		}
		for (const auto Handle : Source.mTopology.GetNodes())
		{
			if (Base.mTopology.ContainsNode(Handle))
			{
				if (Source.mTopology.TryGetNode(Handle)->mPayload.mbStageMaterialized)
				{
					Combined.mTopology.TryGetNode(Combined.ResolveNode(Handle))->mPayload.mbStageMaterialized = true;
				}
				continue;
			}
			auto Node = Source.mTopology.TryGetNode(Handle)->mPayload;
			if (Source.mGeneratedNames.count(Node.mName))
			{
				Node.mName = Names.at(Node.mName);
				Combined.mGeneratedNames.insert(Node.mName);
			}
			else if (Combined.mNames.count(Node.mName))
			{
				return GraphInvalid(
				    ("Concurrent edits declared the same explicit operation name: " + Node.mName).c_str());
			}
			Node.mAttachmentOrder = Combined.mNextAttachmentOrder++;
			const auto Inserted = Combined.mTopology.AddNode(eastl::move(Node));
			if (!Inserted)
			{
				return GraphInvalid("Cannot allocate a committed graph node.");
			}
			const auto& Name = Combined.mTopology.TryGetNode(Inserted)->mPayload.mName;
			Combined.mTopology.TryGetNode(Inserted)->mPayload.mHandle = Inserted;
			Combined.mNames.emplace(Name, Inserted);
			Combined.mNodeRemaps[Handle] = Inserted;
			const auto Prepared = Source.mNodePreparations.find(Handle);
			if (Prepared != Source.mNodePreparations.end())
			{
				Combined.mNodePreparations[Inserted] = Prepared->second;
			}
			Added.push_back(Inserted);
			for (const auto& Output : Combined.mTopology.TryGetNode(Inserted)->mPayload.mOutputs)
			{
				OutputNames.emplace(Output.mResource, "Node output " + Name + "/" + Output.mName + "/");
			}
		}
		for (size_t Index = 0; Index < Source.mResources.size(); ++Index)
		{
			if (Index < Base.mResources.size())
			{
				// Keep is monotonic: independent edits cannot erase another edit's outputs.
				Combined.mResources[Index].mbOutput |= Source.mResources[Index].mbOutput;
				Combined.mResources[Index].mbRevisionOrdered |= Source.mResources[Index].mbRevisionOrdered;
				continue;
			}
			auto Resource = Source.mResources[Index];
			const FArdaDependencyResourceHandle Original{Source.mIdentity,
			    uint32_t(Index),
			    Source.mResourceGenerations[Index]};
			const auto PhysicalIdentity = GetArdaGraphPhysicalIdentity(Resource);
			if (Resource.mbRevisionOrdered && PhysicalIdentity)
			{
				const auto Existing = eastl::find_if(Combined.mResources.begin(),
				    Combined.mResources.end(),
				    [&](const auto& Candidate)
				    {
					    return Candidate.mExternalKind == Resource.mExternalKind &&
					        GetArdaGraphPhysicalIdentity(Candidate) == PhysicalIdentity;
				    });
				if (Existing != Combined.mResources.end())
				{
					const auto CanonicalIndex = uint32_t(Existing - Combined.mResources.begin());
					Existing->mbRevisionOrdered = true;
					Existing->mbOutput |= Resource.mbOutput;
					Combined.mResourceRemaps[Original] = {Combined.mIdentity,
					    CanonicalIndex,
					    Combined.mResourceGenerations[CanonicalIndex]};
					continue;
				}
			}
			if (Source.mGeneratedNames.count(Resource.mName))
			{
				Resource.mName = Names.at(Resource.mName);
				Combined.mGeneratedNames.insert(Resource.mName);
			}
			else if (Resource.mName.find("Node output ") == 0)
			{
				const auto Output = OutputNames.find(Original);
				if (Output != OutputNames.end())
				{
					Resource.mName = Output->second + eastl::to_string(Combined.mResources.size());
				}
			}
			auto Created = Destination.CreateResource(eastl::move(Resource));
			if (!Created)
			{
				return Created.mStatus;
			}
			Combined.mResourceRemaps[Original] = Created.mValue;
		}
		for (const auto Handle : Added)
		{
			auto& Node = Combined.mTopology.TryGetNode(Handle)->mPayload;
			Combined.NormalizeDescription(Combined.GetNodeDescription(Node));
			for (auto& Output : Node.mOutputs)
			{
				Output.mResource = Combined.ResolveResource(Output.mResource);
			}
		}
		for (size_t Index = Base.mManualEdges.size(); Index < Source.mManualEdges.size(); ++Index)
		{
			const auto& Edge = Source.mManualEdges[Index];
			if (auto Status =
			        Destination.AddDependency(Combined.ResolveNode(Edge.first), Combined.ResolveNode(Edge.second));
			    !Status)
			{
				return Status;
			}
		}
		for (const auto& Layout : Source.mCompile.mExecution->mAutomaticLayouts)
		{
			if (eastl::find(Combined.mCompile.mExecution->mAutomaticLayouts.begin(),
			        Combined.mCompile.mExecution->mAutomaticLayouts.end(),
			        Layout) == Combined.mCompile.mExecution->mAutomaticLayouts.end())
			{
				Combined.mCompile.mExecution->mAutomaticLayouts.push_back(Layout);
			}
		}
		if (Edit.mbOptionsChanged)
		{
			Combined.mOptions = Source.mOptions;
		}
		Combined.mNextOperationName = eastl::max(Combined.mNextOperationName, Source.mNextOperationName);
		return {};
	}

	void FArdaDependencyGraph::RefreshConstructionKeys(FArdaImpl& Graph)
	{
		FArdaConstructionKeyScope Scope(Graph);
		for (const auto Handle : Graph.mTopology.GetNodes())
		{
			auto& Node = Graph.mTopology.TryGetNode(Handle)->mPayload;
			if (Node.mbUnboundExternalParameters)
			{
				Node.mConstructionKey = Node.mArchivedParameters;
			}
			else if (Node.mSourceParameters)
			{
				Node.mConstructionKey = Node.mDefinition->mCanonicalKey(Node.mSourceParameters.get());
			}
		}
	}
}
