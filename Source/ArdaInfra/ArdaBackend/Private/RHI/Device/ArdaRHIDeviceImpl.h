/** Facade device implementation and bounded descriptor caches. */
#pragma once

#include "RHI/Device/ArdaRHIDevice.h"
#include "RHI/Providers/ArdaRHIProviderDevice.h"
#include "RHI/Config/ArdaRHIValidation.h"
#include "RHI/Resources/ArdaRHIResourceAccess.h"
#include "RHI/Shaders/ArdaRHIBindingValidation.h"
#include "RHI/Resources/ArdaRHITextureBufferImpl.h"
#include "RHI/Resources/ArdaRHIAccelerationImpl.h"
#include "RHI/Shaders/ArdaRHIShaderImpl.h"
#include "RHI/Pipelines/ArdaRHIPipelineImpl.h"
#include "RHI/Scheduling/ArdaRHISignalImpl.h"
#include "RHI/Memory/ArdaGpuAllocatorPrivate.h"
#include <EASTL/shared_ptr.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>
#include <mutex>
#include <EASTL/type_traits.h>

namespace arda::detail
{
	class FArdaCommandList;

	template <typename Desc, typename Ref>
	class TArdaDescriptorCache
	{
	public:
		Ref Find(const Desc& Descriptor) const
		{
			for (const auto& Entry : mEntries)
			{
				if (Entry.mDesc == Descriptor)
				{
					return Entry.mResource;
				}
			}
			return {};
		}

		void Insert(const Desc& Descriptor, Ref Resource)
		{
			if (mEntries.size() >= 64)
			{
				mEntries.erase(mEntries.begin());
			}
			mEntries.push_back({Descriptor, eastl::move(Resource)});
		}

		void Clear()
		{
			mEntries.clear();
		}

		size_t Size() const noexcept
		{
			return mEntries.size();
		}

	private:
		struct FArdaEntry
		{
			Desc mDesc;
			Ref mResource;
		};

		eastl::vector<FArdaEntry> mEntries;
	};

	class FArdaRHIDeviceImpl final : public FArdaResource, public IArdaRHIDevice
	{
	public:
		FArdaCudaCapabilities GetCudaCapabilities() const override
		{
			return mDevice->GetCudaCapabilities();
		}

		explicit FArdaRHIDeviceImpl(eastl::shared_ptr<IArdaRHIProviderDevice> Device)
		    : FArdaResource(EArdaRHIResourceType::Device, "RHIDevice", this),
		      mLifetimeTracker(eastl::make_shared<FArdaLifetimeTracker>()),
		      mDevice(eastl::move(Device)),
		      mAllocator(mDevice)
		{
		}

		~FArdaRHIDeviceImpl() override
		{
			FlushAndDisablePipelineCachePersistence();
			if (mDevice)
			{
				(void)mDevice->WaitForIdle();
			}
		}

		const FArdaRHICapabilities& GetCapabilities() const noexcept override
		{
			return mDevice->GetCapabilities();
		}

		FArdaRHIFormatSupport QueryFormatSupport(EArdaRHIFormat Format) const noexcept override
		{
			return mDevice->QueryFormatSupport(Format);
		}

		TArdaRHIResult<FArdaRHIDiagnosticSnapshot> CaptureDiagnosticSnapshot() const override
		{
			return mDevice->CaptureDiagnosticSnapshot();
		}

		TArdaRHIResult<FArdaRHITextureRef> CreateTexture(const FArdaRHITextureDesc&) override;
		TArdaRHIResult<FArdaRHITextureReferenceRef> CreateTextureReference(const FArdaRHITextureRef&) override;
		FArdaRHIStatus SetTextureReference(const FArdaRHITextureReferenceRef&, const FArdaRHITextureRef&) override;
		TArdaRHIResult<FArdaRHIBufferRef> CreateBuffer(const FArdaRHIBufferDesc&) override;
		TArdaRHIResult<FArdaRHIUniformBufferRef> CreateUniformBuffer(const FArdaRHIUniformBufferDesc&,
		    const void*) override;
		TArdaRHIResult<FArdaRHITextureRef> ImportNativeTexture(const FArdaRHINativeTextureImportDesc&) override;
		TArdaRHIResult<FArdaRHIBufferRef> ImportNativeBuffer(const FArdaRHINativeBufferImportDesc&) override;
		TArdaRHIResult<FArdaRHIHeapRef> CreateHeap(const FArdaRHIHeapDesc&) override;
		TArdaRHIResult<FArdaRHIBufferRef> CreatePlacedBuffer(const FArdaRHIBufferDesc&,
		    const FArdaRHIHeapRef&,
		    uint64_t) override;
		TArdaRHIResult<FArdaRHITextureRef> CreatePlacedTexture(const FArdaRHITextureDesc&,
		    const FArdaRHIHeapRef&,
		    uint64_t) override;

