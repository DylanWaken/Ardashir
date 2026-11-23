#include "Graph/Node/ArdaGraphSerialization.h"
#include "FileOperations/ArdaFileOperations.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaGraphAccess.h"
#include "Graph/Node/ArdaGraphArchive.h"
#include "Graph/Node/ArdaGraphExternalValues.h"
#include "Graph/Node/ArdaGraphDescriptionArchive.h"
#include "Graph/Node/ArdaGraphClone.h"
#include "Graph/Node/ArdaGraphEditOrdering.h"
#include "Graph/Node/ArdaGraphRevisionState.h"
#include "Graph/Node/ArdaGraphRecordingScope.h"
#include "Graph/Node/ArdaRuntimeNodeDefinition.h"
#include "Inductor/Compiler/ArdaGraphRuleArchive.h"
#include <EASTL/sort.h>

namespace arda
{
	TArdaRHIResult<eastl::string> FArdaDependencyGraph::Serialize() const
	{
		return FArdaGraphSerialization::Serialize(*this);
	}

	TArdaRHIResult<eastl::unique_ptr<FArdaDependencyGraph>> FArdaDependencyGraph::Deserialize(const eastl::string& Data,
	    FArdaRHIDeviceRef Device)
	{
		return FArdaGraphSerialization::Deserialize(Data, eastl::move(Device));
	}

	FArdaRHIStatus FArdaDependencyGraph::Save(const std::filesystem::path& Path) const
	{
		return FArdaGraphSerialization::Save(*this, Path);
	}

	TArdaRHIResult<eastl::unique_ptr<FArdaDependencyGraph>> FArdaDependencyGraph::Load(
	    const std::filesystem::path& Path,
	    FArdaRHIDeviceRef Device)
	{
		return FArdaGraphSerialization::Load(Path, eastl::move(Device));
	}

	namespace
	{
		constexpr uint64_t ArdaGraphArchiveMagic = 0x4850415247414452ull;
		constexpr uint32_t ArdaGraphArchiveVersion = 5;

