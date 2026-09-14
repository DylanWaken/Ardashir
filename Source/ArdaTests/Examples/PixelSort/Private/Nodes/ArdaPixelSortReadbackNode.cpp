#include "Nodes/ArdaPixelSortReadbackNode.h"
#include "ArdaPixelSortNodeInternal.h"
#include <cstring>

namespace arda
{
	std::vector<uint32_t> FArdaPixelSortReadbackNode::ReadPixels(FArdaRHIDeviceRef Device,
	    const FArdaPixelSortReadback& Readback)
	{
		if (!Device || !Readback.mStaging)
		{
			throw std::runtime_error("PixelSort readback has not been recorded.");
		}

		// Remove native row-pitch padding after completion, preserving the original pixel order.
		const auto& Desc = Readback.mStaging->GetDesc().mTexture;
		auto Mapped = TakeArdaExampleValue(Device->MapStagingTexture(Readback.mStaging, {}, EArdaRHICpuAccess::Read));
		std::vector<uint32_t> Pixels(size_t(Desc.mWidth) * Desc.mHeight);
		for (uint32_t Y = 0; Y < Desc.mHeight; ++Y)
		{
			std::memcpy(Pixels.data() + size_t(Y) * Desc.mWidth,
			    static_cast<const uint8_t*>(Mapped.mData) + Y * Mapped.mRowPitch,
			    Desc.mWidth * 4);
		}

		CheckArdaExampleStatus(Device->UnmapStagingTexture(Readback.mStaging));
		return Pixels;
	}

	FArdaDependencyNodeRequirements FArdaPixelSortReadbackNode::GetRequirements(const FArdaParameters&)
	{
		FArdaDependencyNodeRequirements R;
		R.mFeatures.mbRequireStagingTextures = true;
		R.mFeatures.mbRequireTextureCopies = true;
		return R;
	}

	FArdaDependencyNodeMetadata FArdaPixelSortReadbackNode::GetMetadata()
	{
		return {"example.pixel-sort.readback", 1};
	}

	eastl::string FArdaPixelSortReadbackNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Resource(P.mSource);
		Key.Value(reinterpret_cast<uintptr_t>(P.mDestination.get()));
		return Key.Build();
	}

	FArdaRHIStatus FArdaPixelSortReadbackNode::Validate(const FArdaParameters& P)
	{
		if (!P.mDestination)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Readback requires a destination.");
		}
		return {};
	}

	FArdaDependencyNodeDesc FArdaPixelSortReadbackNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::CopySource}};
		D.mbSideEffect = true;
		return D;
	}

	FArdaRHIStatus FArdaPixelSortReadbackNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		const auto Texture = C.GetTexture(P.mSource);

		// Retain CPU-visible storage until the graph ticket completes and ReadPixels consumes it.
		FArdaRHIStagingTextureDesc D;
		D.mTexture = Texture->GetDesc();
		D.mTexture.mbCudaInterop = false;
		D.mCpuAccess = EArdaRHICpuAccess::Read;
		auto Staging = C.GetDevice()->CreateStagingTexture(D);
		if (!Staging)
		{
			return Staging.mStatus;
		}
		P.mDestination->mStaging = eastl::move(Staging.mValue);
		return C.GetCommands().CopyTextureToStaging(*P.mDestination->mStaging, {}, *Texture, {});
	}
}