		FArdaGpuAllocatorStats GetGpuAllocatorStats() const override
		{
			return mAllocator.GetStats();
		}

		FArdaRHIStatus SetGpuAllocatorOptions(const FArdaGpuAllocatorOptions& Options) override
		{
			return mAllocator.SetOptions(Options);
		}

		void TrimGpuAllocator() override
		{
			mDevice->RunGarbageCollection();
			mAllocator.Collect(true);
		}

		TArdaRHIResult<FArdaRHIStagingTextureRef> CreateStagingTexture(const FArdaRHIStagingTextureDesc&) override;
		TArdaRHIResult<FArdaRHIStagingTextureMapping> MapStagingTexture(const FArdaRHIStagingTextureRef&,
		    const FArdaRHITextureSlice&,
		    EArdaRHICpuAccess) override;
		FArdaRHIStatus UnmapStagingTexture(const FArdaRHIStagingTextureRef&) override;
		TArdaRHIResult<FArdaRHIShaderResourceViewRef> CreateShaderResourceView(const TArdaRHIRef<IArdaRHIResource>&,
		    const FArdaRHIViewDesc&) override;
		TArdaRHIResult<FArdaRHIUnorderedAccessViewRef> CreateUnorderedAccessView(
		    const TArdaRHIRef<IArdaRHIResource>&,
		    const FArdaRHIViewDesc&) override;
		TArdaRHIResult<FArdaRHISamplerRef> CreateSampler(const FArdaRHISamplerDesc&) override;
		TArdaRHIResult<FArdaRHIShaderRef> CreateShader(const FArdaRHIShaderDesc&) override;
		TArdaRHIResult<FArdaRHIShaderLibraryRef> CreateShaderLibrary(const void*, size_t, const char*) override;
		TArdaRHIResult<FArdaRHIShaderRef> GetShaderFromLibrary(const FArdaRHIShaderLibraryRef&,
		    const char*,
		    EArdaRHIShaderStage,
		    const char*) override;
		TArdaRHIResult<FArdaRHIInputLayoutRef> CreateInputLayout(
		    const eastl::vector<FArdaRHIVertexAttributeDesc>&) override;
		TArdaRHIResult<FArdaRHIBindingLayoutRef> CreateBindingLayout(const FArdaRHIBindingLayoutDesc&) override;
		TArdaRHIResult<FArdaRHIBindingLayoutRef> CreateBindlessLayout(const FArdaRHIBindlessLayoutDesc&) override;
		TArdaRHIResult<FArdaRHIBindingSetRef> CreateBindingSet(const FArdaRHIBindingSetDesc&) override;
		TArdaRHIResult<FArdaRHIDescriptorTableRef> CreateDescriptorTable(const FArdaRHIBindingLayoutRef&) override;
		TArdaRHIResult<FArdaRHIResourceCollectionRef> CreateResourceCollection(
		    const FArdaRHIResourceCollectionDesc&) override;
		FArdaRHIStatus UpdateResourceCollection(const FArdaRHIResourceCollectionRef&,
		    uint32_t,
		    const FArdaRHIResourceCollectionItem&) override;
		FArdaRHIStatus ResizeDescriptorTable(const FArdaRHIDescriptorTableRef&, uint32_t, bool) override;
		FArdaRHIStatus WriteDescriptorTable(const FArdaRHIDescriptorTableRef&, const FArdaRHIBindingItem&) override;
		TArdaRHIResult<FArdaRHIFramebufferRef> CreateFramebuffer(const FArdaRHIFramebufferDesc&) override;
		TArdaRHIResult<FArdaRHIGraphicsPipelineRef> CreateGraphicsPipeline(
		    const FArdaRHIGraphicsPipelineDesc&) override;
		TArdaRHIResult<FArdaRHIComputePipelineRef> CreateComputePipeline(
		    const FArdaRHIComputePipelineDesc&) override;
		TArdaRHIResult<FArdaRHIMeshletPipelineRef> CreateMeshletPipeline(
		    const FArdaRHIMeshletPipelineDesc&) override;
		TArdaRHIResult<FArdaRHIRasterStateRef> CreateRasterState(const FArdaRHIRasterState&) override;
		TArdaRHIResult<FArdaRHIBlendStateRef> CreateBlendState(const FArdaRHIBlendState&) override;
		TArdaRHIResult<FArdaRHIDepthStencilStateRef> CreateDepthStencilState(
		    const FArdaRHIDepthStencilState&) override;
		TArdaRHIResult<FArdaRHIAccelStructRef> CreateAccelStruct(const FArdaRHIAccelStructDesc&) override;
		TArdaRHIResult<FArdaRHIAccelStructMemoryRequirements> GetAccelStructBuildMemoryRequirements(
		    const FArdaRHIAccelStructDesc&) override;
		TArdaRHIResult<uint64_t> GetAccelStructCompactedSize(const FArdaRHIAccelStructRef&) override;
		TArdaRHIResult<FArdaRHIOpacityMicromapRef> CreateOpacityMicromap(
		    const FArdaRHIOpacityMicromapDesc&) override;
		TArdaRHIResult<uint64_t> GetOpacityMicromapCompactedSize(const FArdaRHIOpacityMicromapRef&) override;
		TArdaRHIResult<FArdaRHIRayTracingPipelineRef> CreateRayTracingPipeline(
		    const FArdaRHIRayTracingPipelineDesc&) override;
		TArdaRHIResult<FArdaRHIShaderTableRef> CreateShaderTable(const FArdaRHIRayTracingPipelineRef&,
		    const FArdaRHIShaderTableDesc&) override;
		FArdaRHIStatus SetShaderTableRecord(const FArdaRHIShaderTableRef&,
		    const FArdaRHIShaderTableRecordDesc&) override;
		FArdaRHIStatus CommitShaderTable(const FArdaRHIShaderTableRef&) override;
		TArdaRHIResult<FArdaRHIShaderBundleRef> CreateShaderBundle(const FArdaRHIShaderBundleDesc&) override;
		FArdaRHIStatus SetShaderBundleRecords(const FArdaRHIShaderBundleRef&,
		    const eastl::vector<FArdaRHIShaderBundleRecord>&) override;
		TArdaRHIResult<FArdaRHIWorkGraphPipelineRef> CreateWorkGraphPipeline(
		    const FArdaRHIWorkGraphPipelineDesc&) override;
		FArdaRHIStatus SetShaderTableRayGeneration(const FArdaRHIShaderTableRef&,
		    const char*,
		    const FArdaRHIBindingSetRef&) override;
		TArdaRHIResult<int> AddShaderTableMiss(const FArdaRHIShaderTableRef&,
		    const char*,
		    const FArdaRHIBindingSetRef&) override;
		TArdaRHIResult<int> AddShaderTableHitGroup(const FArdaRHIShaderTableRef&,
		    const char*,
		    const FArdaRHIBindingSetRef&) override;
		TArdaRHIResult<int> AddShaderTableCallable(const FArdaRHIShaderTableRef&,
		    const char*,
		    const FArdaRHIBindingSetRef&) override;
		TArdaRHIResult<FArdaRHISamplerFeedbackTextureRef> CreateSamplerFeedbackTexture(const FArdaRHITextureRef&,
		    const FArdaRHISamplerFeedbackTextureDesc&) override;
		TArdaRHIResult<FArdaRHIEventQueryRef> CreateEventQuery() override;
		TArdaRHIResult<FArdaRHITimerQueryRef> CreateTimerQuery() override;
		TArdaRHIResult<FArdaRHIGpuFenceRef> CreateGpuFence() override;
		FArdaRHIStatus SignalEventQuery(const FArdaRHIEventQueryRef&, EArdaRHIQueueType) override;
		TArdaRHIResult<bool> PollEventQuery(const FArdaRHIEventQueryRef&) override;
		FArdaRHIStatus WaitEventQuery(const FArdaRHIEventQueryRef&) override;
		FArdaRHIStatus ResetEventQuery(const FArdaRHIEventQueryRef&) override;
		TArdaRHIResult<bool> PollTimerQuery(const FArdaRHITimerQueryRef&) override;
		TArdaRHIResult<float> GetTimerQuerySeconds(const FArdaRHITimerQueryRef&) override;
		FArdaRHIStatus ResetTimerQuery(const FArdaRHITimerQueryRef&) override;
		FArdaRHIStatus SignalGpuFence(const FArdaRHIGpuFenceRef&, EArdaRHIQueueType) override;
		TArdaRHIResult<bool> PollGpuFence(const FArdaRHIGpuFenceRef&) override;
		FArdaRHIStatus WaitGpuFence(const FArdaRHIGpuFenceRef&) override;
		FArdaRHIStatus ResetGpuFence(const FArdaRHIGpuFenceRef&) override;
		TArdaRHIResult<FArdaRHICommandListRef> CreateCommandList(EArdaRHIQueueType, bool) override;
		TArdaRHIResult<uint64_t> ExecuteCommandList(const FArdaRHICommandListRef&) override;
		TArdaRHIResult<uint64_t> ExecuteCommandLists(const eastl::vector<FArdaRHICommandListRef>&,
		    EArdaRHIQueueType) override;

