#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include "Inductor/Compiler/ArdaInductor.h"
#include "Inductor/Rules/ArdaDependencyHazards.h"
#include <exception>

namespace arda
{
	FArdaRHIStatus FArdaInductor::Compile(FArdaDependencyGraph& Graph)
	{
		return Graph.Compile();
	}

	FArdaRHIStatus FArdaInductor::CompilePending(FArdaDependencyGraph& Graph)
	{
		using namespace inductor_detail;
		auto& G = *Graph.mImpl;
		if (G.mbCompiling || !G.mbEditing)
		{
			return Error("Compilation requires an isolated pending graph and cannot recurse.");
		}
		if (auto S = ValidateArdaInductorRules(G.mOptions.mRules); !S)
		{
			return S;
		}

		struct FGuard
		{
			bool& mFlag;

			explicit FGuard(bool& Flag)
			    : mFlag(Flag)
			{
				mFlag = true;
			}

			~FGuard()
			{
				mFlag = false;
			}
		} Guard(G.mbCompiling);

		const char* Applying = "Compiler validation";
		try
		{
			auto Input = FArdaInductorGraphAccess::View(G);
			for (const auto& Rule : G.mOptions.mRules.mPreprocessRules)
			{
				Applying = Rule->GetName();
				if (auto S = Rule->Apply(Input); !S)
				{
					S.mMessage = eastl::string(Applying) + ": " + S.mMessage;
					return S;
				}
			}
			// Structural safety is independent of the chosen transformation list.
			if (auto S = ValidateResourceAccesses(G); !S)
			{
				return S;
			}
			if (!G.mTopology.TopologicalSort().IsAcyclic())
			{
				return Error("Preprocessing left a dependency cycle.");
			}
			auto Required = G.mTopology.CloneSnapshot();
			if (auto S = ResolveArdaDependencyResourceEdges(G, Required); !S)
			{
				return S;
			}
			for (auto E : Required.GetEdges())
			{
				const auto& Edge = *Required.TryGetEdge(E);
				if ((Edge.mPayload.mbResource || Edge.mPayload.mbHazard) &&
				    !G.mTopology.IsReachable(Edge.mFrom, Edge.mTo))
				{
					return Error("Preprocessing omitted a required resource dependency.");
				}
			}
			G.mNames.clear();
			for (auto N : G.mTopology.GetNodes())
			{
				if (!G.mNames.emplace(G.mTopology.TryGetNode(N)->mPayload.mName, N).second)
				{
					return Error("Preprocessing produced duplicate node names.");
				}
			}
			FArdaInductorCompileResult Result;
			for (const auto& Rule : G.mOptions.mRules.mCompileRules)
			{
				Applying = Rule->GetName();
				if (auto S = Rule->Apply(Input, Result); !S)
				{
					S.mMessage = eastl::string(Applying) + ": " + S.mMessage;
					return S;
				}
				if (!Result.mStatus)
				{
					return Result.mStatus;
				}
			}
			Applying = "Compiler validation";
			if (auto S = ValidateCompiledResult(G, Result); !S)
			{
				return S;
			}
			auto Pending = eastl::make_unique<FArdaMemoryPlan>(*Result.mMemoryPlan);
			Result.mRevision = G.mCompile.mRevision + 1;
			G.mCompile = eastl::move(Result);
			G.mPendingMemoryPlan = eastl::move(Pending);
			G.mRuntime.reset();
			return {};
		}
		catch (const std::exception& E)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
			    (eastl::string(Applying) + ": " + E.what()).c_str());
		}
		catch (...)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
			    (eastl::string(Applying) + ": rule threw; pending revision discarded.").c_str());
		}
	}
}
