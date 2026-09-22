#include "Graph/Node/ArdaGraphEdit.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"

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
	      mStatus(Graph.BeginGraphEdit())
	{
		mbOwnsEdit = bool(mStatus);
		if (mbOwnsEdit)
		{
			mGeneration = Graph.mImpl->mEditGeneration;
		}
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
		const auto* Node = mGraph.GetTopology().TryGetNode(Value.GetProducer());
		const auto OutputKind = Node && IsArdaRHIRayTracingShaderStage(Node->mPayload.mDesc.mRayStage) &&
		        Node->mPayload.mDesc.mRayStage != EArdaRHIShaderStage::RayGeneration
		    ? EArdaGraphStageValueKind::RayShader
		    : Node ? GetArdaGraphicsStageOutput(Node->mPayload.mDesc.mGraphicsStage)
		           : EArdaGraphStageValueKind::None;
		if (!Node || Value.GetKind() == EArdaGraphStageValueKind::None || OutputKind != Value.GetKind())
		{
			ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Stage value is empty, foreign, stale or has the wrong output kind."));
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
		const auto& Node = mGraph.GetTopology().TryGetNode(Value.GetProducer())->mPayload;
		const auto Definition = Node.mDefinition;
		const auto Parameters = Node.mParameters;
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
			mGraph.mImpl->mTopology.TryGetNode(Value.GetProducer())->mPayload.mbStageMaterialized = true;
			return Buffer;
		}
		return {};
	}

	FArdaGraphBuffer FArdaGraphEdit::GetStageBuffer(FArdaGraphStageValue Value, const eastl::string& Slot)
	{
		return ValidateStage(Value) ? UseBuffer(mGraph.FindOutput(Value.GetProducer(), Slot)) : FArdaGraphBuffer{};
	}

	FArdaGraphEdit::~FArdaGraphEdit()
	{
		if (IsOpen())
		{
			(void)Cancel();
		}
	}

	FArdaRHIStatus FArdaGraphEdit::Commit()
	{
		if (!CanRecord())
		{
			return mStatus;
		}
		ReportError(mGraph.EndGraphEdit());
		if (mStatus)
		{
			mbOwnsEdit = false;
		}
		return mStatus;
	}

	FArdaRHIStatus FArdaGraphEdit::Cancel()
	{
		if (!IsOpen())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "This scope does not own an open graph edit.");
		}
		auto Status = mGraph.CancelGraphEdit();
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
		return mbOwnsEdit && mGraph.IsEditing() && mGraph.mImpl->mEditGeneration == mGeneration;
	}

	bool FArdaGraphEdit::CanRecord()
	{
		if (!IsOpen() || mGraph.mImpl->mbCompiling)
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
		return mGraph;
	}

	FArdaRHIStatus FArdaGraphEdit::SetOptions(const FArdaInductorOptions& Options)
	{
		if (CanRecord())
		{
			ReportError(mGraph.SetOptions(Options));
		}
		return mStatus;
	}

	FArdaRHIStatus FArdaGraphEdit::Order(const FArdaGraphOperation& Producer, const FArdaGraphOperation& Consumer)
	{
		if (!CanRecord())
		{
			return mStatus;
		}
		if (Producer.mGraph != &mGraph || Consumer.mGraph != &mGraph || !Producer || !Consumer ||
		    !mGraph.GetTopology().TryGetNode(Producer.mNode) || !mGraph.GetTopology().TryGetNode(Consumer.mNode))
		{
			ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Ordering requires two live operations in this graph's current construction."));
		}
		else
		{
			ReportError(mGraph.AddDependency(Producer.mNode, Consumer.mNode));
		}
		return mStatus;
	}

	eastl::string FArdaGraphEdit::ResolveOperationName(const eastl::string& Prefix, eastl::string Name)
	{
		if (Name.empty())
		{
			return MakeName(Prefix);
		}
		if (mGraph.FindNode(Name))
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
		auto& Graph = *mGraph.mImpl;
		eastl::string Name;
		do
		{
			Name = Prefix + "#" + eastl::to_string(Graph.mNextOperationName++);
		} while (Graph.mNames.find(Name) != Graph.mNames.end() ||
		    Graph.mResourceNames.find(Name) != Graph.mResourceNames.end());
		return Name;
	}

	bool FArdaGraphEdit::ValidateResource(FArdaDependencyResourceHandle Resource, EArdaGraphResourceKind Kind)
	{
		if (!CanRecord())
		{
			return false;
		}
		const auto* Desc = mGraph.FindResource(Resource);
		if (!Desc)
		{
			ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Graph value is empty, foreign or no longer live."));
			return false;
		}
		const auto ActualKind = Desc->mExternalAccelerationStructure ? EArdaGraphResourceKind::AccelerationStructure
		    : Desc->mbTexture                                        ? EArdaGraphResourceKind::Texture
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
			ReportError(mGraph.MarkOutput(Resource));
		}
	}
}
