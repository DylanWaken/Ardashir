#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaNodeDeclarationOrigins.h"
#include "Graph/Node/ArdaDependencyGraphStatus.h"
#include "Graph/Node/ArdaGraphConstructionKey.h"
#include "Inductor/Rules/ArdaInductorBindingRules.h"
#include "Inductor/Rules/ArdaInductorRequirements.h"
#include <EASTL/algorithm.h>
#include <EASTL/string.h>

namespace arda
{
	FArdaRHIStatus FArdaDependencyGraph::RevalidateCompilationParameters()
	{
		auto& G = *mImpl;
		if (!G.mbCompiling)
		{
			return GraphInvalid("Compilation parameter validation requires a private compiler snapshot.");
		}
		for (const auto Handle : G.mTopology.GetNodes())
		{
			auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
			if (!Node.mDesc.mbArchivedShaderAccesses &&
			    !(Node.mStageDeclaration && Node.mStageDeclaration->mbArchivedShaderAccesses))
			{
				continue;
			}
			if (!Node.mDefinition || !Node.mSourceParameters || !Node.mDefinition->mResolveResources)
			{
				return GraphInvalid("Compilation requires a registered node resource declaration.");
			}
			FArdaDependencyResourceContext Context(*this, Node.mName, Node.mOutputs);
			auto Resolved = Node.mDefinition->mResolveResources(Context, Node.mSourceParameters);
			if (!Resolved || !Resolved.mValue)
			{
				return Resolved ? GraphInvalid("Resource declaration returned no frozen public parameters.")
				                : Resolved.mStatus;
			}
			if (Context.mOutputs.size() != Node.mOutputs.size())
			{
				return GraphInvalid("Compiled node resource declarations differ from the preprocessed graph outputs.");
			}
			for (const auto& Output : Context.mOutputs)
			{
				const auto Found = eastl::find_if(Node.mOutputs.begin(),
				    Node.mOutputs.end(),
				    [&](const auto& Expected)
				    {
					    return Expected.mName == Output.mName &&
					        G.ResolveResource(Expected.mResource) == G.ResolveResource(Output.mResource);
				    });
				if (Found == Node.mOutputs.end())
				{
					return GraphInvalid("Compilation cannot change a node's declared output identity.");
				}
			}
			Node.mSourceParameters = eastl::move(Resolved.mValue);
		}
		return {};
	}

	TArdaRHIResult<FArdaGraphNodeHandle> FArdaDependencyGraph::AttachErased(eastl::string Name,
	    const eastl::string& Definition,
	    const void* Type,
	    eastl::shared_ptr<const void> Parameters,
	    eastl::shared_ptr<const FArdaDependencyNodeExecutable> RetainedDefinition)
	{
		auto& G = *mImpl;
		if (G.mbCompiling || (G.mbPreprocessing && !G.mbPreprocessEditing))
		{
			return {{}, GraphCompiling()};
		}
		if (!G.mbEditing)
		{
			return {{}, GraphNotEditing()};
		}
		auto D = RetainedDefinition ? eastl::move(RetainedDefinition) : FArdaNodeRegistry::Get().Find(Definition);
		if (Name.empty() || !D || D->mName != Definition || D->mParameterType != Type || !Parameters)
		{
			return {{}, GraphInvalid("Attach requires a known definition and matching typed parameters.")};
		}
		const auto AdmissionError = [&](FArdaRHIStatus Status)
		{
			Status.mMessage = "Node '" + Name + "' (" + Definition + "): " + Status.mMessage;
			return TArdaRHIResult<FArdaGraphNodeHandle>{{}, eastl::move(Status)};
		};
		// Lower an optional CUDA alternative before resource declarations, state caching and scheduling.
		const auto& Device = G.mCompile.mExecution->mDevice;
		if (D->mKind == EArdaDependencyNodeKind::Cuda && Device &&
		    (!Device->GetCudaCapabilities() ||
		        (D->mSelectComputeAlternative && D->mSelectComputeAlternative(Device.Get()))))
		{
			if (!D->mComputeAlternative)
			{
				return AdmissionError(FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
				    "CUDA is unavailable and this node has no compute shader alternative."));
			}
			D = D->mComputeAlternative;
		}
		// Admit explicit requirements before invoking any resource or device setup hook.
		auto Requirements = D->mGetRequirements(Parameters.get());
		// Offline authoring retains capability requirements for compilation against a destination device.
		if (G.mCompile.mExecution->mDevice)
		{
			D->mAddIntrinsicRequirements(Requirements);
		}
		if (auto S = G.mCompile.mExecution->mDevice ? Requirements.Check(G.mCompile.mExecution->mDevice.Get())
		                                            : Requirements.Validate();
		    !S)
		{
			return AdmissionError(eastl::move(S));
		}

