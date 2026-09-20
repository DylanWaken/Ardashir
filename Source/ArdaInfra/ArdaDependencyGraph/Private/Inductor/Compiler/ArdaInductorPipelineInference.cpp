#include "Inductor/Rules/ArdaInductorPipelineInternal.h"

namespace arda
{
	using namespace pipeline_detail;

	TArdaRHIResult<FArdaInductorPipelinePattern> InferArdaInductorPipeline(
	    const eastl::vector<FArdaInductorPipelineNode>& Nodes,
	    const FArdaInductorPipelineRequest& Request)
	{
		eastl::unordered_map<uint64_t, const FArdaInductorPipelineNode*> Lookup;
		for (const auto& Node : Nodes)
		{
			if (!Lookup.emplace(Node.mNodeId, &Node).second)
			{
				return {{}, Invalid("Pipeline topology has duplicate node IDs.")};
			}
		}
		eastl::unordered_map<uint64_t, uint8_t> States;
		eastl::vector<eastl::pair<uint64_t, bool>> Pending{{Request.mTerminalNodeId, false}};
		while (!Pending.empty())
		{
			const auto [Id, bLeave] = Pending.back();
			Pending.pop_back();
			const auto Found = Lookup.find(Id);
			if (Found == Lookup.end())
			{
				return {{}, Invalid("Pipeline topology references a missing node.")};
			}
			if (bLeave)
			{
				States[Id] = 2;
				continue;
			}
			if (States[Id] == 2)
			{
				continue;
			}
			if (States[Id] == 1)
			{
				return {{}, Invalid("Pipeline topology contains a dependency cycle.")};
			}
			States[Id] = 1;
			Pending.push_back({Id, true});
			for (auto Dependency : Found->second->mDependencies)
			{
				Pending.push_back({Dependency, false});
			}
		}
		// Validate the complete ancestry above, even across completed consumers. Stage
		// collection has narrower semantics: an upstream same-family request owns its
		// pipeline, so its shaders/settings do not become part of the next dispatch/draw.
		States.clear();
		eastl::vector<uint64_t> Candidates{Request.mTerminalNodeId};
		eastl::vector<const FArdaInductorPipelineNode*> Reachable;
		while (!Candidates.empty())
		{
			const uint64_t Id = Candidates.back();
			Candidates.pop_back();
			if (!States.emplace(Id, 1).second)
			{
				continue;
			}
			const auto* Node = Lookup.at(Id);
			if (Id != Request.mTerminalNodeId &&
			    eastl::find(Node->mPipelineBoundaries.begin(), Node->mPipelineBoundaries.end(), Request.mKind) !=
			        Node->mPipelineBoundaries.end())
			{
				continue;
			}
			Reachable.push_back(Node);
			Candidates.insert(Candidates.end(), Node->mDependencies.begin(), Node->mDependencies.end());
		}
		eastl::sort(Reachable.begin(),
		    Reachable.end(),
		    [](const auto* A, const auto* B)
		    {
			    return A->mNodeId < B->mNodeId;
		    });
		auto Eligible = [&](const FArdaInductorPipelineContribution& C)
		{
			return (C.mShader && Matches(Request.mKind, C.mShader->GetStage())) ||
			    (C.mConfiguration && C.mConfiguration->mKind == Request.mKind);
		};
		eastl::string Group = Request.mGroup;
		if (Group.empty())
		{
			for (const auto* Node : Reachable)
			{
				for (const auto& C : Node->mContributions)
				{
					if (Eligible(C) && !C.mGroup.empty())
					{
						if (!Group.empty() && Group != C.mGroup)
						{
							return {{},
							    Invalid("Multiple reachable pipeline groups require an explicit request group.")};
						}
						Group = C.mGroup;
					}
				}
			}
		}
		FArdaInductorPipelinePattern Pattern;
		Pattern.mConfiguration.mKind = Request.mKind;
		bool bHaveSettings = false;
		if (Request.mConfiguration)
		{
			if (auto Status = MergeConfiguration(Pattern.mConfiguration, bHaveSettings, *Request.mConfiguration);
			    !Status)
			{
				return {{}, Status};
			}
		}
		// Merge fixed settings before adding standalone stages so discovering a late settings
		// anchor cannot discard shaders already collected from another branch.
		for (const auto* Node : Reachable)
		{
			for (const auto& C : Node->mContributions)
			{
				if (Eligible(C) && (C.mGroup.empty() || C.mGroup == Group) && C.mConfiguration)
				{
					if (auto Status = MergeConfiguration(Pattern.mConfiguration, bHaveSettings, *C.mConfiguration);
					    !Status)
					{
						return {{}, Status};
					}
				}
			}
		}
		for (const auto* Node : Reachable)
		{
			bool bContributed = false;
			for (const auto& C : Node->mContributions)
			{
				if (!Eligible(C) || (!C.mGroup.empty() && C.mGroup != Group))
				{
					continue;
				}
				if (auto Status = MergeLayouts(GlobalLayouts(Pattern.mConfiguration), C.mBindingLayouts); !Status)
				{
					return {{}, Status};
				}
				if (auto Status = AddShader(Pattern.mConfiguration, C); !Status)
				{
					return {{}, Status};
				}
				bContributed = true;
			}
			if (bContributed)
			{
				Pattern.mContributingNodeIds.push_back(Node->mNodeId);
			}
		}
		if (auto Status = MergeLayouts(GlobalLayouts(Pattern.mConfiguration), Request.mBindingLayouts); !Status)
		{
			return {{}, Status};
		}
		for (const auto& Local : Request.mLocalBindingLayouts)
		{
			if (Request.mKind != EArdaPipelineStateKind::RayTracing || !Local.second)
			{
				return {{}, Invalid("Record-local layouts require a ray-tracing request and a valid layout.")};
			}
			auto& Ray = Pattern.mConfiguration.mRayTracing.mDesc;
			bool Found = false;
			for (auto& Shader : Ray.mShaders)
			{
				if (Shader.mExportName == Local.first)
				{
					Found = true;
					if (auto S = MergeLocalLayout(Shader.mLocalBindingLayout, Local.second); !S)
					{
						return {{}, S};
					}
				}
			}
			for (auto& Hit : Ray.mHitGroups)
			{
				if (Hit.mExportName == Local.first)
				{
					Found = true;
					if (auto S = MergeLocalLayout(Hit.mLocalBindingLayout, Local.second); !S)
					{
						return {{}, S};
					}
				}
			}
			if (!Found)
			{
				return {{}, Invalid("Record-local layout names an export absent from the inferred ray pipeline.")};
			}
		}
		if (auto Status = ValidatePattern(Pattern.mConfiguration); !Status)
		{
			return {{}, Status};
		}
		const auto Key = ConfigurationKey(Pattern.mConfiguration);
		if (!Key.mbValid)
		{
			return {{}, Invalid("Pipeline semantics lack a stable shader identity or use an unknown pipeline kind.")};
		}
		Pattern.mStableKey = Key.Finish();
		return {eastl::move(Pattern), {}};
	}
}
