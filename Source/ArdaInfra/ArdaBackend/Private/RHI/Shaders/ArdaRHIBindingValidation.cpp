/** Descriptor validation and resolution before provider calls. */

#include "RHI/Shaders/ArdaRHIBindingValidation.h"
#include "RHI/Resources/ArdaRHIResourceAccess.h"
#include "RHI/Resources/ArdaRHISamplerImpl.h"
#include "RHI/Config/ArdaRHIValidation.h"
#include "RHI/Resources/ArdaRHITextureBufferImpl.h"
#include "RHI/Resources/ArdaRHIAccelerationImpl.h"
#include "RHI/Shaders/ArdaRHIShaderImpl.h"
#include "RHI/Pipelines/ArdaRHIPipelineImpl.h"

namespace arda::detail
{
	FArdaRHIStatus ResolveBindingItem(const FArdaRHIBindingLayoutDesc& Layout,
	    FArdaRHIBindingItem& Item,
	    const FArdaRHIDeviceLimits& Limits)
	{
		// Validate the descriptor address before either provider writes native descriptor storage.
		const auto Declared = eastl::find_if(Layout.mItems.begin(),
		    Layout.mItems.end(),
		    [&Item](const FArdaRHIBindingLayoutItem& Candidate)
		    {
			    return Candidate.mSlot == Item.mSlot && Candidate.mType == Item.mType;
		    });
		if (Declared == Layout.mItems.end() || Item.mType == EArdaRHIBindingType::PushConstants ||
		    Item.mArrayElement >= Declared->mArraySize)
		{
			return Invalid("Binding item is undeclared or its array element exceeds the layout.");
		}

		// Typed views own the descriptor; inline data can confirm it but cannot replace its range or access kind.
		IArdaRHIResource* Resource = Item.mResource.Get();
		const FArdaRHIViewDesc* RetainedView = nullptr;
		if (const auto* View = Cast<FArdaShaderResourceView>(Resource))
		{
			if (Item.mType != EArdaRHIBindingType::TextureSRV &&
			    Item.mType != EArdaRHIBindingType::TypedBufferSRV &&
			    Item.mType != EArdaRHIBindingType::StructuredBufferSRV &&
			    Item.mType != EArdaRHIBindingType::RawBufferSRV)
			{
				return Invalid("A shader-resource view requires an SRV binding type.");
			}
			RetainedView = &View->mDesc;
			Resource = View->mResource.Get();
		}
		else if (const auto* View = Cast<FArdaUnorderedAccessView>(Resource))
		{
			if (Item.mType != EArdaRHIBindingType::TextureUAV &&
			    Item.mType != EArdaRHIBindingType::TypedBufferUAV &&
			    Item.mType != EArdaRHIBindingType::StructuredBufferUAV &&
			    Item.mType != EArdaRHIBindingType::RawBufferUAV)
			{
				return Invalid("An unordered-access view requires a UAV binding type.");
			}
			RetainedView = &View->mDesc;
			Resource = View->mResource.Get();
		}
		if (RetainedView)
		{
			if (!(Item.mView == FArdaRHIViewDesc{}) && !(Item.mView == *RetainedView))
			{
				return Invalid("An inline binding view conflicts with its retained resource view.");
			}
			Item.mView = *RetainedView;
		}

		// Validate the effective descriptor against its underlying native storage before either provider sees it.
		bool Matches = false;
		switch (Item.mType)
		{
		case EArdaRHIBindingType::TextureSRV:
		case EArdaRHIBindingType::TextureUAV:
			Matches = Cast<FArdaTexture>(Resource) != nullptr;
			break;
		case EArdaRHIBindingType::TypedBufferSRV:
		case EArdaRHIBindingType::TypedBufferUAV:
		case EArdaRHIBindingType::StructuredBufferSRV:
		case EArdaRHIBindingType::StructuredBufferUAV:
		case EArdaRHIBindingType::RawBufferSRV:
		case EArdaRHIBindingType::RawBufferUAV:
		case EArdaRHIBindingType::ConstantBuffer:
		case EArdaRHIBindingType::VolatileConstantBuffer:
			Matches = Cast<FArdaBuffer>(Resource) != nullptr;
			break;
		case EArdaRHIBindingType::Sampler:
			Matches = Cast<FArdaSampler>(Resource) != nullptr;
			break;
		case EArdaRHIBindingType::RayTracingAccelStruct:
			Matches = Cast<FArdaAccelStruct>(Resource) != nullptr;
			break;
		case EArdaRHIBindingType::SamplerFeedbackTextureUAV:
			Matches = Cast<FArdaSamplerFeedbackTexture>(Resource) != nullptr;
			break;
		default:
			break;
		}
		if (!Matches)
		{
			return Invalid("Binding descriptor type does not match the resource kind.");
		}

		// Do not let Resolve silently clamp invalid user ranges into different shader accesses.
		if (const auto* Buffer = Cast<FArdaBuffer>(Resource))
		{
			const auto& Range = Item.mView.mBufferRange;
			if (Range.mByteOffset >= Buffer->mDesc.mByteSize || !Range.mByteSize ||
			    (Range.mByteSize != ArdaRHIWholeBuffer &&
			        Range.mByteSize > Buffer->mDesc.mByteSize - Range.mByteOffset))
			{
				return Invalid("Binding buffer view exceeds its resource or is empty.");
			}
			const bool bUniform = Item.mType == EArdaRHIBindingType::ConstantBuffer ||
			    Item.mType == EArdaRHIBindingType::VolatileConstantBuffer;
			const bool bStorage = Item.mType == EArdaRHIBindingType::StructuredBufferSRV ||
			    Item.mType == EArdaRHIBindingType::StructuredBufferUAV ||
			    Item.mType == EArdaRHIBindingType::RawBufferSRV || Item.mType == EArdaRHIBindingType::RawBufferUAV;
			const uint64_t Size = Range.mByteSize == ArdaRHIWholeBuffer
			    ? Buffer->mDesc.mByteSize - Range.mByteOffset
			    : Range.mByteSize;
			const uint64_t Limit = bUniform ? Limits.mMaxUniformBufferRange
			    : bStorage                  ? Limits.mMaxStorageBufferRange
			                                : 0;
			const uint64_t Alignment = bUniform ? Limits.mMinUniformBufferOffsetAlignment
			    : bStorage                      ? Limits.mMinStorageBufferOffsetAlignment
			                                    : 0;
			if ((Limit && Size > Limit) || (Alignment && Range.mByteOffset % Alignment))
			{
				return Invalid("Buffer binding exceeds the device range or offset-alignment limit.");
			}
		}
		else if (const auto* Texture = Cast<FArdaTexture>(Resource))
		{
			const auto& Range = Item.mView.mTextureRange;
			const auto Fits = [](uint32_t Base, uint32_t Count, uint32_t Limit)
			{
				return Base < Limit && Count && (Count == ArdaRHIAllSubresources || Count <= Limit - Base);
			};
			if (!Fits(Range.mBaseMipLevel, Range.mMipLevelCount, Texture->mDesc.mMipLevels) ||
			    !Fits(Range.mBaseArraySlice, Range.mArraySliceCount, Texture->mDesc.mArraySize) ||
			    !Fits(Range.mBasePlane, Range.mPlaneCount, GetArdaRHIFormatPlaneCount(Texture->mDesc.mFormat)))
			{
				return Invalid("Binding texture view exceeds its resource or is empty.");
			}
		}
		return {};
	}

