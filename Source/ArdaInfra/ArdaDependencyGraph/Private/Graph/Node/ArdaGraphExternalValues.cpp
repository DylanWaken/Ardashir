#include "Graph/Node/ArdaGraphExternalValues.h"
#include <EASTL/sort.h>

namespace arda
{
	namespace
	{
		FArdaRHIStatus InvalidExternal(const eastl::string& Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message.c_str());
		}

		void ConfigureHandles(FArdaGraphArchiveWriter& Archive, const FArdaDependencyGraph::FArdaImpl& Graph)
		{
			Archive.mResourceIndex = [&Graph, &Archive](FArdaDependencyResourceHandle Handle)
			{
				if (!Graph.HasResource(Handle))
				{
					Archive.Fail("External parameter contains a stale resource handle.");
					return UINT32_MAX;
				}
				return Graph.ResolveResource(Handle).mIndex;
			};
			Archive.mNodeIndex = [&Graph, &Archive](FArdaGraphNodeHandle Handle)
			{
				Handle = Graph.ResolveNode(Handle);
				for (uint32_t Index = 0; Index < Graph.mArchiveNodes.size(); ++Index)
				{
					if (Graph.ResolveNode(Graph.mArchiveNodes[Index]) == Handle)
					{
						return Index;
					}
				}
				Archive.Fail("External parameter contains a stale node handle.");
				return UINT32_MAX;
			};
		}
	}

	FArdaRHIStatus ValidateArdaGraphExternalBindings(const FArdaDependencyGraph::FArdaImpl& Graph)
	{
		for (const auto& Resource : Graph.mResources)
		{
			if (Resource.IsExternal() && !Resource.HasExternalBinding())
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				    (eastl::string("External resource slot '") + Resource.mName +
				        "' requires a binding before compilation.")
				        .c_str());
			}
		}
		for (const auto& Entry : Graph.mExternalValues)
		{
			if (!Entry.second.mValue)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				    (eastl::string("External value slot '") + Entry.first + "' requires a binding before compilation.")
				        .c_str());
			}
		}
		return {};
	}

	void ConfigureArdaGraphExternalWriter(FArdaGraphArchiveWriter& Archive,
	    const FArdaDependencyGraph::FArdaImpl& Graph)
	{
		Archive.mExternalReference = [&Graph](const void* Identity, const void* Type) -> TArdaRHIResult<eastl::string>
		{
			for (const auto& Entry : Graph.mExternalValues)
			{
				const auto& Binding = Entry.second;
				if (Binding.mValue && Binding.mIdentity == Identity && Binding.mType == Type)
				{
					return {Entry.first, {}};
				}
			}
			return {{},
			    InvalidExternal(
			        "Caller-owned parameter references must be named with BindExternalValue before serialization.")};
		};
		Archive.mExternalSlot = [&Graph](const eastl::string& Name, const void* Type)
		{
			const auto Found = Graph.mExternalValues.find(Name);
			if (Found == Graph.mExternalValues.end())
			{
				return InvalidExternal("External value slot '" + Name + "' is not declared.");
			}
			if (Found->second.mType && Found->second.mType != Type)
			{
				return InvalidExternal("External value slot '" + Name + "' has a different parameter type.");
			}
			return FArdaRHIStatus{};
		};
	}

	void ConfigureArdaGraphExternalReader(FArdaGraphArchiveReader& Archive, FArdaDependencyGraph::FArdaImpl& Graph)
	{
		Archive.mExternalReference = [&Graph](const eastl::string& Name,
		                                 const void* Type) -> TArdaRHIResult<eastl::shared_ptr<const void>>
		{
			const auto Found = Graph.mExternalValues.find(Name);
			if (Found == Graph.mExternalValues.end())
			{
				return {{}, InvalidExternal("Archive references undeclared external value slot '" + Name + "'.")};
			}
			auto& Binding = Found->second;
			if (Binding.mType && Binding.mType != Type)
			{
				return {{}, InvalidExternal("External value slot '" + Name + "' has incompatible parameter types.")};
			}
			Binding.mType = Type;
			return {Binding.mValue, {}};
		};
	}

	FArdaRHIStatus WriteArdaGraphExternalValues(FArdaGraphArchiveWriter& Archive,
	    const FArdaDependencyGraph::FArdaImpl& Graph)
	{
		eastl::vector<eastl::string> Names;
		for (const auto& Entry : Graph.mExternalValues)
		{
			Names.push_back(Entry.first);
		}
		eastl::sort(Names.begin(), Names.end());
		Archive.WriteInteger(Names.size(), 8);
		for (const auto& Name : Names)
		{
			Archive.Fields(Name, Graph.mExternalValues.at(Name).mTypeName);
		}
		ConfigureArdaGraphExternalWriter(Archive, Graph);
		return Archive.GetStatus();
	}

	FArdaRHIStatus ReadArdaGraphExternalValues(FArdaGraphArchiveReader& Archive, FArdaDependencyGraph::FArdaImpl& Graph)
	{
		const auto Count = Archive.ReadCount();
		for (size_t Index = 0; Index < Count && Archive.GetStatus(); ++Index)
		{
			FArdaGraphExternalValueBinding Binding;
			Archive.Fields(Binding.mName, Binding.mTypeName);
			if (Binding.mName.empty() || Binding.mTypeName.empty() ||
			    !Graph.mExternalValues.emplace(Binding.mName, Binding).second)
			{
				Archive.Fail("External value slots require unique nonempty names and portable type names.");
			}
		}
		ConfigureArdaGraphExternalReader(Archive, Graph);
		return Archive.GetStatus();
	}

	FArdaRHIStatus CaptureArdaGraphExternalParameters(FArdaDependencyGraph::FArdaImpl& Graph)
	{
		FArdaRHIStatus Result;
		for (auto Handle : Graph.mTopology.GetNodes())
		{
			if (eastl::find(Graph.mArchiveNodes.begin(), Graph.mArchiveNodes.end(), Handle) ==
			    Graph.mArchiveNodes.end())
			{
				Graph.mArchiveNodes.push_back(Handle);
			}
		}
		const auto Capture = [&](auto& Node, const eastl::shared_ptr<const void>& Parameters)
		{
			if (!Node.mDefinition || !Node.mDefinition->mSerializeParameters || !Parameters)
			{
				if (Result)
				{
					Result =
					    InvalidExternal("Rebinding contextual parameters requires a portable node parameter schema.");
				}
				return;
			}
			FArdaGraphArchiveWriter Archive;
			ConfigureHandles(Archive, Graph);
			ConfigureArdaGraphExternalWriter(Archive, Graph);
			if (!Node.mExternalParameterNames.empty())
			{
				Archive.mExternalNames = &Node.mExternalParameterNames;
			}
			// Unregistered application codecs/references may be supplied later. Do not discard a valid older payload.
			auto Status = Node.mDefinition->mSerializeParameters(Parameters.get(), Archive);
			if (Status && Archive.GetStatus())
			{
				Node.mArchivedParameters = Archive.GetData();
			}
			else if (Result)
			{
				Result = Status ? Archive.GetStatus() : Status;
				Result.mMessage = "Cannot rebind node '" + Node.mName + "': " + Result.mMessage;
			}
		};
		for (auto Handle : Graph.mTopology.GetNodes())
		{
			auto& Node = Graph.mTopology.TryGetNode(Handle)->mPayload;
			Capture(Node, Node.mSourceParameters);
		}
		for (auto& Admission : Graph.mSubgraphAdmissions)
		{
			Capture(Admission, Admission.mParameters);
		}
		return Result;
	}

	FArdaRHIStatus RebindArdaGraphExternalParameters(FArdaDependencyGraph::FArdaImpl& Graph)
	{
		eastl::vector<FArdaDependencyResourceHandle> Resources;
		eastl::vector<EArdaGraphResourceKind> Kinds;
		for (uint32_t Index = 0; Index < Graph.mResources.size(); ++Index)
		{
			Resources.push_back({Graph.mIdentity, Index, Graph.mResourceGenerations[Index]});
			const auto& Resource = Graph.mResources[Index];
			Kinds.push_back(Resource.mExternalKind == EArdaGraphExternalResourceKind::AccelerationStructure
			        ? EArdaGraphResourceKind::AccelerationStructure
			        : Resource.mbTexture ? EArdaGraphResourceKind::Texture
			                             : EArdaGraphResourceKind::Buffer);
		}
		const auto Rebind = [&](auto& Node, eastl::shared_ptr<const void>& Parameters) -> FArdaRHIStatus
		{
			if (Node.mArchivedParameters.empty() || !Node.mDefinition || !Node.mDefinition->mDeserializeParameters)
			{
				return {};
			}
			FArdaGraphArchiveReader Archive(Node.mArchivedParameters);
			Archive.mResources = &Resources;
			Archive.mResourceKinds = &Kinds;
			Archive.mNodes = &Graph.mArchiveNodes;
			eastl::vector<eastl::string> Names;
			Archive.mExternalNames = &Names;
			ConfigureArdaGraphExternalReader(Archive, Graph);
			auto Decoded = Node.mDefinition->mDeserializeParameters(Archive);
			if (!Decoded)
			{
				return Decoded.mStatus;
			}
			if (auto Status = Archive.Finish(); !Status)
			{
				return Status;
			}
			Parameters = eastl::move(Decoded.mValue);
			Node.mExternalParameterNames = eastl::move(Names);
			Node.mbUnboundExternalParameters = Archive.mbHasUnboundExternalReferences;
			return {};
		};
		for (auto Handle : Graph.mTopology.GetNodes())
		{
			auto& Node = Graph.mTopology.TryGetNode(Handle)->mPayload;
			if (auto Status = Rebind(Node, Node.mSourceParameters); !Status)
			{
				return Status;
			}
			if (!Node.mbUnboundExternalParameters && Node.mDefinition && Node.mDefinition->mCanonicalKey)
			{
				Node.mCanonicalKey = Node.mDefinition->mCanonicalKey(Node.mSourceParameters.get());
				Node.mConstructionKey.clear();
			}
			Graph.mNodePreparations.erase(Handle);
		}
		for (auto& Admission : Graph.mSubgraphAdmissions)
		{
			if (auto Status = Rebind(Admission, Admission.mParameters); !Status)
			{
				return Status;
			}
		}
		return {};
	}
}
