#include "Nodes/ArdaPixelSortNoiseNode.h"
#include "ArdaPixelSortNodeInternal.h"

namespace arda
{
	struct FArdaPixelSortNoiseNode::FState
	{
		FArdaRHIDeviceRef mDevice;
		FArdaRHIBindingLayoutRef mNoiseLayout;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mNoise;

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
				Layout.mVisibility = EArdaRHIShaderStage::Compute;
				Layout.mItems = {{0, 1, EArdaRHIBindingType::ConstantBuffer}, {0, 1, EArdaRHIBindingType::TextureUAV}};
				State->mNoiseLayout = Take(Device->CreateBindingLayout(Layout));
				FArdaRHIComputePipelineDesc Compute;
				Compute.mComputeShader = Shader("PixelSortNoise", "NoiseCS", EArdaRHIShaderStage::Compute);
				Compute.mBindingLayouts.push_back(State->mNoiseLayout);
				auto NoiseConfiguration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
				NoiseConfiguration->mKind = EArdaPipelineStateKind::Compute;
				NoiseConfiguration->mCompute.mDesc = eastl::move(Compute);
				State->mNoise = eastl::move(NoiseConfiguration);
				return {};
			}
			catch (const std::exception& Error)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, Error.what());
			}
		}
	};

	struct FArdaPixelSortNoiseNode::FInstanceState
	{
		FArdaRHIBindingSetRef mBinding;
	};

	FArdaDependencyNodeMetadata FArdaPixelSortNoiseNode::GetMetadata()
	{
		return {"example.pixel-sort.noise", 1};
	}

	eastl::string FArdaPixelSortNoiseNode::GetCanonicalKey(const FParameters& P)
	{
		return MakePixelSortNodeKey(P);
	}

	FArdaRHIStatus FArdaPixelSortNoiseNode::Validate(const FParameters& P)
	{
		if (!P.mInput || !P.mWidth || !P.mHeight)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "PixelSort requires frame inputs and a nonempty extent.");
		}
		return {};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaPixelSortNoiseNode::FState>> FArdaPixelSortNoiseNode::Prepare(
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

	TArdaRHIResult<eastl::shared_ptr<FArdaPixelSortNoiseNode::FInstanceState>> FArdaPixelSortNoiseNode::CreateInstance(
	    FArdaRHIDeviceRef,
	    const FParameters&,
	    const FState&)
	{
		return {eastl::make_shared<FInstanceState>(), {}};
	}

	FArdaDependencyNodeDesc FArdaPixelSortNoiseNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mConstants, EArdaDependencyAccess::Read, EArdaRHIResourceState::ConstantBuffer},
		    {P.mNoise, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess}};
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Compute, {}, Prepared.mNoise}};

		return D;
	}

	FArdaRHIStatus FArdaPixelSortNoiseNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		FArdaRHIBindingSetDesc B;
		B.mItems = {{0, 0, EArdaRHIBindingType::ConstantBuffer, C.GetBuffer(P.mConstants), {}}};

		B.mLayout = Prepared.mNoiseLayout;
		B.mItems.push_back({0, 0, EArdaRHIBindingType::TextureUAV, C.GetTexture(P.mNoise), {}});
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

		FArdaRHIComputeState State;
		State.mPipeline = C.GetPipeline()->mCompute;
		State.mBindings = {Bindings};
		if (auto Status = C.GetCommands().SetComputeState(State); !Status)
		{
			return Status;
		}
		C.GetCommands().Dispatch((P.mWidth + 7) / 8, (P.mHeight + 7) / 8);

		return {};
	}
}
