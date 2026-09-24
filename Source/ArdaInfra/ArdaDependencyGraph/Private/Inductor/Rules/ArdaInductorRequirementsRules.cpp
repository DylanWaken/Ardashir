#include "Inductor/Rules/ArdaInductorRequirementsRules.h"
#include "Inductor/Rules/ArdaInductorRequirements.h"
#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include <exception>

namespace arda
{
	FArdaRHIStatus CheckArdaGraphRequiredFeatures(const FArdaDependencyGraph::FArdaImpl& Graph)
	{
		// Custom preprocessing may erase authored operations. Their admission requirements remain
		// part of this construction even when no corresponding node survives in the lowered graph.
		if (Graph.mCompilerSource && Graph.mCompilerSource.get() != &Graph)
		{
			if (auto Status = CheckArdaGraphRequiredFeatures(*Graph.mCompilerSource); !Status)
			{
				return Status;
			}
		}
		for (const auto Handle : Graph.mTopology.GetNodes())
		{
			const auto& Node = Graph.mTopology.TryGetNode(Handle)->mPayload;
			const auto Check = [&]() -> FArdaRHIStatus
			{
				if (!Node.mDefinition || !Node.mDefinition->mGetRequirements || !Node.mSourceParameters)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "Required-feature validation needs a registered definition and frozen public parameters.");
				}
				// Reevaluate the selected implementation's explicit environment contract, even without a device.
				const auto Explicit = Node.mDefinition->mGetRequirements(Node.mSourceParameters.get());
				if (auto Status = Explicit.Check(Graph.mDevice.Get()); !Status)
				{
					return Status;
				}
				if (!Graph.mDevice)
				{
					return {}; // Device-independent graph analysis does not assert inferred native support.
				}
				auto Description = Node.mDesc;
				if (Node.mDefinition->mAddIntrinsicRequirements)
				{
					Node.mDefinition->mAddIntrinsicRequirements(Description.mRequirements);
				}
				// Preprocessing may assemble new pipelines or resource accesses after initial admission.
				InferArdaNodeRequirements(Graph, Node.mDefinition->mKind, Description);
				return Description.mRequirements.Check(Graph.mDevice.Get());
			};
			FArdaRHIStatus Status;
			try
			{
				Status = Check();
			}
			catch (const std::exception& Error)
			{
				Status = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
			}
			catch (...)
			{
				Status = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
				    "A required-feature predicate threw an exception.");
			}
			if (!Status)
			{
				Status.mMessage = "Node '" + Node.mName + "'" +
				    (Node.mDefinition ? " (" + Node.mDefinition->mName + ")" : eastl::string{}) + ": " +
				    Status.mMessage;
				return Status;
			}
		}
		return {};
	}

	FArdaRHIStatus FArdaCheckRequiredFeaturesRule::Apply(const FArdaInductorGraph& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		if (!Result.mStatus)
		{
			return Result.mStatus;
		}
		return CheckArdaGraphRequiredFeatures(FArdaInductorGraphAccess::State(Graph));
	}
}