		FArdaRHIStatus ArchiveInvalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}

	}

	TArdaRHIResult<eastl::string> FArdaGraphSerialization::Serialize(const FArdaDependencyGraph& Graph)
	{
		return Serialize(Graph, nullptr);
	}

	TArdaRHIResult<eastl::string> FArdaGraphSerialization::Serialize(const FArdaDependencyGraph& Graph,
	    eastl::vector<eastl::string>* ReferencedLibraries)
	{
		if (auto Status = CheckArdaGraphRecordingReentry(Graph.mRevisions.get()); !Status)
		{
			return {{}, Status};
		}
		std::lock_guard<std::recursive_mutex> Lock(Graph.mRevisions->mMutex);
		if (Graph.IsEditing())
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Close graph edits before serialization.")};
		}
		if (Graph.mImpl->mbCompiling || Graph.mImpl->mbPreprocessing || Graph.mImpl->mbExecuting)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			        "Serialization is unavailable during preprocessing, compilation or execution.")};
		}
		const auto& Current = Graph.GetReadImpl();
		auto Snapshot = CloneArdaGraphConstruction(
		    !Current.mbPreprocessed && Current.mCompilerSource ? *Current.mCompilerSource : Current,
		    true);
		NormalizeArdaGraphEditRevisions(*Snapshot);
		if (!Snapshot->mbPreprocessed)
		{
			OrderArdaGraphEdits(*Snapshot);
		}
		const auto& Source = *Snapshot;
		FArdaGraphArchiveWriter Archive;
		Archive.Fields(ArdaGraphArchiveMagic, ArdaGraphArchiveVersion);
		Archive.Write(Source.mbPreprocessed);
		WriteArdaGraphOptions(Archive, Source.mOptions);
		// Definitions are stored once per archive, independently of the invocation parameter records.
		eastl::unordered_map<eastl::string, eastl::string> PortableDefinitions;
		for (const auto Handle : Source.mTopology.GetNodes())
		{
			const auto& Definition = Source.mTopology.TryGetNode(Handle)->mPayload.mDefinition;
			if (Definition && !Definition->mPortableDefinition.empty())
			{
				const auto Inserted = PortableDefinitions.emplace(Definition->mName, Definition->mPortableDefinition);
				if (!Inserted.second && Inserted.first->second != Definition->mPortableDefinition)
				{
					return {{}, ArchiveInvalid("Graph contains conflicting portable node definitions.")};
				}
			}
		}
		eastl::vector<eastl::string> DefinitionNames;
		for (const auto& Definition : PortableDefinitions)
		{
			DefinitionNames.push_back(Definition.first);
		}
		eastl::sort(DefinitionNames.begin(), DefinitionNames.end());
		Archive.WriteInteger(DefinitionNames.size(), 8);
		for (const auto& Name : DefinitionNames)
		{
			const auto& Bytes = PortableDefinitions.at(Name);
			Archive.Write(Bytes);
			if (ReferencedLibraries)
			{
				const auto Reference = FArdaRuntimeNodeDefinition::Deserialize(Bytes);
				if (!Reference)
				{
					return {{}, Reference.mStatus};
				}
				const auto& Library = Reference.mValue.mKernel.mLibrary;
				if (eastl::find(ReferencedLibraries->begin(), ReferencedLibraries->end(), Library) ==
				    ReferencedLibraries->end())
				{
					ReferencedLibraries->push_back(Library);
				}
			}
		}
		if (auto Status = WriteArdaGraphExternalValues(Archive, Source); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		Archive.WriteInteger(Source.mResources.size(), 8);
		for (const auto& Resource : Source.mResources)
		{
			Archive.Fields(Resource.mName,
			    Resource.mbTexture,
			    Resource.mbPersistent,
			    Resource.mbOutput,
			    Resource.mExternalKind,
			    Resource.mbRevisionOrdered);
			if (Resource.mExternalKind == EArdaGraphExternalResourceKind::AccelerationStructure)
			{
				Archive.Write(Resource.mAccelerationStructure);
			}
			else if (Resource.mbTexture)
			{
				Archive.Write(Resource.mTexture);
			}
			else
			{
				Archive.Write(Resource.mBuffer);
			}
			Archive.WriteInteger(Resource.mBufferInitializer.GetSize(), 8);
			Archive.WriteBytes(reinterpret_cast<const char*>(Resource.mBufferInitializer.GetData()),
			    Resource.mBufferInitializer.GetSize());
		}
		Archive.WriteInteger(Source.mResources.size(), 8);
		for (uint32_t Index = 0; Index < Source.mResources.size(); ++Index)
		{
			const FArdaDependencyResourceHandle Original{Source.mIdentity, Index, Source.mResourceGenerations[Index]};
			Archive.Write(Source.ResolveResource(Original).mIndex);
		}
		auto Nodes = Source.mTopology.GetNodes();
		eastl::sort(Nodes.begin(),
		    Nodes.end(),
		    [&](auto Left, auto Right)
		    {
			    return Source.mTopology.TryGetNode(Left)->mPayload.mAttachmentOrder <
			        Source.mTopology.TryGetNode(Right)->mPayload.mAttachmentOrder;
		    });
		eastl::unordered_map<FArdaGraphNodeHandle, uint32_t, FArdaGraphNodeHandleHash> NodeIndices;
		for (uint32_t Index = 0; Index < Nodes.size(); ++Index)
		{
			NodeIndices.emplace(Nodes[Index], Index);
		}
		Archive.mResourceIndex = [&](FArdaDependencyResourceHandle Handle)
		{
			if (!Source.HasResource(Handle))
			{
				Archive.Fail("Graph contains a foreign or stale resource parameter.");
				return UINT32_MAX;
			}
			return Source.ResolveResource(Handle).mIndex;
		};
		Archive.mNodeIndex = [&](FArdaGraphNodeHandle Handle)
		{
			const auto Found = NodeIndices.find(Source.ResolveNode(Handle));
			if (Found == NodeIndices.end())
			{
				Archive.Fail("Graph contains a foreign or stale node connection.");
				return UINT32_MAX;
			}
			return Found->second;
		};
		eastl::unordered_map<const FArdaNodeArrayInvocation*, uint32_t> ArrayIndices;
		Archive.WriteInteger(Nodes.size(), 8);
		for (const auto Handle : Nodes)
		{
			const auto& Node = Source.mTopology.TryGetNode(Handle)->mPayload;
			if (!Node.mDefinition || !Node.mDefinition->mSerializeParameters || !Node.mSourceParameters)
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				        "A graph node has no registered public parameter serializer.")};
			}
			Archive.Fields(Node.mName, Node.mDefinition->mName, Node.mDefinition->mVersion);
			FArdaGraphArchiveWriter Parameters;
			Parameters.mResourceIndex = Archive.mResourceIndex;
			Parameters.mNodeIndex = Archive.mNodeIndex;
			Parameters.mExternalReference = Archive.mExternalReference;
			Parameters.mExternalSlot = Archive.mExternalSlot;
			Parameters.mExternalNames = Node.mExternalParameterNames.empty() ? nullptr : &Node.mExternalParameterNames;
			if (auto Status = Node.mDefinition->mSerializeParameters(Node.mSourceParameters.get(), Parameters); !Status)
			{
				Status.mMessage = "Node '" + Node.mName + "': " + Status.mMessage;
				return {{}, eastl::move(Status)};
			}
			Archive.Write(Parameters.GetData());
			Archive.Write(Node.mbStageMaterialized);
			uint32_t Array = UINT32_MAX;
			uint64_t LaneCount = 0;
			if (Node.mArrayInvocation)
			{
				Array = ArrayIndices.emplace(Node.mArrayInvocation.get(), uint32_t(ArrayIndices.size())).first->second;
				LaneCount = Node.mArrayInvocation->mLaneCount;
			}
			Archive.Fields(Array, LaneCount, uint64_t(Node.mArrayLane));
		}
		Archive.WriteInteger(Source.mManualEdges.size(), 8);
		for (const auto& Edge : Source.mManualEdges)
		{
			Archive.Fields(Archive.mNodeIndex(Edge.first), Archive.mNodeIndex(Edge.second));
		}
		{
			for (const auto Handle : Nodes)
			{
				const auto& Node = Source.mTopology.TryGetNode(Handle)->mPayload;
				Archive.Fields(Node.mbGraphicsLowered, Node.mbRayTracingLowered, Node.mbCulled);
				Archive.WriteInteger(Node.mGraphicsStages.size(), 8);
				for (const auto Stage : Node.mGraphicsStages)
				{
					Archive.Write(Archive.mNodeIndex(Stage));
				}
				Archive.WriteInteger(Node.mOutputs.size(), 8);
				for (const auto& Output : Node.mOutputs)
				{
					Archive.Fields(Output.mName,
					    Output.mResource,
					    Output.mRequiredBufferUsage,
					    Output.mRequiredTextureUsage);
				}
				WriteArdaGraphDescription(Archive, Node.mDesc);
				Archive.Write(bool(Node.mStageDeclaration));
				if (Node.mStageDeclaration)
				{
					WriteArdaGraphDescription(Archive, *Node.mStageDeclaration);
				}
			}
			Archive.WriteInteger(Source.mTopology.GetEdges().size(), 8);
			for (const auto Handle : Source.mTopology.GetEdges())
			{
				const auto& Edge = *Source.mTopology.TryGetEdge(Handle);
				Archive.Fields(Archive.mNodeIndex(Edge.mFrom),
				    Archive.mNodeIndex(Edge.mTo),
				    Edge.mPayload.mbResource,
				    Edge.mPayload.mbHazard);
			}
		}
		Archive.WriteInteger(Source.mSubgraphAdmissions.size(), 8);
		for (const auto& Admission : Source.mSubgraphAdmissions)
		{
			if (!Admission.mDefinition || !Admission.mDefinition->mSerializeParameters || !Admission.mParameters)
			{
				return {{}, ArchiveInvalid("Archived subgraph admission lacks its registered parameter schema.")};
			}
			Archive.Fields(Admission.mName, Admission.mDefinition->mName, Admission.mDefinition->mVersion);
			FArdaGraphArchiveWriter Parameters;
			Parameters.mResourceIndex = Archive.mResourceIndex;
			Parameters.mNodeIndex = Archive.mNodeIndex;
			Parameters.mExternalReference = Archive.mExternalReference;
			Parameters.mExternalSlot = Archive.mExternalSlot;
			Parameters.mExternalNames =
			    Admission.mExternalParameterNames.empty() ? nullptr : &Admission.mExternalParameterNames;
			if (auto Status = Admission.mDefinition->mSerializeParameters(Admission.mParameters.get(), Parameters);
			    !Status)
			{
				return {{}, eastl::move(Status)};
			}
			Archive.Write(Parameters.GetData());
			WriteArdaGraphRequirements(Archive, Admission.mRequirements);
		}
		Archive.WriteInteger(Source.mCommittedEdits.size(), 8);
		for (const auto& Edit : Source.mCommittedEdits)
		{
			eastl::vector<uint32_t> LiveNodes;
			for (const auto Node : Edit.mNodes)
			{
				const auto Found = NodeIndices.find(Source.ResolveNode(Node));
				if (Found != NodeIndices.end())
				{
					LiveNodes.push_back(Found->second);
				}
			}
			Archive.WriteInteger(LiveNodes.size(), 8);
			for (const auto Node : LiveNodes)
			{
				Archive.Write(Node);
			}
			Archive.WriteInteger(Edit.mResources.size(), 8);
			for (const auto& Resource : Edit.mResources)
			{
				Archive.Fields(Archive.mResourceIndex(Resource.mResource), Resource.mRevision);
			}
		}
		return Archive.GetStatus() ? TArdaRHIResult<eastl::string>{Archive.GetData(), {}}
		                           : TArdaRHIResult<eastl::string>{{}, Archive.GetStatus()};
	}

	TArdaRHIResult<eastl::unique_ptr<FArdaDependencyGraph>> FArdaGraphSerialization::Deserialize(
	    const eastl::string& Data,
	    FArdaRHIDeviceRef Device)
	{
		FArdaGraphArchiveReader Archive(Data);
		uint64_t Magic = 0;
		uint32_t Version = 0;
		Archive.Fields(Magic, Version);
		if (!Archive.GetStatus())
		{
			return {{}, Archive.GetStatus()};
		}
		if (Magic != ArdaGraphArchiveMagic || Version < 2 || Version > ArdaGraphArchiveVersion)
		{
			return {{}, ArchiveInvalid("Unrecognized graph archive format or version.")};
		}
		bool bPreprocessed = false;
		FArdaInductorOptions Options;
		Archive.Read(bPreprocessed);
		ReadArdaGraphOptions(Archive, Options);
		eastl::vector<eastl::shared_ptr<const FArdaDependencyNodeExecutable>> PortableDefinitions;
		eastl::unordered_map<eastl::string, eastl::shared_ptr<const FArdaDependencyNodeExecutable>> LocalDefinitions;
		const auto DefinitionCount = Version >= 3 ? Archive.ReadCount() : 0;
		for (size_t Index = 0; Index < DefinitionCount && Archive.GetStatus(); ++Index)
		{
			eastl::string Bytes;
			Archive.Read(Bytes);
			if (!Archive.GetStatus())
			{
				return {{}, Archive.GetStatus()};
			}
			const auto Decoded = FArdaRuntimeNodeDefinition::Deserialize(Bytes);
			if (!Decoded)
			{
				return {{}, Decoded.mStatus};
			}
			auto Definition = FArdaRuntimeNodeDefinition::Create(Decoded.mValue);
			if (!Definition)
			{
				return {{}, Definition.mStatus};
			}
			if (auto Existing = FArdaNodeRegistry::Get().Find(Definition.mValue->mName))
			{
				if (Existing->mPortableDefinition != Definition.mValue->mPortableDefinition)
				{
					return {{},
					    ArchiveInvalid("Archive portable definition conflicts with a registered implementation.")};
				}
				Definition.mValue = eastl::move(Existing);
			}
			if (!LocalDefinitions.emplace(Definition.mValue->mName, Definition.mValue).second)
			{
				return {{}, ArchiveInvalid("Archive contains a duplicate portable definition.")};
			}
			PortableDefinitions.push_back(eastl::move(Definition.mValue));
		}
		const auto FindDefinition = [&](const eastl::string& Name)
		{
			const auto Found = LocalDefinitions.find(Name);
			return Found != LocalDefinitions.end() ? Found->second : FArdaNodeRegistry::Get().Find(Name);
		};
		if (!Archive.GetStatus())
		{
			return {{}, Archive.GetStatus()};
		}
		auto Graph = eastl::make_unique<FArdaDependencyGraph>(eastl::move(Device));
		FArdaGraphEdit Edit(*Graph);
		Edit.mbArchiveRestore = true;
		if (!Edit.GetStatus())
		{
			return {{}, Edit.GetStatus()};
		}
		auto& Recording = FArdaGraphAccess::GetGraph(Edit);
		if (auto Status = Edit.SetOptions(Options); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		if (auto Status = ReadArdaGraphExternalValues(Archive, *Recording.mImpl); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		eastl::vector<FArdaDependencyResourceHandle> Resources;
		eastl::vector<EArdaGraphResourceKind> ResourceKinds;
		eastl::vector<FArdaGraphNodeHandle> Nodes;
		Archive.mResources = &Resources;
		Archive.mResourceKinds = &ResourceKinds;
		Archive.mNodes = &Nodes;
		const auto ResourceCount = Archive.ReadCount();
		for (size_t Index = 0; Index < ResourceCount && Archive.GetStatus(); ++Index)
		{
			FArdaDependencyResourceDesc Resource;
			Archive.Fields(Resource.mName,
			    Resource.mbTexture,
			    Resource.mbPersistent,
			    Resource.mbOutput,
			    Resource.mExternalKind);
			if (Version >= 4)
			{
				Archive.Read(Resource.mbRevisionOrdered);
			}
			if (Resource.mExternalKind == EArdaGraphExternalResourceKind::AccelerationStructure)
			{
				Archive.Read(Resource.mAccelerationStructure);
			}
			else if (Resource.mbTexture)
			{
				Archive.Read(Resource.mTexture);
			}
			else
			{
				Archive.Read(Resource.mBuffer);
			}
			if (Version >= 3)
			{
				const uint64_t ByteCount = Archive.ReadInteger(8);
				if (ByteCount > FArdaGraphArchiveWriter::mMaximumBytes)
				{
					Archive.Fail("Buffer initializer exceeds the graph archive byte limit.");
				}
				else
				{
					eastl::string Bytes;
					Archive.ReadBytes(Bytes, static_cast<size_t>(ByteCount));
					if (Archive.GetStatus() && !Bytes.empty())
					{
						Resource.mBufferInitializer =
						    FArdaGraphBufferInitializer(eastl::vector<uint8_t>(Bytes.begin(), Bytes.end()));
					}
				}
			}
			if (!Archive.GetStatus())
			{
				return {{}, Archive.GetStatus()};
			}
			ResourceKinds.push_back(Resource.mExternalKind == EArdaGraphExternalResourceKind::AccelerationStructure
			        ? EArdaGraphResourceKind::AccelerationStructure
			        : Resource.mbTexture ? EArdaGraphResourceKind::Texture
			                             : EArdaGraphResourceKind::Buffer);
			auto Result = Recording.CreateResource(eastl::move(Resource));
			if (!Result)
			{
				return {{}, eastl::move(Result.mStatus)};
			}
			Resources.push_back(Result.mValue);
		}
		const auto AliasCount = Archive.ReadCount();
		if (AliasCount != Resources.size())
		{
			return {{}, ArchiveInvalid("Graph archive resource alias table has the wrong size.")};
		}
		eastl::vector<uint32_t> Aliases;
		for (size_t Index = 0; Index < AliasCount && Archive.GetStatus(); ++Index)
		{
			uint32_t Target = UINT32_MAX;
			Archive.Read(Target);
			if (Target >= Resources.size() || ResourceKinds[Index] != ResourceKinds[Target])
			{
				return {{}, ArchiveInvalid("Graph archive contains an invalid resource alias target.")};
			}
			Aliases.push_back(Target);
		}
		if (!Archive.GetStatus())
		{
			return {{}, Archive.GetStatus()};
		}
		for (size_t Index = 0; Index < AliasCount; ++Index)
		{
			if (Aliases[Aliases[Index]] != Aliases[Index])
			{
				return {{}, ArchiveInvalid("Graph archive resource aliases must resolve directly to canonical slots.")};
			}
			if (Aliases[Index] != Index)
			{
				Recording.mImpl->mResourceRemaps[Resources[Index]] = Resources[Aliases[Index]];
			}
		}
		eastl::vector<eastl::shared_ptr<FArdaNodeArrayInvocation>> Arrays;
		eastl::vector<eastl::unordered_set<uint64_t>> ArrayLanes;
		eastl::unordered_set<eastl::string> Names;
		eastl::vector<eastl::vector<FArdaDependencyResourceHandle>> ParameterResources;
		const auto NodeCount = Archive.ReadCount();
		for (size_t Index = 0; Index < NodeCount && Archive.GetStatus(); ++Index)
		{
			eastl::string Name;
			eastl::string Type;
			eastl::string Payload;
			uint32_t NodeVersion = 0;
			bool bStageMaterialized = false;
			uint32_t Array = UINT32_MAX;
			uint64_t LaneCount = 0;
			uint64_t Lane = 0;
			Archive.Fields(Name, Type, NodeVersion, Payload, bStageMaterialized, Array, LaneCount, Lane);
			if (!Archive.GetStatus())
			{
				return {{}, Archive.GetStatus()};
			}
			if (Name.empty() || !Names.insert(Name).second)
			{
				return {{}, ArchiveInvalid("Graph archive contains an empty or duplicate node name.")};
			}
			const auto Definition = FindDefinition(Type);
			if (!Definition || Definition->mVersion != NodeVersion || !Definition->mDeserializeParameters)
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				        ("Graph archive requires a registered node type with a matching implementation version: " +
				            Type)
				            .c_str())};
			}
			eastl::vector<eastl::string> ExternalNames;
			ParameterResources.emplace_back();
			FArdaGraphArchiveReader Parameters(Payload);
			Parameters.mResourceReferences = &ParameterResources.back();
			Parameters.mExternalNames = &ExternalNames;
			Parameters.mResources = &Resources;
			Parameters.mResourceKinds = &ResourceKinds;
			Parameters.mNodes = &Nodes;
			Parameters.mExternalReference = Archive.mExternalReference;
			auto Decoded = Definition->mDeserializeParameters(Parameters);
			if (!Decoded)
			{
				return {{}, eastl::move(Decoded.mStatus)};
			}
			if (auto Status = Parameters.Finish(); !Status)
			{
				return {{}, eastl::move(Status)};
			}
			FArdaDependencyNode Skeleton;
			Skeleton.mName = Name;
			Skeleton.mDefinition = Definition;
			Skeleton.mSourceParameters = eastl::move(Decoded.mValue);
			Skeleton.mAttachmentOrder = Recording.mImpl->mNextAttachmentOrder++;
			TArdaRHIResult<FArdaGraphNodeHandle> Attached;
			Attached.mValue = Recording.mImpl->mTopology.AddNode(eastl::move(Skeleton));
			if (!Attached.mValue)
			{
				return {{}, ArchiveInvalid("Cannot allocate an archived graph node.")};
			}
			Recording.mImpl->mNames.emplace(Name, Attached.mValue);
			Recording.mImpl->mTopology.TryGetNode(Attached.mValue)->mPayload.mHandle = Attached.mValue;
			Nodes.push_back(Attached.mValue);
			auto& Node = Recording.mImpl->mTopology.TryGetNode(Attached.mValue)->mPayload;
			Node.mArchivedParameters = eastl::move(Payload);
			Node.mExternalParameterNames = eastl::move(ExternalNames);
			Node.mbUnboundExternalParameters = Parameters.mbHasUnboundExternalReferences;
			Node.mbStageMaterialized = bStageMaterialized;
			if (Array != UINT32_MAX)
			{
				if (Array > Arrays.size() || !LaneCount || LaneCount > NodeCount || Lane >= LaneCount)
				{
					return {{}, ArchiveInvalid("Graph archive contains invalid array invocation metadata.")};
				}
				if (Array == Arrays.size())
				{
					auto Invocation = eastl::make_shared<FArdaNodeArrayInvocation>();
					Invocation->mLaneCount = static_cast<size_t>(LaneCount);
					Arrays.push_back(eastl::move(Invocation));
					ArrayLanes.emplace_back();
				}
				if (Arrays[Array]->mLaneCount != LaneCount)
				{
					return {{}, ArchiveInvalid("Graph archive array invocation sizes disagree.")};
				}
				if (!ArrayLanes[Array].insert(Lane).second)
				{
					return {{}, ArchiveInvalid("Graph archive contains duplicate array invocation lanes.")};
				}
				Node.mArrayInvocation = Arrays[Array];
				Node.mArrayLane = static_cast<size_t>(Lane);
			}
			else if (LaneCount || Lane)
			{
				return {{}, ArchiveInvalid("Scalar graph node contains array metadata.")};
			}
		}
		for (size_t Index = 0; Index < Arrays.size(); ++Index)
		{
			if (ArrayLanes[Index].size() != Arrays[Index]->mLaneCount)
			{
				return {{}, ArchiveInvalid("Graph archive is missing array invocation lanes.")};
			}
		}
		const auto EdgeCount = Archive.ReadCount();
		for (size_t Index = 0; Index < EdgeCount && Archive.GetStatus(); ++Index)
		{
			const auto Producer = Archive.ReadNode();
			const auto Consumer = Archive.ReadNode();
			if (!Archive.GetStatus())
			{
				return {{}, Archive.GetStatus()};
			}
			if (auto Status = Recording.AddDependency(Producer, Consumer); !Status)
			{
				return {{}, eastl::move(Status)};
			}
		}
		{
			size_t SemanticNodeIndex = 0;
			for (const auto Handle : Nodes)
			{
				auto& Node = Recording.mImpl->mTopology.TryGetNode(Handle)->mPayload;
				Archive.Fields(Node.mbGraphicsLowered, Node.mbRayTracingLowered, Node.mbCulled);
				Node.mGraphicsStages.clear();
				const auto StageCount = Archive.ReadCount();
				for (size_t Index = 0; Index < StageCount && Archive.GetStatus(); ++Index)
				{
					const auto Stage = Archive.ReadNode();
					if (!Stage)
					{
						Archive.Fail("Archived graphics chain contains an empty stage.");
					}
					Node.mGraphicsStages.push_back(Stage);
				}
				Node.mOutputs.clear();
				const auto OutputCount = Archive.ReadCount();
				eastl::unordered_set<eastl::string> OutputNames;
				for (size_t Index = 0; Index < OutputCount && Archive.GetStatus(); ++Index)
				{
					FArdaDependencyNodeOutput Output;
					Archive.Fields(Output.mName,
					    Output.mResource,
					    Output.mRequiredBufferUsage,
					    Output.mRequiredTextureUsage);
					if (Output.mName.empty() || !Output.mResource || !OutputNames.insert(Output.mName).second)
					{
						Archive.Fail("Archived node output is empty or duplicated.");
					}
					Node.mOutputs.push_back(eastl::move(Output));
				}
				ReadArdaGraphDescription(Archive, Node.mDesc, Version < 5);
				const auto NodeIndex = SemanticNodeIndex++;
				for (const auto& Output : Node.mOutputs)
				{
					const auto Canonical = Recording.mImpl->ResolveResource(Output.mResource);
					const bool bParameter = eastl::any_of(ParameterResources[NodeIndex].begin(),
					    ParameterResources[NodeIndex].end(),
					    [&](auto Resource)
					    {
						    return Recording.mImpl->ResolveResource(Resource) == Canonical;
					    });
					const bool bWrite = eastl::any_of(Node.mDesc.mAccesses.begin(),
					    Node.mDesc.mAccesses.end(),
					    [&](const auto& Access)
					    {
						    return Recording.mImpl->ResolveResource(Access.mResource) == Canonical &&
						        Access.mAccess != EArdaDependencyAccess::Read;
					    });
					if (!bParameter || !bWrite)
					{
						Archive.Fail(
						    "Archived node output does not match its public parameters and declared write accesses.");
					}
				}

				bool bStageDeclaration = false;
				Archive.Read(bStageDeclaration);
				if (bStageDeclaration)
				{
					auto Declaration = eastl::make_shared<FArdaDependencyNodeDesc>();
					ReadArdaGraphDescription(Archive, *Declaration, Version < 5);
					Node.mStageDeclaration = eastl::move(Declaration);
				}
				else
				{
					Node.mStageDeclaration.reset();
				}
			}
			const auto PreviousEdges = Recording.mImpl->mTopology.GetEdges();
			for (const auto Edge : PreviousEdges)
			{
				Recording.mImpl->mTopology.RemoveEdge(Edge);
			}
			eastl::unordered_set<uint64_t> EdgePairs;
			const auto TopologyEdgeCount = Archive.ReadCount();
			for (size_t Index = 0; Index < TopologyEdgeCount && Archive.GetStatus(); ++Index)
			{
				const auto Producer = Archive.ReadNode();
				const auto Consumer = Archive.ReadNode();
				FArdaDependencyEdge Payload;
				Archive.Fields(Payload.mbResource, Payload.mbHazard);
				const uint64_t Pair = (uint64_t(Producer.GetIndex()) << 32) | Consumer.GetIndex();
				if (!Producer || !Consumer || Producer == Consumer || !EdgePairs.insert(Pair).second)
				{
					Archive.Fail("Archived topology edge has invalid endpoints.");
					break;
				}
				const auto Edge = Recording.mImpl->mTopology.AddEdge(Producer, Consumer, Payload);
				if (!Edge.mHandle)
				{
					Archive.Fail("Archived topology edge could not be restored.");
					break;
				}
				Recording.mImpl->mTopology.TryGetEdge(Edge.mHandle)->mPayload = Payload;
			}
		}
		const auto AdmissionCount = Archive.ReadCount();
		eastl::unordered_set<eastl::string> AdmissionNames;
		for (size_t Index = 0; Index < AdmissionCount && Archive.GetStatus(); ++Index)
		{
			FArdaInductorSubgraphAdmission Admission;
			eastl::string Type;
		uint32_t NodeVersion = 0;
		Archive.Fields(Admission.mName, Type, NodeVersion, Admission.mArchivedParameters);
			Admission.mDefinition = FindDefinition(Type);
			if (Admission.mName.empty() || !AdmissionNames.insert(Admission.mName).second || !Admission.mDefinition ||
			    !Admission.mDefinition->mExpand || !Admission.mDefinition->mDeserializeParameters ||
		    Admission.mDefinition->mVersion != NodeVersion)
			{
				return {{},
				    ArchiveInvalid(
				        "Archived subgraph admission requires a unique name and matching registered definition.")};
			}
			FArdaGraphArchiveReader Parameters(Admission.mArchivedParameters);
			Parameters.mResources = &Resources;
			Parameters.mResourceKinds = &ResourceKinds;
			Parameters.mNodes = &Nodes;
			Parameters.mExternalReference = Archive.mExternalReference;
			Parameters.mExternalNames = &Admission.mExternalParameterNames;
			auto Decoded = Admission.mDefinition->mDeserializeParameters(Parameters);
			if (!Decoded)
			{
				return {{}, eastl::move(Decoded.mStatus)};
			}
			if (auto Status = Parameters.Finish(); !Status)
			{
				return {{}, eastl::move(Status)};
			}
			Admission.mParameters = eastl::move(Decoded.mValue);
			Admission.mbUnboundExternalParameters = Parameters.mbHasUnboundExternalReferences;
			ReadArdaGraphRequirements(Archive, Admission.mRequirements, Version < 5);
			Recording.mImpl->mSubgraphAdmissions.push_back(eastl::move(Admission));
		}
		if (Version >= 4)
		{
			const auto EditCount = Archive.ReadCount();
			eastl::unordered_set<uint32_t> ClaimedNodes;
			for (size_t Index = 0; Index < EditCount && Archive.GetStatus(); ++Index)
			{
				FArdaCommittedGraphEdit Committed;
				const auto EditNodeCount = Archive.ReadCount();
				for (size_t Entry = 0; Entry < EditNodeCount && Archive.GetStatus(); ++Entry)
				{
					uint32_t Node = UINT32_MAX;
					Archive.Read(Node);
					if (Node >= Nodes.size() || !ClaimedNodes.insert(Node).second)
					{
						return {{}, ArchiveInvalid("Graph archive edit contains an invalid or multiply-owned node.")};
					}
					Committed.mNodes.push_back(Nodes[Node]);
				}
				const auto RevisionCount = Archive.ReadCount();
				eastl::unordered_set<uint32_t> ClaimedResources;
				for (size_t Entry = 0; Entry < RevisionCount && Archive.GetStatus(); ++Entry)
				{
					uint32_t Resource = UINT32_MAX;
					uint64_t Revision = 0;
					Archive.Fields(Resource, Revision);
					if (Resource >= Resources.size() || !ClaimedResources.insert(Aliases[Resource]).second ||
					    !Recording.mImpl->mResources[Aliases[Resource]].mbRevisionOrdered)
					{
						return {{}, ArchiveInvalid("Graph archive edit contains an invalid resource revision.")};
					}
					Committed.mResources.push_back({Resources[Aliases[Resource]], Revision});
				}
				Recording.mImpl->mCommittedEdits.push_back(eastl::move(Committed));
			}
		}
		if (auto Status = Archive.Finish(); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		Recording.mImpl->mArchiveNodes = Nodes;
		Recording.mImpl->mbPreprocessed = bPreprocessed;
		if (auto Status = Edit.Commit(); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		Graph->mImpl->mbPreprocessed = bPreprocessed;
		if (auto Status = FArdaNodeRegistry::Get().RegisterPortableDefinitions(PortableDefinitions); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		return {eastl::move(Graph), {}};
	}

	FArdaRHIStatus FArdaGraphSerialization::Save(const FArdaDependencyGraph& Graph, const std::filesystem::path& Path)
	{
		eastl::vector<eastl::string> ReferencedLibraries;
		const auto Data = Serialize(Graph, &ReferencedLibraries);
		if (!Data)
		{
			return Data.mStatus;
		}
		const auto Directory = std::filesystem::absolute(Path).parent_path();
		for (const auto& Library : ReferencedLibraries)
		{
			if (auto Status = StageArdaNativeKernelLibrary(Library, Directory); !Status)
			{
				return Status;
			}
		}
		return fileops::AtomicWrite(Path, Data.mValue)
		    ? FArdaRHIStatus{}
		    : FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Could not atomically write the graph archive.");
	}

	TArdaRHIResult<eastl::unique_ptr<FArdaDependencyGraph>> FArdaGraphSerialization::Load(
	    const std::filesystem::path& Path,
	    FArdaRHIDeviceRef Device)
	{
		eastl::string Data;
		if (fileops::ReadBinaryFile(Path, Data, FArdaGraphArchiveWriter::mMaximumBytes) !=
		    fileops::EArdaFileReadResult::Success)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			        "Graph archive is unreadable or exceeds the size limit.")};
		}
		AddArdaNativeKernelDirectory(std::filesystem::absolute(Path).parent_path());
		return Deserialize(Data, eastl::move(Device));
	}
}
