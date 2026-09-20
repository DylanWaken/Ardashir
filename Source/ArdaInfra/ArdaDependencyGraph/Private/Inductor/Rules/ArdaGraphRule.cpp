#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"

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

	const eastl::vector<FArdaDependencyResourceDesc>& FArdaInductorGraph::GetResources() const
	{
		return FArdaInductorGraphAccess::State(*this).mResources;
	}

	FArdaDependencyResourceDesc* FArdaInductorGraph::EditResource(FArdaDependencyResourceHandle Resource)
	{
		auto& G = FArdaInductorGraphAccess::State(*this);
		return G.HasResource(Resource) ? &G.mResources[Resource.mIndex] : nullptr;
	}

	const FArdaInductorOptions& FArdaInductorGraph::GetOptions() const
	{
		return FArdaInductorGraphAccess::State(*this).mOptions;
	}

	FArdaRHIDeviceRef FArdaInductorGraph::GetDevice() const
	{
		return FArdaInductorGraphAccess::State(*this).mDevice;
	}

	double FArdaInductorGraph::GetNodeCost(FArdaGraphNodeHandle Node) const
	{
		const auto& G = FArdaInductorGraphAccess::State(*this);
		const auto* N = G.mTopology.TryGetNode(Node);
		return N ? inductor_detail::NodeCost(G, N->mPayload) : 0;
	}
}
