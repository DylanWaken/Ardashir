#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellGeometryNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	class FArdaGenerateCornellGeometryShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
			ARDA_SHADER_BUFFER_UAV(mVertices, 0, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mIndices, 1, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mMaterials, 2, 0, arda::EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaGenerateCornellGeometryShader);
	};

	struct FArdaCornellGeometryNode::FState
	{
		FArdaRHIDeviceRef mDevice;
		FArdaGlobalShaderMap mShaderMap;
		eastl::vector<FArdaInductorPipelineContribution> mStages;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;

		FArdaRHIStatus Initialize()
		{
			if (!mShaderMap.Initialize(mDevice))
			{
				return CornellShaderError(mShaderMap);
			}
			const auto* Shader0 = mShaderMap.Find(FArdaGenerateCornellGeometryShader::GetStaticType());
			if (!Shader0)
			{
				return CornellShaderError(mShaderMap);
			}
			mStages.push_back(CornellShaderStage(Shader0));

			return {};
		}
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaGenerateCornellGeometryShader,
	    "/ArdaTests/CornellBox/CornellGeometry.hlsl",
	    "CornellGeometryCS",
	    "GenerateCornellGeometryCS",
	    arda::EArdaRHIShaderStage::Compute)

	FArdaDependencyNodeMetadata FArdaCornellGeometryNode::GetMetadata()
	{
		return {"cornell.geometry", 1};
	}

	eastl::string FArdaCornellGeometryNode::GetCanonicalKey(const FParameters& P)
	{
		return MakeCornellNodeKey(P);
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaCornellGeometryNode::FState>> FArdaCornellGeometryNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "This node requires an initialized device.")};
		}
		auto State = eastl::make_shared<FState>();
		State->mDevice = eastl::move(Device);
		auto Status = State->Initialize();
		if (!Status)
		{
			return {{}, eastl::move(Status)};
		}
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaCornellGeometryNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;
		const auto Read = [&](uint32_t I, EArdaRHIResourceState State)
		{
			D.mAccesses.push_back({P.mResources[I], EArdaDependencyAccess::Read, State});
		};
		const auto Write = [&](uint32_t I, EArdaRHIResourceState State)
		{
			D.mAccesses.push_back({P.mResources[I], EArdaDependencyAccess::Write, State});
		};
		for (uint32_t I = 0; I < 3; ++I)
		{
			Write(I, EArdaRHIResourceState::UnorderedAccess);
		}

		D.mPipelineStages = Prepared.mStages;
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Compute, {}, Prepared.mConfiguration}};

		return D;
	}

	FArdaRHIStatus FArdaCornellGeometryNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		auto& Commands = C.GetCommands();
		const auto* Pipeline = C.GetPipeline();
		if (!Pipeline)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Cornell pipeline inference produced no pipeline.");
		}
		FArdaRHIBindingSetDesc Binding;
		for (const auto& Stage : Prepared.mStages)
		{
			if (!Binding.mLayout && !Stage.mBindingLayouts.empty())
			{
				Binding.mLayout = Stage.mBindingLayouts[0];
			}
		}
		const auto Bind = [&](uint32_t Slot, EArdaRHIBindingType Type, FArdaRHIResourceRef Resource)
		{
			FArdaRHIBindingItem I;
			I.mSlot = Slot;
			I.mType = Type;
			I.mResource = eastl::move(Resource);
			Binding.mItems.push_back(eastl::move(I));
		};
		const auto Buffer = [&](uint32_t I)
		{
			return FArdaRHIResourceRef(C.GetBuffer(P.mResources[I]).Get());
		};
		const auto Texture = [&](uint32_t I)
		{
			return FArdaRHIResourceRef(C.GetTexture(P.mResources[I]).Get());
		};

		for (uint32_t I = 0; I < 3; ++I)
		{
			Bind(I, EArdaRHIBindingType::StructuredBufferUAV, Buffer(I));
		}
		auto Bound = C.GetDevice()->CreateBindingSet(Binding);
		if (!Bound)
		{
			return Bound.mStatus;
		}

		FArdaRHIComputeState State;
		State.mPipeline = Pipeline->mCompute;
		State.mBindings = {Bound.mValue};
		if (auto S = Commands.SetComputeState(State); !S)
		{
			return S;
		}
		Commands.Dispatch(P.mWidth, P.mHeight, 1);
		return {};
	}
}
