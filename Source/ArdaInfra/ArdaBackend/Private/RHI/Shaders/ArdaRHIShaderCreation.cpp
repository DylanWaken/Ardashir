#include <mutex>
#include "RHI/Device/ArdaRHIDeviceImpl.h"

namespace arda::detail
{
	TArdaRHIResult<FArdaRHIShaderRef> FArdaRHIDeviceImpl::CreateShader(const FArdaRHIShaderDesc& Desc)
	{
		if (!Desc.mBytecode || Desc.mBytecodeSize == 0 || Desc.mStage == EArdaRHIShaderStage::None)
		{
			return Failure<FArdaRHIShaderRef>(Invalid("Shader bytecode and stage are required."));
		}
		auto Native = mDevice->CreateShader(Desc);
		if (!Native)
		{
			return Failure<FArdaRHIShaderRef>(eastl::move(Native.mStatus));
		}
		return {FArdaRHIShaderRef(new FArdaShader(Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {}};
	}

	TArdaRHIResult<FArdaRHIShaderLibraryRef> FArdaRHIDeviceImpl::CreateShaderLibrary(const void* Bytecode,
	    size_t BytecodeSize,
	    const char* DebugName)
	{
		if (!Bytecode || BytecodeSize == 0)
		{
			return Failure<FArdaRHIShaderLibraryRef>(Invalid("Shader-library bytecode is required."));
		}
		return {FArdaRHIShaderLibraryRef(
		            new FArdaShaderLibrary(Bytecode, BytecodeSize, DebugName, this, mLifetimeTracker)),
		    {}};
	}

	TArdaRHIResult<FArdaRHIShaderRef> FArdaRHIDeviceImpl::GetShaderFromLibrary(
	    const FArdaRHIShaderLibraryRef& Library,
	    const char* EntryPoint,
	    EArdaRHIShaderStage Stage,
	    const char* DebugName)
	{
		auto* Native = Cast<FArdaShaderLibrary>(Library.Get());
		if (!Native || !Owns(Native))
		{
			return Failure<FArdaRHIShaderRef>(WrongDevice());
		}
		if (!EntryPoint || !*EntryPoint)
		{
			return Failure<FArdaRHIShaderRef>(Invalid("A shader-library entry point is required."));
		}
		FArdaRHIShaderDesc Desc;
		Desc.mStage = Stage;
		Desc.mBytecode = Native->mBytecode.data();
		Desc.mBytecodeSize = Native->mBytecode.size();
		Desc.mEntryPoint = EntryPoint;
		Desc.mDebugName = DebugName ? DebugName : EntryPoint;
		return CreateShader(Desc);
	}

	TArdaRHIResult<FArdaRHIInputLayoutRef> FArdaRHIDeviceImpl::CreateInputLayout(
	    const eastl::vector<FArdaRHIVertexAttributeDesc>& Attributes)
	{
		FArdaRHIInputLayoutDesc Desc{Attributes};
		if (auto Status = Validate(Desc); !Status)
		{
			return Failure<FArdaRHIInputLayoutRef>(eastl::move(Status));
		}
		const auto& Limits = GetCapabilities().mLimits;
		uint64_t AttributeCount = 0;
		for (const auto& Attribute : Attributes)
		{
			AttributeCount += Attribute.mArraySize;
			if ((Limits.mMaxVertexBindings && Attribute.mBufferIndex >= Limits.mMaxVertexBindings) ||
			    (Limits.mMaxVertexStride && Attribute.mElementStride > Limits.mMaxVertexStride) ||
			    (GetCapabilities().mbFormatSupportReported &&
			        !QueryFormatSupport(Attribute.mFormat).mbVertexBuffer))
			{
				return Failure<FArdaRHIInputLayoutRef>(
				    Invalid("Vertex attribute exceeds native binding, stride, or format support."));
			}
		}
		if (Limits.mMaxVertexAttributes && AttributeCount > Limits.mMaxVertexAttributes)
		{
			return Failure<FArdaRHIInputLayoutRef>(
			    Invalid("Input layout exceeds the native vertex-attribute limit."));
		}
		std::lock_guard<std::mutex> Lock(mCacheMutex);
		if (auto Existing = mInputLayoutCache.Find(Desc))
		{
			return {Existing, {}};
		}
		FArdaRHIInputLayoutRef Result(new FArdaInputLayout(Desc, this, mLifetimeTracker));
		mInputLayoutCache.Insert(Desc, Result);
		return {Result, {}};
	}

	TArdaRHIResult<FArdaRHIBindingLayoutRef> FArdaRHIDeviceImpl::CreateBindingLayout(
	    const FArdaRHIBindingLayoutDesc& Desc)
	{
		if (auto Status = Validate(Desc); !Status)
		{
			return Failure<FArdaRHIBindingLayoutRef>(eastl::move(Status));
		}
		std::lock_guard<std::mutex> Lock(mCacheMutex);
		if (auto Existing = mBindingLayoutCache.Find(Desc))
		{
			return {Existing, {}};
		}
		auto Native = mDevice->CreateBindingLayout(Desc);
		if (!Native)
		{
			return Failure<FArdaRHIBindingLayoutRef>(eastl::move(Native.mStatus));
		}
		FArdaRHIBindingLayoutRef Result(
		    new FArdaBindingLayout(Desc, eastl::move(Native.mValue), this, mLifetimeTracker));
		mBindingLayoutCache.Insert(Desc, Result);
		return {Result, {}};
	}

	TArdaRHIResult<FArdaRHIBindingLayoutRef> FArdaRHIDeviceImpl::CreateBindlessLayout(
	    const FArdaRHIBindlessLayoutDesc& Desc)
	{
		if (Desc.mRegisterSpaces.empty() || (!Desc.mMaxCapacity && !Desc.mbUnbounded))
		{
			return Failure<FArdaRHIBindingLayoutRef>(
			    Invalid("A bindless layout requires register spaces and a capacity or unbounded mode."));
		}
		const auto& Capabilities = mDevice->GetCapabilities().mDescriptors;
		if (Desc.mbUnbounded && !Capabilities.mbUnboundedArrays)
		{
			return UnsupportedResult<FArdaRHIBindingLayoutRef>(
			    "Unbounded descriptors are unsupported by this device.");
		}
		if (Desc.mbUpdateAfterBind && !Capabilities.mbUpdateAfterBind)
		{
			return UnsupportedResult<FArdaRHIBindingLayoutRef>(
			    "Descriptor update-after-bind is unsupported by this device.");
		}
		if (Desc.mbVariableDescriptorCount && !Capabilities.mbVariableDescriptorCount)
		{
			return UnsupportedResult<FArdaRHIBindingLayoutRef>(
			    "Variable descriptor counts are unsupported by this device.");
		}
		if (Desc.mbDirectHeapIndexing && !Capabilities.mbDirectResourceHeapIndexing)
		{
			return UnsupportedResult<FArdaRHIBindingLayoutRef>(
			    "Direct descriptor-heap indexing is unsupported by this device.");
		}
		FArdaRHIBindlessLayoutDesc ResolvedDesc = Desc;
		const bool bSamplers = Desc.mRegisterSpaces.front().mType == EArdaRHIBindingType::Sampler;
		for (const auto& Item : Desc.mRegisterSpaces)
		{
			if ((Item.mType == EArdaRHIBindingType::Sampler) != bSamplers)
			{
				return Failure<FArdaRHIBindingLayoutRef>(
				    Invalid("Sampler and resource descriptors require separate bindless layouts."));
			}
		}
		const uint32_t CapacityLimit =
		    bSamplers ? Capabilities.mMaxSamplerDescriptors : Capabilities.mMaxResourceDescriptors;
		if (!ResolvedDesc.mMaxCapacity)
		{
			ResolvedDesc.mMaxCapacity = CapacityLimit;
		}
		if (!CapacityLimit || ResolvedDesc.mMaxCapacity > CapacityLimit)
		{
			return Failure<FArdaRHIBindingLayoutRef>(
			    Invalid("Bindless capacity exceeds the device's descriptor limit."));
		}
		if (Desc.mbDirectHeapIndexing && bSamplers && !Capabilities.mbDirectSamplerHeapIndexing)
		{
			return UnsupportedResult<FArdaRHIBindingLayoutRef>(
			    "Direct sampler-heap indexing is unsupported by this device.");
		}
		if (Desc.mbDescriptorBuffer && (!Capabilities.mbDescriptorBuffer || Desc.mbDirectHeapIndexing))
		{
			return UnsupportedResult<FArdaRHIBindingLayoutRef>(
			    "Descriptor buffers require native support and cannot use direct heap indexing.");
		}
		FArdaRHIBindingLayoutDesc NativeDesc;
		NativeDesc.mVisibility = ResolvedDesc.mVisibility;
		NativeDesc.mRegisterSpace = ResolvedDesc.mRegisterSpace;
		NativeDesc.mbRegisterSpaceIsDescriptorSet = true;
		NativeDesc.mDebugName = ResolvedDesc.mDebugName;
		NativeDesc.mItems.reserve(ResolvedDesc.mRegisterSpaces.size());
		for (FArdaRHIBindingLayoutItem Item : ResolvedDesc.mRegisterSpaces)
		{
			if (Item.mType == EArdaRHIBindingType::PushConstants)
			{
				return Failure<FArdaRHIBindingLayoutRef>(
				    Invalid("Push constants cannot be part of a bindless descriptor table."));
			}
			Item.mSlot += ResolvedDesc.mFirstSlot;
			Item.mArraySize = ResolvedDesc.mMaxCapacity;
			NativeDesc.mItems.push_back(Item);
		}
		if (auto Status = Validate(NativeDesc); !Status)
		{
			return Failure<FArdaRHIBindingLayoutRef>(eastl::move(Status));
		}
		auto Native = mDevice->CreateBindlessLayout(ResolvedDesc, NativeDesc);
		if (!Native)
		{
			return Failure<FArdaRHIBindingLayoutRef>(eastl::move(Native.mStatus));
		}
		auto* Layout = new FArdaBindingLayout(NativeDesc, eastl::move(Native.mValue), this, mLifetimeTracker);
		Layout->mbBindless = true;
		Layout->mBindlessDesc = ResolvedDesc;
		return {FArdaRHIBindingLayoutRef(Layout), {}};
	}

	TArdaRHIResult<FArdaRHIDescriptorTableRef> FArdaRHIDeviceImpl::CreateDescriptorTable(
	    const FArdaRHIBindingLayoutRef& LayoutRef)
	{
		auto* Layout = Cast<FArdaBindingLayout>(LayoutRef.Get());
		if (!Layout || !Owns(Layout))
		{
			return Failure<FArdaRHIDescriptorTableRef>(WrongDevice());
		}
		if (!Layout->mbBindless)
		{
			return Failure<FArdaRHIDescriptorTableRef>(
			    Invalid("Descriptor tables require a bindless binding layout."));
		}
		FArdaRHIBindingSetDesc Desc;
		Desc.mLayout = LayoutRef;
		Desc.mDebugName = Layout->mBindlessDesc.mDebugName;
		Desc.mVariableDescriptorCount =
		    Layout->mBindlessDesc.mbVariableDescriptorCount ? Layout->mBindlessDesc.mMaxCapacity : 0;
		auto Native = mDevice->CreateBindingSet(Desc, Layout->mNative, {});
		if (!Native)
		{
			return Failure<FArdaRHIDescriptorTableRef>(eastl::move(Native.mStatus));
		}
		const uint32_t Capacity = Layout->mBindlessDesc.mMaxCapacity;
		return {FArdaRHIDescriptorTableRef(new FArdaDescriptorTable(eastl::move(Desc),
		            eastl::move(Native.mValue),
		            Capacity,
		            Capacity,
		            this,
		            mLifetimeTracker)),
		    {}};
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::ResizeDescriptorTable(const FArdaRHIDescriptorTableRef& TableRef,
	    uint32_t NewSize,
	    bool bKeepContents)
	{
		auto* Table = Cast<FArdaDescriptorTable>(TableRef.Get());
		if (!Table || !Owns(Table))
		{
			return WrongDevice();
		}
		if (!NewSize || NewSize > Table->mMaxCapacity)
		{
			return Invalid("Descriptor table size must be within its bindless layout capacity.");
		}
		auto* Layout = Cast<FArdaBindingLayout>(Table->mDesc.mLayout.Get());
		if (!Layout || !Owns(Layout) || !Layout->mbBindless)
		{
			return WrongDevice();
		}
		std::lock_guard<std::mutex> Lock(Table->mMutex);
		FArdaRHIBindingSetDesc NewDesc = Table->mDesc;
		NewDesc.mVariableDescriptorCount = Layout->mBindlessDesc.mbVariableDescriptorCount ? NewSize : 0;
		if (!bKeepContents)
		{
			NewDesc.mItems.clear();
		}
		else
		{
			NewDesc.mItems.erase(eastl::remove_if(NewDesc.mItems.begin(),
			                         NewDesc.mItems.end(),
			                         [NewSize](const FArdaRHIBindingItem& Item)
			                         {
				                         return Item.mArrayElement >= NewSize;
			                         }),
			    NewDesc.mItems.end());
		}
		eastl::vector<FArdaProviderBinding> Bindings;
		Bindings.reserve(NewDesc.mItems.size());
		for (const FArdaRHIBindingItem& Item : NewDesc.mItems)
		{
			auto* Resource = Cast<FArdaResource>(Item.mResource.Get());
			FArdaProviderObjectRef Native = GetNativeObject(Item.mResource.Get());
			if (!Resource || !Owns(Resource) || !Native)
			{
				return WrongDevice();
			}
			Bindings.push_back({Item, eastl::move(Native)});
		}
		auto Native = mDevice->CreateBindingSet(NewDesc, Layout->mNative, Bindings);
		if (!Native)
		{
			return Native.mStatus;
		}
		Table->mDesc = eastl::move(NewDesc);
		Table->mNative = eastl::move(Native.mValue);
		Table->mCapacity = NewSize;
		return {};
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::WriteDescriptorTable(const FArdaRHIDescriptorTableRef& TableRef,
	    const FArdaRHIBindingItem& Item)
	{
		auto* Table = Cast<FArdaDescriptorTable>(TableRef.Get());
		if (!Table || !Owns(Table))
		{
			return WrongDevice();
		}
		auto* Layout = Cast<FArdaBindingLayout>(Table->mDesc.mLayout.Get());
		if (!Layout || !Owns(Layout) || !Layout->mbBindless)
		{
			return WrongDevice();
		}
		auto* Resource = Cast<FArdaResource>(Item.mResource.Get());
		FArdaProviderObjectRef NativeObject = GetNativeObject(Item.mResource.Get());
		if (!Resource || !Owns(Resource) || !NativeObject)
		{
			return WrongDevice();
		}
		std::lock_guard<std::mutex> Lock(Table->mMutex);
		if (Item.mArrayElement >= Table->mCapacity)
		{
			return Invalid("Descriptor table array element is out of range.");
		}
		FArdaRHIBindingItem ResolvedItem = Item;
		if (auto Status = ResolveBindingItem(Layout->mDesc, ResolvedItem, GetCapabilities().mLimits); !Status)
		{
			return Status;
		}
		FArdaRHIBindingSetDesc NewDesc = Table->mDesc;
		auto Existing = eastl::find_if(NewDesc.mItems.begin(),
		    NewDesc.mItems.end(),
		    [&Item](const FArdaRHIBindingItem& Candidate)
		    {
			    return Candidate.mSlot == Item.mSlot && Candidate.mArrayElement == Item.mArrayElement &&
			        Candidate.mType == Item.mType;
		    });
		if (Existing == NewDesc.mItems.end())
		{
			NewDesc.mItems.push_back(eastl::move(ResolvedItem));
		}
		else
		{
			*Existing = eastl::move(ResolvedItem);
		}
		eastl::vector<FArdaProviderBinding> Bindings;
		Bindings.reserve(NewDesc.mItems.size());
		for (const FArdaRHIBindingItem& Binding : NewDesc.mItems)
		{
			FArdaProviderObjectRef BindingObject = GetNativeObject(Binding.mResource.Get());
			if (!BindingObject)
			{
				return Invalid("Descriptor table contains a non-native resource.");
			}
			Bindings.push_back({Binding, eastl::move(BindingObject)});
		}
		auto Native = mDevice->CreateBindingSet(NewDesc, Layout->mNative, Bindings);
		if (!Native)
		{
			return Native.mStatus;
		}
		Table->mDesc = eastl::move(NewDesc);
		Table->mNative = eastl::move(Native.mValue);
		return {};
	}

	TArdaRHIResult<FArdaRHIBindingSetRef> FArdaRHIDeviceImpl::CreateBindingSet(
	    const FArdaRHIBindingSetDesc& InputDesc)
	{
		auto* Layout = Cast<FArdaBindingLayout>(InputDesc.mLayout.Get());
		if (!Layout || !Owns(Layout))
		{
			return Failure<FArdaRHIBindingSetRef>(WrongDevice());
		}
		if (Layout->mbBindless)
		{
			return Failure<FArdaRHIBindingSetRef>(
			    Invalid("Bindless layouts must be instantiated as descriptor tables."));
		}

		// Reject invalid/duplicate descriptor destinations before checking completeness or calling a provider.
		FArdaRHIBindingSetDesc Desc = InputDesc;
		for (size_t Index = 0; Index < Desc.mItems.size(); ++Index)
		{
			auto& Item = Desc.mItems[Index];
			const auto* Resource = Cast<FArdaResource>(Item.mResource.Get());
			if (!Resource || !Owns(Resource))
			{
				return Failure<FArdaRHIBindingSetRef>(WrongDevice());
			}
			if (auto Status = ResolveBindingItem(Layout->mDesc, Item, GetCapabilities().mLimits); !Status)
			{
				return Failure<FArdaRHIBindingSetRef>(eastl::move(Status));
			}
			for (size_t Previous = 0; Previous < Index; ++Previous)
			{
				const auto& Other = Desc.mItems[Previous];
				if (Other.mSlot == Item.mSlot && Other.mType == Item.mType &&
				    Other.mArrayElement == Item.mArrayElement)
				{
					return Failure<FArdaRHIBindingSetRef>(
					    Invalid("Binding set contains a duplicate descriptor destination."));
				}
			}
		}

		for (const FArdaRHIBindingLayoutItem& Declared : Layout->mDesc.mItems)
		{
			if (Declared.mType == EArdaRHIBindingType::PushConstants)
			{
				continue;
			}
			for (uint32_t Element = 0; Element < eastl::max(1u, Declared.mArraySize); ++Element)
			{
				const bool bFound = eastl::any_of(Desc.mItems.begin(),
				    Desc.mItems.end(),
				    [&Declared, Element](const FArdaRHIBindingItem& Item)
				    {
					    return Item.mSlot == Declared.mSlot && Item.mType == Declared.mType &&
					        Item.mArrayElement == Element;
				    });
				if (!bFound)
				{
					return Failure<FArdaRHIBindingSetRef>(
					    Invalid("A binding set is missing an item required by its layout."));
				}
			}
		}
		eastl::vector<FArdaProviderBinding> Bindings;
		Bindings.reserve(Desc.mItems.size());
		for (const auto& Item : Desc.mItems)
		{
			FArdaProviderObjectRef Object = GetNativeObject(Item.mResource.Get());
			if (!Object)
			{
				return Failure<FArdaRHIBindingSetRef>(
				    Invalid("Binding item does not reference a bindable native resource."));
			}
			Bindings.push_back({Item, eastl::move(Object)});
		}
		auto Native = mDevice->CreateBindingSet(Desc, Layout->mNative, Bindings);
		if (!Native)
		{
			return Failure<FArdaRHIBindingSetRef>(eastl::move(Native.mStatus));
		}
		return {FArdaRHIBindingSetRef(
		            new FArdaBindingSet(eastl::move(Desc), eastl::move(Native.mValue), this, mLifetimeTracker)),
		    {}};
	}

	TArdaRHIResult<FArdaRHIShaderBundleRef> FArdaRHIDeviceImpl::CreateShaderBundle(
	    const FArdaRHIShaderBundleDesc& Desc)
	{
		if (!GetCapabilities().mbShaderBundleDispatch)
		{
			return UnsupportedResult<FArdaRHIShaderBundleRef>("Shader bundles are unsupported by this device.");
		}
		if (!Desc.mMaxRecords)
		{
			return Failure<FArdaRHIShaderBundleRef>(
			    Invalid("A shader bundle requires a non-zero record capacity."));
		}
		return {FArdaRHIShaderBundleRef(new FArdaShaderBundle(Desc, this, mLifetimeTracker)), {}};
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::SetShaderBundleRecords(const FArdaRHIShaderBundleRef& BundleRef,
	    const eastl::vector<FArdaRHIShaderBundleRecord>& Records)
	{
		auto* Bundle = Cast<FArdaShaderBundle>(BundleRef.Get());
		if (!Bundle || !Owns(Bundle))
		{
			return WrongDevice();
		}
		if (Records.size() > Bundle->mDesc.mMaxRecords)
		{
			return Invalid("Shader-bundle records exceed the bundle capacity.");
		}
		for (const auto& Record : Records)
		{
			const bool bCompute = static_cast<bool>(Record.mComputePipeline);
			const bool bMesh = static_cast<bool>(Record.mMeshPipeline);
			if (bCompute == bMesh || bMesh != Bundle->mDesc.mbMeshRecords)
			{
				return Invalid("Each shader-bundle record must match its compute or mesh bundle type.");
			}
			auto* Pipeline = bCompute ? Cast<FArdaResource>(Record.mComputePipeline.Get())
			                          : Cast<FArdaResource>(Record.mMeshPipeline.Get());
			if (!Pipeline || !Owns(Pipeline))
			{
				return WrongDevice();
			}
			for (const auto& Binding : Record.mBindings)
			{
				auto* Set = Cast<FArdaResource>(Binding.Get());
				if (!Set || !Owns(Set))
				{
					return WrongDevice();
				}
			}
			if (!Record.mGroupsX || !Record.mGroupsY || !Record.mGroupsZ)
			{
				return Invalid("Shader-bundle dispatch dimensions must be non-zero.");
			}
		}
		std::lock_guard<std::mutex> Lock(Bundle->mMutex);
		Bundle->mRecords = Records;
		return {};
	}

	TArdaRHIResult<FArdaRHIShaderTableRef> FArdaRHIDeviceImpl::CreateShaderTable(
	    const FArdaRHIRayTracingPipelineRef& PipelineRef,
	    const FArdaRHIShaderTableDesc& Desc)
	{
		auto* Pipeline = Cast<FArdaRayTracingPipeline>(PipelineRef.Get());
		if (!Pipeline || !Owns(Pipeline))
		{
			return Failure<FArdaRHIShaderTableRef>(WrongDevice());
		}
		if (!Desc.mMaxEntries)
		{
			return Failure<FArdaRHIShaderTableRef>(Invalid("A shader table requires a non-zero entry capacity."));
		}
		auto Native = mDevice->CreateShaderTable(Pipeline->mNative, Desc);
		if (!Native)
		{
			return Failure<FArdaRHIShaderTableRef>(eastl::move(Native.mStatus));
		}
		return {FArdaRHIShaderTableRef(
		            new FArdaShaderTable(Desc, PipelineRef, eastl::move(Native.mValue), this, mLifetimeTracker)),
		    {}};
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::SetShaderTableRecord(const FArdaRHIShaderTableRef& TableRef,
	    const FArdaRHIShaderTableRecordDesc& Record)
	{
		auto* Table = Cast<FArdaShaderTable>(TableRef.Get());
		if (!Table || !Owns(Table))
		{
			return WrongDevice();
		}
		if (Record.mRecordIndex >= Table->mDesc.mMaxEntries)
		{
			return Invalid("Shader-table record index exceeds capacity.");
		}
		if (Record.mExportName.empty())
		{
			return Invalid("A shader-table record requires an export name.");
		}
		if (Record.mLocalArguments.size() > Table->mDesc.mMaxLocalArgumentBytes)
		{
			return Invalid("Shader-table local arguments exceed the declared maximum.");
		}
		auto* Bindings = Cast<FArdaResource>(Record.mBindings.Get());
		if (Record.mBindings && (!Bindings || !Owns(Bindings)))
		{
			return WrongDevice();
		}
		auto NativeBindings = CaptureNativeBindings(Bindings);
		if (Record.mBindings && !NativeBindings)
		{
			return Invalid("Local shader-table bindings require a populated binding set or descriptor table.");
		}
		auto* Geometry = Cast<FArdaAccelStruct>(Record.mGeometry.Get());
		if (Record.mGeometry && (!Geometry || !Owns(Geometry)))
		{
			return WrongDevice();
		}
		std::lock_guard<std::mutex> Lock(Table->mMutex);
		const FArdaRHIStatus Status = mDevice->SetShaderTableRecord(Table->mNative,
		    Record,
		    NativeBindings,
		    Geometry ? Geometry->mNative : FArdaProviderObjectRef{});
		if (Status)
		{
			Table->mRecordTypes[Record.mRecordIndex] = Record.mType;
		}
		return Status;
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::CommitShaderTable(const FArdaRHIShaderTableRef& TableRef)
	{
		auto* Table = Cast<FArdaShaderTable>(TableRef.Get());
		if (!Table || !Owns(Table))
		{
			return WrongDevice();
		}
		std::lock_guard<std::mutex> Lock(Table->mMutex);
		if (!Table->HasRayGeneration())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "A shader table requires a ray-generation record before commit.");
		}
		return mDevice->CommitShaderTable(Table->mNative);
	}

	FArdaRHIStatus FArdaRHIDeviceImpl::SetShaderTableRayGeneration(const FArdaRHIShaderTableRef& TableRef,
	    const char* ExportName,
	    const FArdaRHIBindingSetRef& LocalBindings)
	{
		auto* Table = Cast<FArdaShaderTable>(TableRef.Get());
		auto* Set = Cast<FArdaResource>(LocalBindings.Get());
		if (!Table || !Owns(Table) || (LocalBindings && (!Set || !Owns(Set))))
		{
			return WrongDevice();
		}
		if (!ExportName || !*ExportName)
		{
			return Invalid("A ray-generation export name is required.");
		}
		auto NativeBindings = CaptureNativeBindings(Set);
		if (LocalBindings && !NativeBindings)
		{
			return Invalid("Local shader-table bindings require a populated binding set or descriptor table.");
		}
		std::lock_guard<std::mutex> Lock(Table->mMutex);
		const auto Status = mDevice->SetShaderTableRayGeneration(Table->mNative, ExportName, NativeBindings);
		if (Status)
		{
			Table->mRecordTypes[0] = EArdaRHIShaderTableRecordType::RayGeneration;
		}
		return Status;
	}

	namespace
	{
		TArdaRHIResult<int> AddShaderTableEntry(FArdaRHIDeviceImpl& Device,
		    const FArdaRHIShaderTableRef& TableRef,
		    const char* ExportName,
		    const FArdaRHIBindingSetRef& LocalBindings,
		    uint32_t Category)
		{
			auto* Table = Cast<FArdaShaderTable>(TableRef.Get());
			auto* Set = Cast<FArdaResource>(LocalBindings.Get());
			if (!Table || !Device.Owns(Table) || (LocalBindings && (!Set || !Device.Owns(Set))))
			{
				return Failure<int>(WrongDevice());
			}
			if (!ExportName || !*ExportName)
			{
				return Failure<int>(Invalid("A shader-table export name is required."));
			}
			auto NativeBindings = CaptureNativeBindings(Set);
			if (LocalBindings && !NativeBindings)
			{
				return Failure<int>(
				    Invalid("Local shader-table bindings require a populated binding set or descriptor table."));
			}
			std::lock_guard<std::mutex> Lock(Table->mMutex);
			if (Table->CountEntries() >= Table->mDesc.mMaxEntries)
			{
				return Failure<int>(Invalid("The shader table is at capacity."));
			}
			const auto Status = Device.GetProviderDevice().AddShaderTableEntry(Table->mNative,
			    ExportName,
			    NativeBindings,
			    Category);
			if (!Status)
			{
				return Failure<int>(Status);
			}
			const auto Unused = eastl::find(Table->mRecordTypes.begin(), Table->mRecordTypes.end(), eastl::nullopt);
			const int Index = static_cast<int>(Unused - Table->mRecordTypes.begin());
			*Unused = Category == 0 ? EArdaRHIShaderTableRecordType::Miss
			    : Category == 1     ? EArdaRHIShaderTableRecordType::HitGroup
			                        : EArdaRHIShaderTableRecordType::Callable;
			return {Index, {}};
		}
	}

	TArdaRHIResult<int> FArdaRHIDeviceImpl::AddShaderTableMiss(const FArdaRHIShaderTableRef& Table,
	    const char* ExportName,
	    const FArdaRHIBindingSetRef& LocalBindings)
	{
		return AddShaderTableEntry(*this, Table, ExportName, LocalBindings, 0);
	}

	TArdaRHIResult<int> FArdaRHIDeviceImpl::AddShaderTableHitGroup(const FArdaRHIShaderTableRef& Table,
	    const char* ExportName,
	    const FArdaRHIBindingSetRef& LocalBindings)
	{
		return AddShaderTableEntry(*this, Table, ExportName, LocalBindings, 1);
	}

	TArdaRHIResult<int> FArdaRHIDeviceImpl::AddShaderTableCallable(const FArdaRHIShaderTableRef& Table,
	    const char* ExportName,
	    const FArdaRHIBindingSetRef& LocalBindings)
	{
		return AddShaderTableEntry(*this, Table, ExportName, LocalBindings, 2);
	}
}