		FArdaRHIStatus QueueWait(EArdaRHIQueueType WaitQueue,
		    EArdaRHIQueueType ExecutionQueue,
		    uint64_t Submission) override
		{
			return mDevice->QueueWait(WaitQueue, ExecutionQueue, Submission);
		}

		TArdaRHIResult<FArdaRHIMemoryRequirements> QueryTextureMemoryRequirements(
		    const FArdaRHITextureDesc&) override;
		TArdaRHIResult<FArdaRHIMemoryRequirements> QueryBufferMemoryRequirements(
		    const FArdaRHIBufferDesc&) override;
		TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(const FArdaRHITextureRef&) override;
		TArdaRHIResult<FArdaRHIMemoryRequirements> GetBufferMemoryRequirements(const FArdaRHIBufferRef&) override;
		TArdaRHIResult<FArdaRHIMemoryRequirements> GetAccelStructMemoryRequirements(
		    const FArdaRHIAccelStructRef&) override;
		FArdaRHIStatus BindTextureMemory(const FArdaRHITextureRef&, const FArdaRHIHeapRef&, uint64_t) override;
		FArdaRHIStatus BindBufferMemory(const FArdaRHIBufferRef&, const FArdaRHIHeapRef&, uint64_t) override;

		FArdaRHIStatus BindAccelStructMemory(const FArdaRHIAccelStructRef&,
		    const FArdaRHIHeapRef&,
		    uint64_t) override
		{
			return Unsupported("Acceleration structures are unsupported by the backend providers.");
		}

