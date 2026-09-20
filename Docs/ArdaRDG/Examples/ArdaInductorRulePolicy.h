#pragma once

#include "ArdaDependencyGraph.h"
#include <EASTL/algorithm.h>

namespace arda::examples
{
    // Adjust scheduling hints on the private working graph before flattening.
    class FMinimumNodeCostRule final : public FArdaGraphPreprocessRule
    {
    public:
        explicit FMinimumNodeCostRule(uint32_t MinimumCost) : mMinimumCost(MinimumCost) {}

        const char* GetName() const noexcept override { return "MinimumNodeCost"; }

        FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const override
        {
            if (!mMinimumCost)
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Minimum cost must be positive.");
            auto& Topology = Graph.GetTopology();
            for (auto Handle : Topology.GetNodes())
            {
                auto& Description = Topology.TryGetNode(Handle)->mPayload.mDesc;
                Description.mEstimatedCost = eastl::max(Description.mEstimatedCost, mMinimumCost);
            }
            return {};
        }

        uint32_t mMinimumCost;
    };

    // Illustrative ready-list policy: highest estimated node cost first, then name.
    // IDs are opaque; never use an ID as an index into Nodes.
    struct FCostFirstReadyPolicy
    {
        TArdaRHIResult<eastl::vector<uint32_t>> Order(const FArdaInductorGraph&,
            const eastl::vector<FArdaInductorOrderingNode>& Nodes) const
        {
            eastl::vector<uint32_t> Order;
            Order.reserve(Nodes.size());
            while (Order.size() != Nodes.size())
            {
                const FArdaInductorOrderingNode* Best = nullptr;
                for (const auto& Node : Nodes)
                {
                    if (eastl::find(Order.begin(), Order.end(), Node.mNodeId) != Order.end())
                        continue;
                    bool Ready = true;
                    for (uint32_t Predecessor : Node.mDependencies)
                    {
                        if (eastl::find(Order.begin(), Order.end(), Predecessor) == Order.end())
                        {
                            Ready = false;
                            break;
                        }
                    }
                    if (Ready && (!Best || Node.mEstimatedCost > Best->mEstimatedCost ||
                        (Node.mEstimatedCost == Best->mEstimatedCost && Node.mName < Best->mName)))
                        Best = &Node;
                }
                if (!Best)
                    return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                        "The ordering projection has no ready node.")};
                Order.push_back(Best->mNodeId);
            }
            return {eastl::move(Order), {}};
        }
    };

    inline FArdaInductorOptions MakeCostFirstOptions()
    {
        FArdaInductorOptions Options;
        Options.mRules.mCompileRules = {
            MakeArdaGraphRule<FArdaFlattenGraphRule>(),
            MakeArdaGraphRule<FArdaCullUnreachableRule>(),
            MakeArdaGraphRule<FArdaInferPipelinesRule>(),
            MakeArdaGraphRule<TArdaInductorOrderingRule<FCostFirstReadyPolicy>>(),
            MakeArdaGraphRule<FArdaPlanMemoryRule>(),
            MakeArdaGraphRule<FArdaAssignQueuesRule>(),
            MakeArdaGraphRule<FArdaBatchCudaRule>(),
            MakeArdaGraphRule<FArdaFlattenDispatchesRule>(),
            MakeArdaGraphRule<FArdaResolveFencesRule>()};
        return Options;
    }
}
