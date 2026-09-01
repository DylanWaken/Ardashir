#include "RHI/ArdaRHIDevicePrivate.h"

#include "ArdaHash.h"

#include <EASTL/algorithm.h>
#include <EASTL/atomic.h>
#include <EASTL/vector.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace arda::rhi::provider
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
            uint64_t Hash = private_api::ArdaFnv1a64OffsetBasis;
            const auto Append = [&Hash](const void* Data, size_t Size)
            {
                private_api::AppendFnv1a64(Hash, Data, Size);
            };
            const auto AppendUnsigned = [&Hash](uint64_t Value)
            {
                private_api::AppendFnv1a64LittleEndian(Hash, Value);
            };
            AppendUnsigned(static_cast<uint64_t>(Desc.mStage));
            AppendUnsigned(Desc.mEntryPoint.size());
            Append(Desc.mEntryPoint.data(), Desc.mEntryPoint.size());
            AppendUnsigned(Desc.mBytecodeSize);
            if (Desc.mBytecode && Desc.mBytecodeSize)
                Append(Desc.mBytecode, Desc.mBytecodeSize);
            return private_api::FinishPersistentHash(Hash);
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
        class TNativeResource : public FResource, public Interface
        {
        public:
            TNativeResource(
                Desc Descriptor,
                FArdaProviderObjectRef Native,
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
            FArdaProviderObjectRef mNative;
            eastl::shared_ptr<void> mLifetimeToken;
        };

        using FTextureBase = TNativeResource<IArdaRHITexture,
            FArdaRHITextureDesc, EArdaRHIResourceType::Texture>;
        class FTexture final : public FTextureBase
        {
        public:
            using FTextureBase::FTextureBase;
            mutable std::mutex mFacadeStateMutex;
            mutable eastl::vector<EArdaRHIResourceState> mFacadeStates;
            EArdaRHIQueueType mFacadeQueueOwner = EArdaRHIQueueType::Graphics;
            bool mbFacadeQueueOwnerKnown = true;
            FArdaRHIHeapRef mHeap;
            uint64_t mHeapOffset = 0;
        };

        using FBufferBase = TNativeResource<IArdaRHIBuffer,
            FArdaRHIBufferDesc, EArdaRHIResourceType::Buffer>;
        class FBuffer final : public FBufferBase
        {
        public:
            using FBufferBase::FBufferBase;
            mutable std::mutex mFacadeStateMutex;
            EArdaRHIResourceState mFacadeState = EArdaRHIResourceState::Unknown;
            bool mbFacadeStateKnown = false;
            EArdaRHIQueueType mFacadeQueueOwner = EArdaRHIQueueType::Graphics;
            bool mbFacadeQueueOwnerKnown = true;
            FArdaRHIHeapRef mHeap;
            uint64_t mHeapOffset = 0;
        };
        using FHeap = TNativeResource<IArdaRHIHeap,
            FArdaRHIHeapDesc, EArdaRHIResourceType::Heap>;
        using FSampler = TNativeResource<IArdaRHISampler,
            FArdaRHISamplerDesc, EArdaRHIResourceType::Sampler>;
        using FBindingLayoutBase = TNativeResource<IArdaRHIBindingLayout,
            FArdaRHIBindingLayoutDesc, EArdaRHIResourceType::BindingLayout>;
        class FBindingLayout final : public FBindingLayoutBase
        {
        public:
            using FBindingLayoutBase::FBindingLayoutBase;
            bool mbBindless = false;
            FArdaRHIBindlessLayoutDesc mBindlessDesc;
        };
        using FBindingSet = TNativeResource<IArdaRHIBindingSet,
            FArdaRHIBindingSetDesc, EArdaRHIResourceType::BindingSet>;
        class FDescriptorTable final : public FResource,
            public IArdaRHIDescriptorTable
        {
        public:
            FDescriptorTable(
                FArdaRHIBindingSetDesc Desc,
                FArdaProviderObjectRef Native,
                uint32_t Capacity,
                uint32_t MaxCapacity,
                const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(
                    EArdaRHIResourceType::DescriptorTable,
                    Desc.mDebugName,
                    Owner,
                    eastl::move(LifetimeTracker))
                , mDesc(eastl::move(Desc))
                , mNative(eastl::move(Native))
                , mCapacity(Capacity)
                , mMaxCapacity(MaxCapacity)
            {
            }
            const FArdaRHIBindingSetDesc& GetDesc() const noexcept override
            {
                return mDesc;
            }
            uint32_t GetCapacity() const noexcept override
            {
                return mCapacity;
            }
            uint32_t GetFirstDescriptorIndexInHeap() const noexcept override
            {
                std::lock_guard<std::mutex> Lock(mMutex);
                return mNative ? mNative->GetDescriptorBaseIndex() : 0u;
            }

            FArdaRHIBindingSetDesc mDesc;
            FArdaProviderObjectRef mNative;
            uint32_t mCapacity = 0;
            uint32_t mMaxCapacity = 0;
            mutable std::mutex mMutex;
        };
        class FResourceCollection final : public FResource,
            public IArdaRHIResourceCollection
        {
        public:
            FResourceCollection(
                FArdaRHIResourceCollectionDesc Desc,
                FArdaRHIDescriptorTableRef DescriptorTable,
                const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(EArdaRHIResourceType::ResourceCollection,
                    Desc.mDebugName, Owner, eastl::move(LifetimeTracker))
                , mDesc(eastl::move(Desc))
                , mDescriptorTable(eastl::move(DescriptorTable))
            {
            }
            const FArdaRHIResourceCollectionDesc& GetDesc()
                const noexcept override
            {
                return mDesc;
            }
            uint32_t GetFirstDescriptorIndexInHeap()
                const noexcept override
            {
                return mDescriptorTable
                    ? mDescriptorTable->GetFirstDescriptorIndexInHeap()
                    : 0xffffffffu;
            }

            FArdaRHIResourceCollectionDesc mDesc;
            FArdaRHIDescriptorTableRef mDescriptorTable;
            mutable std::mutex mMutex;
        };
        using FFramebuffer = TNativeResource<IArdaRHIFramebuffer,
            FArdaRHIFramebufferDesc, EArdaRHIResourceType::Framebuffer>;
        using FGraphicsPipeline = TNativeResource<IArdaRHIGraphicsPipeline,
            FArdaRHIGraphicsPipelineDesc, EArdaRHIResourceType::GraphicsPipeline>;
        using FComputePipeline = TNativeResource<IArdaRHIComputePipeline,
            FArdaRHIComputePipelineDesc, EArdaRHIResourceType::ComputePipeline>;
        using FMeshletPipeline = TNativeResource<IArdaRHIMeshletPipeline,
            FArdaRHIMeshletPipelineDesc, EArdaRHIResourceType::MeshletPipeline>;
        using FRayTracingPipeline = TNativeResource<IArdaRHIRayTracingPipeline,
            FArdaRHIRayTracingPipelineDesc,
            EArdaRHIResourceType::RayTracingPipeline>;

        class FOpacityMicromap final : public FResource,
            public IArdaRHIOpacityMicromap
        {
        public:
            FOpacityMicromap(FArdaRHIOpacityMicromapDesc Desc,
                FArdaProviderObjectRef Native, uint64_t DeviceAddress,
                const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(EArdaRHIResourceType::OpacityMicromap,
                    Desc.mDebugName, Owner, eastl::move(LifetimeTracker))
                , mDesc(eastl::move(Desc))
                , mNative(eastl::move(Native))
                , mDeviceAddress(DeviceAddress)
            {
            }
            const FArdaRHIOpacityMicromapDesc& GetDesc()
                const noexcept override { return mDesc; }
            bool IsCompacted() const noexcept override
            {
                std::lock_guard<std::mutex> Lock(mStateMutex);
                return mBuildState == EArdaRHIAccelStructBuildState::Compacted;
            }
            uint64_t GetDeviceAddress() const noexcept override
            {
                return mDeviceAddress;
            }
            const void* GetPhysicalIdentity() const noexcept override
            {
                return mNative ? mNative->GetIdentity() : nullptr;
            }
            EArdaRHIAccelStructBuildState GetBuildState()
                const noexcept override
            {
                std::lock_guard<std::mutex> Lock(mStateMutex);
                return mBuildState;
            }

            FArdaRHIOpacityMicromapDesc mDesc;
            FArdaProviderObjectRef mNative;
            uint64_t mDeviceAddress = 0;
            mutable std::mutex mStateMutex;
            EArdaRHIResourceState mFacadeState =
                EArdaRHIResourceState::OpacityMicromapWrite;
            EArdaRHIAccelStructBuildState mBuildState =
                EArdaRHIAccelStructBuildState::Unbuilt;
        };

        class FAccelStruct final : public FResource,
            public IArdaRHIAccelStruct
        {
        public:
            FAccelStruct(
                FArdaRHIAccelStructDesc Desc,
                FArdaRHIAccelStructMemoryRequirements Requirements,
                FArdaProviderObjectRef Native,
                uint64_t DeviceAddress,
                const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(EArdaRHIResourceType::AccelStruct,
                    Desc.mDebugName, Owner, eastl::move(LifetimeTracker))
                , mDesc(eastl::move(Desc))
                , mRequirements(Requirements)
                , mNative(eastl::move(Native))
                , mDeviceAddress(DeviceAddress)
            {
            }
            const FArdaRHIAccelStructDesc& GetDesc() const noexcept override
            {
                return mDesc;
            }
            bool IsCompacted() const noexcept override
            {
                std::lock_guard<std::mutex> Lock(mStateMutex);
                return mBuildState == EArdaRHIAccelStructBuildState::Compacted;
            }
            uint64_t GetDeviceAddress() const noexcept override
            {
                return mDeviceAddress;
            }
            const void* GetPhysicalIdentity() const noexcept override
            {
                return mNative ? mNative->GetIdentity() : nullptr;
            }
            EArdaRHIAccelStructBuildState GetBuildState() const noexcept override
            {
                std::lock_guard<std::mutex> Lock(mStateMutex);
                return mBuildState;
            }

            FArdaRHIAccelStructDesc mDesc;
            FArdaRHIAccelStructMemoryRequirements mRequirements;
            FArdaProviderObjectRef mNative;
            uint64_t mDeviceAddress = 0;
            mutable std::mutex mStateMutex;
            EArdaRHIResourceState mFacadeState =
                EArdaRHIResourceState::AccelStructRead;
            EArdaRHIAccelStructBuildState mBuildState =
                EArdaRHIAccelStructBuildState::Unbuilt;
            FArdaRHIHeapRef mHeap;
            uint64_t mHeapOffset = 0;
        };

        class FShaderTable final : public FResource,
            public IArdaRHIShaderTable
        {
        public:
            FShaderTable(
                FArdaRHIShaderTableDesc Desc,
                FArdaRHIRayTracingPipelineRef Pipeline,
                FArdaProviderObjectRef Native,
                const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(
                    EArdaRHIResourceType::ShaderTable,
                    Desc.mDebugName,
                    Owner,
                    eastl::move(LifetimeTracker))
                , mDesc(eastl::move(Desc))
                , mPipeline(eastl::move(Pipeline))
                , mNative(eastl::move(Native))
                , mWrittenRecords(mDesc.mMaxEntries, false)
            {
            }
            const FArdaRHIShaderTableDesc& GetDesc() const noexcept override
            {
                return mDesc;
            }
            uint32_t GetEntryCount() const noexcept override
            {
                std::lock_guard<std::mutex> Lock(mMutex);
                return mEntryCount;
            }

            FArdaRHIShaderTableDesc mDesc;
            FArdaRHIRayTracingPipelineRef mPipeline;
            FArdaProviderObjectRef mNative;
            mutable std::mutex mMutex;
            uint32_t mEntryCount = 0;
            bool mbHasRayGeneration = false;
            eastl::vector<bool> mWrittenRecords;
        };

        class FShaderBundle final : public FResource,
            public IArdaRHIShaderBundle
        {
        public:
            FShaderBundle(FArdaRHIShaderBundleDesc Desc, const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(EArdaRHIResourceType::ShaderBundle,
                    Desc.mDebugName, Owner, eastl::move(LifetimeTracker))
                , mDesc(eastl::move(Desc))
            {
            }
            const FArdaRHIShaderBundleDesc& GetDesc()
                const noexcept override { return mDesc; }
            uint32_t GetRecordCount() const noexcept override
            {
                std::lock_guard<std::mutex> Lock(mMutex);
                return static_cast<uint32_t>(mRecords.size());
            }
            FArdaRHIShaderBundleDesc mDesc;
            eastl::vector<FArdaRHIShaderBundleRecord> mRecords;
            mutable std::mutex mMutex;
        };

        class FWorkGraphPipeline final : public FResource,
            public IArdaRHIWorkGraphPipeline
        {
        public:
            FWorkGraphPipeline(FArdaRHIWorkGraphPipelineDesc Desc,
                FArdaProviderObjectRef Native, uint64_t BackingMemorySize,
                const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(EArdaRHIResourceType::WorkGraphPipeline,
                    Desc.mDebugName, Owner, eastl::move(LifetimeTracker))
                , mDesc(eastl::move(Desc))
                , mNative(eastl::move(Native))
                , mBackingMemorySize(BackingMemorySize)
            {
            }
            const FArdaRHIWorkGraphPipelineDesc& GetDesc()
                const noexcept override { return mDesc; }
            uint64_t GetBackingMemorySize() const noexcept override
            {
                return mBackingMemorySize;
            }
            FArdaRHIWorkGraphPipelineDesc mDesc;
            FArdaProviderObjectRef mNative;
            uint64_t mBackingMemorySize = 0;
        };

        class FSamplerFeedbackTexture final : public FResource,
            public IArdaRHISamplerFeedbackTexture
        {
        public:
            FSamplerFeedbackTexture(
                FArdaRHISamplerFeedbackTextureDesc Desc,
                FArdaRHITextureRef PairedTexture,
                FArdaProviderObjectRef Native,
                const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(EArdaRHIResourceType::SamplerFeedbackTexture,
                    Desc.mDebugName, Owner, eastl::move(LifetimeTracker))
                , mDesc(eastl::move(Desc))
                , mPairedTexture(eastl::move(PairedTexture))
                , mNative(eastl::move(Native))
            {
            }
            const FArdaRHISamplerFeedbackTextureDesc& GetDesc()
                const noexcept override { return mDesc; }
            const FArdaRHITextureRef& GetPairedTexture()
                const noexcept override { return mPairedTexture; }
            const void* GetPhysicalIdentity() const noexcept override
            {
                return mNative ? mNative->GetIdentity() : nullptr;
            }

            FArdaRHISamplerFeedbackTextureDesc mDesc;
            FArdaRHITextureRef mPairedTexture;
            FArdaProviderObjectRef mNative;
            mutable std::mutex mStateMutex;
            EArdaRHIResourceState mFacadeState =
                EArdaRHIResourceState::Unknown;
            bool mbFacadeStateKnown = false;
        };

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
            FStagingTexture(
                FArdaRHIStagingTextureDesc Desc,
                FArdaProviderObjectRef Native,
                const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(EArdaRHIResourceType::StagingTexture,
                    Desc.mDebugName, Owner, eastl::move(LifetimeTracker))
                , mDesc(eastl::move(Desc))
                , mNative(eastl::move(Native)) {}
            const FArdaRHIStagingTextureDesc& GetDesc() const noexcept override { return mDesc; }
            FArdaRHIStagingTextureDesc mDesc;
            FArdaProviderObjectRef mNative;
        };

        class FShader final : public FResource, public IArdaRHIShader
        {
        public:
            FShader(const FArdaRHIShaderDesc& Desc,
                FArdaProviderObjectRef Native, const void* Owner,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker)
                : FResource(EArdaRHIResourceType::Shader,
                    Desc.mDebugName, Owner, eastl::move(LifetimeTracker))
                , mStage(Desc.mStage)
                , mEntryPoint(Desc.mEntryPoint)
                , mPersistentCacheHash(PersistentShaderHash(Desc))
                , mNative(eastl::move(Native)) {}
            EArdaRHIShaderStage GetStage() const noexcept override { return mStage; }
            uint64_t GetPersistentCacheHash() const noexcept override { return mPersistentCacheHash; }
            EArdaRHIShaderStage mStage = EArdaRHIShaderStage::None;
            eastl::string mEntryPoint;
            uint64_t mPersistentCacheHash = 0;
            FArdaProviderObjectRef mNative;
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

        size_t TextureStateIndex(
            const FArdaRHITextureDesc& Desc,
            uint32_t MipLevel,
            uint32_t ArraySlice,
            uint32_t Plane) noexcept
        {
            return static_cast<size_t>(Plane) * Desc.mMipLevels *
                    Desc.mArraySize +
                static_cast<size_t>(ArraySlice) * Desc.mMipLevels +
                MipLevel;
        }

        void StoreFacadeTextureState(
            eastl::vector<EArdaRHIResourceState>& States,
            const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& InputRange,
            EArdaRHIResourceState State)
        {
            const auto Range = InputRange.Resolve(Desc);
            for (uint32_t Plane = Range.mBasePlane;
                 Plane < Range.mBasePlane + Range.mPlaneCount;
                 ++Plane)
            {
                for (uint32_t ArraySlice = Range.mBaseArraySlice;
                     ArraySlice < Range.mBaseArraySlice + Range.mArraySliceCount;
                     ++ArraySlice)
                {
                    for (uint32_t MipLevel = Range.mBaseMipLevel;
                         MipLevel < Range.mBaseMipLevel + Range.mMipLevelCount;
                         ++MipLevel)
                    {
                        States[TextureStateIndex(
                            Desc, MipLevel, ArraySlice, Plane)] = State;
                    }
                }
            }
        }

        TArdaRHIResult<EArdaRHIResourceState> LoadFacadeTextureState(
            const eastl::vector<EArdaRHIResourceState>& States,
            const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& InputRange)
        {
            const auto Range = InputRange.Resolve(Desc);
            const EArdaRHIResourceState State = States[TextureStateIndex(
                Desc,
                Range.mBaseMipLevel,
                Range.mBaseArraySlice,
                Range.mBasePlane)];
            for (uint32_t Plane = Range.mBasePlane;
                 Plane < Range.mBasePlane + Range.mPlaneCount;
                 ++Plane)
            {
                for (uint32_t ArraySlice = Range.mBaseArraySlice;
                     ArraySlice < Range.mBaseArraySlice + Range.mArraySliceCount;
                     ++ArraySlice)
                {
                    for (uint32_t MipLevel = Range.mBaseMipLevel;
                         MipLevel < Range.mBaseMipLevel + Range.mMipLevelCount;
                         ++MipLevel)
                    {
                        if (States[TextureStateIndex(
                                Desc, MipLevel, ArraySlice, Plane)] != State)
                        {
                            return { {}, FArdaRHIStatus::Error(
                                EArdaRHIResult::InvalidState,
                                "Texture range contains mixed facade states.") };
                        }
                    }
                }
            }
            return { State, {} };
        }

        FArdaProviderObjectRef GetNativeObject(IArdaRHIResource* Resource)
        {
            if (auto* Texture = Cast<FTexture>(Resource)) return Texture->mNative;
            if (auto* Buffer = Cast<FBuffer>(Resource)) return Buffer->mNative;
            if (auto* Texture = Cast<FStagingTexture>(Resource))
                return Texture->mNative;
            if (auto* Sampler = Cast<FSampler>(Resource)) return Sampler->mNative;
            if (auto* Layout = Cast<FBindingLayout>(Resource)) return Layout->mNative;
            if (auto* Set = Cast<FBindingSet>(Resource)) return Set->mNative;
            if (auto* Table = Cast<FDescriptorTable>(Resource))
                return Table->mNative;
            if (auto* Framebuffer = Cast<FFramebuffer>(Resource)) return Framebuffer->mNative;
            if (auto* Pipeline = Cast<FGraphicsPipeline>(Resource)) return Pipeline->mNative;
            if (auto* Pipeline = Cast<FComputePipeline>(Resource)) return Pipeline->mNative;
            if (auto* Pipeline = Cast<FMeshletPipeline>(Resource)) return Pipeline->mNative;
            if (auto* Pipeline = Cast<FRayTracingPipeline>(Resource))
                return Pipeline->mNative;
            if (auto* AccelStruct = Cast<FAccelStruct>(Resource))
                return AccelStruct->mNative;
            if (auto* Micromap = Cast<FOpacityMicromap>(Resource))
                return Micromap->mNative;
            if (auto* Table = Cast<FShaderTable>(Resource))
                return Table->mNative;
            if (auto* Feedback = Cast<FSamplerFeedbackTexture>(Resource))
                return Feedback->mNative;
            if (auto* Shader = Cast<FShader>(Resource)) return Shader->mNative;
            if (auto* View = Cast<FShaderResourceView>(Resource))
                return GetNativeObject(View->mResource.Get());
            if (auto* View = Cast<FUnorderedAccessView>(Resource))
                return GetNativeObject(View->mResource.Get());
            return {};
        }

        TArdaRHIResult<FArdaRHIBindingItem> MakeCollectionBinding(
            const FArdaRHIResourceCollectionItem& Item,
            uint32_t ArrayElement)
        {
            FArdaRHIBindingItem Binding;
            Binding.mSlot = 0;
            Binding.mArrayElement = ArrayElement;
            const auto BufferBindingType = [](const IArdaRHIBuffer& Buffer,
                bool bUav)
            {
                if (Buffer.GetDesc().mStructureStride)
                    return bUav
                        ? EArdaRHIBindingType::StructuredBufferUAV
                        : EArdaRHIBindingType::StructuredBufferSRV;
                return bUav
                    ? EArdaRHIBindingType::RawBufferUAV
                    : EArdaRHIBindingType::RawBufferSRV;
            };
            switch (Item.mType)
            {
            case EArdaRHIResourceCollectionItemType::Texture:
                if (!Item.mTexture) break;
                Binding.mType = EArdaRHIBindingType::TextureSRV;
                Binding.mResource = FArdaRHIResourceRef(Item.mTexture.Get());
                return {Binding, {}};
            case EArdaRHIResourceCollectionItemType::TextureReference:
                if (!Item.mTextureReference ||
                    !Item.mTextureReference->GetTexture()) break;
                Binding.mType = EArdaRHIBindingType::TextureSRV;
                Binding.mResource = FArdaRHIResourceRef(
                    Item.mTextureReference->GetTexture().Get());
                return {Binding, {}};
            case EArdaRHIResourceCollectionItemType::Buffer:
                if (!Item.mBuffer) break;
                Binding.mType = BufferBindingType(*Item.mBuffer, false);
                Binding.mResource = FArdaRHIResourceRef(Item.mBuffer.Get());
                return {Binding, {}};
            case EArdaRHIResourceCollectionItemType::ShaderResourceView:
                if (!Item.mShaderResourceView ||
                    !Item.mShaderResourceView->GetResource()) break;
                Binding.mView = Item.mShaderResourceView->GetDesc();
                Binding.mResource = FArdaRHIResourceRef(
                    Item.mShaderResourceView.Get());
                if (auto* Buffer = dynamic_cast<IArdaRHIBuffer*>(
                        Item.mShaderResourceView->GetResource()))
                    Binding.mType = BufferBindingType(*Buffer, false);
                else if (dynamic_cast<IArdaRHIAccelStruct*>(
                             Item.mShaderResourceView->GetResource()))
                    Binding.mType =
                        EArdaRHIBindingType::RayTracingAccelStruct;
                else
                    Binding.mType = EArdaRHIBindingType::TextureSRV;
                return {Binding, {}};
            case EArdaRHIResourceCollectionItemType::UnorderedAccessView:
                if (!Item.mUnorderedAccessView ||
                    !Item.mUnorderedAccessView->GetResource()) break;
                Binding.mView = Item.mUnorderedAccessView->GetDesc();
                Binding.mResource = FArdaRHIResourceRef(
                    Item.mUnorderedAccessView.Get());
                if (auto* Buffer = dynamic_cast<IArdaRHIBuffer*>(
                        Item.mUnorderedAccessView->GetResource()))
                    Binding.mType = BufferBindingType(*Buffer, true);
                else
                    Binding.mType = EArdaRHIBindingType::TextureUAV;
                return {Binding, {}};
            case EArdaRHIResourceCollectionItemType::AccelerationStructure:
                if (!Item.mAccelerationStructure) break;
                Binding.mType =
                    EArdaRHIBindingType::RayTracingAccelStruct;
                Binding.mResource = FArdaRHIResourceRef(
                    Item.mAccelerationStructure.Get());
                return {Binding, {}};
            case EArdaRHIResourceCollectionItemType::Sampler:
                if (!Item.mSampler) break;
                Binding.mType = EArdaRHIBindingType::Sampler;
                Binding.mResource = FArdaRHIResourceRef(Item.mSampler.Get());
                return {Binding, {}};
            }
            return {{}, Invalid(
                "A resource-collection item is empty or mismatched with its declared type.")};
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

        class FArdaRHIDeviceImpl;

        struct FPendingBufferCopyCompletion
        {
            bool mbBlocking = false;
            FArdaProviderObjectRef mReadbackBuffer;
            size_t mByteSize = 0;
            eastl::vector<uint8_t>* mOutput = nullptr;
            FArdaRHIHostToDeviceCopyCallback mUploadCallback;
            FArdaRHIDeviceToHostCopyCallback mReadbackCallback;
        };

        class FCommandList final : public FResource, public IArdaRHICommandList
        {
        public:
            FCommandList(FArdaRHIDeviceImpl* Device,
                EArdaRHIQueueType Queue,
                eastl::unique_ptr<IArdaProviderCommandList> Native,
                eastl::shared_ptr<FLifetimeTracker> LifetimeTracker);

            IArdaRHIDevice* GetDevice() const noexcept override;
            EArdaRHIQueueType GetQueueType() const noexcept override { return mQueue; }
            FArdaRHIStatus Open() override;
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
            FArdaRHIStatus CopyTexture(
                IArdaRHITexture&, const FArdaRHITextureSlice&,
                IArdaRHITexture&, const FArdaRHITextureSlice&) override;
            FArdaRHIStatus ResolveTexture(
                IArdaRHITexture&, const FArdaRHITextureSlice&,
                IArdaRHITexture&, const FArdaRHITextureSlice&) override;
            FArdaRHIStatus CopyTextureToStaging(IArdaRHIStagingTexture&, const FArdaRHITextureSlice&, IArdaRHITexture&, const FArdaRHITextureSlice&) override;
            FArdaRHIStatus CopyTextureFromStaging(IArdaRHITexture&, const FArdaRHITextureSlice&, IArdaRHIStagingTexture&, const FArdaRHITextureSlice&) override;
            FArdaRHIStatus ClearTexture(IArdaRHITexture&, const FArdaRHITextureSubresourceRange&, const FArdaRHIColor&) override;
            FArdaRHIStatus SetTextureState(IArdaRHITexture&, const FArdaRHITextureSubresourceRange&, EArdaRHIResourceState) override;
            FArdaRHIStatus SetBufferState(IArdaRHIBuffer&, EArdaRHIResourceState) override;
            FArdaRHIStatus TransitionTexture(
                IArdaRHITexture&,
                const FArdaRHITextureTransitionDesc&) override;
            FArdaRHIStatus TransitionBuffer(
                IArdaRHIBuffer&,
                const FArdaRHIBufferTransitionDesc&) override;
            FArdaRHIStatus SetAccelStructState(IArdaRHIAccelStruct&, EArdaRHIResourceState) override;
            TArdaRHIResult<FArdaRHIResourceStateSnapshot> QueryAccelStructState(
                IArdaRHIAccelStruct&) const override;
            void SetAutomaticBarriers(bool bEnabled) override { mNative->SetAutomaticBarriers(bEnabled); }
            FArdaRHIStatus BeginTrackingTextureState(IArdaRHITexture&, const FArdaRHITextureSubresourceRange&, EArdaRHIResourceState) override;
            FArdaRHIStatus BeginTrackingBufferState(IArdaRHIBuffer&, EArdaRHIResourceState) override;
            TArdaRHIResult<FArdaRHIResourceStateSnapshot> QueryTextureState(
                IArdaRHITexture&,
                const FArdaRHITextureSubresourceRange&) const override;
            TArdaRHIResult<FArdaRHIResourceStateSnapshot> QueryBufferState(
                IArdaRHIBuffer&) const override;
            TArdaRHIResult<FArdaRHIResourceStateSnapshot>
                QuerySamplerFeedbackTextureState(
                    IArdaRHISamplerFeedbackTexture&) const override;
            FArdaRHIStatus SetUAVBarriersForTexture(IArdaRHITexture&, bool) override;
            FArdaRHIStatus SetUAVBarriersForBuffer(IArdaRHIBuffer&, bool) override;
            void CommitBarriers() override { mNative->CommitBarriers(); }
            FArdaRHIStatus AliasingBarrier(
                IArdaRHIResource*, IArdaRHIResource*) override;
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
            FArdaRHIStatus DrawIndirect(
                IArdaRHIBuffer&, uint64_t, uint32_t, uint32_t) override;
            FArdaRHIStatus DrawIndexedIndirect(
                IArdaRHIBuffer&, uint64_t, uint32_t, uint32_t) override;
            void Dispatch(uint32_t X, uint32_t Y, uint32_t Z) override { mNative->Dispatch(X, Y, Z); }
            FArdaRHIStatus DispatchIndirect(
                IArdaRHIBuffer&, uint64_t) override;
            FArdaRHIStatus DispatchMesh(uint32_t, uint32_t, uint32_t) override;
            FArdaRHIStatus DispatchRays(uint32_t, uint32_t, uint32_t) override;
            FArdaRHIStatus DispatchRaysIndirect(IArdaRHIBuffer&, uint64_t) override;
            FArdaRHIStatus BuildBottomLevelAccelStruct(IArdaRHIAccelStruct&, const eastl::vector<FArdaRHIRayTracingGeometryDesc>&, EArdaRHIAccelStructBuildFlags) override;
            FArdaRHIStatus BuildTopLevelAccelStruct(IArdaRHIAccelStruct&, const eastl::vector<FArdaRHIRayTracingInstanceDesc>&, EArdaRHIAccelStructBuildFlags) override;
            FArdaRHIStatus BuildTopLevelAccelStructFromBuffer(IArdaRHIAccelStruct&, IArdaRHIBuffer&, uint64_t, size_t, EArdaRHIAccelStructBuildFlags) override;
            FArdaRHIStatus CompactAccelStruct(
                IArdaRHIAccelStruct&, IArdaRHIAccelStruct&) override;
            FArdaRHIStatus DispatchShaderBundle(
                IArdaRHIShaderBundle&) override;
            FArdaRHIStatus DispatchWorkGraph(
                IArdaRHIWorkGraphPipeline&, const void*, uint32_t,
                uint32_t,
                const eastl::vector<FArdaRHIBindingSetRef>&) override;
            FArdaRHIStatus BuildOpacityMicromap(
                IArdaRHIOpacityMicromap&) override;
            FArdaRHIStatus CompactOpacityMicromap(
                IArdaRHIOpacityMicromap&, IArdaRHIOpacityMicromap&) override;
            TArdaRHIResult<FArdaRHIResourceStateSnapshot>
                QueryOpacityMicromapState(
                    IArdaRHIOpacityMicromap&) const override;
            FArdaRHIStatus ClearSamplerFeedbackTexture(
                IArdaRHISamplerFeedbackTexture&) override;
            FArdaRHIStatus DecodeSamplerFeedbackTexture(
                IArdaRHITexture&, IArdaRHISamplerFeedbackTexture&,
                EArdaRHIFormat) override;
            FArdaRHIStatus SetSamplerFeedbackTextureState(
                IArdaRHISamplerFeedbackTexture&,
                EArdaRHIResourceState) override;
            FArdaRHIStatus BeginTimerQuery(IArdaRHITimerQuery&) override;
            FArdaRHIStatus EndTimerQuery(IArdaRHITimerQuery&) override;
            void BeginMarker(const char* Name) override { mNative->BeginMarker(Name); }
            void EndMarker() override { mNative->EndMarker(); }

            IArdaProviderCommandList& GetNative() const noexcept { return *mNative; }
            [[nodiscard]] FArdaRHIStatus ValidateFacadeStartStates() const;
            void CommitFacadeStates();
            eastl::vector<FPendingBufferCopyCompletion> TakeCopyCompletions()
            {
                return eastl::move(mCopyCompletions);
            }

        private:
            bool Owns(const FResource* Resource) const noexcept;
            eastl::vector<EArdaRHIResourceState>& GetFacadeTextureStates(
                FTexture& Texture) const;
            FArdaRHIDeviceImpl* mDevice = nullptr;
            EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
            eastl::unique_ptr<IArdaProviderCommandList> mNative;
            eastl::vector<FPendingBufferCopyCompletion> mCopyCompletions;
            mutable std::unordered_map<
                FTexture*, eastl::vector<EArdaRHIResourceState>>
                mFacadeTextureStates;
            mutable std::unordered_map<FBuffer*, EArdaRHIResourceState>
                mFacadeBufferStates;
            mutable std::unordered_map<FSamplerFeedbackTexture*,
                EArdaRHIResourceState> mFacadeSamplerFeedbackStates;
            mutable std::unordered_map<FTexture*, EArdaRHIQueueType>
                mFacadeTextureQueueOwners;
            mutable std::unordered_map<FBuffer*, EArdaRHIQueueType>
                mFacadeBufferQueueOwners;
            struct FAccelStructTracking
            {
                EArdaRHIResourceState mState =
                    EArdaRHIResourceState::AccelStructRead;
                EArdaRHIAccelStructBuildState mBuildState =
                    EArdaRHIAccelStructBuildState::Unbuilt;
            };
            mutable std::unordered_map<FAccelStruct*, FAccelStructTracking>
                mFacadeAccelStructStates;
            mutable std::unordered_map<FOpacityMicromap*, FAccelStructTracking>
                mFacadeOpacityMicromapStates;
            std::unordered_map<
                FTexture*, eastl::vector<EArdaRHIResourceState>>
                mExpectedTextureStartStates;
            std::unordered_map<FBuffer*, EArdaRHIResourceState>
                mExpectedBufferStartStates;
        };

        class FArdaRHIDeviceImpl final : public FResource, public IArdaRHIDevice
        {
        public:
            explicit FArdaRHIDeviceImpl(eastl::shared_ptr<IArdaRHIProviderDevice> Device)
                : FResource(EArdaRHIResourceType::Device, "RHIDevice", this)
                , mLifetimeTracker(eastl::make_shared<FLifetimeTracker>())
                , mDevice(eastl::move(Device)) {}

            ~FArdaRHIDeviceImpl() override
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
            TArdaRHIResult<FArdaRHIHeapRef> CreateHeap(const FArdaRHIHeapDesc&) override;
            TArdaRHIResult<FArdaRHIStagingTextureRef> CreateStagingTexture(const FArdaRHIStagingTextureDesc&) override;
            TArdaRHIResult<FArdaRHIStagingTextureMapping> MapStagingTexture(const FArdaRHIStagingTextureRef&, const FArdaRHITextureSlice&, EArdaRHICpuAccess) override;
            FArdaRHIStatus UnmapStagingTexture(const FArdaRHIStagingTextureRef&) override;
            TArdaRHIResult<FArdaRHIShaderResourceViewRef> CreateShaderResourceView(const TArdaRHIRef<IArdaRHIResource>&, const FArdaRHIViewDesc&) override;
            TArdaRHIResult<FArdaRHIUnorderedAccessViewRef> CreateUnorderedAccessView(const TArdaRHIRef<IArdaRHIResource>&, const FArdaRHIViewDesc&) override;
            TArdaRHIResult<FArdaRHISamplerRef> CreateSampler(const FArdaRHISamplerDesc&) override;
            TArdaRHIResult<FArdaRHIShaderRef> CreateShader(const FArdaRHIShaderDesc&) override;
            TArdaRHIResult<FArdaRHIShaderLibraryRef> CreateShaderLibrary(const void*, size_t, const char*) override;
            TArdaRHIResult<FArdaRHIShaderRef> GetShaderFromLibrary(const FArdaRHIShaderLibraryRef&, const char*, EArdaRHIShaderStage, const char*) override;
            TArdaRHIResult<FArdaRHIInputLayoutRef> CreateInputLayout(
                const eastl::vector<FArdaRHIVertexAttributeDesc>&) override;
            TArdaRHIResult<FArdaRHIBindingLayoutRef> CreateBindingLayout(const FArdaRHIBindingLayoutDesc&) override;
            TArdaRHIResult<FArdaRHIBindingLayoutRef> CreateBindlessLayout(const FArdaRHIBindlessLayoutDesc&) override;
            TArdaRHIResult<FArdaRHIBindingSetRef> CreateBindingSet(const FArdaRHIBindingSetDesc&) override;
            TArdaRHIResult<FArdaRHIDescriptorTableRef> CreateDescriptorTable(const FArdaRHIBindingLayoutRef&) override;
            TArdaRHIResult<FArdaRHIResourceCollectionRef>
                CreateResourceCollection(
                    const FArdaRHIResourceCollectionDesc&) override;
            FArdaRHIStatus UpdateResourceCollection(
                const FArdaRHIResourceCollectionRef&, uint32_t,
                const FArdaRHIResourceCollectionItem&) override;
            FArdaRHIStatus ResizeDescriptorTable(const FArdaRHIDescriptorTableRef&, uint32_t, bool) override;
            FArdaRHIStatus WriteDescriptorTable(const FArdaRHIDescriptorTableRef&, const FArdaRHIBindingItem&) override;
            TArdaRHIResult<FArdaRHIFramebufferRef> CreateFramebuffer(const FArdaRHIFramebufferDesc&) override;
            TArdaRHIResult<FArdaRHIGraphicsPipelineRef> CreateGraphicsPipeline(const FArdaRHIGraphicsPipelineDesc&) override;
            TArdaRHIResult<FArdaRHIComputePipelineRef> CreateComputePipeline(const FArdaRHIComputePipelineDesc&) override;
            TArdaRHIResult<FArdaRHIMeshletPipelineRef> CreateMeshletPipeline(const FArdaRHIMeshletPipelineDesc&) override;
            TArdaRHIResult<FArdaRHIRasterStateRef> CreateRasterState(const FArdaRHIRasterState&) override;
            TArdaRHIResult<FArdaRHIBlendStateRef> CreateBlendState(const FArdaRHIBlendState&) override;
            TArdaRHIResult<FArdaRHIDepthStencilStateRef> CreateDepthStencilState(const FArdaRHIDepthStencilState&) override;
            TArdaRHIResult<FArdaRHIAccelStructRef> CreateAccelStruct(const FArdaRHIAccelStructDesc&) override;
            TArdaRHIResult<FArdaRHIAccelStructMemoryRequirements>
                GetAccelStructBuildMemoryRequirements(
                    const FArdaRHIAccelStructDesc&) override;
            TArdaRHIResult<uint64_t> GetAccelStructCompactedSize(
                const FArdaRHIAccelStructRef&) override;
            TArdaRHIResult<FArdaRHIOpacityMicromapRef>
                CreateOpacityMicromap(
                    const FArdaRHIOpacityMicromapDesc&) override;
            TArdaRHIResult<uint64_t> GetOpacityMicromapCompactedSize(
                const FArdaRHIOpacityMicromapRef&) override;
            TArdaRHIResult<FArdaRHIRayTracingPipelineRef> CreateRayTracingPipeline(const FArdaRHIRayTracingPipelineDesc&) override;
            TArdaRHIResult<FArdaRHIShaderTableRef> CreateShaderTable(const FArdaRHIRayTracingPipelineRef&, const FArdaRHIShaderTableDesc&) override;
            FArdaRHIStatus SetShaderTableRecord(
                const FArdaRHIShaderTableRef&,
                const FArdaRHIShaderTableRecordDesc&) override;
            FArdaRHIStatus CommitShaderTable(
                const FArdaRHIShaderTableRef&) override;
            TArdaRHIResult<FArdaRHIShaderBundleRef> CreateShaderBundle(
                const FArdaRHIShaderBundleDesc&) override;
            FArdaRHIStatus SetShaderBundleRecords(
                const FArdaRHIShaderBundleRef&,
                const eastl::vector<FArdaRHIShaderBundleRecord>&) override;
            TArdaRHIResult<FArdaRHIWorkGraphPipelineRef>
                CreateWorkGraphPipeline(
                    const FArdaRHIWorkGraphPipelineDesc&) override;
            FArdaRHIStatus SetShaderTableRayGeneration(const FArdaRHIShaderTableRef&, const char*, const FArdaRHIBindingSetRef&) override;
            TArdaRHIResult<int> AddShaderTableMiss(const FArdaRHIShaderTableRef&, const char*, const FArdaRHIBindingSetRef&) override;
            TArdaRHIResult<int> AddShaderTableHitGroup(const FArdaRHIShaderTableRef&, const char*, const FArdaRHIBindingSetRef&) override;
            TArdaRHIResult<int> AddShaderTableCallable(const FArdaRHIShaderTableRef&, const char*, const FArdaRHIBindingSetRef&) override;
            TArdaRHIResult<FArdaRHISamplerFeedbackTextureRef>
                CreateSamplerFeedbackTexture(
                    const FArdaRHITextureRef&,
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
            TArdaRHIResult<uint64_t> ExecuteCommandLists(const eastl::vector<FArdaRHICommandListRef>&, EArdaRHIQueueType) override;
            FArdaRHIStatus QueueWait(
                EArdaRHIQueueType WaitQueue,
                EArdaRHIQueueType ExecutionQueue,
                uint64_t Submission) override
            {
                return mDevice->QueueWait(
                    WaitQueue, ExecutionQueue, Submission);
            }
            TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(const FArdaRHITextureRef&) override;
            TArdaRHIResult<FArdaRHIMemoryRequirements> GetBufferMemoryRequirements(const FArdaRHIBufferRef&) override;
            TArdaRHIResult<FArdaRHIMemoryRequirements> GetAccelStructMemoryRequirements(const FArdaRHIAccelStructRef&) override;
            FArdaRHIStatus BindTextureMemory(const FArdaRHITextureRef&, const FArdaRHIHeapRef&, uint64_t) override;
            FArdaRHIStatus BindBufferMemory(const FArdaRHIBufferRef&, const FArdaRHIHeapRef&, uint64_t) override;
            FArdaRHIStatus BindAccelStructMemory(const FArdaRHIAccelStructRef&, const FArdaRHIHeapRef&, uint64_t) override { return Unsupported("Acceleration structures are unsupported by the backend providers."); }
            TArdaRHIResult<FArdaRHITextureTiling> GetTextureTiling(
                const FArdaRHITextureRef&) override;
            FArdaRHIStatus UpdateTextureTileMappings(
                const FArdaRHITextureRef&,
                const eastl::vector<FArdaRHITextureTileMapping>&,
                EArdaRHIQueueType) override;
            FArdaRHIStatus UpdateBufferTileMappings(
                const FArdaRHIBufferRef&,
                const eastl::vector<FArdaRHIBufferTileMapping>&,
                EArdaRHIQueueType) override;
            FArdaRHIStatus CommitReservedResource(
                const FArdaRHIResourceRef&, uint64_t,
                EArdaRHIQueueType) override;
            TArdaRHIResult<FArdaRHIStreamingBudget>
                QueryStreamingBudget(bool) const override;
            FArdaRHIStatus SetStreamingBudgetReservation(
                uint64_t, bool) override;
            FArdaRHIStatus QueryWorkGraphSupport() const override
            {
                return GetCapabilities().mWorkGraphTier !=
                    EArdaRHIWorkGraphTier::None
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
            FArdaRHIStatus QueryStreamSourceSupport() const override { return Unsupported("Stream-source output is unsupported by the backend providers."); }
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

            IArdaRHIProviderDevice& GetProviderDevice() const noexcept { return *mDevice; }

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
            eastl::shared_ptr<IArdaRHIProviderDevice> mDevice;
            bool mbPipelineCacheDetached = false;
            TDescriptorCache<FArdaRHISamplerDesc, FArdaRHISamplerRef> mSamplerCache;
            TDescriptorCache<FArdaRHIBindingLayoutDesc, FArdaRHIBindingLayoutRef> mBindingLayoutCache;
            TDescriptorCache<FArdaRHIInputLayoutDesc, FArdaRHIInputLayoutRef> mInputLayoutCache;
            TDescriptorCache<FArdaRHIRasterState, FArdaRHIRasterStateRef> mRasterStateCache;
            TDescriptorCache<FArdaRHIBlendState, FArdaRHIBlendStateRef> mBlendStateCache;
            TDescriptorCache<FArdaRHIDepthStencilState, FArdaRHIDepthStencilStateRef> mDepthStateCache;
            TDescriptorCache<FArdaRHIRayTracingPipelineDesc,
                FArdaRHIRayTracingPipelineRef> mRayTracingPipelineCache;
            TDescriptorCache<FArdaRHINativeTextureImportDesc, FArdaRHITextureRef> mTextureImportCache;
            TDescriptorCache<FArdaRHINativeBufferImportDesc, FArdaRHIBufferRef> mBufferImportCache;
        };

        TArdaRHIResult<FArdaRHITextureRef> FArdaRHIDeviceImpl::CreateTexture(
            const FArdaRHITextureDesc& Desc)
        {
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHITextureRef>(eastl::move(Status));
            if (Desc.mbTiled &&
                !(mDevice->GetCapabilities().mResidency.mbReservedTexture2D ||
                  mDevice->GetCapabilities().mResidency.mbReservedTexture3D))
                return UnsupportedResult<FArdaRHITextureRef>(
                    "Tiled textures are unsupported by the backend providers.");
            auto Native = mDevice->CreateTexture(Desc);
            if (!Native) return Failure<FArdaRHITextureRef>(eastl::move(Native.mStatus));
            return { FArdaRHITextureRef(new FTexture(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHITextureReferenceRef>
        FArdaRHIDeviceImpl::CreateTextureReference(const FArdaRHITextureRef& Texture)
        {
            if (!IsOwned(Texture))
                return Failure<FArdaRHITextureReferenceRef>(WrongDevice());
            return { FArdaRHITextureReferenceRef(new FTextureReference(
                Texture, this, mLifetimeTracker)), {} };
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::SetTextureReference(
            const FArdaRHITextureReferenceRef& Reference,
            const FArdaRHITextureRef& Texture)
        {
            auto* Native = Cast<FTextureReference>(Reference.Get());
            if (!Native || !Owns(Native) || !IsOwned(Texture)) return WrongDevice();
            Native->mTexture = Texture;
            return {};
        }

        TArdaRHIResult<FArdaRHIBufferRef> FArdaRHIDeviceImpl::CreateBuffer(
            const FArdaRHIBufferDesc& Desc)
        {
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHIBufferRef>(eastl::move(Status));
            if (Desc.mbTiled &&
                !mDevice->GetCapabilities().mResidency.mbReservedBuffers)
                return UnsupportedResult<FArdaRHIBufferRef>(
                    "Sparse buffers are unsupported by this device.");
            auto Native = mDevice->CreateBuffer(Desc);
            if (!Native) return Failure<FArdaRHIBufferRef>(eastl::move(Native.mStatus));
            return { FArdaRHIBufferRef(new FBuffer(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIHeapRef> FArdaRHIDeviceImpl::CreateHeap(
            const FArdaRHIHeapDesc& Desc)
        {
            if (!Desc.mCapacity || !Desc.mMemoryTypeBits)
                return Failure<FArdaRHIHeapRef>(Invalid(
                    "A heap requires non-zero capacity and compatible memory types."));
            auto Native = mDevice->CreateHeap(Desc);
            if (!Native)
                return Failure<FArdaRHIHeapRef>(eastl::move(Native.mStatus));
            return { FArdaRHIHeapRef(new FHeap(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIMemoryRequirements>
        FArdaRHIDeviceImpl::GetTextureMemoryRequirements(
            const FArdaRHITextureRef& Texture)
        {
            auto* Native = Cast<FTexture>(Texture.Get());
            if (!Native || !Owns(Native))
                return Failure<FArdaRHIMemoryRequirements>(WrongDevice());
            return mDevice->GetTextureMemoryRequirements(
                Native->mNative, Native->mDesc);
        }

        TArdaRHIResult<FArdaRHIMemoryRequirements>
        FArdaRHIDeviceImpl::GetBufferMemoryRequirements(
            const FArdaRHIBufferRef& Buffer)
        {
            auto* Native = Cast<FBuffer>(Buffer.Get());
            if (!Native || !Owns(Native))
                return Failure<FArdaRHIMemoryRequirements>(WrongDevice());
            return mDevice->GetBufferMemoryRequirements(
                Native->mNative, Native->mDesc);
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::BindTextureMemory(
            const FArdaRHITextureRef& Texture,
            const FArdaRHIHeapRef& Heap,
            uint64_t Offset)
        {
            auto* NativeTexture = Cast<FTexture>(Texture.Get());
            auto* NativeHeap = Cast<FHeap>(Heap.Get());
            if (!NativeTexture || !NativeHeap ||
                !Owns(NativeTexture) || !Owns(NativeHeap))
                return WrongDevice();
            if (!NativeTexture->mDesc.mbVirtual)
                return Invalid("Only virtual textures can be bound to an explicit heap.");
            if (NativeTexture->mHeap)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "Virtual texture memory is already bound.");
            const auto Requirements = GetTextureMemoryRequirements(Texture);
            if (!Requirements)
                return Requirements.mStatus;
            if (!Requirements.mValue.mAlignment ||
                Offset % Requirements.mValue.mAlignment != 0 ||
                Offset > NativeHeap->mDesc.mCapacity ||
                Requirements.mValue.mSize >
                    NativeHeap->mDesc.mCapacity - Offset ||
                !(Requirements.mValue.mMemoryTypeBits &
                  NativeHeap->mDesc.mMemoryTypeBits))
            {
                return Invalid("Texture heap binding does not satisfy size, alignment, or memory-type requirements.");
            }
            const FArdaRHIStatus Status = mDevice->BindTextureMemory(
                NativeTexture->mNative,
                NativeTexture->mDesc,
                NativeHeap->mNative,
                Offset);
            if (Status)
            {
                NativeTexture->mHeap = Heap;
                NativeTexture->mHeapOffset = Offset;
            }
            return Status;
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::BindBufferMemory(
            const FArdaRHIBufferRef& Buffer,
            const FArdaRHIHeapRef& Heap,
            uint64_t Offset)
        {
            auto* NativeBuffer = Cast<FBuffer>(Buffer.Get());
            auto* NativeHeap = Cast<FHeap>(Heap.Get());
            if (!NativeBuffer || !NativeHeap ||
                !Owns(NativeBuffer) || !Owns(NativeHeap))
                return WrongDevice();
            if (!NativeBuffer->mDesc.mbVirtual)
                return Invalid("Only virtual buffers can be bound to an explicit heap.");
            if (NativeBuffer->mHeap)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "Virtual buffer memory is already bound.");
            const auto Requirements = GetBufferMemoryRequirements(Buffer);
            if (!Requirements)
                return Requirements.mStatus;
            if (!Requirements.mValue.mAlignment ||
                Offset % Requirements.mValue.mAlignment != 0 ||
                Offset > NativeHeap->mDesc.mCapacity ||
                Requirements.mValue.mSize >
                    NativeHeap->mDesc.mCapacity - Offset ||
                !(Requirements.mValue.mMemoryTypeBits &
                  NativeHeap->mDesc.mMemoryTypeBits))
            {
                return Invalid("Buffer heap binding does not satisfy size, alignment, or memory-type requirements.");
            }
            const FArdaRHIStatus Status = mDevice->BindBufferMemory(
                NativeBuffer->mNative,
                NativeBuffer->mDesc,
                NativeHeap->mNative,
                Offset);
            if (Status)
            {
                NativeBuffer->mHeap = Heap;
                NativeBuffer->mHeapOffset = Offset;
            }
            return Status;
        }

        TArdaRHIResult<FArdaRHITextureTiling>
        FArdaRHIDeviceImpl::GetTextureTiling(
            const FArdaRHITextureRef& Texture)
        {
            auto* Native = Cast<FTexture>(Texture.Get());
            if (!Native || !Owns(Native))
                return Failure<FArdaRHITextureTiling>(WrongDevice());
            if (!Native->mDesc.mbTiled)
                return Failure<FArdaRHITextureTiling>(Invalid(
                    "Texture tiling is available only for tiled textures."));
            return mDevice->GetTextureTiling(Native->mNative);
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::UpdateTextureTileMappings(
            const FArdaRHITextureRef& Texture,
            const eastl::vector<FArdaRHITextureTileMapping>& Mappings,
            EArdaRHIQueueType Queue)
        {
            auto* Native = Cast<FTexture>(Texture.Get());
            if (!Native || !Owns(Native)) return WrongDevice();
            if (!Native->mDesc.mbTiled)
                return Invalid("Tile mappings require a tiled texture.");
            if (!GetCapabilities().IsQueueSupported(Queue))
                return Unsupported("The requested sparse-binding queue is unavailable.");
            eastl::vector<FArdaProviderTextureTileMapping> Resolved;
            Resolved.reserve(Mappings.size());
            for (const auto& Mapping : Mappings)
            {
                if (Mapping.mCoordinates.size() != Mapping.mRegions.size() ||
                    Mapping.mCoordinates.size() != Mapping.mByteOffsets.size())
                    return Invalid(
                        "Texture tile coordinates, regions, and heap offsets must have equal counts.");
                FArdaProviderTextureTileMapping Entry;
                Entry.mCoordinates = Mapping.mCoordinates;
                Entry.mRegions = Mapping.mRegions;
                Entry.mByteOffsets = Mapping.mByteOffsets;
                if (Mapping.mHeap)
                {
                    auto* Heap = Cast<FHeap>(Mapping.mHeap.Get());
                    if (!Heap || !Owns(Heap)) return WrongDevice();
                    Entry.mHeap = Heap->mNative;
                }
                Resolved.push_back(eastl::move(Entry));
            }
            return mDevice->UpdateTextureTileMappings(
                Native->mNative, Resolved, Queue);
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::UpdateBufferTileMappings(
            const FArdaRHIBufferRef& Buffer,
            const eastl::vector<FArdaRHIBufferTileMapping>& Mappings,
            EArdaRHIQueueType Queue)
        {
            auto* Native = Cast<FBuffer>(Buffer.Get());
            if (!Native || !Owns(Native)) return WrongDevice();
            if (!Native->mDesc.mbTiled)
                return Invalid("Tile mappings require a sparse buffer.");
            if (!GetCapabilities().IsQueueSupported(Queue))
                return Unsupported("The requested sparse-binding queue is unavailable.");
            eastl::vector<FArdaProviderBufferTileMapping> Resolved;
            Resolved.reserve(Mappings.size());
            for (const auto& Mapping : Mappings)
            {
                FArdaProviderBufferTileMapping Entry;
                Entry.mBufferOffset = Mapping.mBufferOffset;
                Entry.mByteSize = Mapping.mByteSize;
                Entry.mHeapOffset = Mapping.mHeapOffset;
                Entry.mbCommit = Mapping.mbCommit;
                if (Mapping.mHeap)
                {
                    auto* Heap = Cast<FHeap>(Mapping.mHeap.Get());
                    if (!Heap || !Owns(Heap)) return WrongDevice();
                    Entry.mHeap = Heap->mNative;
                }
                Resolved.push_back(eastl::move(Entry));
            }
            return mDevice->UpdateBufferTileMappings(
                Native->mNative, Resolved, Queue);
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::CommitReservedResource(
            const FArdaRHIResourceRef& Resource,
            uint64_t CommittedBytes,
            EArdaRHIQueueType Queue)
        {
            auto* Base = Cast<FResource>(Resource.Get());
            if (!Base || !Owns(Base)) return WrongDevice();
            if (auto* Texture = Cast<FTexture>(Resource.Get()))
            {
                if (!Texture->mDesc.mbTiled)
                    return Invalid("Reserved commit requires a tiled texture.");
                return mDevice->CommitReservedResource(
                    Texture->mNative, true, CommittedBytes, Queue);
            }
            if (auto* Buffer = Cast<FBuffer>(Resource.Get()))
            {
                if (!Buffer->mDesc.mbTiled)
                    return Invalid("Reserved commit requires a tiled buffer.");
                return mDevice->CommitReservedResource(
                    Buffer->mNative, false, CommittedBytes, Queue);
            }
            return Invalid(
                "Reserved commit supports only tiled textures and buffers.");
        }

        TArdaRHIResult<FArdaRHIStreamingBudget>
        FArdaRHIDeviceImpl::QueryStreamingBudget(bool bLocalMemory) const
        {
            return mDevice->QueryStreamingBudget(bLocalMemory);
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::SetStreamingBudgetReservation(
            uint64_t Bytes, bool bLocalMemory)
        {
            return mDevice->SetStreamingBudgetReservation(
                Bytes, bLocalMemory);
        }

        TArdaRHIResult<FArdaRHIUniformBufferRef> FArdaRHIDeviceImpl::CreateUniformBuffer(
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

        TArdaRHIResult<FArdaRHITextureRef> FArdaRHIDeviceImpl::ImportNativeTexture(
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

        TArdaRHIResult<FArdaRHIBufferRef> FArdaRHIDeviceImpl::ImportNativeBuffer(
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

        TArdaRHIResult<FArdaRHIStagingTextureRef> FArdaRHIDeviceImpl::CreateStagingTexture(
            const FArdaRHIStagingTextureDesc& Desc)
        {
            if (auto Status = Validate(Desc.mTexture); !Status)
                return Failure<FArdaRHIStagingTextureRef>(eastl::move(Status));
            if (Desc.mCpuAccess == EArdaRHICpuAccess::None)
                return Failure<FArdaRHIStagingTextureRef>(Invalid("A staging texture requires CPU access."));
            auto Native = mDevice->CreateStagingTexture(Desc);
            if (!Native)
                return Failure<FArdaRHIStagingTextureRef>(
                    eastl::move(Native.mStatus));
            return { FArdaRHIStagingTextureRef(new FStagingTexture(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIStagingTextureMapping>
        FArdaRHIDeviceImpl::MapStagingTexture(
            const FArdaRHIStagingTextureRef& Texture,
            const FArdaRHITextureSlice& Slice,
            EArdaRHICpuAccess Access)
        {
            auto* Native = Cast<FStagingTexture>(Texture.Get());
            if (!Native || !Owns(Native))
                return Failure<FArdaRHIStagingTextureMapping>(WrongDevice());
            if (Access == EArdaRHICpuAccess::None ||
                Access != Native->mDesc.mCpuAccess)
            {
                return Failure<FArdaRHIStagingTextureMapping>(
                    Invalid("Staging texture mapping access does not match its descriptor."));
            }
            return mDevice->MapStagingTexture(
                Native->mNative, Slice, Access);
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::UnmapStagingTexture(
            const FArdaRHIStagingTextureRef& Texture)
        {
            auto* Native = Cast<FStagingTexture>(Texture.Get());
            if (!Native || !Owns(Native))
                return WrongDevice();
            return mDevice->UnmapStagingTexture(Native->mNative);
        }

        TArdaRHIResult<FArdaRHIShaderResourceViewRef>
        FArdaRHIDeviceImpl::CreateShaderResourceView(
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
        FArdaRHIDeviceImpl::CreateUnorderedAccessView(
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

        TArdaRHIResult<FArdaRHISamplerRef> FArdaRHIDeviceImpl::CreateSampler(
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

        TArdaRHIResult<FArdaRHIShaderRef> FArdaRHIDeviceImpl::CreateShader(
            const FArdaRHIShaderDesc& Desc)
        {
            if (!Desc.mBytecode || Desc.mBytecodeSize == 0 || Desc.mStage == EArdaRHIShaderStage::None)
                return Failure<FArdaRHIShaderRef>(Invalid("Shader bytecode and stage are required."));
            auto Native = mDevice->CreateShader(Desc);
            if (!Native) return Failure<FArdaRHIShaderRef>(eastl::move(Native.mStatus));
            return { FArdaRHIShaderRef(new FShader(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIShaderLibraryRef> FArdaRHIDeviceImpl::CreateShaderLibrary(
            const void* Bytecode, size_t BytecodeSize, const char* DebugName)
        {
            if (!Bytecode || BytecodeSize == 0)
                return Failure<FArdaRHIShaderLibraryRef>(Invalid("Shader-library bytecode is required."));
            return { FArdaRHIShaderLibraryRef(new FShaderLibrary(
                Bytecode, BytecodeSize, DebugName, this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIShaderRef> FArdaRHIDeviceImpl::GetShaderFromLibrary(
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

        TArdaRHIResult<FArdaRHIInputLayoutRef> FArdaRHIDeviceImpl::CreateInputLayout(
            const eastl::vector<FArdaRHIVertexAttributeDesc>& Attributes)
        {
            FArdaRHIInputLayoutDesc Desc{ Attributes };
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHIInputLayoutRef>(eastl::move(Status));
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            if (auto Existing = mInputLayoutCache.Find(Desc)) return { Existing, {} };
            FArdaRHIInputLayoutRef Result(new FInputLayout(
                Desc, this, mLifetimeTracker));
            mInputLayoutCache.Insert(Desc, Result);
            return { Result, {} };
        }

        TArdaRHIResult<FArdaRHIBindingLayoutRef> FArdaRHIDeviceImpl::CreateBindingLayout(
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

        TArdaRHIResult<FArdaRHIBindingLayoutRef>
        FArdaRHIDeviceImpl::CreateBindlessLayout(
            const FArdaRHIBindlessLayoutDesc& Desc)
        {
            if (Desc.mRegisterSpaces.empty() ||
                (!Desc.mMaxCapacity && !Desc.mbUnbounded))
                return Failure<FArdaRHIBindingLayoutRef>(Invalid(
                    "A bindless layout requires register spaces and a capacity or unbounded mode."));
            const auto& Capabilities = mDevice->GetCapabilities().mDescriptors;
            if (Desc.mbUnbounded && !Capabilities.mbUnboundedArrays)
                return UnsupportedResult<FArdaRHIBindingLayoutRef>(
                    "Unbounded descriptors are unsupported by this device.");
            if (Desc.mbUpdateAfterBind && !Capabilities.mbUpdateAfterBind)
                return UnsupportedResult<FArdaRHIBindingLayoutRef>(
                    "Descriptor update-after-bind is unsupported by this device.");
            if (Desc.mbVariableDescriptorCount &&
                !Capabilities.mbVariableDescriptorCount)
                return UnsupportedResult<FArdaRHIBindingLayoutRef>(
                    "Variable descriptor counts are unsupported by this device.");
            if (Desc.mbDirectHeapIndexing &&
                !Capabilities.mbDirectResourceHeapIndexing)
                return UnsupportedResult<FArdaRHIBindingLayoutRef>(
                    "Direct descriptor-heap indexing is unsupported by this device.");
            FArdaRHIBindlessLayoutDesc ResolvedDesc = Desc;
            if (!ResolvedDesc.mMaxCapacity)
                ResolvedDesc.mMaxCapacity =
                    eastl::max(1u, Capabilities.mMaxResourceDescriptors);
            FArdaRHIBindingLayoutDesc NativeDesc;
            NativeDesc.mVisibility = ResolvedDesc.mVisibility;
            NativeDesc.mRegisterSpace = ResolvedDesc.mRegisterSpace;
            NativeDesc.mbRegisterSpaceIsDescriptorSet = true;
            NativeDesc.mDebugName = ResolvedDesc.mDebugName;
            NativeDesc.mItems.reserve(ResolvedDesc.mRegisterSpaces.size());
            for (FArdaRHIBindingLayoutItem Item : ResolvedDesc.mRegisterSpaces)
            {
                if (Item.mType == EArdaRHIBindingType::PushConstants)
                    return Failure<FArdaRHIBindingLayoutRef>(Invalid(
                        "Push constants cannot be part of a bindless descriptor table."));
                Item.mSlot += ResolvedDesc.mFirstSlot;
                Item.mArraySize = ResolvedDesc.mMaxCapacity;
                NativeDesc.mItems.push_back(Item);
            }
            if (auto Status = Validate(NativeDesc); !Status)
                return Failure<FArdaRHIBindingLayoutRef>(eastl::move(Status));
            auto Native = mDevice->CreateBindlessLayout(
                ResolvedDesc, NativeDesc);
            if (!Native)
                return Failure<FArdaRHIBindingLayoutRef>(
                    eastl::move(Native.mStatus));
            auto* Layout = new FBindingLayout(
                NativeDesc,
                eastl::move(Native.mValue),
                this,
                mLifetimeTracker);
            Layout->mbBindless = true;
            Layout->mBindlessDesc = ResolvedDesc;
            return { FArdaRHIBindingLayoutRef(Layout), {} };
        }

        TArdaRHIResult<FArdaRHIDescriptorTableRef>
        FArdaRHIDeviceImpl::CreateDescriptorTable(
            const FArdaRHIBindingLayoutRef& LayoutRef)
        {
            auto* Layout = Cast<FBindingLayout>(LayoutRef.Get());
            if (!Layout || !Owns(Layout))
                return Failure<FArdaRHIDescriptorTableRef>(WrongDevice());
            if (!Layout->mbBindless)
                return Failure<FArdaRHIDescriptorTableRef>(Invalid(
                    "Descriptor tables require a bindless binding layout."));
            FArdaRHIBindingSetDesc Desc;
            Desc.mLayout = LayoutRef;
            Desc.mDebugName = Layout->mBindlessDesc.mDebugName;
            Desc.mVariableDescriptorCount =
                Layout->mBindlessDesc.mbVariableDescriptorCount
                    ? Layout->mBindlessDesc.mMaxCapacity : 0;
            auto Native = mDevice->CreateBindingSet(
                Desc, Layout->mNative, {});
            if (!Native)
                return Failure<FArdaRHIDescriptorTableRef>(
                    eastl::move(Native.mStatus));
            const uint32_t Capacity = Layout->mBindlessDesc.mMaxCapacity;
            return { FArdaRHIDescriptorTableRef(new FDescriptorTable(
                eastl::move(Desc),
                eastl::move(Native.mValue),
                Capacity,
                Capacity,
                this,
                mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIResourceCollectionRef>
        FArdaRHIDeviceImpl::CreateResourceCollection(
            const FArdaRHIResourceCollectionDesc& Desc)
        {
            eastl::vector<FArdaRHIBindingItem> Bindings;
            Bindings.reserve(Desc.mItems.size());
            for (uint32_t Index = 0; Index < Desc.mItems.size(); ++Index)
            {
                auto Binding = MakeCollectionBinding(
                    Desc.mItems[Index], Index);
                if (!Binding)
                    return Failure<FArdaRHIResourceCollectionRef>(
                        eastl::move(Binding.mStatus));
                auto* Resource = Cast<FResource>(
                    Binding.mValue.mResource.Get());
                if (!Resource || !Owns(Resource))
                    return Failure<FArdaRHIResourceCollectionRef>(
                        WrongDevice());
                Bindings.push_back(eastl::move(Binding.mValue));
            }

            FArdaRHIDescriptorTableRef DescriptorTable;
            if (Desc.mbDirectlyIndexed)
            {
                if (Bindings.empty())
                    return Failure<FArdaRHIResourceCollectionRef>(Invalid(
                        "A directly indexed resource collection cannot be empty."));
                const auto& Caps = GetCapabilities().mDescriptors;
                const bool bSampler = Bindings.front().mType ==
                    EArdaRHIBindingType::Sampler;
                if ((!bSampler && !Caps.mbDirectResourceHeapIndexing) ||
                    (bSampler && !Caps.mbDirectSamplerHeapIndexing))
                    return UnsupportedResult<FArdaRHIResourceCollectionRef>(
                        "Direct descriptor-heap indexing is unsupported for this collection.");
                const EArdaRHIBindingType Type = Bindings.front().mType;
                if (eastl::any_of(Bindings.begin(), Bindings.end(),
                        [Type](const FArdaRHIBindingItem& Binding)
                        {
                            return Binding.mType != Type;
                        }))
                    return Failure<FArdaRHIResourceCollectionRef>(Invalid(
                        "A directly indexed collection must use one homogeneous native descriptor type."));
                FArdaRHIBindlessLayoutDesc LayoutDesc;
                LayoutDesc.mVisibility = EArdaRHIShaderStage::All;
                LayoutDesc.mMaxCapacity =
                    static_cast<uint32_t>(Bindings.size());
                LayoutDesc.mbUpdateAfterBind = Desc.mbMutable;
                LayoutDesc.mbDirectHeapIndexing = true;
                LayoutDesc.mLayoutType = bSampler
                    ? EArdaRHIBindlessLayoutType::MutableSampler
                    : EArdaRHIBindlessLayoutType::MutableSrvUavCbv;
                LayoutDesc.mRegisterSpaces.push_back({0, 1, Type});
                LayoutDesc.mDebugName = Desc.mDebugName;
                auto Layout = CreateBindlessLayout(LayoutDesc);
                if (!Layout)
                    return Failure<FArdaRHIResourceCollectionRef>(
                        eastl::move(Layout.mStatus));
                auto Table = CreateDescriptorTable(Layout.mValue);
                if (!Table)
                    return Failure<FArdaRHIResourceCollectionRef>(
                        eastl::move(Table.mStatus));
                for (const auto& Binding : Bindings)
                {
                    const FArdaRHIStatus Status =
                        WriteDescriptorTable(Table.mValue, Binding);
                    if (!Status)
                        return Failure<FArdaRHIResourceCollectionRef>(Status);
                }
                DescriptorTable = eastl::move(Table.mValue);
            }

            return {FArdaRHIResourceCollectionRef(new FResourceCollection(
                Desc, eastl::move(DescriptorTable), this,
                mLifetimeTracker)), {}};
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::UpdateResourceCollection(
            const FArdaRHIResourceCollectionRef& CollectionRef,
            uint32_t Index,
            const FArdaRHIResourceCollectionItem& Item)
        {
            auto* Collection = Cast<FResourceCollection>(CollectionRef.Get());
            if (!Collection || !Owns(Collection)) return WrongDevice();
            std::lock_guard<std::mutex> Lock(Collection->mMutex);
            if (!Collection->mDesc.mbMutable)
                return Invalid("The resource collection is immutable.");
            if (Index >= Collection->mDesc.mItems.size())
                return Invalid("The resource-collection index is out of range.");
            auto Binding = MakeCollectionBinding(Item, Index);
            if (!Binding) return Binding.mStatus;
            auto* Resource = Cast<FResource>(
                Binding.mValue.mResource.Get());
            if (!Resource || !Owns(Resource)) return WrongDevice();
            if (Collection->mDescriptorTable)
            {
                auto Existing = MakeCollectionBinding(
                    Collection->mDesc.mItems[Index], Index);
                if (!Existing) return Existing.mStatus;
                if (Existing.mValue.mType != Binding.mValue.mType)
                    return Invalid(
                        "A directly indexed collection update cannot change descriptor type.");
                const FArdaRHIStatus Status = WriteDescriptorTable(
                    Collection->mDescriptorTable, Binding.mValue);
                if (!Status) return Status;
            }
            Collection->mDesc.mItems[Index] = Item;
            return {};
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::ResizeDescriptorTable(
            const FArdaRHIDescriptorTableRef& TableRef,
            uint32_t NewSize,
            bool bKeepContents)
        {
            auto* Table = Cast<FDescriptorTable>(TableRef.Get());
            if (!Table || !Owns(Table))
                return WrongDevice();
            if (!NewSize || NewSize > Table->mMaxCapacity)
                return Invalid(
                    "Descriptor table size must be within its bindless layout capacity.");
            auto* Layout = Cast<FBindingLayout>(Table->mDesc.mLayout.Get());
            if (!Layout || !Owns(Layout) || !Layout->mbBindless)
                return WrongDevice();
            std::lock_guard<std::mutex> Lock(Table->mMutex);
            FArdaRHIBindingSetDesc NewDesc = Table->mDesc;
            NewDesc.mVariableDescriptorCount =
                Layout->mBindlessDesc.mbVariableDescriptorCount
                    ? NewSize : 0;
            if (!bKeepContents)
            {
                NewDesc.mItems.clear();
            }
            else
            {
                NewDesc.mItems.erase(
                    eastl::remove_if(
                        NewDesc.mItems.begin(),
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
                auto* Resource = Cast<FResource>(Item.mResource.Get());
                FArdaProviderObjectRef Native =
                    GetNativeObject(Item.mResource.Get());
                if (!Resource || !Owns(Resource) || !Native)
                    return WrongDevice();
                Bindings.push_back({Item, eastl::move(Native)});
            }
            auto Native = mDevice->CreateBindingSet(
                NewDesc, Layout->mNative, Bindings);
            if (!Native)
                return Native.mStatus;
            Table->mDesc = eastl::move(NewDesc);
            Table->mNative = eastl::move(Native.mValue);
            Table->mCapacity = NewSize;
            return {};
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::WriteDescriptorTable(
            const FArdaRHIDescriptorTableRef& TableRef,
            const FArdaRHIBindingItem& Item)
        {
            auto* Table = Cast<FDescriptorTable>(TableRef.Get());
            if (!Table || !Owns(Table))
                return WrongDevice();
            auto* Layout = Cast<FBindingLayout>(Table->mDesc.mLayout.Get());
            if (!Layout || !Owns(Layout) || !Layout->mbBindless)
                return WrongDevice();
            auto* Resource = Cast<FResource>(Item.mResource.Get());
            FArdaProviderObjectRef NativeObject =
                GetNativeObject(Item.mResource.Get());
            if (!Resource || !Owns(Resource) || !NativeObject)
                return WrongDevice();
            std::lock_guard<std::mutex> Lock(Table->mMutex);
            if (Item.mArrayElement >= Table->mCapacity)
                return Invalid("Descriptor table array element is out of range.");
            const bool bDeclared = eastl::any_of(
                Layout->mDesc.mItems.begin(),
                Layout->mDesc.mItems.end(),
                [&Item](const FArdaRHIBindingLayoutItem& Declared)
                {
                    return Declared.mSlot == Item.mSlot &&
                        Declared.mType == Item.mType &&
                        Item.mArrayElement < Declared.mArraySize;
                });
            if (!bDeclared)
                return Invalid(
                    "Descriptor table write does not match a bindless register space.");
            FArdaRHIBindingSetDesc NewDesc = Table->mDesc;
            auto Existing = eastl::find_if(
                NewDesc.mItems.begin(),
                NewDesc.mItems.end(),
                [&Item](const FArdaRHIBindingItem& Candidate)
                {
                    return Candidate.mSlot == Item.mSlot &&
                        Candidate.mArrayElement == Item.mArrayElement &&
                        Candidate.mType == Item.mType;
                });
            if (Existing == NewDesc.mItems.end())
                NewDesc.mItems.push_back(Item);
            else
                *Existing = Item;
            eastl::vector<FArdaProviderBinding> Bindings;
            Bindings.reserve(NewDesc.mItems.size());
            for (const FArdaRHIBindingItem& Binding : NewDesc.mItems)
            {
                FArdaProviderObjectRef BindingObject =
                    GetNativeObject(Binding.mResource.Get());
                if (!BindingObject)
                    return Invalid(
                        "Descriptor table contains a non-native resource.");
                Bindings.push_back({Binding, eastl::move(BindingObject)});
            }
            auto Native = mDevice->CreateBindingSet(
                NewDesc, Layout->mNative, Bindings);
            if (!Native)
                return Native.mStatus;
            Table->mDesc = eastl::move(NewDesc);
            Table->mNative = eastl::move(Native.mValue);
            return {};
        }

        TArdaRHIResult<FArdaRHIBindingSetRef> FArdaRHIDeviceImpl::CreateBindingSet(
            const FArdaRHIBindingSetDesc& Desc)
        {
            auto* Layout = Cast<FBindingLayout>(Desc.mLayout.Get());
            if (!Layout || !Owns(Layout))
                return Failure<FArdaRHIBindingSetRef>(WrongDevice());
            if (Layout->mbBindless)
                return Failure<FArdaRHIBindingSetRef>(Invalid(
                    "Bindless layouts must be instantiated as descriptor tables."));
            for (const FArdaRHIBindingLayoutItem& Declared :
                 Layout->mDesc.mItems)
            {
                if (Declared.mType == EArdaRHIBindingType::PushConstants)
                    continue;
                for (uint32_t Element = 0;
                     Element < eastl::max(1u, Declared.mArraySize);
                     ++Element)
                {
                    const bool bFound = eastl::any_of(
                        Desc.mItems.begin(),
                        Desc.mItems.end(),
                        [&Declared, Element](
                            const FArdaRHIBindingItem& Item)
                        {
                            return Item.mSlot == Declared.mSlot &&
                                Item.mType == Declared.mType &&
                                Item.mArrayElement == Element;
                        });
                    if (!bFound)
                        return Failure<FArdaRHIBindingSetRef>(Invalid(
                            "A binding set is missing an item required by its layout."));
                }
            }
            eastl::vector<FArdaProviderBinding> Bindings;
            Bindings.reserve(Desc.mItems.size());
            for (const auto& Item : Desc.mItems)
            {
                auto* Resource = Cast<FResource>(Item.mResource.Get());
                if (!Resource || !Owns(Resource))
                    return Failure<FArdaRHIBindingSetRef>(WrongDevice());
                FArdaProviderObjectRef Object = GetNativeObject(Item.mResource.Get());
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

        TArdaRHIResult<FArdaRHIFramebufferRef> FArdaRHIDeviceImpl::CreateFramebuffer(
            const FArdaRHIFramebufferDesc& Desc)
        {
            if (Desc.mColorAttachments.empty() && !Desc.mDepthAttachment.mTexture)
                return Failure<FArdaRHIFramebufferRef>(
                    Invalid("A framebuffer requires at least one attachment."));
            FArdaProviderFramebufferCreateInfo Info{ Desc };
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
        FArdaRHIDeviceImpl::CreateGraphicsPipeline(const FArdaRHIGraphicsPipelineDesc& Desc)
        {
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHIGraphicsPipelineRef>(eastl::move(Status));
            FArdaProviderGraphicsPipelineCreateInfo Info{ Desc };
            if (Desc.mInputLayout)
            {
                auto* Layout = Cast<FInputLayout>(Desc.mInputLayout.Get());
                if (!Layout || !Owns(Layout))
                    return Failure<FArdaRHIGraphicsPipelineRef>(WrongDevice());
                Info.mInputLayout = &Layout->mDesc;
            }
            const auto SetShader = [this](const FArdaRHIShaderRef& Shader,
                                          FArdaProviderObjectRef& Out)
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
        FArdaRHIDeviceImpl::CreateComputePipeline(const FArdaRHIComputePipelineDesc& Desc)
        {
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHIComputePipelineRef>(eastl::move(Status));
            auto* Shader = Cast<FShader>(Desc.mComputeShader.Get());
            if (!Shader || !Owns(Shader))
                return Failure<FArdaRHIComputePipelineRef>(WrongDevice());
            FArdaProviderComputePipelineCreateInfo Info{ Desc, Shader->mNative };
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

        TArdaRHIResult<FArdaRHIMeshletPipelineRef>
        FArdaRHIDeviceImpl::CreateMeshletPipeline(
            const FArdaRHIMeshletPipelineDesc& Desc)
        {
            if (mDevice->GetCapabilities().mMeshShaderTier ==
                EArdaRHIMeshShaderTier::None)
                return UnsupportedResult<FArdaRHIMeshletPipelineRef>(
                    "Mesh shaders are unsupported by this device.");
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHIMeshletPipelineRef>(eastl::move(Status));

            FArdaProviderMeshletPipelineCreateInfo Info{ Desc };
            const auto SetShader = [this](
                const FArdaRHIShaderRef& Shader,
                FArdaProviderObjectRef& Out)
            {
                if (!Shader) return true;
                auto* Native = Cast<FShader>(Shader.Get());
                if (!Native || !Owns(Native)) return false;
                Out = Native->mNative;
                return true;
            };
            if (!SetShader(Desc.mAmplificationShader, Info.mAmplificationShader) ||
                !SetShader(Desc.mMeshShader, Info.mMeshShader) ||
                !SetShader(Desc.mPixelShader, Info.mPixelShader))
            {
                return Failure<FArdaRHIMeshletPipelineRef>(WrongDevice());
            }
            for (const auto& LayoutRef : Desc.mBindingLayouts)
            {
                auto* Layout = Cast<FBindingLayout>(LayoutRef.Get());
                if (!Layout || !Owns(Layout))
                    return Failure<FArdaRHIMeshletPipelineRef>(WrongDevice());
                Info.mBindingLayouts.push_back(Layout->mNative);
            }
            auto Native = mDevice->CreateMeshletPipeline(Info);
            if (!Native)
                return Failure<FArdaRHIMeshletPipelineRef>(
                    eastl::move(Native.mStatus));
            return { FArdaRHIMeshletPipelineRef(new FMeshletPipeline(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIWorkGraphPipelineRef>
        FArdaRHIDeviceImpl::CreateWorkGraphPipeline(
            const FArdaRHIWorkGraphPipelineDesc& Desc)
        {
            if (GetCapabilities().mWorkGraphTier ==
                EArdaRHIWorkGraphTier::None)
                return UnsupportedResult<FArdaRHIWorkGraphPipelineRef>(
                    "Work graphs are unsupported by this device.");
            if (Desc.mProgramName.empty() || Desc.mShaders.empty() ||
                !Desc.mMaxInputRecords)
                return Failure<FArdaRHIWorkGraphPipelineRef>(Invalid(
                    "A work graph requires a program name, shaders, and input-record capacity."));
            FArdaProviderWorkGraphPipelineCreateInfo Info;
            Info.mDesc = Desc;
            Info.mDesc.mShaders.clear();
            Info.mDesc.mGlobalBindingLayouts.clear();
            for (const auto& ShaderRef : Desc.mShaders)
            {
                auto* Shader = Cast<FShader>(ShaderRef.Get());
                if (!Shader || !Owns(Shader) ||
                    Shader->mStage != EArdaRHIShaderStage::WorkGraph)
                    return Failure<FArdaRHIWorkGraphPipelineRef>(Invalid(
                        "Work-graph shaders must belong to this device and use the WorkGraph stage."));
                Info.mShaders.push_back(Shader->mNative);
            }
            for (const auto& LayoutRef : Desc.mGlobalBindingLayouts)
            {
                auto* Layout = Cast<FBindingLayout>(LayoutRef.Get());
                if (!Layout || !Owns(Layout))
                    return Failure<FArdaRHIWorkGraphPipelineRef>(
                        WrongDevice());
                Info.mBindingLayouts.push_back(Layout->mNative);
            }
            auto Native = mDevice->CreateWorkGraphPipeline(Info);
            if (!Native)
                return Failure<FArdaRHIWorkGraphPipelineRef>(
                    eastl::move(Native.mStatus));
            const uint64_t BackingSize =
                Native.mValue->GetWorkGraphBackingMemorySize();
            return {FArdaRHIWorkGraphPipelineRef(new FWorkGraphPipeline(
                Desc, eastl::move(Native.mValue), BackingSize,
                this, mLifetimeTracker)), {}};
        }

        TArdaRHIResult<FArdaRHISamplerFeedbackTextureRef>
        FArdaRHIDeviceImpl::CreateSamplerFeedbackTexture(
            const FArdaRHITextureRef& PairedTextureRef,
            const FArdaRHISamplerFeedbackTextureDesc& InputDesc)
        {
            if (GetCapabilities().mSamplerFeedbackTier ==
                EArdaRHISamplerFeedbackTier::None)
                return UnsupportedResult<FArdaRHISamplerFeedbackTextureRef>(
                    "Sampler feedback is unsupported by this device.");
            auto* PairedTexture = Cast<FTexture>(PairedTextureRef.Get());
            if (!PairedTexture || !Owns(PairedTexture))
                return Failure<FArdaRHISamplerFeedbackTextureRef>(
                    WrongDevice());
            if (PairedTexture->mDesc.mDimension !=
                    EArdaRHITextureDimension::Texture2D ||
                PairedTexture->mDesc.mSampleCount != 1)
                return Failure<FArdaRHISamplerFeedbackTextureRef>(Invalid(
                    "Sampler feedback requires a non-multisampled 2D paired texture."));
            FArdaRHISamplerFeedbackTextureDesc Desc = InputDesc;
            Desc.mMipRegionX = Desc.mMipRegionX ? Desc.mMipRegionX : 4u;
            Desc.mMipRegionY = Desc.mMipRegionY ? Desc.mMipRegionY : 4u;
            Desc.mMipRegionZ = Desc.mMipRegionZ ? Desc.mMipRegionZ : 1u;
            Desc.mInitialState = Desc.mInitialState ==
                    EArdaRHIResourceState::Unknown
                ? EArdaRHIResourceState::UnorderedAccess
                : Desc.mInitialState;
            auto Native = mDevice->CreateSamplerFeedbackTexture(
                PairedTexture->mNative, PairedTexture->mDesc, Desc);
            if (!Native)
                return Failure<FArdaRHISamplerFeedbackTextureRef>(
                    eastl::move(Native.mStatus));
            auto* Feedback = new FSamplerFeedbackTexture(
                Desc, PairedTextureRef, eastl::move(Native.mValue),
                this, mLifetimeTracker);
            Feedback->mFacadeState = Desc.mInitialState;
            Feedback->mbFacadeStateKnown = true;
            return {FArdaRHISamplerFeedbackTextureRef(Feedback), {}};
        }

        TArdaRHIResult<FArdaRHIRayTracingPipelineRef>
        FArdaRHIDeviceImpl::CreateRayTracingPipeline(
            const FArdaRHIRayTracingPipelineDesc& Desc)
        {
            if (!mDevice->GetCapabilities().mRayTracing.mbPipelineShaders)
                return UnsupportedResult<FArdaRHIRayTracingPipelineRef>(
                    "Ray tracing is unsupported by this device.");
            if (auto Status = Validate(Desc); !Status)
                return Failure<FArdaRHIRayTracingPipelineRef>(
                    eastl::move(Status));
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            if (auto Existing = mRayTracingPipelineCache.Find(Desc))
                return { Existing, {} };
            FArdaProviderRayTracingPipelineCreateInfo Info{ Desc };
            for (const auto& ShaderDesc : Desc.mShaders)
            {
                auto* Shader = Cast<FShader>(ShaderDesc.mShader.Get());
                if (!Shader || !Owns(Shader))
                    return Failure<FArdaRHIRayTracingPipelineRef>(
                        WrongDevice());
                FArdaProviderObjectRef LocalLayout;
                if (ShaderDesc.mLocalBindingLayout)
                {
                    auto* Layout = Cast<FBindingLayout>(
                        ShaderDesc.mLocalBindingLayout.Get());
                    if (!Layout || !Owns(Layout))
                        return Failure<FArdaRHIRayTracingPipelineRef>(
                            WrongDevice());
                    LocalLayout = Layout->mNative;
                }
                Info.mShaders.push_back({
                    ShaderDesc.mExportName,
                    Shader->mEntryPoint,
                    Shader->mNative,
                    eastl::move(LocalLayout) });
            }
            for (const auto& HitDesc : Desc.mHitGroups)
            {
                FArdaProviderRayTracingHitGroup Hit;
                Hit.mExportName = HitDesc.mExportName;
                Hit.mbProceduralPrimitive = HitDesc.mbProceduralPrimitive;
                const auto ResolveHitShader = [this, &HitDesc](
                    const FArdaRHIShaderRef& Ref,
                    const char* Suffix,
                    FArdaProviderRayTracingShader& Output) -> FArdaRHIStatus
                {
                    if (!Ref) return {};
                    auto* Shader = Cast<FShader>(Ref.Get());
                    if (!Shader || !Owns(Shader)) return WrongDevice();
                    Output.mExportName = HitDesc.mExportName;
                    Output.mExportName += Suffix;
                    Output.mEntryPoint = Shader->mEntryPoint;
                    Output.mShader = Shader->mNative;
                    return {};
                };
                if (auto Status = ResolveHitShader(
                        HitDesc.mClosestHitShader, ".closesthit",
                        Hit.mClosestHit); !Status)
                    return Failure<FArdaRHIRayTracingPipelineRef>(Status);
                if (auto Status = ResolveHitShader(
                        HitDesc.mAnyHitShader, ".anyhit",
                        Hit.mAnyHit); !Status)
                    return Failure<FArdaRHIRayTracingPipelineRef>(Status);
                if (auto Status = ResolveHitShader(
                        HitDesc.mIntersectionShader, ".intersection",
                        Hit.mIntersection); !Status)
                    return Failure<FArdaRHIRayTracingPipelineRef>(Status);
                if (HitDesc.mLocalBindingLayout)
                {
                    auto* Layout = Cast<FBindingLayout>(
                        HitDesc.mLocalBindingLayout.Get());
                    if (!Layout || !Owns(Layout))
                        return Failure<FArdaRHIRayTracingPipelineRef>(
                            WrongDevice());
                    Hit.mLocalBindingLayout = Layout->mNative;
                }
                Info.mHitGroups.push_back(eastl::move(Hit));
            }
            for (const auto& LayoutRef : Desc.mGlobalBindingLayouts)
            {
                auto* Layout = Cast<FBindingLayout>(LayoutRef.Get());
                if (!Layout || !Owns(Layout))
                    return Failure<FArdaRHIRayTracingPipelineRef>(
                        WrongDevice());
                Info.mGlobalBindingLayouts.push_back(Layout->mNative);
            }
            auto Native = mDevice->CreateRayTracingPipeline(Info);
            if (!Native)
                return Failure<FArdaRHIRayTracingPipelineRef>(
                    eastl::move(Native.mStatus));
            FArdaRHIRayTracingPipelineRef Result(new FRayTracingPipeline(
                Desc, eastl::move(Native.mValue), this, mLifetimeTracker));
            mRayTracingPipelineCache.Insert(Desc, Result);
            return { Result, {} };
        }

        namespace
        {
            TArdaRHIResult<eastl::vector<FArdaProviderRayTracingGeometry>>
            ResolveRayTracingGeometries(
                FArdaRHIDeviceImpl& Device,
                const eastl::vector<FArdaRHIRayTracingGeometryDesc>& Geometries)
            {
                eastl::vector<FArdaProviderRayTracingGeometry> Native;
                Native.reserve(Geometries.size());
                for (const auto& Geometry : Geometries)
                {
                    FArdaProviderRayTracingGeometry Resolved;
                    Resolved.mDesc = Geometry;
                    if (Geometry.mIndexBuffer)
                    {
                        auto* Buffer = Cast<FBuffer>(Geometry.mIndexBuffer.Get());
                        if (!Buffer || !Device.Owns(Buffer))
                            return Failure<eastl::vector<FArdaProviderRayTracingGeometry>>(
                                WrongDevice());
                        Resolved.mIndexBuffer = Buffer->mNative;
                    }
                    if (Geometry.mVertexOrAABBBuffer)
                    {
                        auto* Buffer = Cast<FBuffer>(
                            Geometry.mVertexOrAABBBuffer.Get());
                        if (!Buffer || !Device.Owns(Buffer))
                            return Failure<eastl::vector<FArdaProviderRayTracingGeometry>>(
                                WrongDevice());
                        Resolved.mVertexOrAABBBuffer = Buffer->mNative;
                    }
                    if (Geometry.mOpacityMicromapIndexBuffer)
                    {
                        auto* Buffer = Cast<FBuffer>(
                            Geometry.mOpacityMicromapIndexBuffer.Get());
                        if (!Buffer || !Device.Owns(Buffer))
                            return Failure<eastl::vector<FArdaProviderRayTracingGeometry>>(
                                WrongDevice());
                        Resolved.mOpacityMicromapIndexBuffer = Buffer->mNative;
                    }
                    if (Geometry.mOpacityMicromap)
                    {
                        auto* Micromap = Cast<FOpacityMicromap>(
                            Geometry.mOpacityMicromap.Get());
                        if (!Micromap || !Device.Owns(Micromap))
                            return Failure<eastl::vector<FArdaProviderRayTracingGeometry>>(
                                WrongDevice());
                        Resolved.mOpacityMicromap = Micromap->mNative;
                    }
                    // Facade references are intentionally stripped at the
                    // provider-neutral/native boundary.
                    Resolved.mDesc.mIndexBuffer = {};
                    Resolved.mDesc.mVertexOrAABBBuffer = {};
                    Resolved.mDesc.mOpacityMicromap = {};
                    Resolved.mDesc.mOpacityMicromapIndexBuffer = {};
                    Native.push_back(eastl::move(Resolved));
                }
                return {eastl::move(Native), {}};
            }
        }

        TArdaRHIResult<FArdaRHIAccelStructMemoryRequirements>
        FArdaRHIDeviceImpl::GetAccelStructBuildMemoryRequirements(
            const FArdaRHIAccelStructDesc& Desc)
        {
            if (!mDevice->GetCapabilities().mRayTracing.mbAccelerationStructures)
                return UnsupportedResult<FArdaRHIAccelStructMemoryRequirements>(
                    "Acceleration structures are unsupported by this device.");
            if (Desc.mbTopLevel == !Desc.mBottomLevelGeometries.empty())
                return Failure<FArdaRHIAccelStructMemoryRequirements>(Invalid(
                    "An acceleration structure must be either TLAS or BLAS."));
            if (Desc.mbTopLevel && !Desc.mTopLevelMaxInstances)
                return Failure<FArdaRHIAccelStructMemoryRequirements>(Invalid(
                    "A TLAS requires a non-zero maximum instance count."));
            auto Geometries = ResolveRayTracingGeometries(
                *this, Desc.mBottomLevelGeometries);
            if (!Geometries)
                return Failure<FArdaRHIAccelStructMemoryRequirements>(
                    eastl::move(Geometries.mStatus));
            return mDevice->GetAccelStructBuildMemoryRequirements(
                Desc, Geometries.mValue);
        }

        TArdaRHIResult<FArdaRHIAccelStructRef>
        FArdaRHIDeviceImpl::CreateAccelStruct(
            const FArdaRHIAccelStructDesc& Desc)
        {
            auto Requirements = GetAccelStructBuildMemoryRequirements(Desc);
            if (!Requirements)
                return Failure<FArdaRHIAccelStructRef>(
                    eastl::move(Requirements.mStatus));
            auto Native = mDevice->CreateAccelStruct(Desc, Requirements.mValue);
            if (!Native)
                return Failure<FArdaRHIAccelStructRef>(
                    eastl::move(Native.mStatus));
            const uint64_t Address = mDevice->GetAccelStructDeviceAddress(
                Native.mValue);
            if (!Address && !Desc.mbVirtual)
                return Failure<FArdaRHIAccelStructRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure,
                    "The native acceleration structure has no device address."));
            return {FArdaRHIAccelStructRef(new FAccelStruct(
                Desc, Requirements.mValue, eastl::move(Native.mValue),
                Address, this, mLifetimeTracker)), {}};
        }

        TArdaRHIResult<FArdaRHIOpacityMicromapRef>
        FArdaRHIDeviceImpl::CreateOpacityMicromap(
            const FArdaRHIOpacityMicromapDesc& Desc)
        {
            if (!mDevice->GetCapabilities().mRayTracing.mbOpacityMicromaps)
                return UnsupportedResult<FArdaRHIOpacityMicromapRef>(
                    "Opacity micromaps are unsupported by this device.");
            if (Desc.mCounts.empty() || !Desc.mInputBuffer ||
                !Desc.mPerMicromapDescBuffer)
                return Failure<FArdaRHIOpacityMicromapRef>(Invalid(
                    "An opacity micromap requires usage counts, encoded data, and per-micromap triangle descriptors."));
            if (!Desc.mbTrackLiveness && !Desc.mbAllowUnsafeLivenessOptOut)
                return Failure<FArdaRHIOpacityMicromapRef>(Invalid(
                    "Disabling opacity-micromap liveness tracking requires an explicit unsafe opt-out."));
            for (const auto& Count : Desc.mCounts)
                if (!Count.mCount)
                    return Failure<FArdaRHIOpacityMicromapRef>(Invalid(
                        "Opacity-micromap usage counts must be non-zero."));
            auto* Input = Cast<FBuffer>(Desc.mInputBuffer.Get());
            auto* Triangles = Cast<FBuffer>(Desc.mPerMicromapDescBuffer.Get());
            if (!Input || !Triangles || !Owns(Input) || !Owns(Triangles))
                return Failure<FArdaRHIOpacityMicromapRef>(WrongDevice());
            if (Desc.mInputBufferOffset >= Input->mDesc.mByteSize ||
                Desc.mPerMicromapDescBufferOffset >= Triangles->mDesc.mByteSize ||
                Desc.mInputBufferOffset % 256u ||
                Desc.mPerMicromapDescBufferOffset % 256u)
                return Failure<FArdaRHIOpacityMicromapRef>(Invalid(
                    "Opacity-micromap input offsets must be in range and 256-byte aligned."));
            if (!HasAnyFlags(Input->mDesc.mUsage,
                    EArdaRHIBufferUsage::OpacityMicromapBuildInput) ||
                !HasAnyFlags(Triangles->mDesc.mUsage,
                    EArdaRHIBufferUsage::OpacityMicromapBuildInput))
                return Failure<FArdaRHIOpacityMicromapRef>(Invalid(
                    "Opacity-micromap inputs require OpacityMicromapBuildInput buffer usage."));
            FArdaRHIOpacityMicromapDesc NativeDesc = Desc;
            NativeDesc.mInputBuffer = {};
            NativeDesc.mPerMicromapDescBuffer = {};
            auto Native = mDevice->CreateOpacityMicromap(
                NativeDesc, Input->mNative, Triangles->mNative);
            if (!Native)
                return Failure<FArdaRHIOpacityMicromapRef>(
                    eastl::move(Native.mStatus));
            const uint64_t Address = mDevice->GetOpacityMicromapDeviceAddress(
                Native.mValue);
            if (!Address)
                return Failure<FArdaRHIOpacityMicromapRef>(
                    FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
                        "The native opacity micromap has no storage address."));
            return {FArdaRHIOpacityMicromapRef(new FOpacityMicromap(
                Desc, eastl::move(Native.mValue), Address,
                this, mLifetimeTracker)), {}};
        }

        TArdaRHIResult<uint64_t>
        FArdaRHIDeviceImpl::GetAccelStructCompactedSize(
            const FArdaRHIAccelStructRef& Ref)
        {
            auto* AccelStruct = Cast<FAccelStruct>(Ref.Get());
            if (!AccelStruct || !Owns(AccelStruct))
                return Failure<uint64_t>(WrongDevice());
            if (!HasAnyFlags(AccelStruct->mDesc.mBuildFlags,
                    EArdaRHIAccelStructBuildFlags::AllowCompaction))
                return Failure<uint64_t>(Invalid(
                    "Compacted size requires AllowCompaction at creation."));
            if (AccelStruct->GetBuildState() ==
                EArdaRHIAccelStructBuildState::Unbuilt)
                return Failure<uint64_t>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "Compacted size is unavailable before a successful build."));
            return mDevice->GetAccelStructCompactedSize(AccelStruct->mNative);
        }

        TArdaRHIResult<uint64_t>
        FArdaRHIDeviceImpl::GetOpacityMicromapCompactedSize(
            const FArdaRHIOpacityMicromapRef& Ref)
        {
            auto* Micromap = Cast<FOpacityMicromap>(Ref.Get());
            if (!Micromap || !Owns(Micromap))
                return Failure<uint64_t>(WrongDevice());
            if (!HasAnyFlags(Micromap->mDesc.mFlags,
                    EArdaRHIOpacityMicromapBuildFlags::AllowCompaction))
                return Failure<uint64_t>(Invalid(
                    "Compacted size requires AllowCompaction at creation."));
            if (Micromap->GetBuildState() ==
                EArdaRHIAccelStructBuildState::Unbuilt)
                return Failure<uint64_t>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "Compacted size is unavailable before a successful micromap build."));
            return mDevice->GetOpacityMicromapCompactedSize(
                Micromap->mNative);
        }

        TArdaRHIResult<FArdaRHIShaderBundleRef>
        FArdaRHIDeviceImpl::CreateShaderBundle(
            const FArdaRHIShaderBundleDesc& Desc)
        {
            if (!GetCapabilities().mbShaderBundleDispatch)
                return UnsupportedResult<FArdaRHIShaderBundleRef>(
                    "Shader bundles are unsupported by this device.");
            if (!Desc.mMaxRecords)
                return Failure<FArdaRHIShaderBundleRef>(Invalid(
                    "A shader bundle requires a non-zero record capacity."));
            return {FArdaRHIShaderBundleRef(new FShaderBundle(
                Desc, this, mLifetimeTracker)), {}};
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::SetShaderBundleRecords(
            const FArdaRHIShaderBundleRef& BundleRef,
            const eastl::vector<FArdaRHIShaderBundleRecord>& Records)
        {
            auto* Bundle = Cast<FShaderBundle>(BundleRef.Get());
            if (!Bundle || !Owns(Bundle)) return WrongDevice();
            if (Records.size() > Bundle->mDesc.mMaxRecords)
                return Invalid(
                    "Shader-bundle records exceed the bundle capacity.");
            for (const auto& Record : Records)
            {
                const bool bCompute = static_cast<bool>(
                    Record.mComputePipeline);
                const bool bMesh = static_cast<bool>(Record.mMeshPipeline);
                if (bCompute == bMesh ||
                    bMesh != Bundle->mDesc.mbMeshRecords)
                    return Invalid(
                        "Each shader-bundle record must match its compute or mesh bundle type.");
                auto* Pipeline = bCompute
                    ? Cast<FResource>(Record.mComputePipeline.Get())
                    : Cast<FResource>(Record.mMeshPipeline.Get());
                if (!Pipeline || !Owns(Pipeline)) return WrongDevice();
                for (const auto& Binding : Record.mBindings)
                {
                    auto* Set = Cast<FResource>(Binding.Get());
                    if (!Set || !Owns(Set)) return WrongDevice();
                }
                if (!Record.mGroupsX || !Record.mGroupsY ||
                    !Record.mGroupsZ)
                    return Invalid(
                        "Shader-bundle dispatch dimensions must be non-zero.");
            }
            std::lock_guard<std::mutex> Lock(Bundle->mMutex);
            Bundle->mRecords = Records;
            return {};
        }

        TArdaRHIResult<FArdaRHIMemoryRequirements>
        FArdaRHIDeviceImpl::GetAccelStructMemoryRequirements(
            const FArdaRHIAccelStructRef& Ref)
        {
            auto* AccelStruct = Cast<FAccelStruct>(Ref.Get());
            if (!AccelStruct || !Owns(AccelStruct))
                return Failure<FArdaRHIMemoryRequirements>(WrongDevice());
            FArdaRHIMemoryRequirements Result;
            Result.mSize = AccelStruct->mRequirements.mResultSize;
            Result.mAlignment = AccelStruct->mRequirements.mResultAlignment;
            return {Result, {}};
        }

        TArdaRHIResult<FArdaRHIShaderTableRef>
        FArdaRHIDeviceImpl::CreateShaderTable(
            const FArdaRHIRayTracingPipelineRef& PipelineRef,
            const FArdaRHIShaderTableDesc& Desc)
        {
            auto* Pipeline = Cast<FRayTracingPipeline>(PipelineRef.Get());
            if (!Pipeline || !Owns(Pipeline))
                return Failure<FArdaRHIShaderTableRef>(WrongDevice());
            if (!Desc.mMaxEntries)
                return Failure<FArdaRHIShaderTableRef>(Invalid(
                    "A shader table requires a non-zero entry capacity."));
            auto Native = mDevice->CreateShaderTable(
                Pipeline->mNative, Desc);
            if (!Native)
                return Failure<FArdaRHIShaderTableRef>(
                    eastl::move(Native.mStatus));
            return { FArdaRHIShaderTableRef(new FShaderTable(
                Desc,
                PipelineRef,
                eastl::move(Native.mValue),
                this,
                mLifetimeTracker)), {} };
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::SetShaderTableRecord(
            const FArdaRHIShaderTableRef& TableRef,
            const FArdaRHIShaderTableRecordDesc& Record)
        {
            auto* Table = Cast<FShaderTable>(TableRef.Get());
            if (!Table || !Owns(Table)) return WrongDevice();
            if (Record.mRecordIndex >= Table->mDesc.mMaxEntries)
                return Invalid("Shader-table record index exceeds capacity.");
            if (Record.mExportName.empty())
                return Invalid("A shader-table record requires an export name.");
            if (Record.mLocalArguments.size() >
                Table->mDesc.mMaxLocalArgumentBytes)
                return Invalid(
                    "Shader-table local arguments exceed the declared maximum.");
            auto* Bindings = Cast<FBindingSet>(Record.mBindings.Get());
            if (Record.mBindings && (!Bindings || !Owns(Bindings)))
                return WrongDevice();
            auto* Geometry = Cast<FAccelStruct>(Record.mGeometry.Get());
            if (Record.mGeometry && (!Geometry || !Owns(Geometry)))
                return WrongDevice();
            std::lock_guard<std::mutex> Lock(Table->mMutex);
            const FArdaRHIStatus Status = mDevice->SetShaderTableRecord(
                Table->mNative, Record,
                Bindings ? Bindings->mNative : FArdaProviderObjectRef{},
                Geometry ? Geometry->mNative : FArdaProviderObjectRef{});
            if (Status && !Table->mWrittenRecords[Record.mRecordIndex])
            {
                Table->mWrittenRecords[Record.mRecordIndex] = true;
                ++Table->mEntryCount;
            }
            if (Status && Record.mType ==
                EArdaRHIShaderTableRecordType::RayGeneration)
                Table->mbHasRayGeneration = true;
            return Status;
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::CommitShaderTable(
            const FArdaRHIShaderTableRef& TableRef)
        {
            auto* Table = Cast<FShaderTable>(TableRef.Get());
            if (!Table || !Owns(Table)) return WrongDevice();
            std::lock_guard<std::mutex> Lock(Table->mMutex);
            if (!Table->mbHasRayGeneration)
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
                    "A shader table requires a ray-generation record before commit.");
            return mDevice->CommitShaderTable(Table->mNative);
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::SetShaderTableRayGeneration(
            const FArdaRHIShaderTableRef& TableRef,
            const char* ExportName,
            const FArdaRHIBindingSetRef& LocalBindings)
        {
            auto* Table = Cast<FShaderTable>(TableRef.Get());
            auto* Set = Cast<FBindingSet>(LocalBindings.Get());
            if (!Table || !Owns(Table) ||
                (LocalBindings && (!Set || !Owns(Set))))
                return WrongDevice();
            if (!ExportName || !*ExportName)
                return Invalid("A ray-generation export name is required.");
            std::lock_guard<std::mutex> Lock(Table->mMutex);
            const auto Status = mDevice->SetShaderTableRayGeneration(
                Table->mNative,
                ExportName,
                Set ? Set->mNative : FArdaProviderObjectRef{});
            if (Status && !Table->mbHasRayGeneration)
            {
                Table->mbHasRayGeneration = true;
                ++Table->mEntryCount;
            }
            return Status;
        }

        namespace
        {
            TArdaRHIResult<int> AddShaderTableEntry(
                FArdaRHIDeviceImpl& Device,
                const FArdaRHIShaderTableRef& TableRef,
                const char* ExportName,
                const FArdaRHIBindingSetRef& LocalBindings,
                uint32_t Category)
            {
                auto* Table = Cast<FShaderTable>(TableRef.Get());
                auto* Set = Cast<FBindingSet>(LocalBindings.Get());
                if (!Table || !Device.Owns(Table) ||
                    (LocalBindings && (!Set || !Device.Owns(Set))))
                    return Failure<int>(WrongDevice());
                if (!ExportName || !*ExportName)
                    return Failure<int>(Invalid(
                        "A shader-table export name is required."));
                std::lock_guard<std::mutex> Lock(Table->mMutex);
                if (Table->mEntryCount >= Table->mDesc.mMaxEntries)
                    return Failure<int>(Invalid(
                        "The shader table is at capacity."));
                const auto Status = Device.GetProviderDevice().AddShaderTableEntry(
                    Table->mNative,
                    ExportName,
                    Set ? Set->mNative : FArdaProviderObjectRef{},
                    Category);
                if (!Status) return Failure<int>(Status);
                return { static_cast<int>(Table->mEntryCount++), {} };
            }
        }

        TArdaRHIResult<int> FArdaRHIDeviceImpl::AddShaderTableMiss(
            const FArdaRHIShaderTableRef& Table,
            const char* ExportName,
            const FArdaRHIBindingSetRef& LocalBindings)
        {
            return AddShaderTableEntry(
                *this, Table, ExportName, LocalBindings, 0);
        }

        TArdaRHIResult<int> FArdaRHIDeviceImpl::AddShaderTableHitGroup(
            const FArdaRHIShaderTableRef& Table,
            const char* ExportName,
            const FArdaRHIBindingSetRef& LocalBindings)
        {
            return AddShaderTableEntry(
                *this, Table, ExportName, LocalBindings, 1);
        }

        TArdaRHIResult<int> FArdaRHIDeviceImpl::AddShaderTableCallable(
            const FArdaRHIShaderTableRef& Table,
            const char* ExportName,
            const FArdaRHIBindingSetRef& LocalBindings)
        {
            return AddShaderTableEntry(
                *this, Table, ExportName, LocalBindings, 2);
        }

        TArdaRHIResult<FArdaRHIRasterStateRef> FArdaRHIDeviceImpl::CreateRasterState(
            const FArdaRHIRasterState& Desc)
        {
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            if (auto Existing = mRasterStateCache.Find(Desc)) return { Existing, {} };
            FArdaRHIRasterStateRef Result(new FRasterState(
                Desc, this, mLifetimeTracker));
            mRasterStateCache.Insert(Desc, Result);
            return { Result, {} };
        }

        TArdaRHIResult<FArdaRHIBlendStateRef> FArdaRHIDeviceImpl::CreateBlendState(
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
        FArdaRHIDeviceImpl::CreateDepthStencilState(const FArdaRHIDepthStencilState& Desc)
        {
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            if (auto Existing = mDepthStateCache.Find(Desc)) return { Existing, {} };
            FArdaRHIDepthStencilStateRef Result(new FDepthStencilState(
                Desc, this, mLifetimeTracker));
            mDepthStateCache.Insert(Desc, Result);
            return { Result, {} };
        }

        TArdaRHIResult<FArdaRHIEventQueryRef> FArdaRHIDeviceImpl::CreateEventQuery()
        {
            return { FArdaRHIEventQueryRef(new FEventQuery(
                "EventQuery", this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHITimerQueryRef> FArdaRHIDeviceImpl::CreateTimerQuery()
        {
            return { FArdaRHITimerQueryRef(new FTimerQuery(
                "TimerQuery", this, mLifetimeTracker)), {} };
        }

        TArdaRHIResult<FArdaRHIGpuFenceRef> FArdaRHIDeviceImpl::CreateGpuFence()
        {
            return { FArdaRHIGpuFenceRef(new FGpuFence(
                "GpuFence", this, mLifetimeTracker)), {} };
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::SignalEventQuery(
            const FArdaRHIEventQueryRef& Query, EArdaRHIQueueType)
        {
            auto* Native = Cast<FEventQuery>(Query.Get());
            if (!Native || !Owns(Native)) return WrongDevice();
            if (auto Status = mDevice->WaitForIdle(); !Status) return Status;
            Native->mbSignaled.store(true, std::memory_order_release);
            return {};
        }

        TArdaRHIResult<bool> FArdaRHIDeviceImpl::PollEventQuery(
            const FArdaRHIEventQueryRef& Query)
        {
            auto* Native = Cast<FEventQuery>(Query.Get());
            if (!Native || !Owns(Native)) return Failure<bool>(WrongDevice());
            return { Native->mbSignaled.load(std::memory_order_acquire), {} };
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::WaitEventQuery(const FArdaRHIEventQueryRef& Query)
        {
            auto Poll = PollEventQuery(Query);
            if (!Poll) return Poll.mStatus;
            return Poll.mValue ? FArdaRHIStatus{} : mDevice->WaitForIdle();
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::ResetEventQuery(const FArdaRHIEventQueryRef& Query)
        {
            auto* Native = Cast<FEventQuery>(Query.Get());
            if (!Native || !Owns(Native)) return WrongDevice();
            Native->mbSignaled.store(false, std::memory_order_release);
            return {};
        }

        TArdaRHIResult<bool> FArdaRHIDeviceImpl::PollTimerQuery(
            const FArdaRHITimerQueryRef& Query)
        {
            auto* Native = Cast<FTimerQuery>(Query.Get());
            if (!Native || !Owns(Native)) return Failure<bool>(WrongDevice());
            return { Native->mbSignaled.load(std::memory_order_acquire), {} };
        }

        TArdaRHIResult<float> FArdaRHIDeviceImpl::GetTimerQuerySeconds(
            const FArdaRHITimerQueryRef& Query)
        {
            auto Poll = PollTimerQuery(Query);
            if (!Poll) return Failure<float>(eastl::move(Poll.mStatus));
            if (!Poll.mValue) return Failure<float>(FArdaRHIStatus::Error(
                EArdaRHIResult::InvalidState, "Timer query has not completed."));
            return { 0.f, {} };
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::ResetTimerQuery(const FArdaRHITimerQueryRef& Query)
        {
            auto* Native = Cast<FTimerQuery>(Query.Get());
            if (!Native || !Owns(Native)) return WrongDevice();
            Native->mbBegun.store(false, std::memory_order_release);
            Native->mbSignaled.store(false, std::memory_order_release);
            return {};
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::SignalGpuFence(
            const FArdaRHIGpuFenceRef& Fence, EArdaRHIQueueType)
        {
            auto* Native = Cast<FGpuFence>(Fence.Get());
            if (!Native || !Owns(Native)) return WrongDevice();
            if (auto Status = mDevice->WaitForIdle(); !Status) return Status;
            Native->mbSignaled.store(true, std::memory_order_release);
            return {};
        }

        TArdaRHIResult<bool> FArdaRHIDeviceImpl::PollGpuFence(const FArdaRHIGpuFenceRef& Fence)
        {
            auto* Native = Cast<FGpuFence>(Fence.Get());
            if (!Native || !Owns(Native)) return Failure<bool>(WrongDevice());
            return { Native->mbSignaled.load(std::memory_order_acquire), {} };
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::WaitGpuFence(const FArdaRHIGpuFenceRef& Fence)
        {
            auto Poll = PollGpuFence(Fence);
            if (!Poll) return Poll.mStatus;
            return Poll.mValue ? FArdaRHIStatus{} : mDevice->WaitForIdle();
        }

        FArdaRHIStatus FArdaRHIDeviceImpl::ResetGpuFence(const FArdaRHIGpuFenceRef& Fence)
        {
            auto* Native = Cast<FGpuFence>(Fence.Get());
            if (!Native || !Owns(Native)) return WrongDevice();
            Native->mbSignaled.store(false, std::memory_order_release);
            return {};
        }

        TArdaRHIResult<FArdaRHICommandListRef> FArdaRHIDeviceImpl::CreateCommandList(
            EArdaRHIQueueType Queue,
            bool bImmediateExecution)
        {
            auto Native = mDevice->CreateCommandList(Queue, bImmediateExecution);
            if (!Native)
                return Failure<FArdaRHICommandListRef>(eastl::move(Native.mStatus));
            return { FArdaRHICommandListRef(new FCommandList(
                this, Queue, eastl::move(Native.mValue), mLifetimeTracker)), {} };
        }

        TArdaRHIResult<uint64_t> FArdaRHIDeviceImpl::FinishCommandListSubmission(
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
            auto ProviderDevice = mDevice;
            const uint64_t Submission = Submitted.mValue;
            const FArdaRHIStatus SubmissionStatus = Submitted.mStatus;
            const auto Complete = [ProviderDevice, Submission, SubmissionStatus](
                eastl::vector<FPendingBufferCopyCompletion> Pending)
            {
                FArdaRHIStatus WaitStatus = SubmissionStatus;
                if (WaitStatus && Submission != 0)
                    WaitStatus = ProviderDevice->WaitForSubmission(Submission);
                FArdaRHIStatus FirstError = WaitStatus;

                for (auto& Completion : Pending)
                {
                    FArdaRHIStatus CopyStatus = WaitStatus;
                    FArdaRHIBufferReadbackResult ReadbackResult;
                    ReadbackResult.mStatus = CopyStatus;
                    if (CopyStatus && Completion.mReadbackBuffer)
                    {
                        auto Mapping = ProviderDevice->MapBuffer(
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
                            ProviderDevice->UnmapBuffer(
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

        TArdaRHIResult<uint64_t> FArdaRHIDeviceImpl::ExecuteCommandList(
            const FArdaRHICommandListRef& CommandList)
        {
            auto* Native = Cast<FCommandList>(CommandList.Get());
            if (!Native || !Owns(Native)) return Failure<uint64_t>(WrongDevice());
            if (const FArdaRHIStatus Status =
                    Native->ValidateFacadeStartStates();
                !Status)
            {
                return Failure<uint64_t>(Status);
            }
            auto Submitted = mDevice->ExecuteCommandList(
                Native->GetNative(), Native->GetQueueType());
            if (Submitted)
                Native->CommitFacadeStates();
            return FinishCommandListSubmission(
                *Native, eastl::move(Submitted));
        }

        TArdaRHIResult<uint64_t> FArdaRHIDeviceImpl::ExecuteCommandLists(
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
                if (const FArdaRHIStatus Status =
                        Native->ValidateFacadeStartStates();
                    !Status)
                {
                    return Failure<uint64_t>(Status);
                }
                auto Submitted = mDevice->ExecuteCommandList(Native->GetNative(), Queue);
                if (Submitted)
                    Native->CommitFacadeStates();
                Submitted = FinishCommandListSubmission(
                    *Native, eastl::move(Submitted));
                if (!Submitted) return Submitted;
                Last = Submitted.mValue;
            }
            return { Last, {} };
        }

        void FArdaRHIDeviceImpl::TrimDescriptorCaches()
        {
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            mSamplerCache.Clear();
            mBindingLayoutCache.Clear();
            mInputLayoutCache.Clear();
            mRasterStateCache.Clear();
            mBlendStateCache.Clear();
            mDepthStateCache.Clear();
            mRayTracingPipelineCache.Clear();
            mTextureImportCache.Clear();
            mBufferImportCache.Clear();
        }

        FArdaRHICacheStats FArdaRHIDeviceImpl::GetDescriptorCacheStats() const noexcept
        {
            std::lock_guard<std::mutex> Lock(mCacheMutex);
            FArdaRHICacheStats Stats;
            Stats.mSamplers = mSamplerCache.Size();
            Stats.mBindingLayouts = mBindingLayoutCache.Size();
            Stats.mInputLayouts = mInputLayoutCache.Size();
            Stats.mRasterStates = mRasterStateCache.Size();
            Stats.mBlendStates = mBlendStateCache.Size();
            Stats.mDepthStencilStates = mDepthStateCache.Size();
            Stats.mRayTracingPipelines = mRayTracingPipelineCache.Size();
            return Stats;
        }

        FArdaRHIResourceLifetimeStats
        FArdaRHIDeviceImpl::GetResourceLifetimeStats() const noexcept
        {
            FArdaRHIResourceLifetimeStats Stats;
            for (size_t Index = 0;
                 Index < static_cast<size_t>(EArdaRHIResourceType::Count);
                 ++Index)
            {
                Stats.mLiveResources[Index] = mLifetimeTracker->Get(
                    static_cast<EArdaRHIResourceType>(Index));
            }
            const FArdaProviderLifetimeStats Native = mDevice->GetLifetimeStats();
            Stats.mResourceDescriptors = Native.mResourceDescriptors;
            Stats.mSamplerDescriptors = Native.mSamplerDescriptors;
            Stats.mDescriptorSets = Native.mDescriptorSets;
            Stats.mPendingSubmissions = Native.mPendingSubmissions;
            return Stats;
        }

        void FArdaRHIDeviceImpl::FlushAndDisablePipelineCachePersistence() noexcept
        {
            if (!mbPipelineCacheDetached && mDevice)
            {
                mDevice->FlushPipelineCache();
                mbPipelineCacheDetached = true;
            }
        }

        FCommandList::FCommandList(
            FArdaRHIDeviceImpl* Device,
            EArdaRHIQueueType Queue,
            eastl::unique_ptr<IArdaProviderCommandList> Native,
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

        FArdaRHIStatus FCommandList::Open()
        {
            mCopyCompletions.clear();
            mFacadeTextureStates.clear();
            mFacadeBufferStates.clear();
            mFacadeSamplerFeedbackStates.clear();
            mFacadeTextureQueueOwners.clear();
            mFacadeBufferQueueOwners.clear();
            mFacadeAccelStructStates.clear();
            mFacadeOpacityMicromapStates.clear();
            mExpectedTextureStartStates.clear();
            mExpectedBufferStartStates.clear();
            return mNative->Open();
        }

        eastl::vector<EArdaRHIResourceState>&
            FCommandList::GetFacadeTextureStates(FTexture& Texture) const
        {
            auto Existing = mFacadeTextureStates.find(&Texture);
            if (Existing != mFacadeTextureStates.end())
                return Existing->second;
            eastl::vector<EArdaRHIResourceState> States;
            {
                std::lock_guard<std::mutex> Lock(Texture.mFacadeStateMutex);
                States = Texture.mFacadeStates;
            }
            if (States.empty())
            {
                States.assign(
                    static_cast<size_t>(Texture.mDesc.mMipLevels) *
                        Texture.mDesc.mArraySize *
                        GetArdaRHIFormatPlaneCount(Texture.mDesc.mFormat),
                    Texture.mDesc.mInitialState);
            }
            return mFacadeTextureStates.emplace(
                &Texture, eastl::move(States)).first->second;
        }

        void FCommandList::CommitFacadeStates()
        {
            for (auto& Entry : mFacadeTextureStates)
            {
                std::lock_guard<std::mutex> Lock(
                    Entry.first->mFacadeStateMutex);
                Entry.first->mFacadeStates = Entry.second;
            }
            for (const auto& Entry : mFacadeTextureQueueOwners)
            {
                std::lock_guard<std::mutex> Lock(
                    Entry.first->mFacadeStateMutex);
                Entry.first->mFacadeQueueOwner = Entry.second;
                Entry.first->mbFacadeQueueOwnerKnown = true;
            }
            for (const auto& Entry : mFacadeBufferStates)
            {
                std::lock_guard<std::mutex> Lock(
                    Entry.first->mFacadeStateMutex);
                Entry.first->mFacadeState = Entry.second;
                Entry.first->mbFacadeStateKnown = true;
            }
            for (const auto& Entry : mFacadeBufferQueueOwners)
            {
                std::lock_guard<std::mutex> Lock(
                    Entry.first->mFacadeStateMutex);
                Entry.first->mFacadeQueueOwner = Entry.second;
                Entry.first->mbFacadeQueueOwnerKnown = true;
            }
            for (const auto& Entry : mFacadeAccelStructStates)
            {
                std::lock_guard<std::mutex> Lock(Entry.first->mStateMutex);
                Entry.first->mFacadeState = Entry.second.mState;
                Entry.first->mBuildState = Entry.second.mBuildState;
            }
            for (const auto& Entry : mFacadeOpacityMicromapStates)
            {
                std::lock_guard<std::mutex> Lock(Entry.first->mStateMutex);
                Entry.first->mFacadeState = Entry.second.mState;
                Entry.first->mBuildState = Entry.second.mBuildState;
            }
            for (const auto& Entry : mFacadeSamplerFeedbackStates)
            {
                std::lock_guard<std::mutex> Lock(Entry.first->mStateMutex);
                Entry.first->mFacadeState = Entry.second;
                Entry.first->mbFacadeStateKnown = true;
            }
        }

        FArdaRHIStatus FCommandList::ValidateFacadeStartStates() const
        {
            for (const auto& Entry : mExpectedTextureStartStates)
            {
                FTexture* Texture = Entry.first;
                eastl::vector<EArdaRHIResourceState> Submitted;
                {
                    std::lock_guard<std::mutex> Lock(
                        Texture->mFacadeStateMutex);
                    Submitted = Texture->mFacadeStates;
                }
                if (Submitted.empty())
                {
                    Submitted.assign(
                        static_cast<size_t>(Texture->mDesc.mMipLevels) *
                            Texture->mDesc.mArraySize *
                            GetArdaRHIFormatPlaneCount(Texture->mDesc.mFormat),
                        Texture->mDesc.mInitialState);
                }
                for (size_t Index = 0; Index < Entry.second.size(); ++Index)
                {
                    if (Entry.second[Index] !=
                            EArdaRHIResourceState::Unknown &&
                        Entry.second[Index] != Submitted[Index])
                    {
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidState,
                            "Facade texture start state differs at submission.");
                    }
                }
            }
            for (const auto& Entry : mExpectedBufferStartStates)
            {
                EArdaRHIResourceState Submitted =
                    Entry.first->mDesc.mInitialState;
                {
                    std::lock_guard<std::mutex> Lock(
                        Entry.first->mFacadeStateMutex);
                    if (Entry.first->mbFacadeStateKnown)
                        Submitted = Entry.first->mFacadeState;
                }
                if (Submitted != Entry.second)
                {
                    return FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidState,
                        "Facade buffer start state differs at submission.");
                }
            }
            return {};
        }

        FArdaRHIStatus FCommandList::Reset()
        {
            mCopyCompletions.clear();
            mFacadeTextureStates.clear();
            mFacadeBufferStates.clear();
            mFacadeSamplerFeedbackStates.clear();
            mFacadeTextureQueueOwners.clear();
            mFacadeBufferQueueOwners.clear();
            mFacadeAccelStructStates.clear();
            mFacadeOpacityMicromapStates.clear();
            mExpectedTextureStartStates.clear();
            mExpectedBufferStartStates.clear();
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
            auto Readback = mDevice->GetProviderDevice().CreateBuffer(ReadbackDesc);
            if (!Readback) return eastl::move(Readback.mStatus);
            if (auto Status = SetBufferState(
                    Source, EArdaRHIResourceState::CopySource);
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
            auto Readback = mDevice->GetProviderDevice().CreateBuffer(ReadbackDesc);
            if (!Readback) return eastl::move(Readback.mStatus);
            if (auto Status = SetBufferState(
                    Source, EArdaRHIResourceState::CopySource);
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

        FArdaRHIStatus FCommandList::CopyTexture(
            IArdaRHITexture& Destination,
            const FArdaRHITextureSlice& DestinationSlice,
            IArdaRHITexture& Source,
            const FArdaRHITextureSlice& SourceSlice)
        {
            auto* Dst = Cast<FTexture>(&Destination);
            auto* Src = Cast<FTexture>(&Source);
            if (!Dst || !Src || !Owns(Dst) || !Owns(Src))
                return WrongDevice();
            FArdaRHITextureCopyExtent Extent;
            if (auto Status = ResolveArdaRHITextureCopyExtent(
                    Dst->mDesc, DestinationSlice,
                    Src->mDesc, SourceSlice, Extent);
                !Status)
                return Status;
            if (Dst == Src &&
                DestinationSlice.mMipLevel == SourceSlice.mMipLevel &&
                DestinationSlice.mArraySlice == SourceSlice.mArraySlice &&
                DestinationSlice.mPlane == SourceSlice.mPlane)
            {
                return Invalid(
                    "A texture subresource cannot be copied onto itself.");
            }
            return mNative->CopyTexture(
                Dst->mNative, Dst->mDesc, DestinationSlice,
                Src->mNative, Src->mDesc, SourceSlice);
        }

        FArdaRHIStatus FCommandList::ResolveTexture(
            IArdaRHITexture& Destination,
            const FArdaRHITextureSlice& DestinationSlice,
            IArdaRHITexture& Source,
            const FArdaRHITextureSlice& SourceSlice)
        {
            auto* Dst = Cast<FTexture>(&Destination);
            auto* Src = Cast<FTexture>(&Source);
            if (!Dst || !Src || !Owns(Dst) || !Owns(Src))
                return WrongDevice();
            FArdaRHITextureCopyExtent Extent;
            if (auto Status = ValidateArdaRHITextureResolve(
                    Dst->mDesc, DestinationSlice,
                    Src->mDesc, SourceSlice, Extent); !Status)
                return Status;
            return mNative->ResolveTexture(
                Dst->mNative, Dst->mDesc, DestinationSlice,
                Src->mNative, Src->mDesc, SourceSlice);
        }

        FArdaRHIStatus FCommandList::CopyTextureToStaging(
            IArdaRHIStagingTexture& Destination,
            const FArdaRHITextureSlice& DestinationSlice,
            IArdaRHITexture& Source,
            const FArdaRHITextureSlice& SourceSlice)
        {
            auto* Dst = Cast<FStagingTexture>(&Destination);
            auto* Src = Cast<FTexture>(&Source);
            if (!Dst || !Src || !Owns(Dst) || !Owns(Src))
                return WrongDevice();
            if (Dst->mDesc.mCpuAccess != EArdaRHICpuAccess::Read)
                return Invalid("A texture readback requires a read staging texture.");
            FArdaRHITextureCopyExtent Extent;
            if (auto Status = ResolveArdaRHITextureCopyExtent(
                    Dst->mDesc.mTexture, DestinationSlice,
                    Src->mDesc, SourceSlice, Extent);
                !Status)
                return Status;
            return mNative->CopyTextureToStaging(
                Dst->mNative,
                Dst->mDesc,
                DestinationSlice,
                Src->mNative,
                Src->mDesc,
                SourceSlice);
        }

        FArdaRHIStatus FCommandList::CopyTextureFromStaging(
            IArdaRHITexture& Destination,
            const FArdaRHITextureSlice& DestinationSlice,
            IArdaRHIStagingTexture& Source,
            const FArdaRHITextureSlice& SourceSlice)
        {
            auto* Dst = Cast<FTexture>(&Destination);
            auto* Src = Cast<FStagingTexture>(&Source);
            if (!Dst || !Src || !Owns(Dst) || !Owns(Src))
                return WrongDevice();
            if (Src->mDesc.mCpuAccess != EArdaRHICpuAccess::Write)
                return Invalid("A texture upload requires a write staging texture.");
            FArdaRHITextureCopyExtent Extent;
            if (auto Status = ResolveArdaRHITextureCopyExtent(
                    Dst->mDesc, DestinationSlice,
                    Src->mDesc.mTexture, SourceSlice, Extent);
                !Status)
                return Status;
            return mNative->CopyTextureFromStaging(
                Dst->mNative,
                Dst->mDesc,
                DestinationSlice,
                Src->mNative,
                Src->mDesc,
                SourceSlice);
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
            const FArdaRHIStatus Status = mNative->SetTextureState(
                Native->mNative, Native->mDesc, Range, State);
            if (Status)
            {
                StoreFacadeTextureState(
                    GetFacadeTextureStates(*Native),
                    Native->mDesc,
                    Range,
                    State);
            }
            return Status;
        }

        FArdaRHIStatus FCommandList::SetBufferState(
            IArdaRHIBuffer& Buffer, EArdaRHIResourceState State)
        {
            auto* Native = Cast<FBuffer>(&Buffer);
            if (!Native || !Owns(Native)) return WrongDevice();
            const FArdaRHIStatus Status = mNative->SetBufferState(
                Native->mNative, Native->mDesc, State);
            if (Status)
                mFacadeBufferStates[Native] = State;
            return Status;
        }

        FArdaRHIStatus FCommandList::TransitionTexture(
            IArdaRHITexture& Texture,
            const FArdaRHITextureTransitionDesc& Transition)
        {
            auto* Native = Cast<FTexture>(&Texture);
            if (!Native || !Owns(Native))
                return WrongDevice();
            const auto Current = LoadFacadeTextureState(
                GetFacadeTextureStates(*Native),
                Native->mDesc,
                Transition.mSubresources);
            if (!Current)
                return Current.mStatus;
            if (!HasAnyFlags(
                    Transition.mFlags,
                    EArdaRHITransitionFlags::Discard) &&
                Transition.mStateBefore != EArdaRHIResourceState::Unknown &&
                Current.mValue != Transition.mStateBefore)
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "Texture transition before-state differs from the facade state.");
            }
            const FArdaRHIStatus Status = mNative->TransitionTexture(
                Native->mNative, Native->mDesc, Transition);
            if (Status && Transition.mbQueueOwnershipTransfer)
                mFacadeTextureQueueOwners[Native] =
                    Transition.mDestinationQueue;
            if (Status && !HasAnyFlags(
                    Transition.mFlags,
                    EArdaRHITransitionFlags::BeginOnly))
            {
                StoreFacadeTextureState(
                    GetFacadeTextureStates(*Native),
                    Native->mDesc,
                    Transition.mSubresources,
                    Transition.mStateAfter);
            }
            return Status;
        }

        FArdaRHIStatus FCommandList::TransitionBuffer(
            IArdaRHIBuffer& Buffer,
            const FArdaRHIBufferTransitionDesc& Transition)
        {
            auto* Native = Cast<FBuffer>(&Buffer);
            if (!Native || !Owns(Native))
                return WrongDevice();
            auto Existing = mFacadeBufferStates.find(Native);
            EArdaRHIResourceState Current = Native->mDesc.mInitialState;
            if (Existing != mFacadeBufferStates.end())
            {
                Current = Existing->second;
            }
            else
            {
                std::lock_guard<std::mutex> Lock(Native->mFacadeStateMutex);
                if (Native->mbFacadeStateKnown)
                    Current = Native->mFacadeState;
            }
            if (!HasAnyFlags(
                    Transition.mFlags,
                    EArdaRHITransitionFlags::Discard) &&
                Transition.mStateBefore != EArdaRHIResourceState::Unknown &&
                Current != Transition.mStateBefore)
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "Buffer transition before-state differs from the facade state.");
            }
            const FArdaRHIStatus Status = mNative->TransitionBuffer(
                Native->mNative, Native->mDesc, Transition);
            if (Status && Transition.mbQueueOwnershipTransfer)
                mFacadeBufferQueueOwners[Native] =
                    Transition.mDestinationQueue;
            if (Status && !HasAnyFlags(
                    Transition.mFlags,
                    EArdaRHITransitionFlags::BeginOnly))
            {
                mFacadeBufferStates[Native] = Transition.mStateAfter;
            }
            return Status;
        }

        FArdaRHIStatus FCommandList::SetAccelStructState(
            IArdaRHIAccelStruct& Resource, EArdaRHIResourceState State)
        {
            auto* AccelStruct = Cast<FAccelStruct>(&Resource);
            if (!AccelStruct || !Owns(AccelStruct)) return WrongDevice();
            if (!HasAnyFlags(State, EArdaRHIResourceState::AccelStructRead) &&
                !HasAnyFlags(State, EArdaRHIResourceState::AccelStructWrite))
                return Invalid(
                    "Acceleration structures require a read or write state.");
            const FArdaRHIStatus Status = mNative->SetAccelStructState(
                AccelStruct->mNative, State);
            if (Status)
            {
                auto Existing = mFacadeAccelStructStates.find(AccelStruct);
                if (Existing == mFacadeAccelStructStates.end())
                {
                    std::lock_guard<std::mutex> Lock(AccelStruct->mStateMutex);
                    Existing = mFacadeAccelStructStates.emplace(
                        AccelStruct, FAccelStructTracking{
                            State, AccelStruct->mBuildState}).first;
                }
                else
                    Existing->second.mState = State;
            }
            return Status;
        }

        TArdaRHIResult<FArdaRHIResourceStateSnapshot>
        FCommandList::QueryAccelStructState(
            IArdaRHIAccelStruct& Resource) const
        {
            auto* AccelStruct = Cast<FAccelStruct>(&Resource);
            if (!AccelStruct || !Owns(AccelStruct))
                return {{}, WrongDevice()};
            FAccelStructTracking Tracking;
            const auto Existing = mFacadeAccelStructStates.find(AccelStruct);
            if (Existing != mFacadeAccelStructStates.end())
                Tracking = Existing->second;
            else
            {
                std::lock_guard<std::mutex> Lock(AccelStruct->mStateMutex);
                Tracking.mState = AccelStruct->mFacadeState;
                Tracking.mBuildState = AccelStruct->mBuildState;
            }
            auto Native = mNative->QueryAccelStructState(
                AccelStruct->mNative);
            if (!Native)
                return {{}, eastl::move(Native.mStatus)};
            FArdaRHIResourceStateSnapshot Snapshot;
            Snapshot.mFacadeState = Tracking.mState;
            Snapshot.mQueue = mQueue;
            Snapshot.mNative = eastl::move(Native.mValue);
            Snapshot.mbFacadeKnown = true;
            return {eastl::move(Snapshot), {}};
        }

        FArdaRHIStatus FCommandList::BeginTrackingTextureState(
            IArdaRHITexture& Texture,
            const FArdaRHITextureSubresourceRange& Range,
            EArdaRHIResourceState State)
        {
            auto* Native = Cast<FTexture>(&Texture);
            if (!Native || !Owns(Native)) return WrongDevice();
            const FArdaRHIStatus Status = mNative->BeginTrackingTextureState(
                Native->mNative, Native->mDesc, Range, State);
            if (Status)
            {
                auto& Expected = mExpectedTextureStartStates[Native];
                if (Expected.empty())
                {
                    Expected.assign(
                        static_cast<size_t>(Native->mDesc.mMipLevels) *
                            Native->mDesc.mArraySize *
                            GetArdaRHIFormatPlaneCount(Native->mDesc.mFormat),
                        EArdaRHIResourceState::Unknown);
                }
                StoreFacadeTextureState(
                    Expected, Native->mDesc, Range, State);
                StoreFacadeTextureState(
                    GetFacadeTextureStates(*Native),
                    Native->mDesc,
                    Range,
                    State);
            }
            return Status;
        }

        FArdaRHIStatus FCommandList::BeginTrackingBufferState(
            IArdaRHIBuffer& Buffer, EArdaRHIResourceState State)
        {
            auto* Native = Cast<FBuffer>(&Buffer);
            if (!Native || !Owns(Native)) return WrongDevice();
            const FArdaRHIStatus Status = mNative->BeginTrackingBufferState(
                Native->mNative, Native->mDesc, State);
            if (Status)
            {
                mExpectedBufferStartStates[Native] = State;
                mFacadeBufferStates[Native] = State;
            }
            return Status;
        }

        TArdaRHIResult<FArdaRHIResourceStateSnapshot>
            FCommandList::QueryTextureState(
                IArdaRHITexture& Texture,
                const FArdaRHITextureSubresourceRange& Range) const
        {
            auto* Native = Cast<FTexture>(&Texture);
            if (!Native || !Owns(Native))
                return { {}, WrongDevice() };
            auto Facade = LoadFacadeTextureState(
                GetFacadeTextureStates(*Native), Native->mDesc, Range);
            if (!Facade)
                return { {}, eastl::move(Facade.mStatus) };
            auto Backend = mNative->QueryTextureState(
                Native->mNative, Native->mDesc, Range);
            if (!Backend)
                return { {}, eastl::move(Backend.mStatus) };
            FArdaRHIResourceStateSnapshot Snapshot;
            Snapshot.mFacadeState = Facade.mValue;
            Snapshot.mQueue = mQueue;
            auto QueueOwner = mFacadeTextureQueueOwners.find(Native);
            if (QueueOwner != mFacadeTextureQueueOwners.end())
            {
                Snapshot.mFacadeQueueOwner = QueueOwner->second;
                Snapshot.mbFacadeQueueOwnerKnown = true;
            }
            else
            {
                std::lock_guard<std::mutex> Lock(Native->mFacadeStateMutex);
                Snapshot.mFacadeQueueOwner = Native->mFacadeQueueOwner;
                Snapshot.mbFacadeQueueOwnerKnown =
                    Native->mbFacadeQueueOwnerKnown;
            }
            Snapshot.mNative = eastl::move(Backend.mValue);
            Snapshot.mbFacadeKnown = true;
            return { eastl::move(Snapshot), {} };
        }

        TArdaRHIResult<FArdaRHIResourceStateSnapshot>
            FCommandList::QueryBufferState(IArdaRHIBuffer& Buffer) const
        {
            auto* Native = Cast<FBuffer>(&Buffer);
            if (!Native || !Owns(Native))
                return { {}, WrongDevice() };
            FArdaRHIResourceStateSnapshot Snapshot;
            Snapshot.mQueue = mQueue;
            auto Existing = mFacadeBufferStates.find(Native);
            if (Existing == mFacadeBufferStates.end())
            {
                EArdaRHIResourceState State = Native->mDesc.mInitialState;
                {
                    std::lock_guard<std::mutex> Lock(
                        Native->mFacadeStateMutex);
                    if (Native->mbFacadeStateKnown)
                        State = Native->mFacadeState;
                }
                Existing = mFacadeBufferStates.emplace(Native, State).first;
            }
            Snapshot.mFacadeState = Existing->second;
            Snapshot.mbFacadeKnown = true;
            auto QueueOwner = mFacadeBufferQueueOwners.find(Native);
            if (QueueOwner != mFacadeBufferQueueOwners.end())
            {
                Snapshot.mFacadeQueueOwner = QueueOwner->second;
                Snapshot.mbFacadeQueueOwnerKnown = true;
            }
            else
            {
                std::lock_guard<std::mutex> Lock(Native->mFacadeStateMutex);
                Snapshot.mFacadeQueueOwner = Native->mFacadeQueueOwner;
                Snapshot.mbFacadeQueueOwnerKnown =
                    Native->mbFacadeQueueOwnerKnown;
            }
            auto Backend = mNative->QueryBufferState(
                Native->mNative, Native->mDesc);
            if (!Backend)
                return { {}, eastl::move(Backend.mStatus) };
            Snapshot.mNative = eastl::move(Backend.mValue);
            return { eastl::move(Snapshot), {} };
        }

        TArdaRHIResult<FArdaRHIResourceStateSnapshot>
            FCommandList::QuerySamplerFeedbackTextureState(
                IArdaRHISamplerFeedbackTexture& Texture) const
        {
            auto* Native = Cast<FSamplerFeedbackTexture>(&Texture);
            if (!Native || !Owns(Native))
                return {{}, WrongDevice()};
            auto Existing = mFacadeSamplerFeedbackStates.find(Native);
            if (Existing == mFacadeSamplerFeedbackStates.end())
            {
                EArdaRHIResourceState State = Native->mDesc.mInitialState;
                {
                    std::lock_guard<std::mutex> Lock(Native->mStateMutex);
                    if (Native->mbFacadeStateKnown)
                        State = Native->mFacadeState;
                }
                Existing = mFacadeSamplerFeedbackStates.emplace(
                    Native, State).first;
            }
            auto Backend = mNative->QuerySamplerFeedbackTextureState(
                Native->mNative);
            if (!Backend)
                return {{}, eastl::move(Backend.mStatus)};
            FArdaRHIResourceStateSnapshot Snapshot;
            Snapshot.mFacadeState = Existing->second;
            Snapshot.mQueue = mQueue;
            Snapshot.mNative = eastl::move(Backend.mValue);
            Snapshot.mbFacadeKnown = true;
            return {eastl::move(Snapshot), {}};
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

        FArdaRHIStatus FCommandList::AliasingBarrier(
            IArdaRHIResource* ResourceBefore,
            IArdaRHIResource* ResourceAfter)
        {
            auto* Before = Cast<FResource>(ResourceBefore);
            auto* After = Cast<FResource>(ResourceAfter);
            if ((Before && !Owns(Before)) || (After && !Owns(After)))
                return WrongDevice();
            if (!Before && !After)
                return Invalid("An aliasing barrier requires at least one resource.");
            return mNative->AliasingBarrier(
                GetNativeObject(ResourceBefore),
                GetNativeObject(ResourceAfter));
        }

        FArdaRHIStatus FCommandList::ClearTextureUInt(
            IArdaRHITexture&, const FArdaRHITextureSubresourceRange&, uint32_t)
        {
            return Unsupported("Integer texture clears are not implemented by the backend providers.");
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
            return Unsupported("Integer buffer clears are not implemented by the backend providers.");
        }

        FArdaRHIStatus FCommandList::SetGraphicsState(const FArdaRHIGraphicsState& State)
        {
            auto* Pipeline = Cast<FGraphicsPipeline>(State.mPipeline.Get());
            auto* Framebuffer = Cast<FFramebuffer>(State.mFramebuffer.Get());
            if (!Pipeline || !Framebuffer || !Owns(Pipeline) || !Owns(Framebuffer))
                return WrongDevice();
            FArdaProviderGraphicsState Native;
            Native.mPipeline = Pipeline->mNative;
            Native.mFramebuffer = Framebuffer->mNative;
            Native.mIndexFormat = State.mIndexFormat;
            Native.mIndexOffset = State.mIndexOffset;
            Native.mViewports = State.mViewports;
            Native.mScissors = State.mScissors;
            for (const auto& Binding : State.mBindings)
            {
                if (auto* Set = Cast<FBindingSet>(Binding.Get()))
                {
                    if (!Owns(Set)) return WrongDevice();
                    Native.mBindings.push_back(Set->mNative);
                }
                else if (auto* Table = Cast<FDescriptorTable>(Binding.Get()))
                {
                    if (!Owns(Table)) return WrongDevice();
                    std::lock_guard<std::mutex> Lock(Table->mMutex);
                    Native.mBindings.push_back(Table->mNative);
                }
                else
                {
                    return WrongDevice();
                }
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
            FArdaProviderComputeState Native;
            Native.mPipeline = Pipeline->mNative;
            for (const auto& Binding : State.mBindings)
            {
                if (auto* Set = Cast<FBindingSet>(Binding.Get()))
                {
                    if (!Owns(Set)) return WrongDevice();
                    Native.mBindings.push_back(Set->mNative);
                }
                else if (auto* Table = Cast<FDescriptorTable>(Binding.Get()))
                {
                    if (!Owns(Table)) return WrongDevice();
                    std::lock_guard<std::mutex> Lock(Table->mMutex);
                    Native.mBindings.push_back(Table->mNative);
                }
                else
                {
                    return WrongDevice();
                }
            }
            return mNative->SetComputeState(Native);
        }

        FArdaRHIStatus FCommandList::DrawIndirect(
            IArdaRHIBuffer& Arguments,
            uint64_t Offset,
            uint32_t DrawCount,
            uint32_t Stride)
        {
            auto* Buffer = Cast<FBuffer>(&Arguments);
            constexpr uint32_t ArgumentSize = 4u * sizeof(uint32_t);
            const uint32_t ResolvedStride = Stride ? Stride : ArgumentSize;
            if (!Buffer || !Owns(Buffer))
                return WrongDevice();
            if (!HasAnyFlags(
                    Buffer->mDesc.mUsage,
                    EArdaRHIBufferUsage::Indirect) ||
                DrawCount == 0 || ResolvedStride < ArgumentSize ||
                Offset > Buffer->mDesc.mByteSize ||
                static_cast<uint64_t>(DrawCount - 1) * ResolvedStride +
                        ArgumentSize >
                    Buffer->mDesc.mByteSize - Offset)
            {
                return Invalid("Indirect draw arguments are out of range or lack Indirect usage.");
            }
            const auto State = QueryBufferState(*Buffer);
            if (!State)
                return State.mStatus;
            if (!State.mValue.IsConsistent() || !HasAnyFlags(
                    State.mValue.mFacadeState,
                    EArdaRHIResourceState::IndirectArgument))
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "Indirect draw arguments must be in IndirectArgument state across facade, backend, and native tracking.");
            }
            return mNative->DrawIndirect(
                Buffer->mNative, Offset, DrawCount, ResolvedStride);
        }

        FArdaRHIStatus FCommandList::DrawIndexedIndirect(
            IArdaRHIBuffer& Arguments,
            uint64_t Offset,
            uint32_t DrawCount,
            uint32_t Stride)
        {
            auto* Buffer = Cast<FBuffer>(&Arguments);
            constexpr uint32_t ArgumentSize = 5u * sizeof(uint32_t);
            const uint32_t ResolvedStride = Stride ? Stride : ArgumentSize;
            if (!Buffer || !Owns(Buffer))
                return WrongDevice();
            if (!HasAnyFlags(
                    Buffer->mDesc.mUsage,
                    EArdaRHIBufferUsage::Indirect) ||
                DrawCount == 0 || ResolvedStride < ArgumentSize ||
                Offset > Buffer->mDesc.mByteSize ||
                static_cast<uint64_t>(DrawCount - 1) * ResolvedStride +
                        ArgumentSize >
                    Buffer->mDesc.mByteSize - Offset)
            {
                return Invalid("Indexed indirect draw arguments are out of range or lack Indirect usage.");
            }
            const auto State = QueryBufferState(*Buffer);
            if (!State)
                return State.mStatus;
            if (!State.mValue.IsConsistent() || !HasAnyFlags(
                    State.mValue.mFacadeState,
                    EArdaRHIResourceState::IndirectArgument))
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "Indexed indirect draw arguments must be in IndirectArgument state across facade, backend, and native tracking.");
            }
            return mNative->DrawIndexedIndirect(
                Buffer->mNative, Offset, DrawCount, ResolvedStride);
        }

        FArdaRHIStatus FCommandList::DispatchIndirect(
            IArdaRHIBuffer& Arguments,
            uint64_t Offset)
        {
            auto* Buffer = Cast<FBuffer>(&Arguments);
            constexpr uint64_t ArgumentSize = 3u * sizeof(uint32_t);
            if (!Buffer || !Owns(Buffer))
                return WrongDevice();
            if (!HasAnyFlags(
                    Buffer->mDesc.mUsage,
                    EArdaRHIBufferUsage::Indirect) ||
                Offset > Buffer->mDesc.mByteSize ||
                ArgumentSize > Buffer->mDesc.mByteSize - Offset)
            {
                return Invalid("Indirect dispatch arguments are out of range or lack Indirect usage.");
            }
            const auto State = QueryBufferState(*Buffer);
            if (!State)
                return State.mStatus;
            if (!State.mValue.IsConsistent() || !HasAnyFlags(
                    State.mValue.mFacadeState,
                    EArdaRHIResourceState::IndirectArgument))
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "Indirect dispatch arguments must be in IndirectArgument state across facade, backend, and native tracking.");
            }
            return mNative->DispatchIndirect(Buffer->mNative, Offset);
        }

        FArdaRHIStatus FCommandList::SetMeshletState(
            const FArdaRHIMeshletState& State)
        {
            if (mDevice->GetCapabilities().mMeshShaderTier ==
                EArdaRHIMeshShaderTier::None)
                return Unsupported("Mesh shaders are unsupported by this device.");
            auto* Pipeline = Cast<FMeshletPipeline>(State.mPipeline.Get());
            auto* Framebuffer = Cast<FFramebuffer>(State.mFramebuffer.Get());
            if (!Pipeline || !Framebuffer ||
                !Owns(Pipeline) || !Owns(Framebuffer))
            {
                return WrongDevice();
            }
            FArdaProviderMeshletState Native;
            Native.mPipeline = Pipeline->mNative;
            Native.mFramebuffer = Framebuffer->mNative;
            Native.mViewports = State.mViewports;
            Native.mScissors = State.mScissors;
            for (const auto& Binding : State.mBindings)
            {
                auto* Set = Cast<FBindingSet>(Binding.Get());
                if (!Set || !Owns(Set)) return WrongDevice();
                Native.mBindings.push_back(Set->mNative);
            }
            return mNative->SetMeshletState(Native);
        }

        FArdaRHIStatus FCommandList::DispatchMesh(
            uint32_t GroupsX,
            uint32_t GroupsY,
            uint32_t GroupsZ)
        {
            if (mDevice->GetCapabilities().mMeshShaderTier ==
                EArdaRHIMeshShaderTier::None)
                return Unsupported("Mesh shaders are unsupported by this device.");
            if (GroupsX == 0 || GroupsY == 0 || GroupsZ == 0)
                return Invalid("Mesh dispatch group counts must be non-zero.");
            return mNative->DispatchMesh(GroupsX, GroupsY, GroupsZ);
        }

        FArdaRHIStatus FCommandList::SetRayTracingState(
            const FArdaRHIRayTracingState& State)
        {
            if (!mDevice->GetCapabilities().mRayTracing.mbPipelineShaders)
                return Unsupported("Ray tracing is unsupported by this device.");
            auto* Table = Cast<FShaderTable>(State.mShaderTable.Get());
            if (!Table || !Owns(Table)) return WrongDevice();
            FArdaProviderRayTracingState Native;
            {
                std::lock_guard<std::mutex> Lock(Table->mMutex);
                if (!Table->mbHasRayGeneration)
                    return FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidState,
                        "The shader table has no ray-generation record.");
                Native.mShaderTable = Table->mNative;
            }
            for (const auto& Binding : State.mBindings)
            {
                auto* Set = Cast<FBindingSet>(Binding.Get());
                if (!Set || !Owns(Set)) return WrongDevice();
                Native.mBindings.push_back(Set->mNative);
            }
            return mNative->SetRayTracingState(Native);
        }

        FArdaRHIStatus FCommandList::DispatchRays(
            uint32_t Width,
            uint32_t Height,
            uint32_t Depth)
        {
            if (!mDevice->GetCapabilities().mRayTracing.mbPipelineShaders)
                return Unsupported("Ray tracing is unsupported by this device.");
            if (!Width || !Height || !Depth)
                return Invalid("Ray dispatch dimensions must be non-zero.");
            const uint64_t InvocationCount =
                uint64_t(Width) * uint64_t(Height) * uint64_t(Depth);
            const uint32_t MaxInvocations = mDevice->GetCapabilities().
                mRayTracing.mMaxRayDispatchInvocations;
            if (MaxInvocations != 0 && InvocationCount > MaxInvocations)
                return Invalid(
                    "Ray dispatch exceeds the device invocation-count limit.");
            return mNative->DispatchRays(Width, Height, Depth);
        }

        FArdaRHIStatus FCommandList::DispatchRaysIndirect(
            IArdaRHIBuffer& Arguments, uint64_t Offset)
        {
            if (!mDevice->GetCapabilities().mRayTracing.mbIndirectDispatch)
                return Unsupported(
                    "Indirect ray dispatch is unsupported by this device.");
            auto* Buffer = Cast<FBuffer>(&Arguments);
            if (!Buffer || !Owns(Buffer)) return WrongDevice();
            if (!HasAnyFlags(Buffer->mDesc.mUsage,
                    EArdaRHIBufferUsage::Indirect) ||
                Offset > Buffer->mDesc.mByteSize ||
                sizeof(uint32_t) * 3 > Buffer->mDesc.mByteSize - Offset)
                return Invalid("Indirect ray-dispatch arguments are invalid.");
            const auto State = QueryBufferState(*Buffer);
            if (!State)
                return State.mStatus;
            if (!State.mValue.IsConsistent() || !HasAnyFlags(
                    State.mValue.mFacadeState,
                    EArdaRHIResourceState::IndirectArgument))
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "Indirect ray-dispatch arguments must be in IndirectArgument state across facade, backend, and native tracking.");
            }
            return mNative->DispatchRaysIndirect(Buffer->mNative, Offset);
        }

        FArdaRHIStatus FCommandList::BuildBottomLevelAccelStruct(
            IArdaRHIAccelStruct& Resource,
            const eastl::vector<FArdaRHIRayTracingGeometryDesc>& Geometries,
            EArdaRHIAccelStructBuildFlags Flags)
        {
            auto* AccelStruct = Cast<FAccelStruct>(&Resource);
            if (!AccelStruct || !Owns(AccelStruct)) return WrongDevice();
            if (AccelStruct->mDesc.mbTopLevel || Geometries.empty())
                return Invalid("A BLAS build requires BLAS geometry.");
            if (HasAnyFlags(Flags,
                    EArdaRHIAccelStructBuildFlags::PerformUpdate) &&
                !HasAnyFlags(AccelStruct->mDesc.mBuildFlags,
                    EArdaRHIAccelStructBuildFlags::AllowUpdate))
                return Invalid(
                    "A BLAS update requires AllowUpdate at creation.");
            auto NativeGeometries = ResolveRayTracingGeometries(
                *mDevice, Geometries);
            if (!NativeGeometries) return NativeGeometries.mStatus;
            const FArdaRHIStatus Status =
                mNative->BuildBottomLevelAccelStruct(
                    AccelStruct->mNative, NativeGeometries.mValue, Flags);
            if (Status)
            {
                mFacadeAccelStructStates[AccelStruct] = {
                    EArdaRHIResourceState::AccelStructRead,
                    HasAnyFlags(Flags,
                        EArdaRHIAccelStructBuildFlags::PerformUpdate)
                        ? EArdaRHIAccelStructBuildState::Updated
                        : EArdaRHIAccelStructBuildState::Built};
            }
            return Status;
        }

        FArdaRHIStatus FCommandList::BuildTopLevelAccelStruct(
            IArdaRHIAccelStruct& Resource,
            const eastl::vector<FArdaRHIRayTracingInstanceDesc>& Instances,
            EArdaRHIAccelStructBuildFlags Flags)
        {
            auto* AccelStruct = Cast<FAccelStruct>(&Resource);
            if (!AccelStruct || !Owns(AccelStruct)) return WrongDevice();
            if (!AccelStruct->mDesc.mbTopLevel ||
                Instances.size() > AccelStruct->mDesc.mTopLevelMaxInstances)
                return Invalid("TLAS instance count exceeds its capacity.");
            if (Instances.empty() && !HasAnyFlags(Flags,
                    EArdaRHIAccelStructBuildFlags::AllowEmptyInstances))
                return Invalid("An empty TLAS build requires AllowEmptyInstances.");
            if (HasAnyFlags(Flags,
                    EArdaRHIAccelStructBuildFlags::PerformUpdate) &&
                !HasAnyFlags(AccelStruct->mDesc.mBuildFlags,
                    EArdaRHIAccelStructBuildFlags::AllowUpdate))
                return Invalid(
                    "A TLAS update requires AllowUpdate at creation.");
            eastl::vector<FArdaProviderRayTracingInstance> NativeInstances;
            NativeInstances.reserve(Instances.size());
            for (const auto& Instance : Instances)
            {
                auto* BottomLevel = Cast<FAccelStruct>(
                    Instance.mBottomLevelAccelStruct.Get());
                if (!BottomLevel || !Owns(BottomLevel) ||
                    BottomLevel->mDesc.mbTopLevel)
                    return WrongDevice();
                FArdaProviderRayTracingInstance Native;
                std::memcpy(Native.mTransform, Instance.mTransform,
                    sizeof(Instance.mTransform));
                Native.mInstanceID = Instance.mInstanceId;
                Native.mInstanceMask = Instance.mInstanceMask;
                Native.mInstanceContributionToHitGroupIndex =
                    Instance.mHitGroupContribution;
                Native.mFlags = Instance.mFlags;
                Native.mBottomLevelAccelStruct = BottomLevel->mNative;
                NativeInstances.push_back(eastl::move(Native));
            }
            const FArdaRHIStatus Status =
                mNative->BuildTopLevelAccelStruct(
                    AccelStruct->mNative, NativeInstances, Flags);
            if (Status)
            {
                mFacadeAccelStructStates[AccelStruct] = {
                    EArdaRHIResourceState::AccelStructRead,
                    HasAnyFlags(Flags,
                        EArdaRHIAccelStructBuildFlags::PerformUpdate)
                        ? EArdaRHIAccelStructBuildState::Updated
                        : EArdaRHIAccelStructBuildState::Built};
            }
            return Status;
        }

        FArdaRHIStatus FCommandList::BuildTopLevelAccelStructFromBuffer(
            IArdaRHIAccelStruct& Resource,
            IArdaRHIBuffer& Instances,
            uint64_t Offset,
            size_t InstanceCount,
            EArdaRHIAccelStructBuildFlags Flags)
        {
            auto* AccelStruct = Cast<FAccelStruct>(&Resource);
            auto* Buffer = Cast<FBuffer>(&Instances);
            if (!AccelStruct || !Buffer || !Owns(AccelStruct) ||
                !Owns(Buffer)) return WrongDevice();
            if (!AccelStruct->mDesc.mbTopLevel ||
                InstanceCount > AccelStruct->mDesc.mTopLevelMaxInstances ||
                Offset > Buffer->mDesc.mByteSize)
                return Invalid("Indirect TLAS build arguments are invalid.");
            const FArdaRHIStatus Status =
                mNative->BuildTopLevelAccelStructFromBuffer(
                    AccelStruct->mNative, Buffer->mNative, Offset,
                    InstanceCount, Flags);
            if (Status)
            {
                mFacadeAccelStructStates[AccelStruct] = {
                    EArdaRHIResourceState::AccelStructRead,
                    HasAnyFlags(Flags,
                        EArdaRHIAccelStructBuildFlags::PerformUpdate)
                        ? EArdaRHIAccelStructBuildState::Updated
                        : EArdaRHIAccelStructBuildState::Built};
            }
            return Status;
        }

        FArdaRHIStatus FCommandList::CompactAccelStruct(
            IArdaRHIAccelStruct& DestinationResource,
            IArdaRHIAccelStruct& SourceResource)
        {
            auto* Destination = Cast<FAccelStruct>(&DestinationResource);
            auto* Source = Cast<FAccelStruct>(&SourceResource);
            if (!Destination || !Source || !Owns(Destination) ||
                !Owns(Source)) return WrongDevice();
            if (Destination == Source ||
                Destination->mDesc.mbTopLevel != Source->mDesc.mbTopLevel ||
                !HasAnyFlags(Source->mDesc.mBuildFlags,
                    EArdaRHIAccelStructBuildFlags::AllowCompaction) ||
                !Destination->mDesc.mResultSizeOverride)
                return Invalid(
                    "Acceleration-structure compaction requires a distinct compact destination.");
            const FArdaRHIStatus Status = mNative->CompactAccelStruct(
                Destination->mNative, Source->mNative);
            if (Status)
            {
                mFacadeAccelStructStates[Destination] = {
                    EArdaRHIResourceState::AccelStructRead,
                    EArdaRHIAccelStructBuildState::Compacted};
            }
            return Status;
        }

        FArdaRHIStatus FCommandList::BuildOpacityMicromap(
            IArdaRHIOpacityMicromap& Resource)
        {
            auto* Micromap = Cast<FOpacityMicromap>(&Resource);
            if (!Micromap || !Owns(Micromap)) return WrongDevice();
            if (!mDevice->GetCapabilities().mRayTracing.mbOpacityMicromaps)
                return Unsupported(
                    "Opacity micromaps are unsupported by this device.");
            const FArdaRHIStatus Status =
                mNative->BuildOpacityMicromap(Micromap->mNative);
            if (Status)
            {
                mFacadeOpacityMicromapStates[Micromap] = {
                    EArdaRHIResourceState::OpacityMicromapBuildInput,
                    EArdaRHIAccelStructBuildState::Built};
            }
            return Status;
        }

        FArdaRHIStatus FCommandList::CompactOpacityMicromap(
            IArdaRHIOpacityMicromap& DestinationResource,
            IArdaRHIOpacityMicromap& SourceResource)
        {
            auto* Destination = Cast<FOpacityMicromap>(&DestinationResource);
            auto* Source = Cast<FOpacityMicromap>(&SourceResource);
            if (!Destination || !Source || !Owns(Destination) ||
                !Owns(Source)) return WrongDevice();
            if (Destination == Source ||
                !HasAnyFlags(Source->mDesc.mFlags,
                    EArdaRHIOpacityMicromapBuildFlags::AllowCompaction) ||
                !Destination->mDesc.mResultSizeOverride)
                return Invalid(
                    "Opacity-micromap compaction requires a distinct compact destination.");
            const FArdaRHIStatus Status = mNative->CompactOpacityMicromap(
                Destination->mNative, Source->mNative);
            if (Status)
            {
                mFacadeOpacityMicromapStates[Destination] = {
                    EArdaRHIResourceState::OpacityMicromapBuildInput,
                    EArdaRHIAccelStructBuildState::Compacted};
            }
            return Status;
        }

        TArdaRHIResult<FArdaRHIResourceStateSnapshot>
        FCommandList::QueryOpacityMicromapState(
            IArdaRHIOpacityMicromap& Resource) const
        {
            auto* Micromap = Cast<FOpacityMicromap>(&Resource);
            if (!Micromap || !Owns(Micromap))
                return {{}, WrongDevice()};
            FAccelStructTracking Tracking;
            const auto Existing =
                mFacadeOpacityMicromapStates.find(Micromap);
            if (Existing != mFacadeOpacityMicromapStates.end())
                Tracking = Existing->second;
            else
            {
                std::lock_guard<std::mutex> Lock(Micromap->mStateMutex);
                Tracking.mState = Micromap->mFacadeState;
                Tracking.mBuildState = Micromap->mBuildState;
            }
            auto Native = mNative->QueryOpacityMicromapState(
                Micromap->mNative);
            if (!Native)
                return {{}, eastl::move(Native.mStatus)};
            FArdaRHIResourceStateSnapshot Snapshot;
            Snapshot.mFacadeState = Tracking.mState;
            Snapshot.mQueue = mQueue;
            Snapshot.mNative = eastl::move(Native.mValue);
            Snapshot.mbFacadeKnown = true;
            return {eastl::move(Snapshot), {}};
        }

        FArdaRHIStatus FCommandList::DispatchShaderBundle(
            IArdaRHIShaderBundle& Resource)
        {
            auto* Bundle = Cast<FShaderBundle>(&Resource);
            if (!Bundle || !Owns(Bundle)) return WrongDevice();
            if (!mDevice->GetCapabilities().mbShaderBundleDispatch)
                return Unsupported(
                    "Shader bundles are unsupported by this device.");
            std::lock_guard<std::mutex> Lock(Bundle->mMutex);
            for (const auto& Record : Bundle->mRecords)
            {
                FArdaRHIStatus Status;
                if (Record.mComputePipeline)
                {
                    FArdaRHIComputeState State;
                    State.mPipeline = Record.mComputePipeline;
                    State.mBindings = Record.mBindings;
                    Status = SetComputeState(State);
                }
                else
                {
                    FArdaRHIMeshletState State;
                    State.mPipeline = Record.mMeshPipeline;
                    State.mBindings = Record.mBindings;
                    Status = SetMeshletState(State);
                }
                if (!Status) return Status;
                if (!Record.mLocalArguments.empty())
                    SetPushConstants(Record.mLocalArguments.data(),
                        Record.mLocalArguments.size());
                if (Record.mComputePipeline)
                    Dispatch(Record.mGroupsX, Record.mGroupsY,
                        Record.mGroupsZ);
                else
                {
                    Status = DispatchMesh(Record.mGroupsX,
                        Record.mGroupsY, Record.mGroupsZ);
                    if (!Status) return Status;
                }
            }
            return {};
        }

        FArdaRHIStatus FCommandList::DispatchWorkGraph(
            IArdaRHIWorkGraphPipeline& Resource,
            const void* Records,
            uint32_t RecordCount,
            uint32_t RecordStride,
            const eastl::vector<FArdaRHIBindingSetRef>& BindingRefs)
        {
            auto* Pipeline = Cast<FWorkGraphPipeline>(&Resource);
            if (!Pipeline || !Owns(Pipeline)) return WrongDevice();
            if (mDevice->GetCapabilities().mWorkGraphTier ==
                EArdaRHIWorkGraphTier::None)
                return Unsupported(
                    "Work graphs are unsupported by this device.");
            if (!Records || !RecordCount || !RecordStride ||
                RecordCount > Pipeline->mDesc.mMaxInputRecords)
                return Invalid(
                    "Work-graph CPU input records are invalid or exceed capacity.");
            eastl::vector<FArdaProviderObjectRef> Bindings;
            Bindings.reserve(BindingRefs.size());
            for (const auto& BindingRef : BindingRefs)
            {
                auto* Binding = Cast<FResource>(BindingRef.Get());
                FArdaProviderObjectRef Native = GetNativeObject(
                    BindingRef.Get());
                if (!Binding || !Owns(Binding) || !Native)
                    return WrongDevice();
                Bindings.push_back(eastl::move(Native));
            }
            return mNative->DispatchWorkGraph(
                Pipeline->mNative, Records, RecordCount,
                RecordStride, Bindings);
        }

        FArdaRHIStatus FCommandList::ClearSamplerFeedbackTexture(
            IArdaRHISamplerFeedbackTexture& Resource)
        {
            auto* Feedback = Cast<FSamplerFeedbackTexture>(&Resource);
            if (!Feedback || !Owns(Feedback)) return WrongDevice();
            if (mDevice->GetCapabilities().mSamplerFeedbackTier ==
                EArdaRHISamplerFeedbackTier::None)
                return Unsupported(
                    "Sampler feedback is unsupported by this device.");
            const FArdaRHIStatus Status =
                mNative->ClearSamplerFeedbackTexture(Feedback->mNative);
            if (Status)
                mFacadeSamplerFeedbackStates[Feedback] =
                    EArdaRHIResourceState::UnorderedAccess;
            return Status;
        }

        FArdaRHIStatus FCommandList::DecodeSamplerFeedbackTexture(
            IArdaRHITexture& DestinationResource,
            IArdaRHISamplerFeedbackTexture& FeedbackResource,
            EArdaRHIFormat Format)
        {
            auto* Destination = Cast<FTexture>(&DestinationResource);
            auto* Feedback = Cast<FSamplerFeedbackTexture>(
                &FeedbackResource);
            if (!Destination || !Feedback || !Owns(Destination) ||
                !Owns(Feedback)) return WrongDevice();
            if (Format != EArdaRHIFormat::R8UInt ||
                Destination->mDesc.mFormat != Format)
                return Invalid(
                    "Decoded sampler feedback requires an R8UInt destination texture.");
            const FArdaRHIStatus Status =
                mNative->DecodeSamplerFeedbackTexture(
                    Destination->mNative, Destination->mDesc,
                    Feedback->mNative, Format);
            if (Status)
            {
                mFacadeSamplerFeedbackStates[Feedback] =
                    EArdaRHIResourceState::ResolveSource;
                auto& States = GetFacadeTextureStates(*Destination);
                eastl::fill(States.begin(), States.end(),
                    EArdaRHIResourceState::ResolveDest);
            }
            return Status;
        }

        FArdaRHIStatus FCommandList::SetSamplerFeedbackTextureState(
            IArdaRHISamplerFeedbackTexture& Resource,
            EArdaRHIResourceState State)
        {
            auto* Feedback = Cast<FSamplerFeedbackTexture>(&Resource);
            if (!Feedback || !Owns(Feedback)) return WrongDevice();
            const FArdaRHIStatus Status =
                mNative->SetSamplerFeedbackTextureState(
                    Feedback->mNative, State);
            if (Status)
                mFacadeSamplerFeedbackStates[Feedback] = State;
            return Status;
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

    FArdaRHIDeviceRef CreateArdaRHIDevice(
        eastl::shared_ptr<IArdaRHIProviderDevice> Device)
    {
        return Device ? FArdaRHIDeviceRef(new FArdaRHIDeviceImpl(eastl::move(Device)))
                      : FArdaRHIDeviceRef{};
    }
}
