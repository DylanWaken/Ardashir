#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellPresentNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	class FArdaCornellPresentVertexShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_DECLARE_GLOBAL_SHADER(FArdaCornellPresentVertexShader);
	};

	class FArdaCornellPresentPixelShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
			ARDA_SHADER_TEXTURE_SRV(mAccumulation, 0, 0, arda::EArdaRHIShaderStage::Pixel)
			ARDA_SHADER_UNIFORM_BUFFER(mFrame, 0, 0, arda::EArdaRHIShaderStage::Pixel)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaCornellPresentPixelShader);
	};

	struct FArdaCornellPresentNode::FState
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
			const auto* Shader0 = mShaderMap.Find(FArdaCornellPresentVertexShader::GetStaticType());
			if (!Shader0)
			{
				return CornellShaderError(mShaderMap);
			}
			mStages.push_back(CornellShaderStage(Shader0));
			const auto* Shader1 = mShaderMap.Find(FArdaCornellPresentPixelShader::GetStaticType());
			if (!Shader1)
			{
				return CornellShaderError(mShaderMap);
			}
			mStages.push_back(CornellShaderStage(Shader1));
			auto Present = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Present->mKind = EArdaPipelineStateKind::Graphics;
			Present->mGraphics.mDesc.mRasterState.mCullMode = EArdaRHICullMode::None;
			Present->mGraphics.mDesc.mDepthStencilState.mbDepthTest = false;
			Present->mGraphics.mDesc.mDepthStencilState.mbDepthWrite = false;
			Present->mGraphics.mDesc.mSampleCount = 0;
			mConfiguration = eastl::move(Present);
			return {};
		}
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER_WITHOUT_PARAMETERS(FArdaCornellPresentVertexShader,
	    "/ArdaTests/CornellBox/CornellPresent.hlsl",
	    "CornellPresentVS",
	    "CornellPresentVS",
	    arda::EArdaRHIShaderStage::Vertex)
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaCornellPresentPixelShader,
	    "/ArdaTests/CornellBox/CornellPresent.hlsl",
	    "CornellPresentPS",
	    "CornellPresentPS",
	    arda::EArdaRHIShaderStage::Pixel)

	FArdaDependencyNodeMetadata FArdaCornellPresentNode::GetMetadata()
	{
		return {"cornell.present", 1};
	}

	eastl::string FArdaCornellPresentNode::GetCanonicalKey(const FParameters& P)
	{
		return MakeCornellNodeKey(P);
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaCornellPresentNode::FState>> FArdaCornellPresentNode::Prepare(
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

	FArdaDependencyNodeDesc FArdaCornellPresentNode::Describe(const FParameters& P, const FState& Prepared)
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
		Read(0, EArdaRHIResourceState::ShaderResource);
		Read(1, EArdaRHIResourceState::ConstantBuffer);
		Write(2, EArdaRHIResourceState::RenderTarget);
		D.mColorTargets = {P.mResources[2]};

		D.mPipelineStages = Prepared.mStages;
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Graphics, {}, Prepared.mConfiguration}};

		return D;
	}

	FArdaRHIStatus FArdaCornellPresentNode::Record(FArdaDependencyExecutionContext& C,
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
		Bind(0, EArdaRHIBindingType::TextureSRV, Texture(0));
		Bind(0, EArdaRHIBindingType::ConstantBuffer, Buffer(1));
		auto Bound = C.GetDevice()->CreateBindingSet(Binding);
		if (!Bound)
		{
			return Bound.mStatus;
		}

		FArdaRHIGraphicsState State;
		State.mPipeline = Pipeline->mGraphics;
		State.mFramebuffer = C.GetFramebuffer();
		State.mBindings = {Bound.mValue};
		State.mViewports.push_back({0.f, float(P.mWidth), 0.f, float(P.mHeight), 0.f, 1.f});
		State.mScissors.push_back({0, int32_t(P.mWidth), 0, int32_t(P.mHeight)});
		if (auto S = Commands.SetGraphicsState(State); !S)
		{
			return S;
		}
		Commands.Draw({3});
		return {};
	}
}
