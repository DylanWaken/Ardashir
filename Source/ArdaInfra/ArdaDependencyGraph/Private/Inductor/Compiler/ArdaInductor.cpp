#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include "Inductor/Compiler/ArdaInductor.h"
#include "Inductor/Compiler/ArdaInductorCompiledGraph.h"
#include "Inductor/Compiler/ArdaInductorNodePreparation.h"
#include "Graph/Node/ArdaGraphClone.h"
#include "Graph/Node/ArdaGraphExternalValues.h"
#include "Graph/Node/ArdaGraphRevisionState.h"
#include "Graph/Node/ArdaGraphRecordingScope.h"
#include <exception>

namespace arda
{
	TArdaRHIResult<FArdaInductorCompileResult> FArdaInductor::Compile(const FArdaDependencyGraph& Graph)
	{
		return CompileWithContext(Graph, nullptr);
	}

	TArdaRHIResult<FArdaInductorCompileResult> FArdaInductor::Compile(const FArdaDependencyGraph& Graph,
	    FArdaRHIDeviceRef Device)
	{
		return CompileWithContext(Graph, nullptr, eastl::move(Device));
	}

	TArdaRHIResult<FArdaInductorCompileResult> FArdaInductor::CompileWithContext(const FArdaDependencyGraph& Graph,
	    const FArdaInductorExecutionState* Context,
	    FArdaRHIDeviceRef Device)
	{
		using namespace inductor_detail;
		if (auto Status = CheckArdaGraphRecordingReentry(Graph.mRevisions.get()); !Status)
		{
			return {{}, Status};
		}
		std::lock_guard<std::recursive_mutex> Lock(Graph.mRevisions->mMutex);

		const auto& Source = Graph.GetReadImpl();
		if (Graph.IsEditing() || Source.mbCompiling || Source.mbPreprocessing || !Source.mbPreprocessed)
		{
			return {{}, Error("Compilation requires a closed preprocessed graph; call Preprocess first.")};
		}
		if (auto Status = ValidateArdaInductorRules(Source.mOptions.mRules); !Status)
		{
			return {{}, Status};
		}
		for (const auto& Resource : Source.mResources)
		{
			if (Resource.IsExternal() && !Resource.HasExternalBinding())
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				        (eastl::string("External resource slot '") + Resource.mName +
				            "' requires a binding before compilation.")
				            .c_str())};
			}
		}
		auto Snapshot = CloneArdaGraphConstruction(Source);
		auto& G = *Snapshot;
		if (Device)
		{
			G.mCompile.mExecution->mDevice = eastl::move(Device);
		}
		if (Context)
		{
			auto& Execution = *G.mCompile.mExecution;
			Execution.mPipelineCache = Context->mPipelineCache;
			Execution.mAutomaticLayouts = Context->mAutomaticLayouts;
			Execution.mPersistentBuffers = Context->mPersistentBuffers;
			Execution.mPersistentTextures = Context->mPersistentTextures;
		}
		for (const auto& Entry : G.mExternalValues)
		{
			if (!Entry.second.mValue)
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				        (eastl::string("External value slot '") + Entry.first +
				            "' requires a binding before compilation.")
				            .c_str())};
			}
		}

		struct FArdaCompileGuard
		{
			bool& mCompiling;

			explicit FArdaCompileGuard(bool& Value)
			    : mCompiling(Value)
			{
				mCompiling = true;
			}

			~FArdaCompileGuard()
			{
				mCompiling = false;
			}
		} Guard(Graph.mImpl->mbCompiling);

		G.mbCompiling = true;
		const char* Applying = "Compiler preparation";
		try
		{
			if (auto Status = RebindArdaGraphExternalParameters(G); !Status)
			{
				return {{}, Status};
			}
			if (auto Status = CheckArdaGraphRequiredFeatures(G); !Status)
			{
				return {{}, Status};
			}
			{
				FArdaDependencyGraph DeclarationContext;
				DeclarationContext.mImpl = Snapshot;
				if (auto Status = DeclarationContext.RevalidateCompilationParameters(); !Status)
				{
					return {{}, Status};
				}
			}
			if (auto Status = InitializeArdaInductorNodes(G); !Status)
			{
				return {{}, Status};
			}
			G.mbUsingCompiledDescriptions = true;
			auto Input = FArdaInductorGraphAccess::View(G);
			FArdaInductorCompileResult Result;
			Result.mNodes = G.mCompile.mNodes;
			Result.mSubgraphAdmissions = G.mSubgraphAdmissions;
			for (const auto& Rule : G.mOptions.mRules.mCompileRules)
			{
				Applying = Rule->GetName();
				if (auto Status = Rule->Apply(Input, Result); !Status)
				{
					Status.mMessage = eastl::string(Applying) + ": " + Status.mMessage;
					return {{}, Status};
				}
				if (!Result.mStatus)
				{
					return {{}, Result.mStatus};
				}
			}
			Applying = "Compiler validation";
			if (auto Status = ValidateCompiledResult(G, Result); !Status)
			{
				return {{}, Status};
			}
			Result.mRevision = Source.mCompile.mRevision + 1;
			Result.mExecution = G.mCompile.mExecution;
			Result.mExecution->mRuntime.reset();
			G.mbCompiling = false;
			G.mbUsingCompiledDescriptions = false;
			G.mCompile = {};
			G.mCompile.mExecution = eastl::make_shared<FArdaInductorExecutionState>();
			G.mCompile.mExecution->mDevice = Result.mExecution->mDevice;
			auto Retained = eastl::make_shared<FArdaInductorCompiledGraph>();
			Retained->mGraph = eastl::move(Snapshot);
			Result.mGraph = eastl::move(Retained);
			return {eastl::move(Result), {}};
		}
		catch (const std::exception& Exception)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
			        (eastl::string(Applying) + ": " + Exception.what()).c_str())};
		}
		catch (...)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
			        (eastl::string(Applying) + ": rule threw; compilation result discarded.").c_str())};
		}
	}

	FArdaRHIStatus FArdaInductor::CompilePending(FArdaDependencyGraph& Graph,
	    const FArdaInductorExecutionState* Context)
	{
		auto Compiled = CompileWithContext(Graph, Context);
		if (!Compiled)
		{
			return Compiled.mStatus;
		}
		Graph.mImpl->mCompile = eastl::move(Compiled.mValue);
		Graph.mImpl->mCompile.mExecution->mbExecutorClaimed = true;
		Graph.mImpl->mbUsingCompiledDescriptions = true;
		return {};
	}
}
