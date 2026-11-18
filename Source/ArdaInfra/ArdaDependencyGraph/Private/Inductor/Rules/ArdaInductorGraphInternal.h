#pragma once

#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaInductorGraph.h"

#include <EASTL/sort.h>
#include <EASTL/unordered_set.h>

namespace arda::inductor_detail
{
	using FArdaHandle = FArdaGraphNodeHandle;

	inline FArdaRHIStatus Error(const char* Text)
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Text);
	}

	template <class Graph>
	inline bool IsDeclaration(const Graph& G, const FArdaDependencyNode& Node)
	{
		const auto& Desc = G.GetNodeDescription(Node);
		return Desc.mbPipelineStageOnly ||
		    (Node.mDefinition->mKind == EArdaDependencyNodeKind::Synchronization && !Node.mDefinition->mRecord &&
		        Desc.mAccesses.empty());
	}

	inline bool AddBytes(uint64_t& A, uint64_t B)
	{
		if (B > UINT64_MAX - A)
		{
			return false;
		}
		A += B;
		return true;
	}

	FArdaRHIStatus ValidateResourceAccesses(const FArdaDependencyGraph::FArdaImpl&, bool AllowAutomaticUsage = false);
	FArdaRHIStatus ValidateResourceUsage(const FArdaDependencyGraph::FArdaImpl&);
}

namespace arda
{
	struct FArdaInductorGraphAccess
	{
		static FArdaDependencyGraph::FArdaImpl& State(const FArdaInductorGraph& Graph)
		{
			return *static_cast<FArdaDependencyGraph::FArdaImpl*>(Graph.mState);
		}

		static FArdaInductorGraph View(FArdaDependencyGraph::FArdaImpl& Graph, FArdaDependencyGraph* Owner = nullptr)
		{
			return FArdaInductorGraph(&Graph, Owner);
		}

		static FArdaDependencyGraph* Owner(const FArdaInductorGraph& Graph)
		{
			return Graph.mOwner;
		}
	};
}
