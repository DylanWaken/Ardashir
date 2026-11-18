#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include "Graph/Node/ArdaGraphEdit.h"

namespace arda
{
	FArdaDependencyTopology& FArdaInductorGraph::GetTopology()
	{
		return FArdaInductorGraphAccess::State(*this).mTopology;
	}

	const FArdaDependencyTopology& FArdaInductorGraph::GetTopology() const
	{
		return FArdaInductorGraphAccess::State(*this).mTopology;
	}

	FArdaDependencyNodeDesc& FArdaInductorGraph::GetNodeDescription(FArdaGraphNodeHandle Node)
	{
		auto& State = FArdaInductorGraphAccess::State(*this);
		return State.GetNodeDescription(State.mTopology.TryGetNode(Node)->mPayload);
	}

	const FArdaDependencyNodeDesc& FArdaInductorGraph::GetNodeDescription(FArdaGraphNodeHandle Node) const
	{
		auto& State = FArdaInductorGraphAccess::State(*this);
		return State.GetNodeDescription(State.mTopology.TryGetNode(Node)->mPayload);
	}

	const eastl::vector<FArdaDependencyResourceDesc>& FArdaInductorGraph::GetResources() const
	{
		return FArdaInductorGraphAccess::State(*this).mResources;
	}

	FArdaDependencyResourceDesc* FArdaInductorGraph::EditResource(FArdaDependencyResourceHandle Resource)
	{
		auto& G = FArdaInductorGraphAccess::State(*this);
		Resource = G.ResolveResource(Resource);
		return G.HasResource(Resource) ? &G.mResources[Resource.mIndex] : nullptr;
	}

	const FArdaInductorOptions& FArdaInductorGraph::GetOptions() const
	{
		return FArdaInductorGraphAccess::State(*this).mOptions;
	}

	FArdaRHIStatus FArdaInductorGraph::Edit(const eastl::function<FArdaRHIStatus(FArdaGraphEdit&)>& Content)
	{
		auto& State = FArdaInductorGraphAccess::State(*this);
		if (!mOwner || !State.mbPreprocessing || State.mbPreprocessEditing || !Content)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Graph editing requires an active preprocessing rule and a nonrecursive callback.");
		}

		struct FArdaEditGuard
		{
			bool& mFlag;

			explicit FArdaEditGuard(bool& Flag)
			    : mFlag(Flag)
			{
				mFlag = true;
			}

			~FArdaEditGuard()
			{
				mFlag = false;
			}
		} Guard(State.mbPreprocessEditing);

		FArdaGraphEdit Edit(*mOwner, {}, false);
		Edit.ReportError(Content(Edit));
		return Edit.GetStatus();
	}

	FArdaRHIStatus FArdaInductorGraph::RemoveNode(FArdaGraphNodeHandle Handle)
	{
		auto& State = FArdaInductorGraphAccess::State(*this);
		Handle = State.ResolveNode(Handle);
		auto* Node = State.mTopology.TryGetNode(Handle);
		if (!State.mbPreprocessing || !Node)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Node removal requires an active preprocessing rule and a live node.");
		}
		State.mNames.erase(Node->mPayload.mName);
		State.mNodePreparations.erase(Handle);
		State.mManualEdges.erase(eastl::remove_if(State.mManualEdges.begin(),
		                             State.mManualEdges.end(),
		                             [&](const auto& Edge)
		                             {
			                             return Edge.first == Handle || Edge.second == Handle;
		                             }),
		    State.mManualEdges.end());
		for (auto& Edit : State.mCommittedEdits)
		{
			Edit.mNodes.erase(eastl::remove(Edit.mNodes.begin(), Edit.mNodes.end(), Handle), Edit.mNodes.end());
		}
		State.mTopology.RemoveNode(Handle);
		return {};
	}

	FArdaRHIDeviceRef FArdaInductorGraph::GetDevice() const
	{
		return FArdaInductorGraphAccess::State(*this).mCompile.mExecution->mDevice;
	}

	double FArdaInductorGraph::GetNodeCost(FArdaGraphNodeHandle Node) const
	{
		const auto& G = FArdaInductorGraphAccess::State(*this);
		const auto* N = G.mTopology.TryGetNode(Node);
		return N ? inductor_detail::NodeCost(G, N->mPayload) : 0;
	}
}