		TArdaRHIResult<FArdaRHITextureTiling> GetTextureTiling(const FArdaRHITextureRef&) override;
		FArdaRHIStatus UpdateTextureTileMappings(const FArdaRHITextureRef&,
		    const eastl::vector<FArdaRHITextureTileMapping>&,
		    EArdaRHIQueueType) override;
		FArdaRHIStatus UpdateBufferTileMappings(const FArdaRHIBufferRef&,
		    const eastl::vector<FArdaRHIBufferTileMapping>&,
		    EArdaRHIQueueType) override;
		FArdaRHIStatus CommitReservedResource(const FArdaRHIResourceRef&, uint64_t, EArdaRHIQueueType) override;
		TArdaRHIResult<FArdaRHIStreamingBudget> QueryStreamingBudget(bool) const override;
		FArdaRHIStatus SetStreamingBudgetReservation(uint64_t, bool) override;

		FArdaRHIStatus QueryWorkGraphSupport() const override
		{
			return GetCapabilities().mWorkGraphTier != EArdaRHIWorkGraphTier::None
			    ? FArdaRHIStatus{}
			    : Unsupported("Work graphs are unsupported by this device.");
		}

		FArdaRHIStatus QueryShaderBundleSupport() const override
		{
			return GetCapabilities().mbShaderBundleDispatch
			    ? FArdaRHIStatus{}
			    : Unsupported("Shader bundles are unsupported by this device.");
		}

