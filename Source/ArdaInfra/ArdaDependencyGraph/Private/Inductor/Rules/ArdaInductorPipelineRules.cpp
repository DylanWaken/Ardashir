#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include "Inductor/Rules/ArdaInductorShaderContracts.h"

namespace arda::inductor_detail
{
	FArdaRHIStatus ValidatePipelines(const FArdaDependencyGraph::FArdaImpl& Graph,
	    const eastl::vector<FArdaHandle>& Live)
	{
		eastl::vector<FArdaInductorPipelineNode> Nodes;
		Nodes.reserve(Live.size());
		for (const auto Handle : Live)
		{
			FArdaInductorPipelineNode Node;
			Node.mNodeId = Handle.GetIndex();
			Node.mContributions = Graph.mTopology.TryGetNode(Handle)->mPayload.mDesc.mPipelineStages;
			for (const auto& Request : Graph.mTopology.TryGetNode(Handle)->mPayload.mDesc.mPipelines)
			{
				Node.mPipelineBoundaries.push_back(Request.mKind);
			}
			// Typed graphics chains already carry exactly their own stages on the terminal.
			// Resource ancestry must not import shaders from unrelated draws or declarations.
			if (!Graph.mTopology.TryGetNode(Handle)->mPayload.mbGraphicsLowered &&
			    !Graph.mTopology.TryGetNode(Handle)->mPayload.mbRayTracingLowered)
			{
				for (const auto Edge : Graph.mTopology.GetIncomingEdges(Handle))
				{
					Node.mDependencies.push_back(Graph.mTopology.TryGetEdge(Edge)->mFrom.GetIndex());
				}
			}
			Nodes.push_back(eastl::move(Node));
		}
		for (const auto Handle : Live)
		{
			eastl::unordered_set<eastl::string> Slots;
			const auto& Requests = Graph.mTopology.TryGetNode(Handle)->mPayload.mDesc.mPipelines;
			for (const auto& Request : Requests)
			{
				if (!Slots.insert(Request.mSlot.empty() ? eastl::string("default") : Request.mSlot).second)
				{
					return Error("A node requests duplicate pipeline slots.");
				}
			}
			for (auto Request : Requests)
			{
				if (Request.mSlot.empty())
				{
					Request.mSlot = "default";
				}
				Request.mTerminalNodeId = Handle.GetIndex();
				const auto Pattern = InferArdaInductorPipeline(Nodes, Request);
				if (!Pattern)
				{
					return Pattern.mStatus;
				}
				if (auto Status = ValidateArdaNodeShaderContract(Graph.mTopology.TryGetNode(Handle)->mPayload.mDesc,
				        Pattern.mValue.mConfiguration,
				        Request.mSlot);
				    !Status)
				{
					return Status;
				}
			}
		}
		return {};
	}

}

namespace arda
{
	FArdaRHIStatus FArdaInferPipelinesRule::Apply(const FArdaInductorGraph& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		const auto& G = FArdaInductorGraphAccess::State(Graph);
		if (auto S = inductor_detail::ValidateNodeOrder(G, Result); !S)
		{
			return S;
		}
		return inductor_detail::ValidatePipelines(G, Result.mExecutionOrder);
	}
}
