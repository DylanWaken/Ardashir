#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/Node/ArdaGraphAccess.h"
#include "Graph/Resources/ArdaGraphResourceValidation.h"

#include "Graph/DefaultNodes/ArdaDependencyGraphNodes.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"
#include "RHI/Scheduling/ArdaRHIResourceCopies.h"

#include <EASTL/utility.h>

namespace arda
{
	namespace
	{
		template <class ResourceType, class ImportType, class UseType>
		auto BindVersionedResource(FArdaGraphEdit& Edit,
		    const TArdaRHIRef<ResourceType>& Resource,
		    eastl::string Name,
		    uint64_t Revision,
		    EArdaGraphExternalResourceKind Kind,
		    ImportType&& Import,
		    UseType&& Use)
		{
			using FArdaResult = decltype(Use(FArdaDependencyResourceHandle{}));
			if (!Edit.CanRecord())
			{
				return FArdaResult{};
			}
			if (!Resource || Name.empty())
			{
				Edit.ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Versioned imports require non-null storage and a nonempty name."));
				return FArdaResult{};
			}
			auto Handle = FArdaGraphAccess::FindPhysicalResource(FArdaGraphAccess::GetGraph(Edit),
			    GetArdaGraphPhysicalIdentity(Resource),
			    Kind);
			const auto Named = Edit.GetGraph().FindResource(Name);
			if (Named && Named != Handle)
			{
				Edit.ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "A versioned import name already identifies different storage."));
				return FArdaResult{};
			}
			if (!Handle)
			{
				auto Imported = Import(eastl::move(Name));
				Edit.ReportError(Imported.mStatus);
				if (!Imported)
				{
					return FArdaResult{};
				}
				Handle = Imported.mValue;
			}
			Edit.SetResourceRevision(Handle, Revision);
			return Edit.GetStatus() ? Use(Handle) : FArdaResult{};
		}

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
			const auto Footprint =
			    GetArdaRHITextureBufferFootprint(ArdaGraphShapeDescription(Description->mTexture), Slice);
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
		Desc.mName = FArdaGraphAccess::ResolveResourceName(Edit, "buffer", eastl::move(Name));
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
		Desc.mName = FArdaGraphAccess::ResolveResourceName(Edit, "texture", eastl::move(Name));
		Desc.mbTexture = true;
		Desc.mTexture = Description;
		Desc.mbPersistent = Lifetime == EArdaGraphResourceLifetime::Persistent;
		auto Result = FArdaGraphAccess::CreateResource(FArdaGraphAccess::GetGraph(Edit), eastl::move(Desc));
		Edit.ReportError(Result.mStatus);
		return Result ? Edit.UseTexture(Result.mValue) : FArdaGraphTexture{};
	}

	FArdaGraphBuffer AllocateInitializedBuffer(FArdaGraphEdit& Edit,
	    const FArdaRHIBufferDesc& Description,
	    eastl::vector<uint8_t> Bytes,
	    eastl::string Name)
	{
		if (!Edit.CanRecord())
		{
			return {};
		}
		if (Bytes.empty())
		{
			Edit.ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Initialized buffers require a nonempty byte prefix."));
			return {};
		}
		FArdaDependencyResourceDesc Desc;
		Desc.mName = FArdaGraphAccess::ResolveResourceName(Edit, "buffer", eastl::move(Name));
		Desc.mBuffer = Description;
		Desc.mbPersistent = true;
		Desc.mBufferInitializer = FArdaGraphBufferInitializer(eastl::move(Bytes));
		auto Result = FArdaGraphAccess::CreateResource(FArdaGraphAccess::GetGraph(Edit), eastl::move(Desc));
		Edit.ReportError(Result.mStatus);
		return Result ? Edit.UseBuffer(Result.mValue) : FArdaGraphBuffer{};
	}

	FArdaGraphBuffer BindBuffer(FArdaGraphEdit& Edit, FArdaRHIBufferRef Buffer, eastl::string Name)
	{
		if (!Edit.CanRecord())
		{
			return {};
		}
		auto Result =
		    FArdaGraphAccess::ImportBuffer(FArdaGraphAccess::GetGraph(Edit), eastl::move(Name), eastl::move(Buffer));
		Edit.ReportError(Result.mStatus);
		return Result ? Edit.UseBuffer(Result.mValue) : FArdaGraphBuffer{};
	}

	FArdaGraphBuffer DeclareExternalBuffer(FArdaGraphEdit& Edit, const FArdaRHIBufferDesc& Expected, eastl::string Name)
	{
		if (!Edit.CanRecord())
		{
			return {};
		}
		FArdaDependencyResourceDesc Desc;
		Desc.mName = eastl::move(Name);
		Desc.mBuffer = Expected;
		Desc.mExternalKind = EArdaGraphExternalResourceKind::Buffer;
		auto Result = FArdaGraphAccess::CreateResource(FArdaGraphAccess::GetGraph(Edit), eastl::move(Desc));
		Edit.ReportError(Result.mStatus);
		return Result ? Edit.UseBuffer(Result.mValue) : FArdaGraphBuffer{};
	}

	FArdaGraphTexture DeclareExternalTexture(FArdaGraphEdit& Edit,
	    const FArdaRHITextureDesc& Expected,
	    eastl::string Name)
	{
		if (!Edit.CanRecord())
		{
			return {};
		}
		FArdaDependencyResourceDesc Desc;
		Desc.mName = eastl::move(Name);
		Desc.mTexture = Expected;
		Desc.mbTexture = true;
		Desc.mExternalKind = EArdaGraphExternalResourceKind::Texture;
		auto Result = FArdaGraphAccess::CreateResource(FArdaGraphAccess::GetGraph(Edit), eastl::move(Desc));
		Edit.ReportError(Result.mStatus);
		return Result ? Edit.UseTexture(Result.mValue) : FArdaGraphTexture{};
	}

	FArdaGraphAccelerationStructure DeclareExternalAccelerationStructure(FArdaGraphEdit& Edit,
	    const FArdaRHIAccelStructDesc& Expected,
	    eastl::string Name)
	{
		if (!Edit.CanRecord())
		{
			return {};
		}
		FArdaDependencyResourceDesc Desc;
		Desc.mName = eastl::move(Name);
		Desc.mAccelerationStructure = ArdaGraphAccelerationStructureShape(Expected);
		Desc.mExternalKind = EArdaGraphExternalResourceKind::AccelerationStructure;
		auto Result = FArdaGraphAccess::CreateResource(FArdaGraphAccess::GetGraph(Edit), eastl::move(Desc));
		Edit.ReportError(Result.mStatus);
		return Result ? Edit.UseAccelerationStructure(Result.mValue) : FArdaGraphAccelerationStructure{};
	}

	FArdaGraphTexture BindTexture(FArdaGraphEdit& Edit, FArdaRHITextureRef Texture, eastl::string Name)
	{
		if (!Edit.CanRecord())
		{
			return {};
		}
		auto Result =
		    FArdaGraphAccess::ImportTexture(FArdaGraphAccess::GetGraph(Edit), eastl::move(Name), eastl::move(Texture));
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
		    eastl::move(Name),
		    eastl::move(AccelerationStructure));
		Edit.ReportError(Result.mStatus);
		return Result ? Edit.UseAccelerationStructure(Result.mValue) : FArdaGraphAccelerationStructure{};
	}

	FArdaGraphBuffer BindBuffer(FArdaGraphEdit& Edit, FArdaRHIBufferRef Buffer, eastl::string Name, uint64_t Revision)
	{
		return BindVersionedResource(
		    Edit,
		    Buffer,
		    eastl::move(Name),
		    Revision,
		    EArdaGraphExternalResourceKind::Buffer,
		    [&](eastl::string Slot)
		    {
			    return FArdaGraphAccess::ImportBuffer(FArdaGraphAccess::GetGraph(Edit), eastl::move(Slot), Buffer);
		    },
		    [&](FArdaDependencyResourceHandle Handle)
		    {
			    return Edit.UseBuffer(Handle);
		    });
	}

	FArdaGraphTexture BindTexture(FArdaGraphEdit& Edit,
	    FArdaRHITextureRef Texture,
	    eastl::string Name,
	    uint64_t Revision)
	{
		return BindVersionedResource(
		    Edit,
		    Texture,
		    eastl::move(Name),
		    Revision,
		    EArdaGraphExternalResourceKind::Texture,
		    [&](eastl::string Slot)
		    {
			    return FArdaGraphAccess::ImportTexture(FArdaGraphAccess::GetGraph(Edit), eastl::move(Slot), Texture);
		    },
		    [&](FArdaDependencyResourceHandle Handle)
		    {
			    return Edit.UseTexture(Handle);
		    });
	}

	FArdaGraphAccelerationStructure BindAccelerationStructure(FArdaGraphEdit& Edit,
	    FArdaRHIAccelStructRef Structure,
	    eastl::string Name,
	    uint64_t Revision)
	{
		return BindVersionedResource(
		    Edit,
		    Structure,
		    eastl::move(Name),
		    Revision,
		    EArdaGraphExternalResourceKind::AccelerationStructure,
		    [&](eastl::string Slot)
		    {
			    return FArdaGraphAccess::ImportAccelerationStructure(FArdaGraphAccess::GetGraph(Edit),
			        eastl::move(Slot),
			        Structure);
		    },
		    [&](FArdaDependencyResourceHandle Handle)
		    {
			    return Edit.UseAccelerationStructure(Handle);
		    });
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
		Parameters.mSource = Source;
		Parameters.mDestination = Destination;
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
		Parameters.mDestination = Destination;
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
