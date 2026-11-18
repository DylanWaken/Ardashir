#include "Graph/Node/ArdaGraphEdit.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaGraphRevisionState.h"
#include "Graph/Node/ArdaGraphRecordingScope.h"

#include <EASTL/algorithm.h>

namespace arda
{
	FArdaGraphOperation::FArdaGraphOperation(FArdaGraphEdit& Edit, FArdaGraphNodeHandle Node)
	    : mEdit(&Edit),
	      mGraph(&Edit.mGraph),
	      mNode(Node)
	{
	}

	FArdaGraphNodeHandle FArdaGraphOperation::GetHandle() const noexcept
	{
		return mNode;
	}

	FArdaGraphOperation::operator bool() const noexcept
	{
		return bool(mNode);
	}

	FArdaGraphBuffer FArdaGraphOperation::GetBuffer(const eastl::string& Slot) const
	{
		return mEdit ? mEdit->UseBuffer(mEdit->GetGraph().FindOutput(mNode, Slot)) : FArdaGraphBuffer{};
	}

	FArdaGraphTexture FArdaGraphOperation::GetTexture(const eastl::string& Slot) const
	{
		return mEdit ? mEdit->UseTexture(mEdit->GetGraph().FindOutput(mNode, Slot)) : FArdaGraphTexture{};
	}

	FArdaGraphAccelerationStructure FArdaGraphOperation::GetAccelerationStructure(const eastl::string& Slot) const
	{
		return mEdit ? mEdit->UseAccelerationStructure(mEdit->GetGraph().FindOutput(mNode, Slot))
		             : FArdaGraphAccelerationStructure{};
	}

	FArdaGraphEdit::FArdaGraphEdit(FArdaDependencyGraph& Graph)
	    : mGraph(Graph),
	      mStatus()
	{
		mStatus = Graph.BeginGraphEdit(*this);
		mbOwnsEdit = bool(mStatus);
	}

	FArdaGraphEdit::FArdaGraphEdit(FArdaDependencyGraph& Graph, eastl::string Prefix, bool bSubgraphContent)
	    : mGraph(Graph),
	      mbOwnsEdit(true),
	      mbSubgraphContent(bSubgraphContent),
	      mbPreprocessContent(true),
	      mNamePrefix(eastl::move(Prefix)),
	      mFirstSubgraphResource(Graph.mImpl->mResources.size()),
	      mFirstSubgraphAttachmentOrder(Graph.mImpl->mNextAttachmentOrder)
	{
	}

	FArdaGraphVertices FArdaGraphOperation::GetVertices() const
	{
		return mEdit && mEdit->ValidateStage({mNode, EArdaGraphStageValueKind::Vertices}) ? FArdaGraphVertices(mNode)
		                                                                                  : FArdaGraphVertices{};
	}

	FArdaGraphFragments FArdaGraphOperation::GetFragments() const
	{
		return mEdit && mEdit->ValidateStage({mNode, EArdaGraphStageValueKind::Fragments}) ? FArdaGraphFragments(mNode)
		                                                                                   : FArdaGraphFragments{};
	}

	FArdaGraphPixels FArdaGraphOperation::GetPixels() const
	{
		return mEdit && mEdit->ValidateStage({mNode, EArdaGraphStageValueKind::Pixels}) ? FArdaGraphPixels(mNode)
		                                                                                : FArdaGraphPixels{};
	}