	size_t PushConstantCapacity(const eastl::vector<FArdaRHIBindingLayoutRef>& Layouts)
	{
		// One portable update is broadcast to every push block, so it must fit the smallest block.
		size_t Capacity = SIZE_MAX;
		for (const auto& Layout : Layouts)
		{
			for (const auto& Item : Layout->GetDesc().mItems)
			{
				if (Item.mType == EArdaRHIBindingType::PushConstants)
				{
					Capacity = eastl::min(Capacity, size_t(Item.mArraySize));
				}
			}
		}
		return Capacity == SIZE_MAX ? 0 : Capacity;
	}

	TArdaRHIResult<FArdaRHIBindingItem> MakeCollectionBinding(const FArdaRHIResourceCollectionItem& Item,
	    uint32_t ArrayElement)
	{
		FArdaRHIBindingItem Binding;
		Binding.mSlot = 0;
		Binding.mArrayElement = ArrayElement;
		const auto BufferBindingType = [](const IArdaRHIBuffer& Buffer, bool bUav)
		{
			if (Buffer.GetDesc().mStructureStride)
			{
				return bUav ? EArdaRHIBindingType::StructuredBufferUAV : EArdaRHIBindingType::StructuredBufferSRV;
			}
			return bUav ? EArdaRHIBindingType::RawBufferUAV : EArdaRHIBindingType::RawBufferSRV;
		};
		switch (Item.mType)
		{
		case EArdaRHIResourceCollectionItemType::Texture:
			if (!Item.mTexture)
			{
				break;
			}
			Binding.mType = EArdaRHIBindingType::TextureSRV;
			Binding.mResource = FArdaRHIResourceRef(Item.mTexture.Get());
			return {Binding, {}};
		case EArdaRHIResourceCollectionItemType::TextureReference:
			if (!Item.mTextureReference || !Item.mTextureReference->GetTexture())
			{
				break;
			}
			Binding.mType = EArdaRHIBindingType::TextureSRV;
			Binding.mResource = FArdaRHIResourceRef(Item.mTextureReference->GetTexture().Get());
			return {Binding, {}};
		case EArdaRHIResourceCollectionItemType::Buffer:
			if (!Item.mBuffer)
			{
				break;
			}
			Binding.mType = BufferBindingType(*Item.mBuffer, false);
			Binding.mResource = FArdaRHIResourceRef(Item.mBuffer.Get());
			return {Binding, {}};
		case EArdaRHIResourceCollectionItemType::ShaderResourceView:
			if (!Item.mShaderResourceView || !Item.mShaderResourceView->GetResource())
			{
				break;
			}
			Binding.mView = Item.mShaderResourceView->GetDesc();
			Binding.mResource = FArdaRHIResourceRef(Item.mShaderResourceView.Get());
			if (auto* Buffer = dynamic_cast<IArdaRHIBuffer*>(Item.mShaderResourceView->GetResource()))
			{
				Binding.mType = BufferBindingType(*Buffer, false);
			}
			else if (dynamic_cast<IArdaRHIAccelStruct*>(Item.mShaderResourceView->GetResource()))
			{
				Binding.mType = EArdaRHIBindingType::RayTracingAccelStruct;
			}
			else
			{
				Binding.mType = EArdaRHIBindingType::TextureSRV;
			}
			return {Binding, {}};
		case EArdaRHIResourceCollectionItemType::UnorderedAccessView:
			if (!Item.mUnorderedAccessView || !Item.mUnorderedAccessView->GetResource())
			{
				break;
			}
			Binding.mView = Item.mUnorderedAccessView->GetDesc();
			Binding.mResource = FArdaRHIResourceRef(Item.mUnorderedAccessView.Get());
			if (auto* Buffer = dynamic_cast<IArdaRHIBuffer*>(Item.mUnorderedAccessView->GetResource()))
			{
				Binding.mType = BufferBindingType(*Buffer, true);
			}
			else
			{
				Binding.mType = EArdaRHIBindingType::TextureUAV;
			}
			return {Binding, {}};
		case EArdaRHIResourceCollectionItemType::AccelerationStructure:
			if (!Item.mAccelerationStructure)
			{
				break;
			}
			Binding.mType = EArdaRHIBindingType::RayTracingAccelStruct;
			Binding.mResource = FArdaRHIResourceRef(Item.mAccelerationStructure.Get());
			return {Binding, {}};
		case EArdaRHIResourceCollectionItemType::Sampler:
			if (!Item.mSampler)
			{
				break;
			}
			Binding.mType = EArdaRHIBindingType::Sampler;
			Binding.mResource = FArdaRHIResourceRef(Item.mSampler.Get());
			return {Binding, {}};
		}
		return {{}, Invalid("A resource-collection item is empty or mismatched with its declared type.")};
	}
}
