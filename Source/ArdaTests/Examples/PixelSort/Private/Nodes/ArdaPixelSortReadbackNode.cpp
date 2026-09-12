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
		const auto& Desc = Readback.mStaging->GetDesc().mTexture;
		auto Mapped = Take(Device->MapStagingTexture(Readback.mStaging, {}, EArdaRHICpuAccess::Read));
		std::vector<uint32_t> Pixels(size_t(Desc.mWidth) * Desc.mHeight);
		for (uint32_t Y = 0; Y < Desc.mHeight; ++Y)
		{
			std::memcpy(Pixels.data() + size_t(Y) * Desc.mWidth,
			    static_cast<const uint8_t*>(Mapped.mData) + Y * Mapped.mRowPitch,
			    Desc.mWidth * 4);
		}
		Check(Device->UnmapStagingTexture(Readback.mStaging));
		return Pixels;
	}

	FArdaDependencyNodeMetadata FArdaPixelSortReadbackNode::GetMetadata()
	{
		return {"example.pixel-sort.readback", 1};
	}

	eastl::string FArdaPixelSortReadbackNode::GetCanonicalKey(const FParameters& P)
	{
		eastl::string Key;
		AppendResource(Key, P.mSource);
		Append(Key, reinterpret_cast<uintptr_t>(P.mDestination.get()));
		return Key;
	}

	FArdaRHIStatus FArdaPixelSortReadbackNode::Validate(const FParameters& P)
	{
		if (!P.mDestination)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Readback requires a destination.");
		}
		return {};
	}

	FArdaDependencyNodeDesc FArdaPixelSortReadbackNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::CopySource}};
		D.mbSideEffect = true;
		return D;
	}

	FArdaRHIStatus FArdaPixelSortReadbackNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		const auto Texture = C.GetTexture(P.mSource);
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