		// Roll back only allocations made by this attachment; never reuse abandoned generations.
		struct FArdaResourceRollback
		{
			FArdaImpl& G;
			size_t Count;
			bool mbCommitted = false;

			~FArdaResourceRollback()
			{
				if (mbCommitted)
				{
					return;
				}
				while (G.mResources.size() > Count)
				{
					G.mResourceNames.erase(G.mResources.back().mName);
					G.mResources.pop_back();
					G.mResourceGenerations.pop_back();
				}
			}
		} Rollback{G, G.mResources.size()};

		// Normalize logical outputs before comparing the existing instance's canonical identity.
		const auto ExistingHandle = FindNode(Name);
		FArdaDependencyResourceContext Resources(*this,
		    Name,
		    ExistingHandle ? G.mTopology.TryGetNode(ExistingHandle)->mPayload.mOutputs
		                   : eastl::vector<FArdaDependencyNodeOutput>{});
		{
			auto Resolved = D->mResolveResources(Resources, Parameters);
			if (!Resolved)
			{
				return {{}, eastl::move(Resolved.mStatus)};
			}
			if (!Resolved.mValue)
			{
				return {{}, GraphInvalid("Resource declaration returned null parameters.")};
			}
			Parameters = eastl::move(Resolved.mValue);
		}
		auto Key = D->mCanonicalKey(Parameters.get());
		eastl::string ConstructionKey;
		{
			FArdaConstructionKeyScope Scope(G);
			ConstructionKey = D->mCanonicalKey(Parameters.get());
		}
		if (auto Existing = FindNode(Name))
		{
			const auto& N = G.mTopology.TryGetNode(Existing)->mPayload;
			if (N.mDefinition != D || (N.mCanonicalKey != Key && N.mConstructionKey != ConstructionKey))
			{
				return {{},
				    GraphInvalid(
				        "An operation name identifies different parameters; use a fresh operation name in this construction.")};
			}
			if (Resources.mOutputs.size() != N.mOutputs.size())
			{
				return {{}, GraphInvalid("Node output declaration changed under an existing name.")};
			}
			for (size_t I = 0; I < N.mOutputs.size(); ++I)
			{
				if (Resources.mOutputs[I].mName != N.mOutputs[I].mName ||
				    Resources.mOutputs[I].mResource.mIndex != N.mOutputs[I].mResource.mIndex ||
				    Resources.mOutputs[I].mRequiredBufferUsage != N.mOutputs[I].mRequiredBufferUsage ||
				    Resources.mOutputs[I].mRequiredTextureUsage != N.mOutputs[I].mRequiredTextureUsage ||
				    !G.HasResource(Resources.mOutputs[I].mResource))
				{
					return {{}, GraphInvalid("Node output declaration changed under an existing name.")};
				}
			}
			if (G.mCompile.mExecution->mDevice)
			{
				if (auto S = G.GetNodeDescription(N).mRequirements.Check(G.mCompile.mExecution->mDevice.Get()); !S)
				{
					return AdmissionError(eastl::move(S));
				}
			}
			return {Existing, {}};
		}

		const auto SourceParameters = Parameters;

		// Describe with immutable declaration state; mutable instances belong to compiled results.
		{
			auto Prepared = D->mPrepareDeclaration(G.mCompile.mExecution->mDevice, Parameters);
			if (!Prepared)
			{
				return {{}, eastl::move(Prepared.mStatus)};
			}
			if (!Prepared.mValue)
			{
				return {{}, GraphInvalid("Node preparation returned no retained execution parameters.")};
			}
			Parameters = eastl::move(Prepared.mValue);
		}

