#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include "Inductor/Rules/ArdaInductorRayTracingRules.h"
#include "Inductor/Rules/ArdaInductorBindingRules.h"

namespace arda
{
	namespace
	{
		bool IsRayProvider(EArdaRHIShaderStage Stage)
		{
			return Stage == EArdaRHIShaderStage::Miss || Stage == EArdaRHIShaderStage::ClosestHit ||
			    Stage == EArdaRHIShaderStage::AnyHit || Stage == EArdaRHIShaderStage::Intersection ||
			    Stage == EArdaRHIShaderStage::Callable;
		}

		FArdaRHIStatus ValidateRayStage(const FArdaDependencyGraph::FArdaImpl& Graph, const FArdaDependencyNode& Node)
		{
			using namespace inductor_detail;
			const auto& D = Node.mDesc;
			const bool Terminal = D.mRayStage == EArdaRHIShaderStage::RayGeneration;
			if (!Terminal && !IsRayProvider(D.mRayStage))
			{
				return Error("A ray node must declare one recognized ray-tracing stage.");
			}
			if (!Node.mDefinition || Node.mDefinition->mKind != EArdaDependencyNodeKind::Compute ||
			    D.mGraphicsStage != EArdaGraphicsStage::None || D.mGraphicsInput || D.mGraphicsInputLayout ||
			    !D.mColorTargets.empty() || D.mDepthTarget)
			{
				return Error(
				    "Ray stages require a compute-domain definition without raster state or framebuffer attachments.");
			}
			if (!Terminal &&
			    (!D.mPipelines.empty() || !D.mShaderTables.empty() || D.mbSideEffect || D.mWorkspaceBytes ||
			        D.mTransientWorkspaceBytes || !D.mRayInputs.empty()))
			{
				return Error(
				    "Ray shader providers cannot request a dispatch, shader table, private workspace or other ray inputs.");
			}
			if (Terminal &&
			    (D.mPipelines.size() > 1 ||
			        (!D.mPipelines.empty() &&
			            (D.mPipelines.front().mKind != EArdaPipelineStateKind::RayTracing ||
			                (!D.mPipelines.front().mSlot.empty() && D.mPipelines.front().mSlot != "default")))))
			{
				return Error("An assembled ray-generation node owns one default ray-tracing pipeline.");
			}
			for (const auto Input : D.mRayInputs)
			{
				const auto* Provider = Graph.mTopology.TryGetNode(Input.GetProducer());
				if (Input.GetKind() != EArdaGraphStageValueKind::RayShader || !Provider ||
				    !IsRayProvider(Provider->mPayload.mDesc.mRayStage))
				{
					return Error(
					    "Ray inputs require live miss, hit or callable shader values from this graph construction.");
				}
			}
			uint32_t ShaderCount = 0;
			for (const auto& C : D.mPipelineStages)
			{
				if (!C.mGroup.empty())
				{
					return Error("Typed ray stages select providers through values rather than pipeline groups.");
				}
				if (C.mShader)
				{
					if (C.mShader->GetStage() != D.mRayStage)
					{
						return Error("A ray node contributes a shader for a different pipeline stage.");
					}
					++ShaderCount;
					const bool Hit = D.mRayStage == EArdaRHIShaderStage::ClosestHit ||
					    D.mRayStage == EArdaRHIShaderStage::AnyHit || D.mRayStage == EArdaRHIShaderStage::Intersection;
					if ((Hit && C.mHitGroupName.empty()) || (!Hit && C.mExportName.empty()))
					{
						return Error(
						    "Ray shader contributions require an export name or hit-group name for their stage.");
					}
				}
				if (C.mConfiguration)
				{
					const auto& Config = *C.mConfiguration;
					if (!Terminal || Config.mKind != EArdaPipelineStateKind::RayTracing ||
					    !Config.mRayTracing.mDesc.mShaders.empty() || !Config.mRayTracing.mDesc.mHitGroups.empty())
					{
						return Error(
						    "Only ray generation supplies ray configuration, without embedded shader exports or hit groups.");
					}
				}
			}
			if (ShaderCount != 1)
			{
				return Error("A ray stage must contribute exactly one shader of its declared stage.");
			}
			for (const auto& Request : D.mPipelines)
			{
				if (!Request.mGroup.empty() ||
				    (Request.mConfiguration &&
				        (Request.mConfiguration->mKind != EArdaPipelineStateKind::RayTracing ||
				            !Request.mConfiguration->mRayTracing.mDesc.mShaders.empty() ||
				            !Request.mConfiguration->mRayTracing.mDesc.mHitGroups.empty())))
				{
					return Error("Ray-generation configuration must use its explicitly selected shader values.");
				}
			}
			for (const auto& B : D.mShaderBindings)
			{
				if ((!B.mPipelineSlot.empty() && B.mPipelineSlot != "default") || !B.mMetadata ||
				    !B.mMetadata->GetStatus())
				{
					return Error("Ray stage shader bindings require valid metadata and the default pipeline slot.");
				}
				eastl::vector<FArdaFlattenedShaderParameterMember> Members;
				B.mMetadata->GetFlattenedMembers(Members);
				for (const auto& Member : Members)
				{
					if (Member.mMember->mKind != EArdaShaderParameterKind::Value &&
					    Member.mMember->mVisibility != D.mRayStage)
					{
						return Error("Ray shader bindings must use the visibility of their owning stage.");
					}
				}
			}
			for (const auto& B : D.mBindlessTables)
			{
				if (B.mLayout.mVisibility != D.mRayStage)
				{
					return Error("Ray stage bindless tables must use the visibility of their owning stage.");
				}
			}
			return {};
		}

