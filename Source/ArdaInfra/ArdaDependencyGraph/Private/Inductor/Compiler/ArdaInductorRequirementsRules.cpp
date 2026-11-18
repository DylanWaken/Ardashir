#include "Inductor/Compiler/ArdaInductorRequirementsRules.h"
#include "Inductor/Rules/ArdaInductorRequirements.h"
#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include <exception>

namespace arda
{
	namespace
	{
		FArdaRHIStatus RehydrateEnvironment(FArdaDependencyNodeRequirements& Requirements,
		    const FArdaDependencyNodeRequirements& Declaration)
		{
			for (auto& Condition : Requirements.mEnvironment)
			{
				for (const auto& Fresh : Declaration.mEnvironment)
				{
					if (Fresh.mName == Condition.mName)
					{
						Condition.mCheck = Fresh.mCheck;
						break;
					}
				}
				if (!Condition.mCheck)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
					    ("Archived environment predicate '" + Condition.mName +
					        "' is absent from the registered node declaration.")
					        .c_str());
				}
			}
			return {};
		}

		FArdaRHIStatus CheckFeatures(const FArdaDependencyGraph::FArdaImpl& Graph, const FArdaRHIDeviceRef& Device)
		{
			// Custom preprocessing may erase authored operations. Their admission requirements remain
			// part of this construction even when no corresponding node survives in the lowered graph.
			if (Graph.mCompilerSource && Graph.mCompilerSource.get() != &Graph)
			{
				if (auto Status = CheckFeatures(*Graph.mCompilerSource, Device); !Status)
				{
					return Status;
				}
			}
			for (const auto& Admission : Graph.mSubgraphAdmissions)
			{
				FArdaRHIStatus Status;
				try
				{
					if (!Admission.mDefinition || !Admission.mDefinition->mGetRequirements || !Admission.mParameters)
					{
						Status = FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
						    "Expanded subgraph admission requires its definition and frozen parameters.");
					}
					else
					{
						const auto Explicit = Admission.mDefinition->mGetRequirements(Admission.mParameters.get());
						Status = Explicit.Check(Device.Get());
						if (Status && Device)
						{
							auto Requirements = Admission.mRequirements;
							if (auto Hydrated = RehydrateEnvironment(Requirements, Explicit); !Hydrated)
							{
								return Hydrated;
							}
							Admission.mDefinition->mAddIntrinsicRequirements(Requirements);
							Status = Requirements.Check(Device.Get());
						}
					}
				}
				catch (const std::exception& Error)
				{
					Status = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
				}
				catch (...)
				{
					Status = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
					    "An expanded subgraph required-feature predicate threw an exception.");
				}
				if (!Status)
				{
					Status.mMessage = "Subgraph '" + Admission.mName + "': " + Status.mMessage;
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
					if (auto Status = Explicit.Check(Device.Get()); !Status)
					{
						return Status;
					}
					if (!Device)
					{
						return {}; // Device-independent graph analysis does not assert inferred native support.
					}
					auto Description = Graph.GetNodeDescription(Node);
					if (auto Status = RehydrateEnvironment(Description.mRequirements, Explicit); !Status)
					{
						return Status;
					}
					if (Node.mDefinition->mAddIntrinsicRequirements)
					{
						Node.mDefinition->mAddIntrinsicRequirements(Description.mRequirements);
					}
					// Preprocessing may assemble new pipelines or resource accesses after initial admission.
					InferArdaNodeRequirements(Graph, Node.mDefinition->mKind, Description);
					return Description.mRequirements.Check(Device.Get());
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
	}

	FArdaRHIStatus CheckArdaGraphRequiredFeatures(const FArdaDependencyGraph::FArdaImpl& Graph)
	{
		return CheckFeatures(Graph, Graph.mCompile.mExecution->mDevice);
	}

	FArdaRHIStatus FArdaCheckRequiredFeaturesRule::Compile(const FArdaInductorGraph& Graph,
	    FArdaInductorCompileResult& Result) const
	{
		if (!Result.mStatus)
		{
			return Result.mStatus;
		}
		return CheckArdaGraphRequiredFeatures(FArdaInductorGraphAccess::State(Graph));
	}
}
