#include "ArdaNativeRHI.h"

#include <EASTL/algorithm.h>
#include <EASTL/atomic.h>
#include <EASTL/vector.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

namespace arda::rhi::native
{
    namespace
    {
        FArdaRHIStatus Invalid(const char* Message)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
        }

        FArdaRHIStatus Unsupported(const char* Message)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Message);
        }

        FArdaRHIStatus WrongDevice()
        {
            return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice,
                "Resource belongs to another RHI device or implementation.");
        }

        template <typename T>
        TArdaRHIResult<T> Failure(FArdaRHIStatus Status)
        {
            return { {}, eastl::move(Status) };
        }

        template <typename T>
        TArdaRHIResult<T> UnsupportedResult(const char* Message)
        {
            return Failure<T>(Unsupported(Message));
        }

        uint64_t PersistentShaderHash(const FArdaRHIShaderDesc& Desc) noexcept
        {
            uint64_t Hash = 14695981039346656037ull;
            const auto Append = [&Hash](const void* Data, size_t Size)
            {
                const auto* Bytes = static_cast<const uint8_t*>(Data);
                for (size_t Index = 0; Index < Size; ++Index)
                {
                    Hash ^= Bytes[Index];
                    Hash *= 1099511628211ull;
                }
            };
            const auto AppendUnsigned = [&Append](uint64_t Value)
            {
                uint8_t Bytes[8]{};
                for (uint32_t Index = 0; Index < 8; ++Index)
                    Bytes[Index] = static_cast<uint8_t>(Value >> (Index * 8));
                Append(Bytes, sizeof(Bytes));
            };
            AppendUnsigned(static_cast<uint64_t>(Desc.mStage));
            AppendUnsigned(Desc.mEntryPoint.size());
            Append(Desc.mEntryPoint.data(), Desc.mEntryPoint.size());
            AppendUnsigned(Desc.mBytecodeSize);
            if (Desc.mBytecode && Desc.mBytecodeSize)
                Append(Desc.mBytecode, Desc.mBytecodeSize);
            return Hash == 0 ? 1 : Hash;
        }

        class FLifetimeTracker
        {
        public:
            void Add(EArdaRHIResourceType Type) noexcept
            {
                mLive[static_cast<size_t>(Type)].fetch_add(
                    1, std::memory_order_relaxed);
            }
            void Remove(EArdaRHIResourceType Type) noexcept
            {
                mLive[static_cast<size_t>(Type)].fetch_sub(
                    1, std::memory_order_relaxed);
            }
            size_t Get(EArdaRHIResourceType Type) const noexcept
            {
                return mLive[static_cast<size_t>(Type)].load(
                    std::memory_order_relaxed);
            }

        private:
            std::atomic<size_t> mLive[
                static_cast<size_t>(EArdaRHIResourceType::Count)]{};
        };

        class FResource : public virtual IArdaRHIResource
        {
        public:
            FResource(EArdaRHIResourceType Type, eastl::string Name,
                const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker = {})
                : mType(Type), mName(eastl::move(Name)), mOwner(Owner)
                , mLifetimeTracker(eastl::move(LifetimeTracker))
            {
                if (mLifetimeTracker) mLifetimeTracker->Add(mType);
            }

            void AddRef() noexcept final
            {
                mReferences.fetch_add(1, std::memory_order_relaxed);
            }

            void Release() noexcept final
            {
                if (mReferences.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    delete this;
            }

            EArdaRHIResourceType GetResourceType() const noexcept final { return mType; }
            const char* GetDebugName() const noexcept final { return mName.c_str(); }
            const void* GetOwner() const noexcept { return mOwner; }

        protected:
            ~FResource() override
            {
                if (mLifetimeTracker) mLifetimeTracker->Remove(mType);
            }

        private:
            std::atomic<uint32_t> mReferences{ 0 };
            EArdaRHIResourceType mType;
            eastl::string mName;
            const void* mOwner = nullptr;
            eastl::shared_ptr<FLifetimeTracker> mLifetimeTracker;
        };

        template <typename Interface, typename Desc, EArdaRHIResourceType Type>
        class TNativeResource final : public FResource, public Interface
        {
        public:
            TNativeResource(
                Desc Descriptor,
                FArdaNativeObjectRef Native,
                const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker,
                eastl::shared_ptr<void> LifetimeToken = {})
                : FResource(Type, Descriptor.mDebugName, Owner,
                    eastl::move(LifetimeTracker))
                , mDesc(eastl::move(Descriptor))
                , mNative(eastl::move(Native))
                , mLifetimeToken(eastl::move(LifetimeToken))
            {
            }

            const Desc& GetDesc() const noexcept override { return mDesc; }
            const void* GetPhysicalIdentity() const noexcept
            {
                return mNative ? mNative->GetIdentity() : nullptr;
            }

            Desc mDesc;
            FArdaNativeObjectRef mNative;
            eastl::shared_ptr<void> mLifetimeToken;
        };

        using FTexture = TNativeResource<IArdaRHITexture,
            FArdaRHITextureDesc, EArdaRHIResourceType::Texture>;
        using FBuffer = TNativeResource<IArdaRHIBuffer,
            FArdaRHIBufferDesc, EArdaRHIResourceType::Buffer>;
        using FSampler = TNativeResource<IArdaRHISampler,
            FArdaRHISamplerDesc, EArdaRHIResourceType::Sampler>;
        using FBindingLayout = TNativeResource<IArdaRHIBindingLayout,
            FArdaRHIBindingLayoutDesc, EArdaRHIResourceType::BindingLayout>;
        using FBindingSet = TNativeResource<IArdaRHIBindingSet,
            FArdaRHIBindingSetDesc, EArdaRHIResourceType::BindingSet>;
        using FFramebuffer = TNativeResource<IArdaRHIFramebuffer,
            FArdaRHIFramebufferDesc, EArdaRHIResourceType::Framebuffer>;
        using FGraphicsPipeline = TNativeResource<IArdaRHIGraphicsPipeline,
            FArdaRHIGraphicsPipelineDesc, EArdaRHIResourceType::GraphicsPipeline>;
        using FComputePipeline = TNativeResource<IArdaRHIComputePipeline,
            FArdaRHIComputePipelineDesc, EArdaRHIResourceType::ComputePipeline>;

        class FTextureReference final : public FResource, public IArdaRHITextureReference
        {
        public:
            FTextureReference(FArdaRHITextureRef Texture, const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(EArdaRHIResourceType::TextureReference,
                    "TextureReference", Owner, eastl::move(LifetimeTracker))
                , mTexture(eastl::move(Texture)) {}
            const FArdaRHITextureRef& GetTexture() const noexcept override { return mTexture; }
            FArdaRHITextureRef mTexture;
        };

        class FUniformBuffer final : public FResource, public IArdaRHIUniformBuffer
        {
        public:
            FUniformBuffer(FArdaRHIUniformBufferDesc Desc,
                FArdaRHIBufferRef Buffer, const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(EArdaRHIResourceType::UniformBuffer,
                    Desc.mDebugName, Owner, eastl::move(LifetimeTracker))
                , mDesc(eastl::move(Desc)), mBuffer(eastl::move(Buffer)) {}
            const FArdaRHIUniformBufferDesc& GetDesc() const noexcept override { return mDesc; }
            const FArdaRHIBufferRef& GetBuffer() const noexcept override { return mBuffer; }
            FArdaRHIUniformBufferDesc mDesc;
            FArdaRHIBufferRef mBuffer;
        };

        class FStagingTexture final : public FResource, public IArdaRHIStagingTexture
        {
        public:
            FStagingTexture(FArdaRHIStagingTextureDesc Desc, const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(EArdaRHIResourceType::StagingTexture,
                    Desc.mDebugName, Owner, eastl::move(LifetimeTracker))
                , mDesc(eastl::move(Desc)) {}
            const FArdaRHIStagingTextureDesc& GetDesc() const noexcept override { return mDesc; }
            FArdaRHIStagingTextureDesc mDesc;
        };

        class FShader final : public FResource, public IArdaRHIShader
        {
        public:
            FShader(const FArdaRHIShaderDesc& Desc,
                FArdaNativeObjectRef Native, const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(EArdaRHIResourceType::Shader,
                    Desc.mDebugName, Owner, eastl::move(LifetimeTracker))
                , mStage(Desc.mStage)
                , mPersistentCacheHash(PersistentShaderHash(Desc))
                , mNative(eastl::move(Native)) {}
            EArdaRHIShaderStage GetStage() const noexcept override { return mStage; }
            uint64_t GetPersistentCacheHash() const noexcept override { return mPersistentCacheHash; }
            EArdaRHIShaderStage mStage = EArdaRHIShaderStage::None;
            uint64_t mPersistentCacheHash = 0;
            FArdaNativeObjectRef mNative;
        };

        class FShaderLibrary final : public FResource, public IArdaRHIShaderLibrary
        {
        public:
            FShaderLibrary(const void* Bytecode, size_t Size, const char* Name,
                const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(EArdaRHIResourceType::ShaderLibrary,
                    Name ? Name : "", Owner, eastl::move(LifetimeTracker))
                , mBytecode(static_cast<const uint8_t*>(Bytecode),
                    static_cast<const uint8_t*>(Bytecode) + Size) {}
            eastl::vector<uint8_t> mBytecode;
        };

        class FInputLayout final : public FResource, public IArdaRHIInputLayout
        {
        public:
            FInputLayout(FArdaRHIInputLayoutDesc Desc, const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(EArdaRHIResourceType::InputLayout,
                    "InputLayout", Owner, eastl::move(LifetimeTracker))
                , mDesc(eastl::move(Desc)) {}
            const FArdaRHIInputLayoutDesc& GetDesc() const noexcept override { return mDesc; }
            FArdaRHIInputLayoutDesc mDesc;
        };

        template <typename Interface, EArdaRHIResourceType Type>
        class TView final : public FResource, public Interface
        {
        public:
            TView(TArdaRHIRef<IArdaRHIResource> Resource,
                FArdaRHIViewDesc Desc, const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(Type,
                    Type == EArdaRHIResourceType::ShaderResourceView ? "SRV" : "UAV",
                    Owner, eastl::move(LifetimeTracker))
                , mResource(eastl::move(Resource)), mDesc(eastl::move(Desc)) {}
            IArdaRHIResource* GetResource() const noexcept override { return mResource.Get(); }
            const FArdaRHIViewDesc& GetDesc() const noexcept override { return mDesc; }
            TArdaRHIRef<IArdaRHIResource> mResource;
            FArdaRHIViewDesc mDesc;
        };

        using FShaderResourceView = TView<IArdaRHIShaderResourceView,
            EArdaRHIResourceType::ShaderResourceView>;
        using FUnorderedAccessView = TView<IArdaRHIUnorderedAccessView,
            EArdaRHIResourceType::UnorderedAccessView>;

        template <typename Interface, typename Desc, EArdaRHIResourceType Type>
        class TLogicalState final : public FResource, public Interface
        {
        public:
            TLogicalState(Desc Descriptor, const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(Type, "CachedState", Owner,
                    eastl::move(LifetimeTracker)), mDesc(eastl::move(Descriptor)) {}
            const Desc& GetDesc() const noexcept override { return mDesc; }
            Desc mDesc;
        };

        using FRasterState = TLogicalState<IArdaRHIRasterState,
            FArdaRHIRasterState, EArdaRHIResourceType::RasterState>;
        using FBlendState = TLogicalState<IArdaRHIBlendState,
            FArdaRHIBlendState, EArdaRHIResourceType::BlendState>;
        using FDepthStencilState = TLogicalState<IArdaRHIDepthStencilState,
            FArdaRHIDepthStencilState, EArdaRHIResourceType::DepthStencilState>;

        template <typename Interface, EArdaRHIResourceType Type>
        class TSignal final : public FResource, public Interface
        {
        public:
            TSignal(const char* Name, const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(Type, Name, Owner, eastl::move(LifetimeTracker)) {}
            std::atomic<bool> mbSignaled{ false };
            std::atomic<bool> mbBegun{ false };
        };

        using FEventQuery = TSignal<IArdaRHIEventQuery, EArdaRHIResourceType::EventQuery>;
        using FTimerQuery = TSignal<IArdaRHITimerQuery, EArdaRHIResourceType::TimerQuery>;
        using FGpuFence = TSignal<IArdaRHIGpuFence, EArdaRHIResourceType::GpuFence>;

        template <typename T>
        T* Cast(IArdaRHIResource* Resource) noexcept
        {
            return dynamic_cast<T*>(Resource);
        }

        template <typename T>
        const T* Cast(const IArdaRHIResource* Resource) noexcept
        {
            return dynamic_cast<const T*>(Resource);
        }

        FArdaNativeObjectRef GetNativeObject(IArdaRHIResource* Resource)
        {
            if (auto* Texture = Cast<FTexture>(Resource)) return Texture->mNative;
            if (auto* Buffer = Cast<FBuffer>(Resource)) return Buffer->mNative;
            if (auto* Sampler = Cast<FSampler>(Resource)) return Sampler->mNative;
            if (auto* Layout = Cast<FBindingLayout>(Resource)) return Layout->mNative;
            if (auto* Set = Cast<FBindingSet>(Resource)) return Set->mNative;
            if (auto* Framebuffer = Cast<FFramebuffer>(Resource)) return Framebuffer->mNative;
            if (auto* Pipeline = Cast<FGraphicsPipeline>(Resource)) return Pipeline->mNative;
            if (auto* Pipeline = Cast<FComputePipeline>(Resource)) return Pipeline->mNative;
            if (auto* Shader = Cast<FShader>(Resource)) return Shader->mNative;
            if (auto* View = Cast<FShaderResourceView>(Resource))
                return GetNativeObject(View->mResource.Get());
            if (auto* View = Cast<FUnorderedAccessView>(Resource))
                return GetNativeObject(View->mResource.Get());
            return {};
        }

        template <typename Desc, typename Ref>
        class TDescriptorCache
        {
        public:
            Ref Find(const Desc& Descriptor) const
            {
                for (const auto& Entry : mEntries)
                    if (Entry.mDesc == Descriptor) return Entry.mResource;
                return {};
            }

            void Insert(const Desc& Descriptor, Ref Resource)
            {
                if (mEntries.size() >= 64)
                    mEntries.erase(mEntries.begin());
                mEntries.push_back({ Descriptor, eastl::move(Resource) });
            }

            void Clear() { mEntries.clear(); }
            size_t Size() const noexcept { return mEntries.size(); }

        private:
            struct FEntry { Desc mDesc; Ref mResource; };
            eastl::vector<FEntry> mEntries;
        };

        class FArdaNativeDevice;

        struct FPendingBufferCopyCompletion
        {
            bool mbBlocking = false;
            FArdaNativeObjectRef mReadbackBuffer;
            size_t mByteSize = 0;
            eastl::vector<uint8_t>* mOutput = nullptr;
            FArdaRHIHostToDeviceCopyCallback mUploadCallback;
            FArdaRHIDeviceToHostCopyCallback mReadbackCallback;
        };

        class FCommandList final : public FResource, public IArdaRHICommandList
        {
        public:
            FCommandList(FArdaNativeDevice* Device,
                EArdaRHIQueueType Queue,
                eastl::unique_ptr<IArdaNativeCommandList> Native,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker);

            IArdaRHIDevice* GetDevice() const noexcept override;
            EArdaRHIQueueType GetQueueType() const noexcept override { return mQueue; }
            FArdaRHIStatus Open() override { return mNative->Open(); }
            FArdaRHIStatus Close() override { return mNative->Close(); }
            FArdaRHIStatus Reset() override;
            FArdaRHIStatus WriteBuffer(IArdaRHIBuffer&, const void*, size_t, uint64_t) override;
            FArdaRHIStatus CopyBufferHostToDevice(
                IArdaRHIBuffer&, const void*, size_t, uint64_t) override;
            FArdaRHIStatus CopyBufferHostToDeviceAsync(
                IArdaRHIBuffer&, const void*, size_t,
                FArdaRHIHostToDeviceCopyCallback, uint64_t) override;
            FArdaRHIStatus CopyBufferDeviceToHost(
                IArdaRHIBuffer&, eastl::vector<uint8_t>&,
                uint64_t, uint64_t) override;
            FArdaRHIStatus CopyBufferDeviceToHostAsync(
                IArdaRHIBuffer&, FArdaRHIDeviceToHostCopyCallback,
                uint64_t, uint64_t) override;
            FArdaRHIStatus CopyBuffer(IArdaRHIBuffer&, uint64_t, IArdaRHIBuffer&, uint64_t, uint64_t) override;
            FArdaRHIStatus CopyTextureToStaging(IArdaRHIStagingTexture&, const FArdaRHITextureSlice&, IArdaRHITexture&, const FArdaRHITextureSlice&) override;
            FArdaRHIStatus CopyTextureFromStaging(IArdaRHITexture&, const FArdaRHITextureSlice&, IArdaRHIStagingTexture&, const FArdaRHITextureSlice&) override;
            FArdaRHIStatus ClearTexture(IArdaRHITexture&, const FArdaRHITextureSubresourceRange&, const FArdaRHIColor&) override;
            FArdaRHIStatus SetTextureState(IArdaRHITexture&, const FArdaRHITextureSubresourceRange&, EArdaRHIResourceState) override;
            FArdaRHIStatus SetBufferState(IArdaRHIBuffer&, EArdaRHIResourceState) override;
            FArdaRHIStatus SetAccelStructState(IArdaRHIAccelStruct&, EArdaRHIResourceState) override;
            void SetAutomaticBarriers(bool bEnabled) override { mNative->SetAutomaticBarriers(bEnabled); }
            FArdaRHIStatus BeginTrackingTextureState(IArdaRHITexture&, const FArdaRHITextureSubresourceRange&, EArdaRHIResourceState) override;
            FArdaRHIStatus BeginTrackingBufferState(IArdaRHIBuffer&, EArdaRHIResourceState) override;
            FArdaRHIStatus SetUAVBarriersForTexture(IArdaRHITexture&, bool) override;
            FArdaRHIStatus SetUAVBarriersForBuffer(IArdaRHIBuffer&, bool) override;
            void CommitBarriers() override { mNative->CommitBarriers(); }
            FArdaRHIStatus ClearTextureUInt(IArdaRHITexture&, const FArdaRHITextureSubresourceRange&, uint32_t) override;
            FArdaRHIStatus ClearDepthStencilTexture(IArdaRHITexture&, const FArdaRHITextureSubresourceRange&, bool, float, bool, uint8_t) override;
            FArdaRHIStatus ClearBufferUInt(IArdaRHIBuffer&, uint32_t) override;
            FArdaRHIStatus SetGraphicsState(const FArdaRHIGraphicsState&) override;
            FArdaRHIStatus SetComputeState(const FArdaRHIComputeState&) override;
            FArdaRHIStatus SetMeshletState(const FArdaRHIMeshletState&) override;
            FArdaRHIStatus SetRayTracingState(const FArdaRHIRayTracingState&) override;
            void SetPushConstants(const void* Data, size_t Size) override { mNative->SetPushConstants(Data, Size); }
            void Draw(const FArdaRHIDrawArguments& Arguments) override { mNative->Draw(Arguments); }
            void DrawIndexed(const FArdaRHIDrawArguments& Arguments) override { mNative->DrawIndexed(Arguments); }
            void Dispatch(uint32_t X, uint32_t Y, uint32_t Z) override { mNative->Dispatch(X, Y, Z); }
            FArdaRHIStatus DispatchMesh(uint32_t, uint32_t, uint32_t) override { return Unsupported("Mesh shaders are unsupported by the native modules."); }
            FArdaRHIStatus DispatchRays(uint32_t, uint32_t, uint32_t) override { return Unsupported("Ray tracing pipelines are unsupported by the native modules."); }
            FArdaRHIStatus BuildBottomLevelAccelStruct(IArdaRHIAccelStruct&, const eastl::vector<FArdaRHIRayTracingGeometryDesc>&, EArdaRHIAccelStructBuildFlags) override { return Unsupported("Acceleration structures are unsupported by the native modules."); }
            FArdaRHIStatus BuildTopLevelAccelStruct(IArdaRHIAccelStruct&, const eastl::vector<FArdaRHIRayTracingInstanceDesc>&, EArdaRHIAccelStructBuildFlags) override { return Unsupported("Acceleration structures are unsupported by the native modules."); }
            FArdaRHIStatus BuildTopLevelAccelStructFromBuffer(IArdaRHIAccelStruct&, IArdaRHIBuffer&, uint64_t, size_t, EArdaRHIAccelStructBuildFlags) override { return Unsupported("Acceleration structures are unsupported by the native modules."); }
            FArdaRHIStatus BuildOpacityMicromap(IArdaRHIOpacityMicromap&) override { return Unsupported("Opacity micromaps are unsupported by the native modules."); }
            FArdaRHIStatus ClearSamplerFeedbackTexture(IArdaRHISamplerFeedbackTexture&) override { return Unsupported("Sampler feedback is unsupported by the native modules."); }
            FArdaRHIStatus DecodeSamplerFeedbackTexture(IArdaRHIBuffer&, IArdaRHISamplerFeedbackTexture&, EArdaRHIFormat) override { return Unsupported("Sampler feedback is unsupported by the native modules."); }
            FArdaRHIStatus SetSamplerFeedbackTextureState(IArdaRHISamplerFeedbackTexture&, EArdaRHIResourceState) override { return Unsupported("Sampler feedback is unsupported by the native modules."); }
            FArdaRHIStatus BeginTimerQuery(IArdaRHITimerQuery&) override;
            FArdaRHIStatus EndTimerQuery(IArdaRHITimerQuery&) override;
            void BeginMarker(const char* Name) override { mNative->BeginMarker(Name); }
            void EndMarker() override { mNative->EndMarker(); }

            IArdaNativeCommandList& GetNative() const noexcept { return *mNative; }
            eastl::vector<FPendingBufferCopyCompletion> TakeCopyCompletions()
            {
                return eastl::move(mCopyCompletions);
            }

        private:
            bool Owns(const FResource* Resource) const noexcept;
            FArdaNativeDevice* mDevice = nullptr;
            EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
            eastl::unique_ptr<IArdaNativeCommandList> mNative;
            eastl::vector<FPendingBufferCopyCompletion> mCopyCompletions;
        };

        class FArdaNativeDevice final : public FResource, public IArdaRHIDevice
        {
        public:
            explicit FArdaNativeDevice(eastl::shared_ptr<IArdaNativeApiDevice> Device)
                : FResource(EArdaRHIResourceType::Device, "NativeDevice", this)
                , mLifetimeTracker(eastl::make_shared<FLifetimeTracker>())
                , mDevice(eastl::move(Device)) {}

            ~FArdaNativeDevice() override
            {
                FlushAndDisablePipelineCachePersistence();
                if (mDevice)
                    (void)mDevice->WaitForIdle();
            }

            const FArdaRHICapabilities& GetCapabilities() const noexcept override
            {
                return mDevice->GetCapabilities();
            }

            TArdaRHIResult<FArdaRHITextureRef> CreateTexture(const FArdaRHITextureDesc&) override;
            TArdaRHIResult<FArdaRHITextureReferenceRef> CreateTextureReference(const FArdaRHITextureRef&) override;
            FArdaRHIStatus SetTextureReference(const FArdaRHITextureReferenceRef&, const FArdaRHITextureRef&) override;
            TArdaRHIResult<FArdaRHIBufferRef> CreateBuffer(const FArdaRHIBufferDesc&) override;
            TArdaRHIResult<FArdaRHIUniformBufferRef> CreateUniformBuffer(const FArdaRHIUniformBufferDesc&, const void*) override;
            TArdaRHIResult<FArdaRHITextureRef> ImportNativeTexture(const FArdaRHINativeTextureImportDesc&) override;
            TArdaRHIResult<FArdaRHIBufferRef> ImportNativeBuffer(const FArdaRHINativeBufferImportDesc&) override;
            TArdaRHIResult<FArdaRHIHeapRef> CreateHeap(const FArdaRHIHeapDesc&) override { return UnsupportedResult<FArdaRHIHeapRef>("Explicit heaps are unsupported by the native modules."); }
            TArdaRHIResult<FArdaRHIStagingTextureRef> CreateStagingTexture(const FArdaRHIStagingTextureDesc&) override;
            TArdaRHIResult<FArdaRHIStagingTextureMapping> MapStagingTexture(const FArdaRHIStagingTextureRef&, const FArdaRHITextureSlice&, EArdaRHICpuAccess) override { return UnsupportedResult<FArdaRHIStagingTextureMapping>("Staging texture mapping is not implemented by the native modules."); }
            FArdaRHIStatus UnmapStagingTexture(const FArdaRHIStagingTextureRef&) override { return Unsupported("Staging texture mapping is not implemented by the native modules."); }
            TArdaRHIResult<FArdaRHIShaderResourceViewRef> CreateShaderResourceView(const TArdaRHIRef<IArdaRHIResource>&, const FArdaRHIViewDesc&) override;
            TArdaRHIResult<FArdaRHIUnorderedAccessViewRef> CreateUnorderedAccessView(const TArdaRHIRef<IArdaRHIResource>&, const FArdaRHIViewDesc&) override;
            TArdaRHIResult<FArdaRHISamplerRef> CreateSampler(const FArdaRHISamplerDesc&) override;
            TArdaRHIResult<FArdaRHIShaderRef> CreateShader(const FArdaRHIShaderDesc&) override;
            TArdaRHIResult<FArdaRHIShaderLibraryRef> CreateShaderLibrary(const void*, size_t, const char*) override;
            TArdaRHIResult<FArdaRHIShaderRef> GetShaderFromLibrary(const FArdaRHIShaderLibraryRef&, const char*, EArdaRHIShaderStage, const char*) override;
            TArdaRHIResult<FArdaRHIInputLayoutRef> CreateInputLayout(const eastl::vector<FArdaRHIVertexAttributeDesc>&, const FArdaRHIShaderRef&) override;
            TArdaRHIResult<FArdaRHIBindingLayoutRef> CreateBindingLayout(const FArdaRHIBindingLayoutDesc&) override;
            TArdaRHIResult<FArdaRHIBindingLayoutRef> CreateBindlessLayout(const FArdaRHIBindlessLayoutDesc&) override { return UnsupportedResult<FArdaRHIBindingLayoutRef>("Bindless layouts are unsupported by the native modules."); }
            TArdaRHIResult<FArdaRHIBindingSetRef> CreateBindingSet(const FArdaRHIBindingSetDesc&) override;
            TArdaRHIResult<FArdaRHIDescriptorTableRef> CreateDescriptorTable(const FArdaRHIBindingLayoutRef&) override { return UnsupportedResult<FArdaRHIDescriptorTableRef>("Descriptor tables are unsupported by the native modules."); }
            FArdaRHIStatus ResizeDescriptorTable(const FArdaRHIDescriptorTableRef&, uint32_t, bool) override { return Unsupported("Descriptor tables are unsupported by the native modules."); }
            FArdaRHIStatus WriteDescriptorTable(const FArdaRHIDescriptorTableRef&, const FArdaRHIBindingItem&) override { return Unsupported("Descriptor tables are unsupported by the native modules."); }
            TArdaRHIResult<FArdaRHIFramebufferRef> CreateFramebuffer(const FArdaRHIFramebufferDesc&) override;
            TArdaRHIResult<FArdaRHIGraphicsPipelineRef> CreateGraphicsPipeline(const FArdaRHIGraphicsPipelineDesc&) override;
            TArdaRHIResult<FArdaRHIComputePipelineRef> CreateComputePipeline(const FArdaRHIComputePipelineDesc&) override;
            TArdaRHIResult<FArdaRHIMeshletPipelineRef> CreateMeshletPipeline(const FArdaRHIMeshletPipelineDesc&) override { return UnsupportedResult<FArdaRHIMeshletPipelineRef>("Mesh shaders are unsupported by the native modules."); }
            TArdaRHIResult<FArdaRHIRasterStateRef> CreateRasterState(const FArdaRHIRasterState&) override;
            TArdaRHIResult<FArdaRHIBlendStateRef> CreateBlendState(const FArdaRHIBlendState&) override;
            TArdaRHIResult<FArdaRHIDepthStencilStateRef> CreateDepthStencilState(const FArdaRHIDepthStencilState&) override;
            TArdaRHIResult<FArdaRHIAccelStructRef> CreateAccelStruct(const FArdaRHIAccelStructDesc&) override { return UnsupportedResult<FArdaRHIAccelStructRef>("Acceleration structures are unsupported by the native modules."); }
            TArdaRHIResult<FArdaRHIOpacityMicromapRef> CreateOpacityMicromap(const FArdaRHIOpacityMicromapDesc&) override { return UnsupportedResult<FArdaRHIOpacityMicromapRef>("Opacity micromaps are unsupported by the native modules."); }
            TArdaRHIResult<FArdaRHIRayTracingPipelineRef> CreateRayTracingPipeline(const FArdaRHIRayTracingPipelineDesc&) override { return UnsupportedResult<FArdaRHIRayTracingPipelineRef>("Ray tracing pipelines are unsupported by the native modules."); }
            TArdaRHIResult<FArdaRHIShaderTableRef> CreateShaderTable(const FArdaRHIRayTracingPipelineRef&, const FArdaRHIShaderTableDesc&) override { return UnsupportedResult<FArdaRHIShaderTableRef>("Shader tables are unsupported by the native modules."); }
            FArdaRHIStatus SetShaderTableRayGeneration(const FArdaRHIShaderTableRef&, const char*, const FArdaRHIBindingSetRef&) override { return Unsupported("Shader tables are unsupported by the native modules."); }
            TArdaRHIResult<int> AddShaderTableMiss(const FArdaRHIShaderTableRef&, const char*, const FArdaRHIBindingSetRef&) override { return UnsupportedResult<int>("Shader tables are unsupported by the native modules."); }
            TArdaRHIResult<int> AddShaderTableHitGroup(const FArdaRHIShaderTableRef&, const char*, const FArdaRHIBindingSetRef&) override { return UnsupportedResult<int>("Shader tables are unsupported by the native modules."); }
            TArdaRHIResult<int> AddShaderTableCallable(const FArdaRHIShaderTableRef&, const char*, const FArdaRHIBindingSetRef&) override { return UnsupportedResult<int>("Shader tables are unsupported by the native modules."); }
            TArdaRHIResult<FArdaRHISamplerFeedbackTextureRef> CreateSamplerFeedbackTexture(const FArdaRHITextureRef&, const FArdaRHISamplerFeedbackTextureDesc&) override { return UnsupportedResult<FArdaRHISamplerFeedbackTextureRef>("Sampler feedback is unsupported by the native modules."); }
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
            TArdaRHIResult<uint64_t> ExecuteCommandLists(const eastl::vector<FArdaRHICommandListRef>&, EArdaRHIQueueType) override;
            FArdaRHIStatus QueueWait(EArdaRHIQueueType, EArdaRHIQueueType, uint64_t) override { return mDevice->WaitForIdle(); }
            TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(const FArdaRHITextureRef&) override { return UnsupportedResult<FArdaRHIMemoryRequirements>("Explicit heaps are unsupported by the native modules."); }
            TArdaRHIResult<FArdaRHIMemoryRequirements> GetBufferMemoryRequirements(const FArdaRHIBufferRef&) override { return UnsupportedResult<FArdaRHIMemoryRequirements>("Explicit heaps are unsupported by the native modules."); }
            TArdaRHIResult<FArdaRHIMemoryRequirements> GetAccelStructMemoryRequirements(const FArdaRHIAccelStructRef&) override { return UnsupportedResult<FArdaRHIMemoryRequirements>("Acceleration structures are unsupported by the native modules."); }
            FArdaRHIStatus BindTextureMemory(const FArdaRHITextureRef&, const FArdaRHIHeapRef&, uint64_t) override { return Unsupported("Explicit heaps are unsupported by the native modules."); }
            FArdaRHIStatus BindBufferMemory(const FArdaRHIBufferRef&, const FArdaRHIHeapRef&, uint64_t) override { return Unsupported("Explicit heaps are unsupported by the native modules."); }
            FArdaRHIStatus BindAccelStructMemory(const FArdaRHIAccelStructRef&, const FArdaRHIHeapRef&, uint64_t) override { return Unsupported("Acceleration structures are unsupported by the native modules."); }
            TArdaRHIResult<FArdaRHITextureTiling> GetTextureTiling(const FArdaRHITextureRef&) override { return UnsupportedResult<FArdaRHITextureTiling>("Tiled resources are unsupported by the native modules."); }
            FArdaRHIStatus UpdateTextureTileMappings(const FArdaRHITextureRef&, const eastl::vector<FArdaRHITextureTileMapping>&, EArdaRHIQueueType) override { return Unsupported("Tiled resources are unsupported by the native modules."); }
            FArdaRHIStatus QueryWorkGraphSupport() const override { return Unsupported("Work graphs are unsupported by the native modules."); }
            FArdaRHIStatus QueryShaderBundleSupport() const override { return Unsupported("Shader bundles are unsupported by the native modules."); }
            FArdaRHIStatus QueryCustomPresentSupport() const override { return Unsupported("Custom presentation is not exposed by the native modules."); }
            FArdaRHIStatus QueryStreamSourceSupport() const override { return Unsupported("Stream-source output is unsupported by the native modules."); }
            void TrimDescriptorCaches() override;
            FArdaRHICacheStats GetDescriptorCacheStats() const noexcept override;
            FArdaRHIResourceLifetimeStats
                GetResourceLifetimeStats() const noexcept override;
            FArdaRHIStatus WaitForIdle() override { return mDevice->WaitForIdle(); }
            void FlushAndDisablePipelineCachePersistence() noexcept override;
            void RunGarbageCollection() override { mDevice->RunGarbageCollection(); }

            bool Owns(const FResource* Resource) const noexcept
            {
                return Resource && Resource->GetOwner() == this;
            }

            IArdaNativeApiDevice& GetNativeDevice() const noexcept { return *mDevice; }

        private:
            TArdaRHIResult<uint64_t> FinishCommandListSubmission(
                FCommandList& CommandList,
                TArdaRHIResult<uint64_t> Submitted);
            template <typename Resource>
            bool IsOwned(const TArdaRHIRef<Resource>& Ref) const noexcept
            {
                return !Ref || Owns(Cast<FResource>(Ref.Get()));
            }

            mutable std::mutex mCacheMutex;
            eastl::shared_ptr<FLifetimeTracker> mLifetimeTracker;
            eastl::shared_ptr<IArdaNativeApiDevice> mDevice;
            bool mbPipelineCacheDetached = false;
            TDescriptorCache<FArdaRHISamplerDesc, FArdaRHISamplerRef> mSamplerCache;
            TDescriptorCache<FArdaRHIBindingLayoutDesc, FArdaRHIBindingLayoutRef> mBindingLayoutCache;
            TDescriptorCache<FArdaRHIInputLayoutDesc, FArdaRHIInputLayoutRef> mInputLayoutCache;
            TDescriptorCache<FArdaRHIRasterState, FArdaRHIRasterStateRef> mRasterStateCache;
            TDescriptorCache<FArdaRHIBlendState, FArdaRHIBlendStateRef> mBlendStateCache;
            TDescriptorCache<FArdaRHIDepthStencilState, FArdaRHIDepthStencilStateRef> mDepthStateCache;
            TDescriptorCache<FArdaRHINativeTextureImportDesc, FArdaRHITextureRef> mTextureImportCache;
            TDescriptorCache<FArdaRHINativeBufferImportDesc, FArdaRHIBufferRef> mBufferImportCache;
        };

        TArdaRHIResult<FArdaRHITextureRef> FArdaNativeDevice::CreateTexture(
            const FArdaRHITextureDesc& Desc)
        {
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHITextureRef>(eastl::move(Status));
            if (Desc.mbVirtual || Desc.mbTiled)
                return UnsupportedResult<FArdaRHITextureRef>(
                    "Virtual and tiled textures are unsupported by the native modules.");
            auto Native = mDevice->CreateTexture(Desc);
            if (!Native) return Failure<FArdaRHITextureRef>(eastl::move(Native.mStatus));
            return { FArdaRHITextureRef(new FTexture(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHITextureReferenceRef>
        FArdaNativeDevice::CreateTextureReference(const FArdaRHITextureRef& Texture)
        {
            if (!IsOwned(Texture))
                return Failure<FArdaRHITextureReferenceRef>(WrongDevice());
            return { FArdaRHITextureReferenceRef(new FTextureReference(
                Texture, this, mLifetimeTracker)), {} };
        }

        FArdaRHIStatus FArdaNativeDevice::SetTextureReference(
            const FArdaRHITextureReferenceRef& Reference,
            const FArdaRHITextureRef& Texture)
        {
            auto* Native = Cast<FTextureReference>(Reference.Get());
            if (!Native || !Owns(Native) || !IsOwned(Texture)) return WrongDevice();
            Native->mTexture = Texture;
            return {};
        }

        TArdaRHIResult<FArdaRHIBufferRef> FArdaNativeDevice::CreateBuffer(
            const FArdaRHIBufferDesc& Desc)
        {
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHIBufferRef>(eastl::move(Status));
            if (Desc.mbVirtual)
                return UnsupportedResult<FArdaRHIBufferRef>(
                    "Virtual buffers are unsupported by the native modules.");
            auto Native = mDevice->CreateBuffer(Desc);
            if (!Native) return Failure<FArdaRHIBufferRef>(eastl::move(Native.mStatus));
            return { FArdaRHIBufferRef(new FBuffer(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIUniformBufferRef> FArdaNativeDevice::CreateUniformBuffer(
            const FArdaRHIUniformBufferDesc& Desc,
            const void* InitialData)
        {
            if (Desc.mByteSize == 0)
                return Failure<FArdaRHIUniformBufferRef>(Invalid("Uniform buffer size must be non-zero."));
            FArdaRHIBufferDesc BufferDesc;
            BufferDesc.mByteSize = Desc.mByteSize;
            BufferDesc.mMaxVersions = Desc.mMaxVersions;
            BufferDesc.mUsage = EArdaRHIBufferUsage::Constant;
            BufferDesc.mInitialState = EArdaRHIResourceState::ConstantBuffer;
            BufferDesc.mbKeepInitialState = true;
            BufferDesc.mDebugName = Desc.mDebugName;
            auto Buffer = CreateBuffer(BufferDesc);
            if (!Buffer) return Failure<FArdaRHIUniformBufferRef>(eastl::move(Buffer.mStatus));
            if (InitialData)
            {
                auto Commands = CreateCommandList(EArdaRHIQueueType::Graphics, true);
                if (!Commands) return Failure<FArdaRHIUniformBufferRef>(eastl::move(Commands.mStatus));
                if (auto Status = Commands.mValue->Open(); !Status)
                    return Failure<FArdaRHIUniformBufferRef>(eastl::move(Status));
                if (auto Status = Commands.mValue->WriteBuffer(*Buffer.mValue, InitialData, Desc.mByteSize); !Status)
                    return Failure<FArdaRHIUniformBufferRef>(eastl::move(Status));
                if (auto Status = Commands.mValue->Close(); !Status)
                    return Failure<FArdaRHIUniformBufferRef>(eastl::move(Status));
                auto Submitted = ExecuteCommandList(Commands.mValue);
                if (!Submitted) return Failure<FArdaRHIUniformBufferRef>(eastl::move(Submitted.mStatus));
            }
            return { FArdaRHIUniformBufferRef(new FUniformBuffer(
                Desc, Buffer.mValue, this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHITextureRef> FArdaNativeDevice::ImportNativeTexture(
            const FArdaRHINativeTextureImportDesc& Desc)
        {
            if (!Desc.mNativeObject)
                return Failure<FArdaRHITextureRef>(Invalid("Native texture object is null."));
            if (Desc.mOwnership == EArdaRHINativeOwnership::Transferred)
                return UnsupportedResult<FArdaRHITextureRef>(
                    "Transferred native resource ownership is not portable; provide a lifetime token and Borrowed ownership.");
            if (auto Status = Validate(Desc.mTexture); !Status)
                return Failure<FArdaRHITextureRef>(eastl::move(Status));
            if (Desc.mNativeType != mDevice->GetTextureImportType())
                return UnsupportedResult<FArdaRHITextureRef>(
                    "Native texture type does not match the selected backend module.");
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            if (auto Existing = mTextureImportCache.Find(Desc)) return { Existing, {} };
            auto Native = mDevice->ImportTexture(Desc);
            if (!Native) return Failure<FArdaRHITextureRef>(eastl::move(Native.mStatus));
            FArdaRHITextureDesc TextureDesc = Desc.mTexture;
            TextureDesc.mInitialState = Desc.mInitialState == EArdaRHIResourceState::Unknown
                ? TextureDesc.mInitialState : Desc.mInitialState;
            FArdaRHITextureRef Result(new FTexture(
                eastl::move(TextureDesc), eastl::move(Native.mValue), this,
                mLifetimeTracker, Desc.mLifetimeToken));
            mTextureImportCache.Insert(Desc, Result);
            return { Result, {} };
        }

        TArdaRHIResult<FArdaRHIBufferRef> FArdaNativeDevice::ImportNativeBuffer(
            const FArdaRHINativeBufferImportDesc& Desc)
        {
            if (!Desc.mNativeObject)
                return Failure<FArdaRHIBufferRef>(Invalid("Native buffer object is null."));
            if (Desc.mOwnership == EArdaRHINativeOwnership::Transferred)
                return UnsupportedResult<FArdaRHIBufferRef>(
                    "Transferred native resource ownership is not portable; provide a lifetime token and Borrowed ownership.");
            if (auto Status = Validate(Desc.mBuffer); !Status)
                return Failure<FArdaRHIBufferRef>(eastl::move(Status));
            if (Desc.mNativeType != mDevice->GetBufferImportType())
                return UnsupportedResult<FArdaRHIBufferRef>(
                    "Native buffer type does not match the selected backend module.");
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            if (auto Existing = mBufferImportCache.Find(Desc)) return { Existing, {} };
            auto Native = mDevice->ImportBuffer(Desc);
            if (!Native) return Failure<FArdaRHIBufferRef>(eastl::move(Native.mStatus));
            FArdaRHIBufferDesc BufferDesc = Desc.mBuffer;
            BufferDesc.mInitialState = Desc.mInitialState == EArdaRHIResourceState::Unknown
                ? BufferDesc.mInitialState : Desc.mInitialState;
            FArdaRHIBufferRef Result(new FBuffer(
                eastl::move(BufferDesc), eastl::move(Native.mValue), this,
                mLifetimeTracker, Desc.mLifetimeToken));
            mBufferImportCache.Insert(Desc, Result);
            return { Result, {} };
        }

        TArdaRHIResult<FArdaRHIStagingTextureRef> FArdaNativeDevice::CreateStagingTexture(
            const FArdaRHIStagingTextureDesc& Desc)
        {
            if (auto Status = Validate(Desc.mTexture); !Status)
                return Failure<FArdaRHIStagingTextureRef>(eastl::move(Status));
            if (Desc.mCpuAccess == EArdaRHICpuAccess::None)
                return Failure<FArdaRHIStagingTextureRef>(Invalid("A staging texture requires CPU access."));
            return { FArdaRHIStagingTextureRef(new FStagingTexture(
                Desc, this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIShaderResourceViewRef>
        FArdaNativeDevice::CreateShaderResourceView(
            const TArdaRHIRef<IArdaRHIResource>& Resource,
            const FArdaRHIViewDesc& Desc)
        {
            auto* Native = Cast<FResource>(Resource.Get());
            if (!Native || !Owns(Native) || (!Cast<FTexture>(Resource.Get()) && !Cast<FBuffer>(Resource.Get())))
                return Failure<FArdaRHIShaderResourceViewRef>(WrongDevice());
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHIShaderResourceViewRef>(eastl::move(Status));
            return { FArdaRHIShaderResourceViewRef(new FShaderResourceView(
                Resource, Desc, this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIUnorderedAccessViewRef>
        FArdaNativeDevice::CreateUnorderedAccessView(
            const TArdaRHIRef<IArdaRHIResource>& Resource,
            const FArdaRHIViewDesc& Desc)
        {
            auto* Native = Cast<FResource>(Resource.Get());
            if (!Native || !Owns(Native) || (!Cast<FTexture>(Resource.Get()) && !Cast<FBuffer>(Resource.Get())))
                return Failure<FArdaRHIUnorderedAccessViewRef>(WrongDevice());
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHIUnorderedAccessViewRef>(eastl::move(Status));
            return { FArdaRHIUnorderedAccessViewRef(new FUnorderedAccessView(
                Resource, Desc, this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHISamplerRef> FArdaNativeDevice::CreateSampler(
            const FArdaRHISamplerDesc& Desc)
        {
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHISamplerRef>(eastl::move(Status));
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            if (auto Existing = mSamplerCache.Find(Desc)) return { Existing, {} };
            auto Native = mDevice->CreateSampler(Desc);
            if (!Native) return Failure<FArdaRHISamplerRef>(eastl::move(Native.mStatus));
            FArdaRHISamplerRef Result(new FSampler(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker));
            mSamplerCache.Insert(Desc, Result);
            return { Result, {} };
        }

        TArdaRHIResult<FArdaRHIShaderRef> FArdaNativeDevice::CreateShader(
            const FArdaRHIShaderDesc& Desc)
        {
            if (!Desc.mBytecode || Desc.mBytecodeSize == 0 || Desc.mStage == EArdaRHIShaderStage::None)
                return Failure<FArdaRHIShaderRef>(Invalid("Shader bytecode and stage are required."));
            auto Native = mDevice->CreateShader(Desc);
            if (!Native) return Failure<FArdaRHIShaderRef>(eastl::move(Native.mStatus));
            return { FArdaRHIShaderRef(new FShader(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIShaderLibraryRef> FArdaNativeDevice::CreateShaderLibrary(
            const void* Bytecode, size_t BytecodeSize, const char* DebugName)
        {
            if (!Bytecode || BytecodeSize == 0)
                return Failure<FArdaRHIShaderLibraryRef>(Invalid("Shader-library bytecode is required."));
            return { FArdaRHIShaderLibraryRef(new FShaderLibrary(
                Bytecode, BytecodeSize, DebugName, this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIShaderRef> FArdaNativeDevice::GetShaderFromLibrary(
            const FArdaRHIShaderLibraryRef& Library,
            const char* EntryPoint,
            EArdaRHIShaderStage Stage,
            const char* DebugName)
        {
            auto* Native = Cast<FShaderLibrary>(Library.Get());
            if (!Native || !Owns(Native)) return Failure<FArdaRHIShaderRef>(WrongDevice());
            if (!EntryPoint || !*EntryPoint)
                return Failure<FArdaRHIShaderRef>(Invalid("A shader-library entry point is required."));
            FArdaRHIShaderDesc Desc;
            Desc.mStage = Stage;
            Desc.mBytecode = Native->mBytecode.data();
            Desc.mBytecodeSize = Native->mBytecode.size();
            Desc.mEntryPoint = EntryPoint;
            Desc.mDebugName = DebugName ? DebugName : EntryPoint;
            return CreateShader(Desc);
        }

        TArdaRHIResult<FArdaRHIInputLayoutRef> FArdaNativeDevice::CreateInputLayout(
            const eastl::vector<FArdaRHIVertexAttributeDesc>& Attributes,
            const FArdaRHIShaderRef& VertexShader)
        {
            FArdaRHIInputLayoutDesc Desc{ Attributes, VertexShader };
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHIInputLayoutRef>(eastl::move(Status));
            if (!IsOwned(VertexShader))
                return Failure<FArdaRHIInputLayoutRef>(WrongDevice());
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            if (auto Existing = mInputLayoutCache.Find(Desc)) return { Existing, {} };
            FArdaRHIInputLayoutRef Result(new FInputLayout(
                Desc, this, mLifetimeTracker));
            mInputLayoutCache.Insert(Desc, Result);
            return { Result, {} };
        }

        TArdaRHIResult<FArdaRHIBindingLayoutRef> FArdaNativeDevice::CreateBindingLayout(
            const FArdaRHIBindingLayoutDesc& Desc)
        {
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHIBindingLayoutRef>(eastl::move(Status));
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            if (auto Existing = mBindingLayoutCache.Find(Desc)) return { Existing, {} };
            auto Native = mDevice->CreateBindingLayout(Desc);
            if (!Native) return Failure<FArdaRHIBindingLayoutRef>(eastl::move(Native.mStatus));
            FArdaRHIBindingLayoutRef Result(new FBindingLayout(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker));
            mBindingLayoutCache.Insert(Desc, Result);
            return { Result, {} };
        }

        TArdaRHIResult<FArdaRHIBindingSetRef> FArdaNativeDevice::CreateBindingSet(
            const FArdaRHIBindingSetDesc& Desc)
        {
            auto* Layout = Cast<FBindingLayout>(Desc.mLayout.Get());
            if (!Layout || !Owns(Layout))
                return Failure<FArdaRHIBindingSetRef>(WrongDevice());
            eastl::vector<FArdaNativeBinding> Bindings;
            Bindings.reserve(Desc.mItems.size());
            for (const auto& Item : Desc.mItems)
            {
                auto* Resource = Cast<FResource>(Item.mResource.Get());
                if (!Resource || !Owns(Resource))
                    return Failure<FArdaRHIBindingSetRef>(WrongDevice());
                FArdaNativeObjectRef Object = GetNativeObject(Item.mResource.Get());
                if (!Object)
                    return Failure<FArdaRHIBindingSetRef>(
                        Invalid("Binding item does not reference a bindable native resource."));
                Bindings.push_back({ Item, eastl::move(Object) });
            }
            auto Native = mDevice->CreateBindingSet(Desc, Layout->mNative, Bindings);
            if (!Native) return Failure<FArdaRHIBindingSetRef>(eastl::move(Native.mStatus));
            return { FArdaRHIBindingSetRef(new FBindingSet(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIFramebufferRef> FArdaNativeDevice::CreateFramebuffer(
            const FArdaRHIFramebufferDesc& Desc)
        {
            if (Desc.mColorAttachments.empty() && !Desc.mDepthAttachment.mTexture)
                return Failure<FArdaRHIFramebufferRef>(
                    Invalid("A framebuffer requires at least one attachment."));
            FArdaNativeFramebufferCreateInfo Info{ Desc };
            Info.mColors.reserve(Desc.mColorAttachments.size());
            for (const auto& Target : Desc.mColorAttachments)
            {
                auto* Texture = Cast<FTexture>(Target.mTexture.Get());
                if (!Texture || !Owns(Texture))
                    return Failure<FArdaRHIFramebufferRef>(WrongDevice());
                Info.mColors.push_back({ Target, Texture->mNative });
            }
            if (Desc.mDepthAttachment.mTexture)
            {
                auto* Texture = Cast<FTexture>(Desc.mDepthAttachment.mTexture.Get());
                if (!Texture || !Owns(Texture))
                    return Failure<FArdaRHIFramebufferRef>(WrongDevice());
                Info.mDepth = { Desc.mDepthAttachment, Texture->mNative };
            }
            auto Native = mDevice->CreateFramebuffer(Info);
            if (!Native) return Failure<FArdaRHIFramebufferRef>(eastl::move(Native.mStatus));
            return { FArdaRHIFramebufferRef(new FFramebuffer(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIGraphicsPipelineRef>
        FArdaNativeDevice::CreateGraphicsPipeline(const FArdaRHIGraphicsPipelineDesc& Desc)
        {
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHIGraphicsPipelineRef>(eastl::move(Status));
            FArdaNativeGraphicsPipelineCreateInfo Info{ Desc };
            if (Desc.mInputLayout)
            {
                auto* Layout = Cast<FInputLayout>(Desc.mInputLayout.Get());
                if (!Layout || !Owns(Layout))
                    return Failure<FArdaRHIGraphicsPipelineRef>(WrongDevice());
                Info.mInputLayout = &Layout->mDesc;
            }
            const auto SetShader = [this](const FArdaRHIShaderRef& Shader,
                                          FArdaNativeObjectRef& Out)
            {
                if (!Shader) return true;
                auto* Native = Cast<FShader>(Shader.Get());
                if (!Native || !Owns(Native)) return false;
                Out = Native->mNative;
                return true;
            };
            if (!SetShader(Desc.mVertexShader, Info.mVertexShader) ||
                !SetShader(Desc.mHullShader, Info.mHullShader) ||
                !SetShader(Desc.mDomainShader, Info.mDomainShader) ||
                !SetShader(Desc.mGeometryShader, Info.mGeometryShader) ||
                !SetShader(Desc.mPixelShader, Info.mPixelShader))
                return Failure<FArdaRHIGraphicsPipelineRef>(WrongDevice());
            for (const auto& LayoutRef : Desc.mBindingLayouts)
            {
                auto* Layout = Cast<FBindingLayout>(LayoutRef.Get());
                if (!Layout || !Owns(Layout))
                    return Failure<FArdaRHIGraphicsPipelineRef>(WrongDevice());
                Info.mBindingLayouts.push_back(Layout->mNative);
            }
            auto Native = mDevice->CreateGraphicsPipeline(Info);
            if (!Native) return Failure<FArdaRHIGraphicsPipelineRef>(eastl::move(Native.mStatus));
            return { FArdaRHIGraphicsPipelineRef(new FGraphicsPipeline(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIComputePipelineRef>
        FArdaNativeDevice::CreateComputePipeline(const FArdaRHIComputePipelineDesc& Desc)
        {
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHIComputePipelineRef>(eastl::move(Status));
            auto* Shader = Cast<FShader>(Desc.mComputeShader.Get());
            if (!Shader || !Owns(Shader))
                return Failure<FArdaRHIComputePipelineRef>(WrongDevice());
            FArdaNativeComputePipelineCreateInfo Info{ Desc, Shader->mNative };
            for (const auto& LayoutRef : Desc.mBindingLayouts)
            {
                auto* Layout = Cast<FBindingLayout>(LayoutRef.Get());
                if (!Layout || !Owns(Layout))
                    return Failure<FArdaRHIComputePipelineRef>(WrongDevice());
                Info.mBindingLayouts.push_back(Layout->mNative);
            }
            auto Native = mDevice->CreateComputePipeline(Info);
            if (!Native) return Failure<FArdaRHIComputePipelineRef>(eastl::move(Native.mStatus));
            return { FArdaRHIComputePipelineRef(new FComputePipeline(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIRasterStateRef> FArdaNativeDevice::CreateRasterState(
            const FArdaRHIRasterState& Desc)
        {
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            if (auto Existing = mRasterStateCache.Find(Desc)) return { Existing, {} };
            FArdaRHIRasterStateRef Result(new FRasterState(
                Desc, this, mLifetimeTracker));
            mRasterStateCache.Insert(Desc, Result);
            return { Result, {} };
        }

        TArdaRHIResult<FArdaRHIBlendStateRef> FArdaNativeDevice::CreateBlendState(
            const FArdaRHIBlendState& Desc)
        {
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            if (auto Existing = mBlendStateCache.Find(Desc)) return { Existing, {} };
            FArdaRHIBlendStateRef Result(new FBlendState(
                Desc, this, mLifetimeTracker));
            mBlendStateCache.Insert(Desc, Result);
            return { Result, {} };
        }

        TArdaRHIResult<FArdaRHIDepthStencilStateRef>
        FArdaNativeDevice::CreateDepthStencilState(const FArdaRHIDepthStencilState& Desc)
        {
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            if (auto Existing = mDepthStateCache.Find(Desc)) return { Existing, {} };
            FArdaRHIDepthStencilStateRef Result(new FDepthStencilState(
                Desc, this, mLifetimeTracker));
            mDepthStateCache.Insert(Desc, Result);
            return { Result, {} };
        }

        TArdaRHIResult<FArdaRHIEventQueryRef> FArdaNativeDevice::CreateEventQuery()
        {
            return { FArdaRHIEventQueryRef(new FEventQuery(
                "EventQuery", this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHITimerQueryRef> FArdaNativeDevice::CreateTimerQuery()
        {
            return { FArdaRHITimerQueryRef(new FTimerQuery(
                "TimerQuery", this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIGpuFenceRef> FArdaNativeDevice::CreateGpuFence()
        {
            return { FArdaRHIGpuFenceRef(new FGpuFence(
                "GpuFence", this, mLifetimeTracker)), {} };
        }

        FArdaRHIStatus FArdaNativeDevice::SignalEventQuery(
            const FArdaRHIEventQueryRef& Query, EArdaRHIQueueType)
        {
            auto* Native = Cast<FEventQuery>(Query.Get());
            if (!Native || !Owns(Native)) return WrongDevice();
            if (auto Status = mDevice->WaitForIdle(); !Status) return Status;
            Native->mbSignaled.store(true, std::memory_order_release);
            return {};
        }

        TArdaRHIResult<bool> FArdaNativeDevice::PollEventQuery(
            const FArdaRHIEventQueryRef& Query)
        {
            auto* Native = Cast<FEventQuery>(Query.Get());
            if (!Native || !Owns(Native)) return Failure<bool>(WrongDevice());
            return { Native->mbSignaled.load(std::memory_order_acquire), {} };
        }

        FArdaRHIStatus FArdaNativeDevice::WaitEventQuery(const FArdaRHIEventQueryRef& Query)
        {
            auto Poll = PollEventQuery(Query);
            if (!Poll) return Poll.mStatus;
            return Poll.mValue ? FArdaRHIStatus{} : mDevice->WaitForIdle();
        }

        FArdaRHIStatus FArdaNativeDevice::ResetEventQuery(const FArdaRHIEventQueryRef& Query)
        {
            auto* Native = Cast<FEventQuery>(Query.Get());
            if (!Native || !Owns(Native)) return WrongDevice();
            Native->mbSignaled.store(false, std::memory_order_release);
            return {};
        }

        TArdaRHIResult<bool> FArdaNativeDevice::PollTimerQuery(
            const FArdaRHITimerQueryRef& Query)
        {
            auto* Native = Cast<FTimerQuery>(Query.Get());
            if (!Native || !Owns(Native)) return Failure<bool>(WrongDevice());
            return { Native->mbSignaled.load(std::memory_order_acquire), {} };
        }

        TArdaRHIResult<float> FArdaNativeDevice::GetTimerQuerySeconds(
            const FArdaRHITimerQueryRef& Query)
        {
            auto Poll = PollTimerQuery(Query);
            if (!Poll) return Failure<float>(eastl::move(Poll.mStatus));
            if (!Poll.mValue) return Failure<float>(FArdaRHIStatus::Error(
                EArdaRHIResult::InvalidState, "Timer query has not completed."));
            return { 0.f, {} };
        }

        FArdaRHIStatus FArdaNativeDevice::ResetTimerQuery(const FArdaRHITimerQueryRef& Query)
        {
            auto* Native = Cast<FTimerQuery>(Query.Get());
            if (!Native || !Owns(Native)) return WrongDevice();
            Native->mbBegun.store(false, std::memory_order_release);
            Native->mbSignaled.store(false, std::memory_order_release);
            return {};
        }

        FArdaRHIStatus FArdaNativeDevice::SignalGpuFence(
            const FArdaRHIGpuFenceRef& Fence, EArdaRHIQueueType)
        {
            auto* Native = Cast<FGpuFence>(Fence.Get());
            if (!Native || !Owns(Native)) return WrongDevice();
            if (auto Status = mDevice->WaitForIdle(); !Status) return Status;
            Native->mbSignaled.store(true, std::memory_order_release);
            return {};
        }

        TArdaRHIResult<bool> FArdaNativeDevice::PollGpuFence(const FArdaRHIGpuFenceRef& Fence)
        {
            auto* Native = Cast<FGpuFence>(Fence.Get());
            if (!Native || !Owns(Native)) return Failure<bool>(WrongDevice());
            return { Native->mbSignaled.load(std::memory_order_acquire), {} };
        }

        FArdaRHIStatus FArdaNativeDevice::WaitGpuFence(const FArdaRHIGpuFenceRef& Fence)
        {
            auto Poll = PollGpuFence(Fence);
            if (!Poll) return Poll.mStatus;
            return Poll.mValue ? FArdaRHIStatus{} : mDevice->WaitForIdle();
        }

        FArdaRHIStatus FArdaNativeDevice::ResetGpuFence(const FArdaRHIGpuFenceRef& Fence)
        {
            auto* Native = Cast<FGpuFence>(Fence.Get());
            if (!Native || !Owns(Native)) return WrongDevice();
            Native->mbSignaled.store(false, std::memory_order_release);
            return {};
        }

        TArdaRHIResult<FArdaRHICommandListRef> FArdaNativeDevice::CreateCommandList(
            EArdaRHIQueueType Queue,
            bool bImmediateExecution)
        {
            auto Native = mDevice->CreateCommandList(Queue, bImmediateExecution);
            if (!Native)
                return Failure<FArdaRHICommandListRef>(eastl::move(Native.mStatus));
            return { FArdaRHICommandListRef(new FCommandList(
                this, Queue, eastl::move(Native.mValue), mLifetimeTracker)), {} };
        }

        TArdaRHIResult<uint64_t> FArdaNativeDevice::FinishCommandListSubmission(
            FCommandList& CommandList,
            TArdaRHIResult<uint64_t> Submitted)
        {
            auto Completions = CommandList.TakeCopyCompletions();
            if (Completions.empty()) return Submitted;

            const bool bBlocking = eastl::any_of(
                Completions.begin(), Completions.end(),
                [](const FPendingBufferCopyCompletion& Completion)
                {
                    return Completion.mbBlocking;
                });
            auto NativeDevice = mDevice;
            const uint64_t Submission = Submitted.mValue;
            const FArdaRHIStatus SubmissionStatus = Submitted.mStatus;
            const auto Complete = [NativeDevice, Submission, SubmissionStatus](
                eastl::vector<FPendingBufferCopyCompletion> Pending)
            {
                FArdaRHIStatus WaitStatus = SubmissionStatus;
                if (WaitStatus && Submission != 0)
                    WaitStatus = NativeDevice->WaitForSubmission(Submission);
                FArdaRHIStatus FirstError = WaitStatus;

                for (auto& Completion : Pending)
                {
                    FArdaRHIStatus CopyStatus = WaitStatus;
                    FArdaRHIBufferReadbackResult ReadbackResult;
                    ReadbackResult.mStatus = CopyStatus;
                    if (CopyStatus && Completion.mReadbackBuffer)
                    {
                        auto Mapping = NativeDevice->MapBuffer(
                            Completion.mReadbackBuffer, 0,
                            Completion.mByteSize);
                        if (!Mapping)
                        {
                            CopyStatus = eastl::move(Mapping.mStatus);
                            ReadbackResult.mStatus = CopyStatus;
                        }
                        else
                        {
                            ReadbackResult.mValue.resize(
                                Completion.mByteSize);
                            std::memcpy(
                                ReadbackResult.mValue.data(),
                                Mapping.mValue,
                                Completion.mByteSize);
                            NativeDevice->UnmapBuffer(
                                Completion.mReadbackBuffer);
                        }
                    }

                    if (Completion.mOutput)
                    {
                        if (CopyStatus)
                            *Completion.mOutput = ReadbackResult.mValue;
                        else
                            Completion.mOutput->clear();
                    }
                    try
                    {
                        if (Completion.mUploadCallback)
                            Completion.mUploadCallback(CopyStatus);
                        if (Completion.mReadbackCallback)
                            Completion.mReadbackCallback(
                                eastl::move(ReadbackResult));
                    }
                    catch (...)
                    {
                        if (CopyStatus)
                            CopyStatus = FArdaRHIStatus::Error(
                                EArdaRHIResult::BackendFailure,
                                "A buffer-copy completion callback threw an exception.");
                    }
                    if (FirstError && !CopyStatus)
                        FirstError = CopyStatus;
                }
                return FirstError;
            };

            if (bBlocking)
            {
                const FArdaRHIStatus CompletionStatus =
                    Complete(eastl::move(Completions));
                if (!CompletionStatus)
                    return Failure<uint64_t>(CompletionStatus);
                return Submitted;
            }

            std::thread(
                [Complete, Pending = eastl::move(Completions)]() mutable
                {
                    (void)Complete(eastl::move(Pending));
                }).detach();
            return Submitted;
        }

        TArdaRHIResult<uint64_t> FArdaNativeDevice::ExecuteCommandList(
            const FArdaRHICommandListRef& CommandList)
        {
            auto* Native = Cast<FCommandList>(CommandList.Get());
            if (!Native || !Owns(Native)) return Failure<uint64_t>(WrongDevice());
            return FinishCommandListSubmission(
                *Native,
                mDevice->ExecuteCommandList(
                    Native->GetNative(), Native->GetQueueType()));
        }

        TArdaRHIResult<uint64_t> FArdaNativeDevice::ExecuteCommandLists(
            const eastl::vector<FArdaRHICommandListRef>& CommandLists,
            EArdaRHIQueueType Queue)
        {
            if (CommandLists.empty())
                return Failure<uint64_t>(Invalid("At least one command list is required."));
            uint64_t Last = 0;
            for (const auto& CommandList : CommandLists)
            {
                auto* Native = Cast<FCommandList>(CommandList.Get());
                if (!Native || !Owns(Native) || Native->GetQueueType() != Queue)
                    return Failure<uint64_t>(WrongDevice());
                auto Submitted = mDevice->ExecuteCommandList(Native->GetNative(), Queue);
                Submitted = FinishCommandListSubmission(
                    *Native, eastl::move(Submitted));
                if (!Submitted) return Submitted;
                Last = Submitted.mValue;
            }
            return { Last, {} };
        }

        void FArdaNativeDevice::TrimDescriptorCaches()
        {
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            mSamplerCache.Clear();
            mBindingLayoutCache.Clear();
            mInputLayoutCache.Clear();
            mRasterStateCache.Clear();
            mBlendStateCache.Clear();
            mDepthStateCache.Clear();
            mTextureImportCache.Clear();
            mBufferImportCache.Clear();
        }

        FArdaRHICacheStats FArdaNativeDevice::GetDescriptorCacheStats() const noexcept
        {
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            FArdaRHICacheStats Stats;
            Stats.mSamplers = mSamplerCache.Size();
            Stats.mBindingLayouts = mBindingLayoutCache.Size();
            Stats.mInputLayouts = mInputLayoutCache.Size();
            Stats.mRasterStates = mRasterStateCache.Size();
            Stats.mBlendStates = mBlendStateCache.Size();
            Stats.mDepthStencilStates = mDepthStateCache.Size();
            return Stats;
        }

        FArdaRHIResourceLifetimeStats
        FArdaNativeDevice::GetResourceLifetimeStats() const noexcept
        {
            FArdaRHIResourceLifetimeStats Stats;
            for (size_t Index = 0;
                 Index < static_cast<size_t>(EArdaRHIResourceType::Count);
                 ++Index)
            {
                Stats.mLiveResources[Index] = mLifetimeTracker->Get(
                    static_cast<EArdaRHIResourceType>(Index));
            }
            const FArdaNativeLifetimeStats Native = mDevice->GetLifetimeStats();
            Stats.mResourceDescriptors = Native.mResourceDescriptors;
            Stats.mSamplerDescriptors = Native.mSamplerDescriptors;
            Stats.mDescriptorSets = Native.mDescriptorSets;
            Stats.mPendingSubmissions = Native.mPendingSubmissions;
            return Stats;
        }

        void FArdaNativeDevice::FlushAndDisablePipelineCachePersistence() noexcept
        {
            if (!mbPipelineCacheDetached && mDevice)
            {
                mDevice->FlushPipelineCache();
                mbPipelineCacheDetached = true;
            }
        }

        FCommandList::FCommandList(
            FArdaNativeDevice* Device,
            EArdaRHIQueueType Queue,
            eastl::unique_ptr<IArdaNativeCommandList> Native,
            eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
            : FResource(EArdaRHIResourceType::CommandList, "CommandList", Device,
                eastl::move(LifetimeTracker))
            , mDevice(Device), mQueue(Queue), mNative(eastl::move(Native))
        {
        }

        IArdaRHIDevice* FCommandList::GetDevice() const noexcept { return mDevice; }

        bool FCommandList::Owns(const FResource* Resource) const noexcept
        {
            return Resource && Resource->GetOwner() == mDevice;
        }

        FArdaRHIStatus FCommandList::Reset()
        {
            mCopyCompletions.clear();
            return mNative->Reset();
        }

        FArdaRHIStatus FCommandList::WriteBuffer(
            IArdaRHIBuffer& Buffer, const void* Data, size_t Size, uint64_t Offset)
        {
            auto* Native = Cast<FBuffer>(&Buffer);
            if (!Native || !Owns(Native)) return WrongDevice();
            if (!Data || Size == 0 || Offset > Native->mDesc.mByteSize ||
                Size > Native->mDesc.mByteSize - Offset)
                return Invalid("Buffer write range is invalid.");
            return mNative->WriteBuffer(Native->mNative, Native->mDesc, Data, Size, Offset);
        }

        FArdaRHIStatus FCommandList::CopyBufferHostToDevice(
            IArdaRHIBuffer& Destination,
            const void* SourceData,
            size_t Size,
            uint64_t DestinationOffset)
        {
            if (auto Status = WriteBuffer(
                    Destination, SourceData, Size, DestinationOffset);
                !Status)
                return Status;
            FPendingBufferCopyCompletion Completion;
            Completion.mbBlocking = true;
            mCopyCompletions.push_back(eastl::move(Completion));
            return {};
        }

        FArdaRHIStatus FCommandList::CopyBufferHostToDeviceAsync(
            IArdaRHIBuffer& Destination,
            const void* SourceData,
            size_t Size,
            FArdaRHIHostToDeviceCopyCallback Callback,
            uint64_t DestinationOffset)
        {
            if (!Callback)
                return Invalid(
                    "An asynchronous host-to-device copy requires a callback.");
            if (auto Status = WriteBuffer(
                    Destination, SourceData, Size, DestinationOffset);
                !Status)
                return Status;
            FPendingBufferCopyCompletion Completion;
            Completion.mUploadCallback = eastl::move(Callback);
            mCopyCompletions.push_back(eastl::move(Completion));
            return {};
        }

        FArdaRHIStatus FCommandList::CopyBufferDeviceToHost(
            IArdaRHIBuffer& Source,
            eastl::vector<uint8_t>& Output,
            uint64_t SourceOffset,
            uint64_t Size)
        {
            auto* Native = Cast<FBuffer>(&Source);
            if (!Native || !Owns(Native)) return WrongDevice();
            if (SourceOffset > Native->mDesc.mByteSize)
                return Invalid("Buffer readback offset is invalid.");
            const uint64_t ResolvedSize = Size == ArdaRHIWholeBuffer
                ? Native->mDesc.mByteSize - SourceOffset : Size;
            if (ResolvedSize == 0 ||
                ResolvedSize > Native->mDesc.mByteSize - SourceOffset ||
                ResolvedSize > static_cast<uint64_t>(SIZE_MAX))
                return Invalid("Buffer readback range is invalid.");

            FArdaRHIBufferDesc ReadbackDesc;
            ReadbackDesc.mByteSize = ResolvedSize;
            ReadbackDesc.mCpuAccess = EArdaRHICpuAccess::Read;
            ReadbackDesc.mInitialState = EArdaRHIResourceState::CopyDest;
            ReadbackDesc.mbKeepInitialState = true;
            ReadbackDesc.mDebugName = "Buffer readback";
            auto Readback = mDevice->GetNativeDevice().CreateBuffer(ReadbackDesc);
            if (!Readback) return eastl::move(Readback.mStatus);
            if (auto Status = mNative->SetBufferState(
                    Native->mNative, Native->mDesc,
                    EArdaRHIResourceState::CopySource);
                !Status)
                return Status;
            if (auto Status = mNative->CopyBuffer(
                    Readback.mValue, 0, Native->mNative,
                    SourceOffset, ResolvedSize);
                !Status)
                return Status;

            FPendingBufferCopyCompletion Completion;
            Completion.mbBlocking = true;
            Completion.mReadbackBuffer = eastl::move(Readback.mValue);
            Completion.mByteSize = static_cast<size_t>(ResolvedSize);
            Completion.mOutput = &Output;
            mCopyCompletions.push_back(eastl::move(Completion));
            return {};
        }

        FArdaRHIStatus FCommandList::CopyBufferDeviceToHostAsync(
            IArdaRHIBuffer& Source,
            FArdaRHIDeviceToHostCopyCallback Callback,
            uint64_t SourceOffset,
            uint64_t Size)
        {
            if (!Callback)
                return Invalid(
                    "An asynchronous device-to-host copy requires a callback.");
            auto* Native = Cast<FBuffer>(&Source);
            if (!Native || !Owns(Native)) return WrongDevice();
            if (SourceOffset > Native->mDesc.mByteSize)
                return Invalid("Buffer readback offset is invalid.");
            const uint64_t ResolvedSize = Size == ArdaRHIWholeBuffer
                ? Native->mDesc.mByteSize - SourceOffset : Size;
            if (ResolvedSize == 0 ||
                ResolvedSize > Native->mDesc.mByteSize - SourceOffset ||
                ResolvedSize > static_cast<uint64_t>(SIZE_MAX))
                return Invalid("Buffer readback range is invalid.");

            FArdaRHIBufferDesc ReadbackDesc;
            ReadbackDesc.mByteSize = ResolvedSize;
            ReadbackDesc.mCpuAccess = EArdaRHICpuAccess::Read;
            ReadbackDesc.mInitialState = EArdaRHIResourceState::CopyDest;
            ReadbackDesc.mbKeepInitialState = true;
            ReadbackDesc.mDebugName = "Asynchronous buffer readback";
            auto Readback = mDevice->GetNativeDevice().CreateBuffer(ReadbackDesc);
            if (!Readback) return eastl::move(Readback.mStatus);
            if (auto Status = mNative->SetBufferState(
                    Native->mNative, Native->mDesc,
                    EArdaRHIResourceState::CopySource);
                !Status)
                return Status;
            if (auto Status = mNative->CopyBuffer(
                    Readback.mValue, 0, Native->mNative,
                    SourceOffset, ResolvedSize);
                !Status)
                return Status;

            FPendingBufferCopyCompletion Completion;
            Completion.mReadbackBuffer = eastl::move(Readback.mValue);
            Completion.mByteSize = static_cast<size_t>(ResolvedSize);
            Completion.mReadbackCallback = eastl::move(Callback);
            mCopyCompletions.push_back(eastl::move(Completion));
            return {};
        }

        FArdaRHIStatus FCommandList::CopyBuffer(
            IArdaRHIBuffer& Destination, uint64_t DestinationOffset,
            IArdaRHIBuffer& Source, uint64_t SourceOffset, uint64_t Size)
        {
            auto* Dst = Cast<FBuffer>(&Destination);
            auto* Src = Cast<FBuffer>(&Source);
            if (!Dst || !Src || !Owns(Dst) || !Owns(Src)) return WrongDevice();
            if (Size == 0 || DestinationOffset > Dst->mDesc.mByteSize ||
                Size > Dst->mDesc.mByteSize - DestinationOffset ||
                SourceOffset > Src->mDesc.mByteSize ||
                Size > Src->mDesc.mByteSize - SourceOffset)
                return Invalid("Buffer copy range is invalid.");
            return mNative->CopyBuffer(
                Dst->mNative, DestinationOffset, Src->mNative, SourceOffset, Size);
        }

        FArdaRHIStatus FCommandList::CopyTextureToStaging(
            IArdaRHIStagingTexture&, const FArdaRHITextureSlice&,
            IArdaRHITexture&, const FArdaRHITextureSlice&)
        {
            return Unsupported("Staging texture copies are not implemented by the native modules.");
        }

        FArdaRHIStatus FCommandList::CopyTextureFromStaging(
            IArdaRHITexture&, const FArdaRHITextureSlice&,
            IArdaRHIStagingTexture&, const FArdaRHITextureSlice&)
        {
            return Unsupported("Staging texture copies are not implemented by the native modules.");
        }

        FArdaRHIStatus FCommandList::ClearTexture(
            IArdaRHITexture& Texture,
            const FArdaRHITextureSubresourceRange& Range,
            const FArdaRHIColor& Color)
        {
            auto* Native = Cast<FTexture>(&Texture);
            if (!Native || !Owns(Native)) return WrongDevice();
            return mNative->ClearTexture(Native->mNative, Native->mDesc, Range, Color);
        }

        FArdaRHIStatus FCommandList::SetTextureState(
            IArdaRHITexture& Texture,
            const FArdaRHITextureSubresourceRange& Range,
            EArdaRHIResourceState State)
        {
            auto* Native = Cast<FTexture>(&Texture);
            if (!Native || !Owns(Native)) return WrongDevice();
            return mNative->SetTextureState(Native->mNative, Native->mDesc, Range, State);
        }

        FArdaRHIStatus FCommandList::SetBufferState(
            IArdaRHIBuffer& Buffer, EArdaRHIResourceState State)
        {
            auto* Native = Cast<FBuffer>(&Buffer);
            if (!Native || !Owns(Native)) return WrongDevice();
            return mNative->SetBufferState(Native->mNative, Native->mDesc, State);
        }

        FArdaRHIStatus FCommandList::SetAccelStructState(
            IArdaRHIAccelStruct&, EArdaRHIResourceState)
        {
            return Unsupported("Acceleration structures are unsupported by the native modules.");
        }

        FArdaRHIStatus FCommandList::BeginTrackingTextureState(
            IArdaRHITexture& Texture,
            const FArdaRHITextureSubresourceRange& Range,
            EArdaRHIResourceState State)
        {
            auto* Native = Cast<FTexture>(&Texture);
            if (!Native || !Owns(Native)) return WrongDevice();
            return mNative->BeginTrackingTextureState(
                Native->mNative, Native->mDesc, Range, State);
        }

        FArdaRHIStatus FCommandList::BeginTrackingBufferState(
            IArdaRHIBuffer& Buffer, EArdaRHIResourceState State)
        {
            auto* Native = Cast<FBuffer>(&Buffer);
            if (!Native || !Owns(Native)) return WrongDevice();
            return mNative->BeginTrackingBufferState(Native->mNative, Native->mDesc, State);
        }

        FArdaRHIStatus FCommandList::SetUAVBarriersForTexture(
            IArdaRHITexture& Texture, bool bEnabled)
        {
            auto* Native = Cast<FTexture>(&Texture);
            if (!Native || !Owns(Native)) return WrongDevice();
            return mNative->SetUAVBarriersForTexture(Native->mNative, bEnabled);
        }

        FArdaRHIStatus FCommandList::SetUAVBarriersForBuffer(
            IArdaRHIBuffer& Buffer, bool bEnabled)
        {
            auto* Native = Cast<FBuffer>(&Buffer);
            if (!Native || !Owns(Native)) return WrongDevice();
            return mNative->SetUAVBarriersForBuffer(Native->mNative, bEnabled);
        }

        FArdaRHIStatus FCommandList::ClearTextureUInt(
            IArdaRHITexture&, const FArdaRHITextureSubresourceRange&, uint32_t)
        {
            return Unsupported("Integer texture clears are not implemented by the native modules.");
        }

        FArdaRHIStatus FCommandList::ClearDepthStencilTexture(
            IArdaRHITexture& Texture,
            const FArdaRHITextureSubresourceRange& Range,
            bool bClearDepth, float Depth, bool bClearStencil, uint8_t Stencil)
        {
            auto* Native = Cast<FTexture>(&Texture);
            if (!Native || !Owns(Native)) return WrongDevice();
            return mNative->ClearDepthStencilTexture(
                Native->mNative, Native->mDesc, Range,
                bClearDepth, Depth, bClearStencil, Stencil);
        }

        FArdaRHIStatus FCommandList::ClearBufferUInt(IArdaRHIBuffer&, uint32_t)
        {
            return Unsupported("Integer buffer clears are not implemented by the native modules.");
        }

        FArdaRHIStatus FCommandList::SetGraphicsState(const FArdaRHIGraphicsState& State)
        {
            auto* Pipeline = Cast<FGraphicsPipeline>(State.mPipeline.Get());
            auto* Framebuffer = Cast<FFramebuffer>(State.mFramebuffer.Get());
            if (!Pipeline || !Framebuffer || !Owns(Pipeline) || !Owns(Framebuffer))
                return WrongDevice();
            FArdaNativeGraphicsState Native;
            Native.mPipeline = Pipeline->mNative;
            Native.mFramebuffer = Framebuffer->mNative;
            Native.mIndexFormat = State.mIndexFormat;
            Native.mIndexOffset = State.mIndexOffset;
            Native.mViewports = State.mViewports;
            Native.mScissors = State.mScissors;
            for (const auto& Binding : State.mBindings)
            {
                auto* Set = Cast<FBindingSet>(Binding.Get());
                if (!Set || !Owns(Set)) return WrongDevice();
                Native.mBindings.push_back(Set->mNative);
            }
            for (const auto& Binding : State.mVertexBuffers)
            {
                auto* Buffer = Cast<FBuffer>(Binding.mBuffer.Get());
                if (!Buffer || !Owns(Buffer)) return WrongDevice();
                uint32_t Stride = 0;
                if (Pipeline->mDesc.mInputLayout)
                {
                    const auto& Attributes = Pipeline->mDesc.mInputLayout->GetDesc().mAttributes;
                    for (const auto& Attribute : Attributes)
                        if (Attribute.mBufferIndex == Binding.mSlot)
                            Stride = eastl::max(Stride, Attribute.mElementStride);
                }
                Native.mVertexBuffers.push_back({ Buffer->mNative, Binding.mSlot,
                    Binding.mOffset, Stride, Buffer->mDesc.mByteSize });
            }
            if (State.mIndexBuffer)
            {
                auto* Buffer = Cast<FBuffer>(State.mIndexBuffer.Get());
                if (!Buffer || !Owns(Buffer)) return WrongDevice();
                Native.mIndexBuffer = Buffer->mNative;
            }
            return mNative->SetGraphicsState(Native);
        }

        FArdaRHIStatus FCommandList::SetComputeState(const FArdaRHIComputeState& State)
        {
            auto* Pipeline = Cast<FComputePipeline>(State.mPipeline.Get());
            if (!Pipeline || !Owns(Pipeline)) return WrongDevice();
            FArdaNativeComputeState Native;
            Native.mPipeline = Pipeline->mNative;
            for (const auto& Binding : State.mBindings)
            {
                auto* Set = Cast<FBindingSet>(Binding.Get());
                if (!Set || !Owns(Set)) return WrongDevice();
                Native.mBindings.push_back(Set->mNative);
            }
            return mNative->SetComputeState(Native);
        }

        FArdaRHIStatus FCommandList::SetMeshletState(const FArdaRHIMeshletState&)
        {
            return Unsupported("Mesh shaders are unsupported by the native modules.");
        }

        FArdaRHIStatus FCommandList::SetRayTracingState(const FArdaRHIRayTracingState&)
        {
            return Unsupported("Ray tracing pipelines are unsupported by the native modules.");
        }

        FArdaRHIStatus FCommandList::BeginTimerQuery(IArdaRHITimerQuery& Query)
        {
            auto* Native = Cast<FTimerQuery>(&Query);
            if (!Native || !Owns(Native)) return WrongDevice();
            Native->mbBegun.store(true, std::memory_order_release);
            Native->mbSignaled.store(false, std::memory_order_release);
            return {};
        }

        FArdaRHIStatus FCommandList::EndTimerQuery(IArdaRHITimerQuery& Query)
        {
            auto* Native = Cast<FTimerQuery>(&Query);
            if (!Native || !Owns(Native)) return WrongDevice();
            if (!Native->mbBegun.load(std::memory_order_acquire))
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
                    "Timer query was not begun.");
            Native->mbSignaled.store(true, std::memory_order_release);
            return {};
        }
    }

    FArdaRHIDeviceRef CreateArdaNativeRHIDevice(
        eastl::shared_ptr<IArdaNativeApiDevice> Device)
    {
        return Device ? FArdaRHIDeviceRef(new FArdaNativeDevice(eastl::move(Device)))
                      : FArdaRHIDeviceRef{};
    }
}
