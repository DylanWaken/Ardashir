#include "Nodes/ArdaPixelSortPresentNode.h"
#include "ArdaPixelSortNodeInternal.h"

namespace arda
{
	struct FArdaPixelSortPresentNode::FState
	{
		FArdaRHIDeviceRef mDevice;
		FArdaRHIBindingLayoutRef mPresentLayout;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mPresent;

		FArdaRHIStatus Initialize()
		{
			try
			{
				const auto Directory = GetArdaExampleDirectory();

				auto* State = this;
				const auto Device = State->mDevice;
				const auto Shader = [&](const char* Name, const char* Entry, EArdaRHIShaderStage Stage)
				{
					return LoadArdaPixelSortShader(Device, Directory, Name, Entry, Stage);
				};
				FArdaRHIBindingLayoutDesc Layout;
				Layout.mVisibility = EArdaRHIShaderStage::Pixel;
				Layout.mItems = {{0, 1, EArdaRHIBindingType::ConstantBuffer},
				    {0, 1, EArdaRHIBindingType::TextureSRV},
				    {1, 1, EArdaRHIBindingType::TextureSRV}};
				State->mPresentLayout = Take(Device->CreateBindingLayout(Layout));
				FArdaRHIGraphicsPipelineDesc Graphics;
				Graphics.mVertexShader = Shader("PixelSortVertex", "PresentVS", EArdaRHIShaderStage::Vertex);
				Graphics.mPixelShader = Shader("PixelSortPixel", "PresentPS", EArdaRHIShaderStage::Pixel);
				Graphics.mBindingLayouts.push_back(State->mPresentLayout);
				Graphics.mSampleCount = 0; // Filled from the imported color target during graph compilation.

				// SV_VertexID generates a fullscreen triangle: no mesh, vertex buffer,
				// depth attachment, or scene system is required to present the texture.
				Graphics.mDepthStencilState.mbDepthTest = Graphics.mDepthStencilState.mbDepthWrite = false;
				Graphics.mRasterState.mCullMode = EArdaRHICullMode::None;
				auto PresentConfiguration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
				PresentConfiguration->mKind = EArdaPipelineStateKind::Graphics;
				PresentConfiguration->mGraphics.mDesc = eastl::move(Graphics);
				State->mPresent = eastl::move(PresentConfiguration);
				return {};
			}
			catch (const std::exception& Error)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, Error.what());
			}
		}
	};

	struct FArdaPixelSortPresentNode::FInstanceState
	{
		FArdaRHIBindingSetRef mBinding;
	};

	FArdaDependencyNodeMetadata FArdaPixelSortPresentNode::GetMetadata()
	{
		return {"example.pixel-sort.present", 1};
	}

	eastl::string FArdaPixelSortPresentNode::GetCanonicalKey(const FParameters& P)
	{
		return MakePixelSortNodeKey(P);
	}

	FArdaRHIStatus FArdaPixelSortPresentNode::Validate(const FParameters& P)
	{
		if (!P.mInput || !P.mWidth || !P.mHeight)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "PixelSort requires frame inputs and a nonempty extent.");
		}
		return {};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaPixelSortPresentNode::FState>> FArdaPixelSortPresentNode::Prepare(
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

	TArdaRHIResult<eastl::shared_ptr<FArdaPixelSortPresentNode::FInstanceState>> FArdaPixelSortPresentNode::
	    CreateInstance(FArdaRHIDeviceRef, const FParameters&, const FState&)
	{
		return {eastl::make_shared<FInstanceState>(), {}};
	}

	FArdaDependencyNodeDesc FArdaPixelSortPresentNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mConstants, EArdaDependencyAccess::Read, EArdaRHIResourceState::ConstantBuffer},
		    {P.mNoise, EArdaDependencyAccess::Read, EArdaRHIResourceState::PixelShaderResource},
		    {P.mSorted, EArdaDependencyAccess::Read, EArdaRHIResourceState::PixelShaderResource},
		    {P.mColor, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget}};
		D.mColorTargets = {P.mColor};
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Graphics, {}, Prepared.mPresent}};

		return D;
	}

	FArdaRHIStatus FArdaPixelSortPresentNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		FArdaRHIBindingSetDesc B;
		B.mItems = {{0, 0, EArdaRHIBindingType::ConstantBuffer, C.GetBuffer(P.mConstants), {}}};

		B.mLayout = Prepared.mPresentLayout;
		B.mItems.push_back({0, 0, EArdaRHIBindingType::TextureSRV, C.GetTexture(P.mSorted), {}});
		B.mItems.push_back({1, 0, EArdaRHIBindingType::TextureSRV, C.GetTexture(P.mNoise), {}});
		auto& Bindings = InstanceState.mBinding;
		bool bMatches = Bindings && Bindings->GetDesc().mLayout == B.mLayout &&
		    Bindings->GetDesc().mItems.size() == B.mItems.size();
		for (size_t I = 0; bMatches && I < B.mItems.size(); ++I)
		{
			bMatches = Bindings->GetDesc().mItems[I].mResource == B.mItems[I].mResource;
		}
		if (!bMatches)
		{
			auto Created = C.GetDevice()->CreateBindingSet(B);
			if (!Created)
			{
				return Created.mStatus;
			}
			Bindings = eastl::move(Created.mValue);
		}

		FArdaRHIGraphicsState State;
		State.mPipeline = C.GetPipeline()->mGraphics;
		State.mFramebuffer = C.GetFramebuffer();
		State.mBindings = {Bindings};
		State.mViewports = {{0.f, float(P.mWidth), 0.f, float(P.mHeight), 0.f, 1.f}};
		State.mScissors = {{0, int32_t(P.mWidth), 0, int32_t(P.mHeight)}};
		if (auto Status = C.GetCommands().SetGraphicsState(State); !Status)
		{
			return Status;
		}
		C.GetCommands().Draw({3});

		return {};
	}
}