	bool FArdaGraphEdit::ValidateStage(FArdaGraphStageValue Value)
	{
		if (!CanRecord())
		{
			return false;
		}
		const auto* Node =
		    GetRecordingGraph().GetTopology().TryGetNode(GetRecordingGraph().mImpl->ResolveNode(Value.GetProducer()));
		const auto* Desc = Node ? &GetRecordingGraph().GetNodeDescription(Node->mPayload.mHandle) : nullptr;
		const auto OutputKind = Desc && IsArdaRHIRayTracingShaderStage(Desc->mRayStage) &&
		        Desc->mRayStage != EArdaRHIShaderStage::RayGeneration
		    ? EArdaGraphStageValueKind::RayShader
		    : Desc ? GetArdaGraphicsStageOutput(Desc->mGraphicsStage)
		           : EArdaGraphStageValueKind::None;
		if (!Node || Value.GetKind() == EArdaGraphStageValueKind::None || OutputKind != Value.GetKind())
		{
			ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Stage value is empty, foreign, stale or has the wrong output kind."));
			return false;
		}
		if (mbSubgraphContent && Node->mPayload.mAttachmentOrder < mFirstSubgraphAttachmentOrder)
		{
			ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Subgraph content cannot access a parent logical stage value."));
			return false;
		}
		return true;
	}

	FArdaGraphControlPoints FArdaGraphOperation::GetControlPoints() const
	{
		return mEdit && mEdit->ValidateStage({mNode, EArdaGraphStageValueKind::ControlPoints})
		    ? FArdaGraphControlPoints(mNode)
		    : FArdaGraphControlPoints{};
	}

	FArdaGraphMeshPayload FArdaGraphOperation::GetMeshPayload() const
	{
		return mEdit && mEdit->ValidateStage({mNode, EArdaGraphStageValueKind::MeshPayload})
		    ? FArdaGraphMeshPayload(mNode)
		    : FArdaGraphMeshPayload{};
	}

	TArdaGraphStageValue<EArdaGraphStageValueKind::RayShader> FArdaGraphOperation::GetRayShader() const
	{
		using FArdaResult = TArdaGraphStageValue<EArdaGraphStageValueKind::RayShader>;
		return mEdit && mEdit->ValidateStage({mNode, EArdaGraphStageValueKind::RayShader}) ? FArdaResult(mNode)
		                                                                                   : FArdaResult{};
	}