		template <class T>
		void AppendRay(eastl::vector<T>& Destination, const eastl::vector<T>& Source)
		{
			Destination.insert(Destination.end(), Source.begin(), Source.end());
		}
	}

	FArdaRHIStatus FArdaAssembleRayTracingStagesRule::Apply(FArdaInductorGraph& Graph) const
	{
		using namespace inductor_detail;
		auto& G = FArdaInductorGraphAccess::State(Graph);
		eastl::unordered_map<uint32_t, FArdaDependencyNodeDesc> Authored;
		bool Lowered = false;
		for (auto Handle : G.mTopology.GetNodes())
		{
			const auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
			if (Node.mDesc.mRayStage == EArdaRHIShaderStage::None)
			{
				if (!Node.mDesc.mRayInputs.empty())
				{
					return Error("Only a declared ray-generation node may consume logical ray shader values.");
				}
				continue;
			}
			if (Node.mbRayTracingLowered)
			{
				Lowered = true;
				continue;
			}
			if (auto Status = ValidateRayStage(G, Node); !Status)
			{
				Status.mMessage = Node.mName + ": " + Status.mMessage;
				return Status;
			}
			Authored.emplace(Handle.GetIndex(), Node.mDesc);
		}
		if (Authored.empty())
		{
			return {};
		}
		if (Lowered)
		{
			return Error("Ray assembly cannot mix authored and already lowered stages.");
		}

		// Validate all declarations, even unconsumed providers, before discarding their physical descriptions.
		for (auto Handle : G.mTopology.GetNodes())
		{
			auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
			if (!Authored.count(Handle.GetIndex()))
			{
				continue;
			}
			Node.mDesc.mbPipelineStageOnly = false;
			if (Node.mDesc.mPipelines.empty())
			{
				Node.mDesc.mPipelines.push_back({"default", 0, EArdaPipelineStateKind::RayTracing});
			}
			if (auto Status = DescribeArdaShaderBindings(G, Node.mDesc); !Status)
			{
				return Status;
			}
		}
		// Usage is resolved after all graphics and ray stage descriptions have been assembled.
		if (auto Status = ValidateResourceAccesses(G, true); !Status)
		{
			return Status;
		}

		for (auto Handle : G.mTopology.GetNodes())
		{
			auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
			const auto Found = Authored.find(Handle.GetIndex());
			if (Found == Authored.end())
			{
				continue;
			}
			const auto& Original = Found->second;
			Node.mbRayTracingLowered = true;
			if (Original.mRayStage != EArdaRHIShaderStage::RayGeneration)
			{
				Node.mDesc = {};
				Node.mDesc.mRayStage = Original.mRayStage;
				Node.mDesc.mbPipelineStageOnly = true;
				continue;
			}
			Node.mDesc = Original;
			Node.mDesc.mbPipelineStageOnly = false;
			if (Node.mDesc.mPipelines.empty())
			{
				Node.mDesc.mPipelines.push_back({"default", 0, EArdaPipelineStateKind::RayTracing});
			}
			eastl::unordered_set<uint32_t> Included;
			for (const auto Input : Original.mRayInputs)
			{
				const auto Provider = Input.GetProducer();
				if (!Included.insert(Provider.GetIndex()).second)
				{
					continue;
				}
				const auto& Desc = Authored.at(Provider.GetIndex());
				Node.mDesc.mbDeclarativeParameters |= Desc.mbDeclarativeParameters;
				(void)G.mTopology.AddEdge(Provider, Handle, {});
				AppendRay(Node.mDesc.mPipelineStages, Desc.mPipelineStages);
				AppendRay(Node.mDesc.mAccesses, Desc.mAccesses);
				AppendRay(Node.mDesc.mShaderBindings, Desc.mShaderBindings);
				AppendRay(Node.mDesc.mBindlessTables, Desc.mBindlessTables);
				if (UINT32_MAX - Node.mDesc.mEstimatedCost < Desc.mEstimatedCost)
				{
					return Error("Assembled ray stage cost overflows its scheduling hint.");
				}
				Node.mDesc.mEstimatedCost += Desc.mEstimatedCost;
			}
			if (auto Status = DescribeArdaShaderBindings(G, Node.mDesc); !Status)
			{
				return Status;
			}
			FArdaInductorPipelineNode Projection;
			Projection.mNodeId = Handle.GetIndex();
			Projection.mContributions = Node.mDesc.mPipelineStages;
			auto Request = Node.mDesc.mPipelines.front();
			Request.mTerminalNodeId = Projection.mNodeId;
			const auto Pattern = InferArdaInductorPipeline({eastl::move(Projection)}, Request);
			if (!Pattern)
			{
				return Pattern.mStatus;
			}
		}
		return {};
	}
}