		// Infer descriptor requirements before automatic binding setup can create native objects.
		auto Description = D->mDescribe(Parameters.get());
		if (!Description)
		{
			return AdmissionError(eastl::move(Description.mStatus));
		}
		auto Desc = eastl::move(Description.mValue);
		G.NormalizeDescription(Desc);
		Desc.mRequirements = eastl::move(Requirements);
		InferArdaNodeRequirements(G, D->mKind, Desc);
		if (G.mCompile.mExecution->mDevice)
		{
			if (auto S = Desc.mRequirements.Check(G.mCompile.mExecution->mDevice.Get()); !S)
			{
				return AdmissionError(eastl::move(S));
			}
		}
		// Stage schemas are composed onto a draw by the preprocess assembler before native layouts are made.
		if (auto S = Desc.mGraphicsStage == EArdaGraphicsStage::None && Desc.mRayStage == EArdaRHIShaderStage::None
		        ? DescribeArdaShaderAccesses(G, Desc)
		        : FArdaRHIStatus{};
		    !S)
		{
			return {{}, eastl::move(S)};
		}

		// Freeze pipeline values even when the author retains a mutable shared pointer.
		for (auto& Stage : Desc.mPipelineStages)
		{
			if (Stage.mConfiguration)
			{
				Stage.mConfiguration =
				    eastl::make_shared<const FArdaInductorPipelineConfiguration>(*Stage.mConfiguration);
			}
		}
		for (auto& Pipeline : Desc.mPipelines)
		{
			if (Pipeline.mConfiguration)
			{
				Pipeline.mConfiguration =
				    eastl::make_shared<const FArdaInductorPipelineConfiguration>(*Pipeline.mConfiguration);
			}
		}

		// Validate graph-visible effects before committing the node and its provisional outputs.
		if (!Desc.mEstimatedCost)
		{
			return {{}, GraphInvalid("Node estimated cost must be positive.")};
		}
		for (const auto& A : Desc.mAccesses)
		{
			if (!G.HasResource(A.mResource))
			{
				return {{}, GraphInvalid("Node parameters contain a foreign or stale resource version.")};
			}
		}
		// Deferred stage layouts still need to prove that a node-owned physical output is written.
		// Keep the authored description unexpanded: preprocessing derives its complete draw accesses.
		auto OutputDesc = Desc;
		if (!Resources.mOutputs.empty() &&
		    (Desc.mGraphicsStage != EArdaGraphicsStage::None || Desc.mRayStage != EArdaRHIShaderStage::None))
		{
			if (auto S = DescribeArdaShaderAccesses(G, OutputDesc); !S)
			{
				return {{}, eastl::move(S)};
			}
		}
		for (const auto& Output : Resources.mOutputs)
		{
			if (!eastl::any_of(OutputDesc.mAccesses.begin(),
			        OutputDesc.mAccesses.end(),
			        [&](const auto& A)
			        {
				        return A.mResource == Output.mResource && A.mAccess != EArdaDependencyAccess::Read;
			        }))
			{
				return {{}, GraphInvalid("A declared node output must have a write access.")};
			}
		}
		if (G.mNextAttachmentOrder == UINT64_MAX)
		{
			return {{}, GraphInvalid("Node attachment order exhausted.")};
		}
		auto H = G.mTopology.AddNode({Name,
		    eastl::move(Key),
		    eastl::move(D),
		    eastl::move(Resources.mOutputs),
		    SourceParameters,
		    G.mNextAttachmentOrder,
		    eastl::move(ConstructionKey)});
		if (!H)
		{
			return {{}, GraphInvalid("Cannot allocate another graph node.")};
		}
		G.mTopology.TryGetNode(H)->mPayload.mHandle = H;
		StampArdaNodeDeclarationOrigins(Desc, H);
		G.mTopology.TryGetNode(H)->mPayload.mDesc = eastl::move(Desc);
		G.mNodePreparations[H] = eastl::move(Parameters);
		++G.mNextAttachmentOrder;
		G.mNames.emplace(eastl::move(Name), H);
		Rollback.mbCommitted = true;
		return {H, {}};
	}
}
