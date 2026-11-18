#pragma once

#include "Inductor/Rules/ArdaInductorGraphInternal.h"
#include <EASTL/algorithm.h>

namespace arda
{
	/** Reopen prior stage declarations only when new stages require another assembly pass. */
	class FArdaStageDeclarationRestore
	{
	public:
		FArdaStageDeclarationRestore(FArdaDependencyGraph::FArdaImpl& Graph, bool bGraphics)
		    : mGraph(Graph)
		{
			const auto IsStage = [bGraphics](const FArdaDependencyNode& Node)
			{
				return bGraphics ? Node.mDesc.mGraphicsStage != EArdaGraphicsStage::None
				                 : Node.mDesc.mRayStage != EArdaRHIShaderStage::None;
			};
			const auto IsLowered = [bGraphics](const FArdaDependencyNode& Node)
			{
				return bGraphics ? Node.mbGraphicsLowered : Node.mbRayTracingLowered;
			};
			const auto Handles = Graph.mTopology.GetNodes();
			if (eastl::none_of(Handles.begin(),
			        Handles.end(),
			        [&](FArdaGraphNodeHandle Handle)
			        {
				        const auto& Node = Graph.mTopology.TryGetNode(Handle)->mPayload;
				        return IsStage(Node) && !IsLowered(Node);
			        }))
			{
				return;
			}
			for (auto Handle : Handles)
			{
				auto& Node = Graph.mTopology.TryGetNode(Handle)->mPayload;
				if (!IsStage(Node) || !IsLowered(Node) || !Node.mStageDeclaration)
				{
					continue;
				}
				mOptimizations.emplace(Handle,
				    FArdaStageOptimizations{Node.mDesc.mEstimatedCost,
				        Node.mDesc.mWorkspaceBytes,
				        Node.mDesc.mTransientWorkspaceBytes,
				        Node.mDesc.mbSideEffect,
				        Node.mDesc.mRequirements});
				Node.mDesc = *Node.mStageDeclaration;
				Graph.NormalizeDescription(Node.mDesc);
				if (bGraphics)
				{
					Node.mbGraphicsLowered = false;
					Node.mGraphicsStages.clear();
				}
				else
				{
					Node.mbRayTracingLowered = false;
				}
			}
		}

		/** Preserve previous graph optimizations without counting aggregated stage costs twice. */
		void PreserveOptimizations()
		{
			for (const auto& Entry : mOptimizations)
			{
				auto& Desc = mGraph.mTopology.TryGetNode(Entry.first)->mPayload.mDesc;
				Desc.mEstimatedCost = Entry.second.mEstimatedCost;
				Desc.mWorkspaceBytes = Entry.second.mWorkspaceBytes;
				Desc.mTransientWorkspaceBytes = Entry.second.mTransientWorkspaceBytes;
				Desc.mbSideEffect = Entry.second.mbSideEffect;
				Desc.mRequirements = Entry.second.mRequirements;
			}
		}

	private:
		struct FArdaStageOptimizations
		{
			uint32_t mEstimatedCost;
			uint64_t mWorkspaceBytes;
			uint64_t mTransientWorkspaceBytes;
			bool mbSideEffect;
			FArdaDependencyNodeRequirements mRequirements;
		};

		FArdaDependencyGraph::FArdaImpl& mGraph;
		eastl::unordered_map<FArdaGraphNodeHandle, FArdaStageOptimizations, FArdaGraphNodeHandleHash> mOptimizations;
	};
}