		FArdaRHIStatus QueryCustomPresentSupport() const override
		{
			return GetCapabilities().mbCustomPresent
			    ? FArdaRHIStatus{}
			    : Unsupported("Custom presentation is unsupported by this device.");
		}

		FArdaRHIStatus QueryStreamSourceSupport() const override
		{
			return Unsupported("Stream-source output is unsupported by the backend providers.");
		}

		void TrimDescriptorCaches() override;
		FArdaRHICacheStats GetDescriptorCacheStats() const noexcept override;
		FArdaRHIResourceLifetimeStats GetResourceLifetimeStats() const noexcept override;

		FArdaRHIStatus WaitForIdle() override
		{
			return mDevice->WaitForIdle();
		}

		FArdaRHIStatus WaitForSubmission(uint64_t Submission) override
		{
			return mDevice->WaitForSubmission(Submission);
		}

		TArdaRHIResult<bool> PollSubmission(uint64_t Submission) override
		{
			return mDevice->PollSubmission(Submission);
		}

		void FlushAndDisablePipelineCachePersistence() noexcept override;

		void RunGarbageCollection() override
		{
			mDevice->RunGarbageCollection();
			mAllocator.Collect();
		}

		bool Owns(const FArdaResource* Resource) const noexcept
		{
			return Resource && Resource->GetOwner() == mLifetimeTracker.get();
		}

		IArdaRHIProviderDevice& GetProviderDevice() const noexcept
		{
			return *mDevice;
		}

	private:
		TArdaRHIResult<uint64_t> FinishCommandListSubmission(FArdaCommandList& CommandList,
		    TArdaRHIResult<uint64_t> Submitted);

		template <typename Resource>
		bool IsOwned(const TArdaRHIRef<Resource>& Ref) const noexcept
		{
			return !Ref || Owns(Cast<FArdaResource>(Ref.Get()));
		}

		bool ResolveShader(const FArdaRHIShaderRef& Shader, FArdaProviderObjectRef& Out) const
		{
			if (!Shader)
			{
				return true;
			}
			auto* Native = Cast<FArdaShader>(Shader.Get());
			if (!Native || !Owns(Native))
			{
				return false;
			}
			Out = Native->mNative;
			return true;
		}

		FArdaRHIStatus ResolveBindingLayouts(const eastl::vector<FArdaRHIBindingLayoutRef>& Layouts,
		    eastl::vector<FArdaProviderObjectRef>& OutLayouts) const
		{
			OutLayouts.reserve(Layouts.size());
			for (const auto& Ref : Layouts)
			{
				auto* Layout = Cast<FArdaBindingLayout>(Ref.Get());
				if (!Owns(Layout))
				{
					return WrongDevice();
				}
				OutLayouts.push_back(Layout->mNative);
			}
			return {};
		}

