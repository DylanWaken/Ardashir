#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/Node/ArdaGraphAccess.h"

#include "Graph/DefaultNodes/ArdaDependencyGraphNodes.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"
#include "RHI/Scheduling/ArdaRHIResourceCopies.h"

#include <EASTL/utility.h>

namespace arda
{
	namespace
	{
		/** Texture has already been validated by the calling operation's UseTexture check. */
		bool ResolveBufferLayout(FArdaGraphEdit& Edit,
		    FArdaGraphTexture Texture,
		    const FArdaRHITextureSlice& Slice,
		    FArdaRHITextureBufferLayout& Layout)
		{
			if (Layout.mRowPitch)
			{
				return true;
			}
			const auto* Description = Edit.GetGraph().FindResource(Texture.GetHandle());
			const auto Footprint = GetArdaRHITextureBufferFootprint(Description->mTexture, Slice);
			Edit.ReportError(Footprint.mStatus);
			if (!Footprint)
			{
				return false;
			}
			Layout.mRowPitch = Footprint.mValue.mLayout.mRowPitch;
			return true;
		}
	}

	FArdaGraphBuffer AllocateBuffer(FArdaGraphEdit& Edit,
	    const FArdaRHIBufferDesc& Description,
	    eastl::string Name,
	    EArdaGraphResourceLifetime Lifetime)
	{
		if (!Edit.CanRecord())
		{
			return {};
		}
		if (Lifetime != EArdaGraphResourceLifetime::Transient && Lifetime != EArdaGraphResourceLifetime::Persistent)
		{
			Edit.ReportError(
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Invalid graph resource lifetime."));
			return {};
		}
		FArdaDependencyResourceDesc Desc;
		Desc.mName = Name.empty() ? Edit.MakeName("buffer") : eastl::move(Name);
		Desc.mBuffer = Description;
		Desc.mbPersistent = Lifetime == EArdaGraphResourceLifetime::Persistent;
		auto Result = FArdaGraphAccess::CreateResource(FArdaGraphAccess::GetGraph(Edit), eastl::move(Desc));
		Edit.ReportError(Result.mStatus);
		return Result ? Edit.UseBuffer(Result.mValue) : FArdaGraphBuffer{};
	}

	FArdaGraphTexture AllocateTexture(FArdaGraphEdit& Edit,
	    const FArdaRHITextureDesc& Description,
	    eastl::string Name,
	    EArdaGraphResourceLifetime Lifetime)
	{
		if (!Edit.CanRecord())
		{
			return {};
		}
		if (Lifetime != EArdaGraphResourceLifetime::Transient && Lifetime != EArdaGraphResourceLifetime::Persistent)
		{
			Edit.ReportError(
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Invalid graph resource lifetime."));
			return {};
		}
		FArdaDependencyResourceDesc Desc;
		Desc.mName = Name.empty() ? Edit.MakeName("texture") : eastl::move(Name);
		Desc.mbTexture = true;
		Desc.mTexture = Description;
		Desc.mbPersistent = Lifetime == EArdaGraphResourceLifetime::Persistent;
		auto Result = FArdaGraphAccess::CreateResource(FArdaGraphAccess::GetGraph(Edit), eastl::move(Desc));
		Edit.ReportError(Result.mStatus);
		return Result ? Edit.UseTexture(Result.mValue) : FArdaGraphTexture{};
	}

	FArdaGraphBuffer BindBuffer(FArdaGraphEdit& Edit, FArdaRHIBufferRef Buffer, eastl::string Name)
	{
		if (!Edit.CanRecord())
		{
			return {};
		}
		auto Result = FArdaGraphAccess::ImportBuffer(FArdaGraphAccess::GetGraph(Edit),
		    Name.empty() ? Edit.MakeName("external-buffer") : eastl::move(Name),
		    eastl::move(Buffer));
		Edit.ReportError(Result.mStatus);
		return Result ? Edit.UseBuffer(Result.mValue) : FArdaGraphBuffer{};
	}

	FArdaGraphTexture BindTexture(FArdaGraphEdit& Edit, FArdaRHITextureRef Texture, eastl::string Name)
	{
		if (!Edit.CanRecord())
		{
			return {};
		}
		auto Result = FArdaGraphAccess::ImportTexture(FArdaGraphAccess::GetGraph(Edit),
		    Name.empty() ? Edit.MakeName("external-texture") : eastl::move(Name),
		    eastl::move(Texture));
		Edit.ReportError(Result.mStatus);
		return Result ? Edit.UseTexture(Result.mValue) : FArdaGraphTexture{};
	}

	FArdaGraphAccelerationStructure BindAccelerationStructure(FArdaGraphEdit& Edit,
	    FArdaRHIAccelStructRef AccelerationStructure,
	    eastl::string Name)
	{
		if (!Edit.CanRecord())
		{
			return {};
		}
		auto Result = FArdaGraphAccess::ImportAccelerationStructure(FArdaGraphAccess::GetGraph(Edit),
		    Name.empty() ? Edit.MakeName("external-acceleration-structure") : eastl::move(Name),
		    eastl::move(AccelerationStructure));
		Edit.ReportError(Result.mStatus);
		return Result ? Edit.UseAccelerationStructure(Result.mValue) : FArdaGraphAccelerationStructure{};
	}

	FArdaGraphBuffer Upload(FArdaGraphEdit& Edit,
	    FArdaGraphBuffer Destination,
	    eastl::vector<uint8_t> Bytes,
	    uint64_t DestinationOffset)
	{
		if (!Edit.CanRecord() || !Edit.UseBuffer(Destination.GetHandle()))
		{
			return {};
		}
		FArdaMemoryUploadBufferParameters Parameters;
		Parameters.mDestination = Destination.GetHandle();
		Parameters.mBytes = eastl::move(Bytes);
		Parameters.mDestinationOffset = DestinationOffset;
		UploadBuffer(Edit, eastl::move(Parameters));
		return Edit.GetStatus() ? Destination : FArdaGraphBuffer{};
	}

	FArdaGraphTexture Upload(FArdaGraphEdit& Edit,
	    FArdaGraphTexture Destination,
	    eastl::vector<uint8_t> Bytes,
	    FArdaRHITextureSlice Slice)
	{
		if (!Edit.CanRecord() || !Edit.UseTexture(Destination.GetHandle()))
		{
			return {};
		}
		FArdaMemoryUploadTextureParameters Parameters;
		Parameters.mDestination = Destination.GetHandle();
		Parameters.mBytes = eastl::move(Bytes);
		Parameters.mSlice = Slice;
		UploadTexture(Edit, eastl::move(Parameters));
		return Edit.GetStatus() ? Destination : FArdaGraphTexture{};
	}

	FArdaGraphBuffer CopyTo(FArdaGraphEdit& Edit,
	    FArdaGraphBuffer Source,
	    FArdaGraphBuffer Destination,
	    uint64_t ByteSize,
	    uint64_t SourceOffset,
	    uint64_t DestinationOffset)
	{
		if (!Edit.CanRecord() || !Edit.UseBuffer(Source.GetHandle()) || !Edit.UseBuffer(Destination.GetHandle()))
		{
			return {};
		}
		FArdaMemoryCopyBufferParameters Parameters;
		Parameters.mSource = Source.GetHandle();
		Parameters.mDestination = Destination.GetHandle();
		Parameters.mByteSize = ByteSize;
		Parameters.mSourceOffset = SourceOffset;
		Parameters.mDestinationOffset = DestinationOffset;
		CopyBuffer(Edit, eastl::move(Parameters));
		return Edit.GetStatus() ? Destination : FArdaGraphBuffer{};
	}

	FArdaGraphTexture CopyTo(FArdaGraphEdit& Edit,
	    FArdaGraphTexture Source,
	    FArdaGraphTexture Destination,
	    FArdaRHITextureSlice SourceSlice,
	    FArdaRHITextureSlice DestinationSlice)
	{
		if (!Edit.CanRecord() || !Edit.UseTexture(Source.GetHandle()) || !Edit.UseTexture(Destination.GetHandle()))
		{
			return {};
		}
		FArdaMemoryCopyTextureParameters Parameters;
		Parameters.mSource = Source.GetHandle();
		Parameters.mDestination = Destination.GetHandle();
		Parameters.mSourceSlice = SourceSlice;
		Parameters.mDestinationSlice = DestinationSlice;
		CopyTexture(Edit, eastl::move(Parameters));
		return Edit.GetStatus() ? Destination : FArdaGraphTexture{};
	}

	FArdaGraphTexture CopyTo(FArdaGraphEdit& Edit,
	    FArdaGraphBuffer Source,
	    FArdaGraphTexture Destination,
	    FArdaRHITextureSlice DestinationSlice,
	    FArdaRHITextureBufferLayout SourceLayout)
	{
		if (!Edit.CanRecord() || !Edit.UseBuffer(Source.GetHandle()) || !Edit.UseTexture(Destination.GetHandle()))
		{
			return {};
		}
		if (!ResolveBufferLayout(Edit, Destination, DestinationSlice, SourceLayout))
		{
			return {};
		}
		FArdaGraphBufferToTextureParameters Parameters;
		Parameters.mBuffer = Source.GetHandle();
		Parameters.mTexture = Destination.GetHandle();
		Parameters.mLayout = SourceLayout;
		Parameters.mSlice = DestinationSlice;
		BufferToTexture(Edit, eastl::move(Parameters));
		return Edit.GetStatus() ? Destination : FArdaGraphTexture{};
	}

	FArdaGraphBuffer CopyTo(FArdaGraphEdit& Edit,
	    FArdaGraphTexture Source,
	    FArdaGraphBuffer Destination,
	    FArdaRHITextureSlice SourceSlice,
	    FArdaRHITextureBufferLayout DestinationLayout)
	{
		if (!Edit.CanRecord() || !Edit.UseTexture(Source.GetHandle()) || !Edit.UseBuffer(Destination.GetHandle()))
		{
			return {};
		}
		if (!ResolveBufferLayout(Edit, Source, SourceSlice, DestinationLayout))
		{
			return {};
		}
		FArdaGraphTextureToBufferParameters Parameters;
		Parameters.mTexture = Source.GetHandle();
		Parameters.mBuffer = Destination.GetHandle();
		Parameters.mSlice = SourceSlice;
		Parameters.mLayout = DestinationLayout;
		TextureToBuffer(Edit, eastl::move(Parameters));
		return Edit.GetStatus() ? Destination : FArdaGraphBuffer{};
	}

	FArdaGraphAccelerationStructure CopyTo(FArdaGraphEdit& Edit,
	    FArdaGraphAccelerationStructure Source,
	    FArdaGraphAccelerationStructure Destination)
	{
		if (!Edit.CanRecord() || !Edit.UseAccelerationStructure(Source.GetHandle()) ||
		    !Edit.UseAccelerationStructure(Destination.GetHandle()))
		{
			return {};
		}
		FArdaMemoryCopyAccelerationStructureParameters Parameters;
		Parameters.mSource = Source.GetHandle();
		Parameters.mDestination = Destination.GetHandle();
		CopyAccelerationStructure(Edit, eastl::move(Parameters));
		return Edit.GetStatus() ? Destination : FArdaGraphAccelerationStructure{};
	}

	FArdaGraphBuffer Clear(FArdaGraphEdit& Edit, FArdaGraphBuffer Destination, uint32_t Value)
	{
		if (!Edit.CanRecord() || !Edit.UseBuffer(Destination.GetHandle()))
		{
			return {};
		}
		FArdaMemoryClearBufferParameters Parameters;
		Parameters.mDestination = Destination.GetHandle();
		Parameters.mValue = Value;
		ClearBuffer(Edit, eastl::move(Parameters));
		return Edit.GetStatus() ? Destination : FArdaGraphBuffer{};
	}

	FArdaGraphTexture Clear(FArdaGraphEdit& Edit,
	    FArdaGraphTexture Destination,
	    FArdaRHIColor Color,
	    FArdaRHITextureSubresourceRange Range)
	{
		if (!Edit.CanRecord() || !Edit.UseTexture(Destination.GetHandle()))
		{
			return {};
		}
		FArdaMemoryClearTextureParameters Parameters;
		Parameters.mDestination = Destination.GetHandle();
		Parameters.mColor = Color;
		Parameters.mRange = Range;
		ClearTexture(Edit, eastl::move(Parameters));
		return Edit.GetStatus() ? Destination : FArdaGraphTexture{};
	}

	FArdaGraphBuffer Clone(FArdaGraphEdit& Edit, FArdaGraphBuffer Source)
	{
		if (!Edit.CanRecord() || !Edit.UseBuffer(Source.GetHandle()))
		{
			return {};
		}
		FArdaMemoryCopyBufferParameters Parameters;
		Parameters.mSource = Source.GetHandle();
		return CopyBuffer(Edit, eastl::move(Parameters)).GetBuffer();
	}

	eastl::shared_ptr<eastl::vector<uint8_t>> Readback(FArdaGraphEdit& Edit,
	    FArdaGraphBuffer Source,
	    uint64_t SourceOffset,
	    uint64_t ByteSize)
	{
		if (!Edit.CanRecord() || !Edit.UseBuffer(Source.GetHandle()))
		{
			return {};
		}
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		FArdaMemoryReadbackBufferParameters Parameters;
		Parameters.mSource = Source.GetHandle();
		Parameters.mDestination = Bytes;
		Parameters.mSourceOffset = SourceOffset;
		Parameters.mByteSize = ByteSize;
		ReadbackBuffer(Edit, eastl::move(Parameters));
		return Edit.GetStatus() ? Bytes : eastl::shared_ptr<eastl::vector<uint8_t>>{};
	}

	eastl::shared_ptr<eastl::vector<uint8_t>> Readback(FArdaGraphEdit& Edit,
	    FArdaGraphTexture Source,
	    FArdaRHITextureSlice Slice)
	{
		if (!Edit.CanRecord() || !Edit.UseTexture(Source.GetHandle()))
		{
			return {};
		}
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		FArdaMemoryReadbackTextureParameters Parameters;
		Parameters.mSource = Source.GetHandle();
		Parameters.mDestination = Bytes;
		Parameters.mSlice = Slice;
		ReadbackTexture(Edit, eastl::move(Parameters));
		return Edit.GetStatus() ? Bytes : eastl::shared_ptr<eastl::vector<uint8_t>>{};
	}
}