	FArdaGraphBuffer FArdaGraphEdit::FetchBuffer(FArdaGraphStageValue Value, const FArdaStageBufferRequest& Request)
	{
		if (!ValidateStage(Value))
		{
			return {};
		}
		// Retain before invoking the adapter: recording may reallocate topology storage.
		const auto& Node = GetRecordingGraph()
		                       .GetTopology()
		                       .TryGetNode(GetRecordingGraph().mImpl->ResolveNode(Value.GetProducer()))
		                       ->mPayload;
		const auto Definition = Node.mDefinition;
		const auto Parameters = Node.mSourceParameters;
		if (!Definition->mFetchBuffer)
		{
			ReportError(FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "This graphics stage has no registered buffer materializer. Define FetchBuffer on its node class."));
			return {};
		}
		auto Buffer = Definition->mFetchBuffer(*this, Parameters.get(), Request);
		Buffer = UseBuffer(Buffer.GetHandle());
		if (Buffer && ValidateStage(Value))
		{
			GetRecordingGraph()
			    .mImpl->mTopology.TryGetNode(GetRecordingGraph().mImpl->ResolveNode(Value.GetProducer()))
			    ->mPayload.mbStageMaterialized = true;
			return Buffer;
		}
		return {};
	}

	FArdaGraphBuffer FArdaGraphEdit::GetStageBuffer(FArdaGraphStageValue Value, const eastl::string& Slot)
	{
		return ValidateStage(Value) ? UseBuffer(GetRecordingGraph().FindOutput(Value.GetProducer(), Slot))
		                            : FArdaGraphBuffer{};
	}

	FArdaGraphEdit::~FArdaGraphEdit()
	{
		if (IsOpen() && !mbPreprocessContent)
		{
			(void)Cancel();
		}
	}

	FArdaRHIStatus FArdaGraphEdit::Commit()
	{
		if (mbPreprocessContent)
		{
			ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Preprocessing content is committed by the preprocessor after its callback completes."));
			return mStatus;
		}
		if (!CanRecord())
		{
			return mStatus;
		}
		ReportError(mGraph.EndGraphEdit(*this));
		if (mStatus)
		{
			mbOwnsEdit = false;
		}
		return mStatus;
	}

	FArdaRHIStatus FArdaGraphEdit::Cancel()
	{
		if (mbPreprocessContent)
		{
			ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Preprocessing content cannot close its borrowed edit."));
			return mStatus;
		}
		if (!IsOpen())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "This scope does not own an open graph edit.");
		}
		auto Status = mGraph.CancelGraphEdit(*this);
		if (Status)
		{
			mbOwnsEdit = false;
		}
		return Status;
	}

	const FArdaRHIStatus& FArdaGraphEdit::GetStatus() const noexcept
	{
		return mStatus;
	}

	bool FArdaGraphEdit::IsOpen() const noexcept
	{
		return mbOwnsEdit &&
		    (mbPreprocessContent ? mGraph.mImpl->mbEditing : mRecordingGraph && mRecordingGraph->mImpl->mbEditing);
	}

	bool FArdaGraphEdit::CanRecord()
	{
		const auto& Graph = *GetRecordingGraph().mImpl;
		if (!IsOpen() || Graph.mbCompiling ||
		    (Graph.mbPreprocessing && !(mbPreprocessContent && Graph.mbPreprocessEditing)))
		{
			ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Operations require this scope's open graph edit."));
		}
		return bool(mStatus);
	}

	void FArdaGraphEdit::ReportError(FArdaRHIStatus Status)
	{
		if (mStatus && !Status)
		{
			mStatus = eastl::move(Status);
		}
	}

	const FArdaDependencyGraph& FArdaGraphEdit::GetGraph() const noexcept
	{
		return GetRecordingGraph();
	}

	FArdaDependencyGraph& FArdaGraphEdit::GetRecordingGraph() const noexcept
	{
		return mRecordingGraph ? *mRecordingGraph : mGraph;
	}

	FArdaRHIStatus FArdaGraphEdit::SetOptions(const FArdaInductorOptions& Options)
	{
		if (mbPreprocessContent)
		{
			ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Preprocessing content inherits its parent graph options."));
			return mStatus;
		}
		if (CanRecord())
		{
			ReportError(GetRecordingGraph().SetOptions(Options));
			mbOptionsChanged = bool(mStatus);
		}
		return mStatus;
	}

	FArdaRHIStatus FArdaGraphEdit::SetResourceRevision(FArdaDependencyResourceHandle Resource, uint64_t Revision)
	{
		if (!CanRecord())
		{
			return mStatus;
		}
		auto& Graph = *GetRecordingGraph().mImpl;
		if (mbPreprocessContent || !Graph.HasResource(Resource))
		{
			ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Resource revisions require a live resource in an ordinary graph edit."));
			return mStatus;
		}
		Resource = Graph.ResolveResource(Resource);
		for (const auto& Existing : mResourceRevisions)
		{
			if (Existing.mResource == Resource)
			{
				if (Existing.mRevision != Revision)
				{
					ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "An edit cannot assign two revisions to the same resource."));
				}
				return mStatus;
			}
		}
		mResourceRevisions.push_back({Resource, Revision});
		Graph.mResources[Resource.mIndex].mbRevisionOrdered = true;
		return mStatus;
	}

	FArdaRHIStatus FArdaGraphEdit::Order(const FArdaGraphOperation& Producer, const FArdaGraphOperation& Consumer)
	{
		if (!CanRecord())
		{
			return mStatus;
		}
		if (Producer.mGraph != &mGraph || Consumer.mGraph != &mGraph || !Producer || !Consumer ||
		    !GetRecordingGraph().GetTopology().TryGetNode(GetRecordingGraph().mImpl->ResolveNode(Producer.mNode)) ||
		    !GetRecordingGraph().GetTopology().TryGetNode(GetRecordingGraph().mImpl->ResolveNode(Consumer.mNode)))
		{
			ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Ordering requires two live operations in this graph's current construction."));
		}
		else
		{
			ReportError(GetRecordingGraph().AddDependency(GetRecordingGraph().mImpl->ResolveNode(Producer.mNode),
			    GetRecordingGraph().mImpl->ResolveNode(Consumer.mNode)));
		}
		return mStatus;
	}

	eastl::string FArdaGraphEdit::ResolveOperationName(const eastl::string& Prefix, eastl::string Name)
	{
		if (Name.empty())
		{
			return MakeName(Prefix);
		}
		Name = mNamePrefix + Name;
		if (GetRecordingGraph().FindNode(Name))
		{
			ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Operation names must be unique within a construction."));
		}
		return Name;
	}

	eastl::string FArdaGraphEdit::MakeName(const eastl::string& Prefix)
	{
		if (!CanRecord())
		{
			return {};
		}
		auto& Graph = *GetRecordingGraph().mImpl;
		eastl::string Name;
		do
		{
			Name = mNamePrefix + Prefix + "#" + eastl::to_string(Graph.mNextOperationName++);
		} while (Graph.mNames.find(Name) != Graph.mNames.end() ||
		    Graph.mResourceNames.find(Name) != Graph.mResourceNames.end());
		Graph.mGeneratedNames.insert(Name);
		return Name;
	}

	bool FArdaGraphEdit::ValidateResource(FArdaDependencyResourceHandle Resource, EArdaGraphResourceKind Kind)
	{
		if (!CanRecord())
		{
			return false;
		}
		const auto* Desc = GetRecordingGraph().FindResource(Resource);
		if (!Desc)
		{
			ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Graph value is empty, foreign or no longer live."));
			return false;
		}
		const auto ActualKind = Desc->mExternalKind == EArdaGraphExternalResourceKind::AccelerationStructure
		    ? EArdaGraphResourceKind::AccelerationStructure
		    : Desc->mbTexture ? EArdaGraphResourceKind::Texture
		                      : EArdaGraphResourceKind::Buffer;
		if (Kind != ActualKind)
		{
			ReportError(
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Graph value has the wrong resource kind."));
			return false;
		}
		return true;
	}

	FArdaGraphBuffer FArdaGraphEdit::UseBuffer(FArdaDependencyResourceHandle Resource)
	{
		return ValidateResource(Resource, EArdaGraphResourceKind::Buffer) ? FArdaGraphBuffer(Resource)
		                                                                  : FArdaGraphBuffer{};
	}

	FArdaGraphTexture FArdaGraphEdit::UseTexture(FArdaDependencyResourceHandle Resource)
	{
		return ValidateResource(Resource, EArdaGraphResourceKind::Texture) ? FArdaGraphTexture(Resource)
		                                                                   : FArdaGraphTexture{};
	}

	FArdaGraphAccelerationStructure FArdaGraphEdit::UseAccelerationStructure(FArdaDependencyResourceHandle Resource)
	{
		return ValidateResource(Resource, EArdaGraphResourceKind::AccelerationStructure)
		    ? FArdaGraphAccelerationStructure(Resource)
		    : FArdaGraphAccelerationStructure{};
	}

	void FArdaGraphEdit::Keep(FArdaDependencyResourceHandle Resource)
	{
		if (CanRecord())
		{
			const auto& Graph = *GetRecordingGraph().mImpl;
			const auto Resolved = Graph.ResolveResource(Resource);
			if (mbSubgraphContent && Resolved.mIndex < mFirstSubgraphResource &&
			    eastl::none_of(
			        mSubgraphAccesses.begin(),
			        mSubgraphAccesses.end(),
			        [&](const auto& Access)
			        {
				        return Graph.ResolveResource(Access.mResource) == Resolved;
			        }))
			{
				ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Subgraph content cannot keep an undeclared parent resource."));
				return;
			}
			ReportError(GetRecordingGraph().MarkOutput(Resource));
		}
	}
}
