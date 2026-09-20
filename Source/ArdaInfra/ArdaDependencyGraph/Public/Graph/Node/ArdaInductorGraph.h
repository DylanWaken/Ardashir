/** Mutable preprocessing view of the compiler-owned graph snapshot. */
#pragma once
#include "Graph/Node/ArdaDependencyTopology.h"
#include "Graph/Resources/ArdaDependencyResource.h"
#include "RHI/Device/ArdaRHIDevice.h"

namespace arda
{
	struct FArdaInductorOptions;
	struct FArdaInductorGraphAccess;

	/** Borrowed compiler input, valid only during rule application. Preprocess rules may edit
     * topology and resource descriptors. Compile rules receive a const view. Changes affect the
     * pending revision only; a failed compilation discards the entire snapshot.
     */
	class FArdaInductorGraph
	{
	public:
		FArdaDependencyTopology& GetTopology();
		const FArdaDependencyTopology& GetTopology() const;
		const eastl::vector<FArdaDependencyResourceDesc>& GetResources() const;
		FArdaDependencyResourceDesc* EditResource(FArdaDependencyResourceHandle Resource);
		const FArdaInductorOptions& GetOptions() const;
		FArdaRHIDeviceRef GetDevice() const;
		double GetNodeCost(FArdaGraphNodeHandle Node) const;

	private:
		friend struct FArdaInductorGraphAccess;

		explicit FArdaInductorGraph(void* State)
		    : mState(State)
		{
		}

		void* mState;
	};
}
