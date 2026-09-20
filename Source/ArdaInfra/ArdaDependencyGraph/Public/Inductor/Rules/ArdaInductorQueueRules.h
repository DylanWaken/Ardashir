/** Public queue policy contract used by compilation and schedule cost evaluation. */
#pragma once

#include "Graph/Node/ArdaDependencyTopology.h"
#include "Graph/Resources/ArdaDependencyResource.h"
#include "Inductor/Rules/ArdaGraphRule.h"
#include "RHI/Device/ArdaRHIDevice.h"
#include <EASTL/functional.h>

namespace arda
{
	struct FArdaInductorOptions;

	/** Borrowed immutable inputs valid only for the duration of Assign. Policies may be
	 * evaluated repeatedly for candidate orders and must not retain these references. */
	struct FArdaInductorQueueContext
	{
		const FArdaDependencyTopology& mTopology;
		const eastl::vector<FArdaDependencyResourceDesc>& mResources;
		const FArdaInductorOptions& mOptions;
		FArdaRHIDeviceRef mDevice;
		eastl::function<double(FArdaGraphNodeHandle)> mNodeCost;
	};

	/** Queue placement transform; multiple instances may refine the same result in list order.
     * Assign is also used by cost evaluation and must be deterministic and side-effect free.
     */
	class FArdaInductorQueueRule : public FArdaGraphCompileRule
	{
	public:
		FArdaRHIStatus Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const final;
		virtual FArdaRHIStatus Assign(const FArdaInductorQueueContext& Graph,
		    FArdaInductorCompileResult& Result) const = 0;
	};

	class FArdaAssignQueuesRule final : public FArdaInductorQueueRule
	{
	public:
		explicit FArdaAssignQueuesRule(bool EnableCopy = true, bool EnableCompute = true)
		    : mbEnableCopy(EnableCopy),
		      mbEnableCompute(EnableCompute)
		{
		}

		const char* GetName() const noexcept override
		{
			return "AssignQueues";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Assign(const FArdaInductorQueueContext&, FArdaInductorCompileResult&) const override;
		bool mbEnableCopy, mbEnableCompute;
	};

	class FArdaGraphicsQueueRule final : public FArdaInductorQueueRule
	{
	public:
		const char* GetName() const noexcept override
		{
			return "GraphicsQueue";
		}

		bool AllowsAdaptiveScheduling() const noexcept override
		{
			return true;
		}

		FArdaRHIStatus Assign(const FArdaInductorQueueContext&, FArdaInductorCompileResult&) const override;
	};
}
