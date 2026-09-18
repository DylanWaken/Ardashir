#include "RHI/Device/ArdaRHIDeviceImpl.h"
#include "RHI/Resources/ArdaRHIResourceCollectionImpl.h"
#include "RHI/Shaders/ArdaRHIBindingValidation.h"
#include <EASTL/algorithm.h>
#include <mutex>

namespace arda::detail
{
	TArdaRHIResult<FArdaRHIResourceCollectionRef> FArdaRHIDeviceImpl::CreateResourceCollection(
	    const FArdaRHIResourceCollectionDesc& Desc)
	{
		eastl::vector<FArdaRHIBindingItem> Bindings;
		Bindings.reserve(Desc.mItems.size());
		for (uint32_t Index = 0; Index < Desc.mItems.size(); ++Index)
		{
			auto Binding = MakeCollectionBinding(Desc.mItems[Index], Index);
			if (!Binding)
			{
				return Failure<FArdaRHIResourceCollectionRef>(eastl::move(Binding.mStatus));
			}
			auto* Resource = Cast<FArdaResource>(Binding.mValue.mResource.Get());
			if (!Resource || !Owns(Resource))
			{
				return Failure<FArdaRHIResourceCollectionRef>(WrongDevice());
			}
			Bindings.push_back(eastl::move(Binding.mValue));
		}

		FArdaRHIDescriptorTableRef DescriptorTable;
		if (Desc.mbDirectlyIndexed)
		{
			if (Bindings.empty())
			{
				return Failure<FArdaRHIResourceCollectionRef>(
				    Invalid("A directly indexed resource collection cannot be empty."));
			}
			const auto& Caps = GetCapabilities().mDescriptors;
			const bool bSampler = Bindings.front().mType == EArdaRHIBindingType::Sampler;
			if ((!bSampler && !Caps.mbDirectResourceHeapIndexing) ||
			    (bSampler && !Caps.mbDirectSamplerHeapIndexing))
			{
				return UnsupportedResult<FArdaRHIResourceCollectionRef>(
				    "Direct descriptor-heap indexing is unsupported for this collection.");
			}
			const EArdaRHIBindingType Type = Bindings.front().mType;
			if (eastl::any_of(Bindings.begin(),
			        Bindings.end(),
			        [Type](const FArdaRHIBindingItem& Binding)
			        {
				        return Binding.mType != Type;
			        }))
			{
				return Failure<FArdaRHIResourceCollectionRef>(
				    Invalid("A directly indexed collection must use one homogeneous native descriptor type."));
			}
			FArdaRHIBindlessLayoutDesc LayoutDesc;
			LayoutDesc.mVisibility = EArdaRHIShaderStage::All;
			LayoutDesc.mMaxCapacity = static_cast<uint32_t>(Bindings.size());
			LayoutDesc.mbUpdateAfterBind = Desc.mbMutable;
			LayoutDesc.mbDirectHeapIndexing = true;
			LayoutDesc.mLayoutType = bSampler ? EArdaRHIBindlessLayoutType::MutableSampler
			                                  : EArdaRHIBindlessLayoutType::MutableSrvUavCbv;
			LayoutDesc.mRegisterSpaces.push_back({0, 1, Type});
			LayoutDesc.mDebugName = Desc.mDebugName;
			auto Layout = CreateBindlessLayout(LayoutDesc);
			if (!Layout)
			{
				return Failure<FArdaRHIResourceCollectionRef>(eastl::move(Layout.mStatus));
			}
			auto Table = CreateDescriptorTable(Layout.mValue);
			if (!Table)
			{
				return Failure<FArdaRHIResourceCollectionRef>(eastl::move(Table.mStatus));
			}
			for (const auto& Binding : Bindings)
			{
				const FArdaRHIStatus Status = WriteDescriptorTable(Table.mValue, Binding);
				if (!Status)
				{
					return Failure<FArdaRHIResourceCollectionRef>(Status);
				}
			}
			DescriptorTable = eastl::move(Table.mValue);
		}

		return {FArdaRHIResourceCollectionRef(
		            new FArdaResourceCollection(Desc, eastl::move(DescriptorTable), this, mLifetimeTracker)),
		    {}};
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::UpdateResourceCollection(const FArdaRHIResourceCollectionRef& CollectionRef,
	    uint32_t Index,
	    const FArdaRHIResourceCollectionItem& Item)
	{
		auto* Collection = Cast<FArdaResourceCollection>(CollectionRef.Get());
		if (!Collection || !Owns(Collection))
		{
			return WrongDevice();
		}
		std::lock_guard<std::mutex> Lock(Collection->mMutex);
		if (!Collection->mDesc.mbMutable)
		{
			return Invalid("The resource collection is immutable.");
		}
		if (Index >= Collection->mDesc.mItems.size())
		{
			return Invalid("The resource-collection index is out of range.");
		}
		auto Binding = MakeCollectionBinding(Item, Index);
		if (!Binding)
		{
			return Binding.mStatus;
		}
		auto* Resource = Cast<FArdaResource>(Binding.mValue.mResource.Get());
		if (!Resource || !Owns(Resource))
		{
			return WrongDevice();
		}
		if (Collection->mDescriptorTable)
		{
			auto Existing = MakeCollectionBinding(Collection->mDesc.mItems[Index], Index);
			if (!Existing)
			{
				return Existing.mStatus;
			}
			if (Existing.mValue.mType != Binding.mValue.mType)
			{
				return Invalid("A directly indexed collection update cannot change descriptor type.");
			}
			const FArdaRHIStatus Status = WriteDescriptorTable(Collection->mDescriptorTable, Binding.mValue);
			if (!Status)
			{
				return Status;
			}
		}
		Collection->mDesc.mItems[Index] = Item;
		return {};
	}
}
