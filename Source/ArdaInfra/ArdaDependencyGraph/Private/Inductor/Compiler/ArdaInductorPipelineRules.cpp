#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include "Inductor/Rules/ArdaInductorShaderContracts.h"

namespace arda::inductor_detail
{
	FArdaRHIStatus ValidatePipelines(const FArdaDependencyGraph::FArdaImpl& Graph,
	    const eastl::vector<FArdaHandle>& Live)
	{
		for (const auto Handle : Live)
		{
			FArdaInductorPipelineNode Projection;
			Projection.mNodeId = Handle.GetIndex();
			Projection.mContributions = Graph.mCompile.mNodes.at(Handle).mDesc.mPipelineStages;
			for (const auto& Request : Graph.mCompile.mNodes.at(Handle).mDesc.mPipelines)
			{
				Projection.mPipelineBoundaries.push_back(Request.mKind);
			}
			const eastl::vector<FArdaInductorPipelineNode> Nodes{eastl::move(Projection)};
			eastl::unordered_set<eastl::string> Slots;
			const auto& Requests = Graph.mCompile.mNodes.at(Handle).mDesc.mPipelines;
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
				if (auto Status = ValidateArdaNodeShaderContract(Graph.mCompile.mNodes.at(Handle).mDesc,
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
	FArdaRHIStatus FArdaInferPipelinesRule::Compile(const FArdaInductorGraph& Graph,
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
