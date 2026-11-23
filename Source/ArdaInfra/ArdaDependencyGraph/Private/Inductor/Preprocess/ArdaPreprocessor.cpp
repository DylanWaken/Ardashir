#include "Inductor/Preprocess/ArdaPreprocessor.h"
#include "Graph/Node/ArdaGraphClone.h"
#include "Graph/Node/ArdaGraphRevisionState.h"
#include "Graph/Node/ArdaGraphEditOrdering.h"
#include "Graph/Node/ArdaGraphRecordingScope.h"
#include "Inductor/Rules/ArdaInductorGraphInternal.h"
#include "Inductor/Rules/ArdaDependencyHazards.h"

#include <exception>

namespace arda
{
	TArdaRHIResult<eastl::unique_ptr<FArdaDependencyGraph>> FArdaDependencyGraph::Preprocess() const
	{
		return FArdaPreprocessor::Preprocess(*this);
	}

	bool FArdaDependencyGraph::IsPreprocessed() const
	{
		return mImpl->mbPreprocessed;
	}

	TArdaRHIResult<eastl::unique_ptr<FArdaDependencyGraph>> FArdaPreprocessor::Preprocess(
	    const FArdaDependencyGraph& Source)
	{
		if (auto Status = CheckArdaGraphRecordingReentry(Source.mRevisions.get()); !Status)
		{
			return {{}, Status};
		}
		std::lock_guard<std::recursive_mutex> Lock(Source.mRevisions->mMutex);

		if (Source.mRevisions->mActiveEdits || Source.mImpl->mbPreprocessing ||
		    Source.mImpl->mbCompiling || Source.mImpl->mbExecuting)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			        "Preprocessing requires closed edits and cannot recurse.")};
		}

		struct FArdaSourceGuard
		{
			bool& mLocked;
			bool& mPreprocessing;
			bool mbPrevious;

			explicit FArdaSourceGuard(bool& Locked, bool& Preprocessing)
			    : mLocked(Locked),
			      mPreprocessing(Preprocessing),
			      mbPrevious(Locked)
			{
				mLocked = true;
				mPreprocessing = true;
			}

			~FArdaSourceGuard()
			{
				mLocked = mbPrevious;
				mPreprocessing = false;
			}
		} Guard(Source.mRevisions->mbLocked, Source.mImpl->mbPreprocessing);

		auto Graph = eastl::make_unique<FArdaDependencyGraph>();
		const auto& Admission = Source.mImpl->mAdmissionPreprocessed;
		Graph->mImpl =
		    Admission ? CloneArdaGraphConstruction(*Admission, true) : CloneArdaGraphConstruction(*Source.mImpl);
		Graph->mRevisions->mbBuilding = true;
		if (!Graph->mImpl->mResourceGenerationSource)
		{
			Graph->mImpl->mResourceGenerationSource = Source.mRevisions->mResourceGenerations;
		}
		Graph->mRevisions->mResourceGenerations = Graph->mImpl->mResourceGenerationSource;
		auto Status = PreprocessPending(*Graph);
		if (!Status)
		{
			return {{}, eastl::move(Status)};
		}
		return {eastl::move(Graph), {}};
	}

	FArdaRHIStatus FArdaPreprocessor::PreprocessPending(FArdaDependencyGraph& Graph)
	{
		using namespace inductor_detail;
		auto& G = *Graph.mImpl;
		if (G.mbPreprocessing || G.mbCompiling)
		{
			return Error("Preprocessing cannot run recursively or during compilation.");
		}
		if (G.mbPreprocessed)
		{
			return {};
		}
		OrderArdaGraphEdits(G);

		struct FArdaPreprocessGuard
		{
			FArdaDependencyGraph::FArdaImpl& mGraph;
			bool mbWasEditing;

			explicit FArdaPreprocessGuard(FArdaDependencyGraph::FArdaImpl& State)
			    : mGraph(State),
			      mbWasEditing(State.mbEditing)
			{
				State.mbEditing = true;
				State.mbPreprocessing = true;
			}

			~FArdaPreprocessGuard()
			{
				mGraph.mbPreprocessing = false;
				mGraph.mbEditing = mbWasEditing;
			}
		} Guard(G);

		const char* Applying = "Preprocessor validation";
		try
		{
			auto Input = FArdaInductorGraphAccess::View(G, &Graph);
			for (const auto& Rule : G.mOptions.mRules.mPreprocessRules)
			{
				if (!Rule || !Rule->GetName() || !*Rule->GetName())
				{
					return Error("Preprocessing requires non-null named rules.");
				}
				Applying = Rule->GetName();
				if (auto Status = Rule->Apply(Input); !Status)
				{
					Status.mMessage = eastl::string(Applying) + ": " + Status.mMessage;
					return Status;
				}
			}
			Applying = "Preprocessor validation";
			G.mNames.clear();
			for (auto Handle : G.mTopology.GetNodes())
			{
				const auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
				if (Node.mHandle != Handle || !Node.mDefinition || !Node.mSourceParameters)
				{
					return Error(
					    "Preprocessing inserted an invalid node; use the graph editing API to invoke a registered definition.");
				}
				if (Node.mDefinition->mExpand)
				{
					return Error("Subgraph nodes require FArdaExpandSubgraphsRule before compilation.");
				}
				if (Node.mDesc.mGraphicsStage != EArdaGraphicsStage::None && !Node.mbGraphicsLowered)
				{
					return Error("Graphics stages require FArdaAssembleGraphicsStagesRule before compilation.");
				}
				if (Node.mDesc.mRayStage != EArdaRHIShaderStage::None && !Node.mbRayTracingLowered)
				{
					return Error("Ray stages require FArdaAssembleRayTracingStagesRule before compilation.");
				}
				if (Node.mDesc.mGraphicsStage == EArdaGraphicsStage::Pixel)
				{
					if (Node.mGraphicsStages.size() < 3 || Node.mGraphicsStages.back() != Handle)
					{
						return Error("Preprocessing invalidated an assembled graphics chain.");
					}
					for (auto Stage : Node.mGraphicsStages)
					{
						if (!G.mTopology.ContainsNode(Stage) ||
						    (Stage != Handle && !G.mTopology.IsReachable(Stage, Handle)))
						{
							return Error("Preprocessing removed a required graphics stage dependency.");
						}
					}
				}
				if (!G.mNames.emplace(Node.mName, Handle).second)
				{
					return Error("Preprocessing produced duplicate node names.");
				}
			}
			if (auto Status = ValidateResourceAccesses(G); !Status)
			{
				return Status;
			}
			if (!G.mTopology.TopologicalSort().IsAcyclic())
			{
				return Error("Preprocessing left a dependency cycle.");
			}
			auto Required = G.mTopology.CloneSnapshot();
			if (auto Status = ResolveArdaDependencyResourceEdges(G, Required); !Status)
			{
				return Status;
			}
			for (auto Handle : Required.GetEdges())
			{
				const auto& Edge = *Required.TryGetEdge(Handle);
				if ((Edge.mPayload.mbResource || Edge.mPayload.mbHazard) &&
				    !G.mTopology.IsReachable(Edge.mFrom, Edge.mTo))
				{
					return Error("Preprocessing omitted a required resource dependency.");
				}
			}
			G.mbPreprocessed = true;
			return {};
		}
		catch (const std::exception& Error)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
			    (eastl::string(Applying) + ": " + Error.what()).c_str());
		}
		catch (...)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
			    (eastl::string(Applying) + ": preprocessing rule threw.").c_str());
		}
	}
}