		template <typename Resource,
		    typename ImportDesc,
		    typename Descriptor,
		    typename Ref,
		    typename ImportOperation,
		    typename QueryOperation>
		TArdaRHIResult<Ref> ImportNativeResource(const ImportDesc& Desc,
		    const Descriptor& ResourceDescriptor,
		    TArdaDescriptorCache<ImportDesc, Ref>& Cache,
		    EArdaRHINativeResourceType NativeType,
		    ImportOperation Import,
		    QueryOperation GetRequirements)
		{
			constexpr bool bTexture = eastl::is_same_v<Resource, FArdaTexture>;
			if (Desc.mMemoryAllocationInfo.mbKnown &&
			    (!Desc.mMemoryAllocationInfo.mIdentity || !Desc.mMemoryAllocationInfo.mByteSize))
			{
				return Failure<Ref>(Invalid("Known native allocation metadata requires identity and capacity."));
			}
			if (ResourceDescriptor.mbCudaInterop)
			{
				return UnsupportedResult<Ref>("CUDA sharing requires a backend-created allocation.");
			}
			if (!Desc.mNativeObject)
			{
				return Failure<Ref>(
				    Invalid(bTexture ? "Native texture object is null." : "Native buffer object is null."));
			}
			if (Desc.mOwnership == EArdaRHINativeOwnership::Transferred)
			{
				return UnsupportedResult<Ref>(
				    "Transferred native resource ownership is not portable; provide a lifetime token and Borrowed ownership.");
			}
			if (auto Status = ValidateResourceCapabilities(ResourceDescriptor,
			        GetCapabilities(),
			        QueryFormatSupport(ResourceDescriptor.mFormat));
			    !Status)
			{
				return Failure<Ref>(eastl::move(Status));
			}
			if (Desc.mNativeType != NativeType)
			{
				return UnsupportedResult<Ref>(bTexture
				        ? "Native texture type does not match the selected backend module."
				        : "Native buffer type does not match the selected backend module.");
			}
			std::lock_guard<std::mutex> Lock(mCacheMutex);
			if (auto Existing = Cache.Find(Desc))
			{
				return {Existing, {}};
			}
			auto Native = (mDevice.get()->*Import)(Desc);
			if (!Native)
			{
				return Failure<Ref>(eastl::move(Native.mStatus));
			}
			if (Desc.mMemoryAllocationInfo.mbKnown)
			{
				const auto ProviderInfo = Native.mValue->GetMemoryAllocationInfo();
				if (ProviderInfo.mbKnown && !(ProviderInfo == Desc.mMemoryAllocationInfo))
				{
					return Failure<Ref>(Invalid("Native allocation hint disagrees with provider-owned storage."));
				}
				const auto Requirements = (mDevice.get()->*GetRequirements)(Native.mValue, ResourceDescriptor);
				if (!Requirements)
				{
					return Failure<Ref>(Requirements.mStatus);
				}
				if (Desc.mMemoryAllocationInfo.mByteSize < Requirements.mValue.mSize)
				{
					return Failure<Ref>(
					    Invalid(bTexture ? "Native allocation capacity is smaller than its texture requirements."
					                     : "Native allocation capacity is smaller than its buffer requirements."));
				}
			}
			auto ResourceDesc = ResourceDescriptor;
			ResourceDesc.mInitialState = Desc.mInitialState == EArdaRHIResourceState::Unknown
			    ? ResourceDesc.mInitialState
			    : Desc.mInitialState;
			Ref Result(new Resource(eastl::move(ResourceDesc),
			    eastl::move(Native.mValue),
			    this,
			    mLifetimeTracker,
			    Desc.mLifetimeToken,
			    Desc.mMemoryAllocationInfo));
			Cache.Insert(Desc, Result);
			return {Result, {}};
		}

		mutable std::mutex mCacheMutex;
		eastl::shared_ptr<FArdaLifetimeTracker> mLifetimeTracker;
		eastl::shared_ptr<IArdaRHIProviderDevice> mDevice;
		FArdaGpuAllocator mAllocator;
		bool mbPipelineCacheDetached = false;
		TArdaDescriptorCache<FArdaRHISamplerDesc, FArdaRHISamplerRef> mSamplerCache;
		TArdaDescriptorCache<FArdaRHIBindingLayoutDesc, FArdaRHIBindingLayoutRef> mBindingLayoutCache;
		TArdaDescriptorCache<FArdaRHIInputLayoutDesc, FArdaRHIInputLayoutRef> mInputLayoutCache;
		TArdaDescriptorCache<FArdaRHIRasterState, FArdaRHIRasterStateRef> mRasterStateCache;
		TArdaDescriptorCache<FArdaRHIBlendState, FArdaRHIBlendStateRef> mBlendStateCache;
		TArdaDescriptorCache<FArdaRHIDepthStencilState, FArdaRHIDepthStencilStateRef> mDepthStateCache;
		TArdaDescriptorCache<FArdaRHIRayTracingPipelineDesc, FArdaRHIRayTracingPipelineRef>
		    mRayTracingPipelineCache;
		TArdaDescriptorCache<FArdaRHINativeTextureImportDesc, FArdaRHITextureRef> mTextureImportCache;
		TArdaDescriptorCache<FArdaRHINativeBufferImportDesc, FArdaRHIBufferRef> mBufferImportCache;
	};
}
