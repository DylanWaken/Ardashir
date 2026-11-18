/** Mutable preprocessing view of the compiler-owned graph snapshot. */
#pragma once
#include "Graph/Node/ArdaDependencyTopology.h"
#include "Graph/Resources/ArdaDependencyResource.h"
#include "RHI/Device/ArdaRHIDevice.h"
#include <EASTL/functional.h>

namespace arda
{
	struct FArdaInductorOptions;
	struct FArdaInductorGraphAccess;
	class FArdaDependencyGraph;
	class FArdaGraphEdit;

	/** Borrowed graph view, valid only during rule application. Preprocessing may invoke registered
     * nodes through Edit, remove nodes, and revise descriptions, connections or resource declarations.
     * Compile rules receive a const view. Failed preprocessing discards its private graph copy.
     */
	class FArdaInductorGraph
	{
	public:
		FArdaDependencyTopology& GetTopology();
		const FArdaDependencyTopology& GetTopology() const;
		/** Result-owned execution description for one live node. Preprocessing may revise its effects. */
		FArdaDependencyNodeDesc& GetNodeDescription(FArdaGraphNodeHandle Node);
		const FArdaDependencyNodeDesc& GetNodeDescription(FArdaGraphNodeHandle Node) const;
		const eastl::vector<FArdaDependencyResourceDesc>& GetResources() const;
		FArdaDependencyResourceDesc* EditResource(FArdaDependencyResourceHandle Resource);
		/** Apply normal graph operations directly to the preprocessing copy. The callback cannot commit it. */
		FArdaRHIStatus Edit(const eastl::function<FArdaRHIStatus(FArdaGraphEdit&)>& Content);
		/** Remove a node, its ordering connections and private authoring cache. */
		FArdaRHIStatus RemoveNode(FArdaGraphNodeHandle Node);
		const FArdaInductorOptions& GetOptions() const;
		FArdaRHIDeviceRef GetDevice() const;
		double GetNodeCost(FArdaGraphNodeHandle Node) const;

	private:
		friend struct FArdaInductorGraphAccess;

		explicit FArdaInductorGraph(void* State, FArdaDependencyGraph* Owner)
		    : mState(State),
		      mOwner(Owner)
		{
		}

		void* mState;
		FArdaDependencyGraph* mOwner;
	};
}
