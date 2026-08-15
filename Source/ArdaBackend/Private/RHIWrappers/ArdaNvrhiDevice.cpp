#include "RHIWrappers/ArdaNvrhiDevice.h"
#include "RHIWrappers/ArdaNvrhiConversions.h"

#include <atomic>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#include <nvrhi/d3d12.h>
#endif
#include <nvrhi/vulkan.h>

namespace arda::rhi::private_impl
{
    namespace
    {
        class FResource : public virtual IArdaRHIResource
        {
        public:
            FResource(EArdaRHIResourceType Type, const eastl::string& Name, const void* Owner)
                : mType(Type), mName(Name), mOwner(Owner) {}

            void AddRef() noexcept final { mReferences.fetch_add(1, std::memory_order_relaxed); }
            void Release() noexcept final
            {
                if (mReferences.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    delete this;
            }
            EArdaRHIResourceType GetResourceType() const noexcept final { return mType; }
            const char* GetDebugName() const noexcept final { return mName.c_str(); }
            const void* GetOwner() const noexcept { return mOwner; }

        protected:
            ~FResource() override = default;
            void SetOwner(const void* Owner) noexcept { mOwner = Owner; }

        private:
            std::atomic<uint32_t> mReferences{ 0 };
            EArdaRHIResourceType mType;
            eastl::string mName;
            const void* mOwner;
        };

        template <typename Interface, typename Desc, typename Handle, EArdaRHIResourceType Type>
        class TResource final : public FResource, public Interface
        {
        public:
            TResource(const Desc& Descriptor, Handle NativeHandle, const void* Owner)
                : FResource(Type, Descriptor.mDebugName, Owner), mDesc(Descriptor), mHandle(std::move(NativeHandle)) {}
            const Desc& GetDesc() const noexcept override { return mDesc; }
            const void* GetPhysicalIdentity() const noexcept { return mHandle.Get(); }
            Handle mHandle;
        private:
            Desc mDesc;
        };

        using FTexture = TResource<IArdaRHITexture, FArdaRHITextureDesc, nvrhi::TextureHandle, EArdaRHIResourceType::Texture>;
        using FBuffer = TResource<IArdaRHIBuffer, FArdaRHIBufferDesc, nvrhi::BufferHandle, EArdaRHIResourceType::Buffer>;
        using FHeap = TResource<IArdaRHIHeap, FArdaRHIHeapDesc, nvrhi::HeapHandle, EArdaRHIResourceType::Heap>;
        using FStagingTexture = TResource<IArdaRHIStagingTexture, FArdaRHIStagingTextureDesc, nvrhi::StagingTextureHandle, EArdaRHIResourceType::StagingTexture>;
        using FSampler = TResource<IArdaRHISampler, FArdaRHISamplerDesc, nvrhi::SamplerHandle, EArdaRHIResourceType::Sampler>;
        class FBindingLayout final : public FResource, public IArdaRHIBindingLayout
        {
        public:
            FBindingLayout(const FArdaRHIBindingLayoutDesc& Desc,
                nvrhi::BindingLayoutHandle Handle, const void* Owner,
                bool bUnsafeDescriptorTables = false)
                : FResource(EArdaRHIResourceType::BindingLayout, Desc.mDebugName, Owner)
                , mDesc(Desc)
                , mHandle(std::move(Handle))
                , mbUnsafeDescriptorTables(bUnsafeDescriptorTables)
            {
            }
            const FArdaRHIBindingLayoutDesc& GetDesc() const noexcept override { return mDesc; }
            FArdaRHIBindingLayoutDesc mDesc;
            nvrhi::BindingLayoutHandle mHandle;
            bool mbUnsafeDescriptorTables = false;
        };
        using FBindingSet = TResource<IArdaRHIBindingSet, FArdaRHIBindingSetDesc, nvrhi::BindingSetHandle, EArdaRHIResourceType::BindingSet>;
        using FFramebuffer = TResource<IArdaRHIFramebuffer, FArdaRHIFramebufferDesc, nvrhi::FramebufferHandle, EArdaRHIResourceType::Framebuffer>;
        using FGraphicsPipeline = TResource<IArdaRHIGraphicsPipeline, FArdaRHIGraphicsPipelineDesc, nvrhi::GraphicsPipelineHandle, EArdaRHIResourceType::GraphicsPipeline>;
        using FComputePipeline = TResource<IArdaRHIComputePipeline, FArdaRHIComputePipelineDesc, nvrhi::ComputePipelineHandle, EArdaRHIResourceType::ComputePipeline>;
        using FMeshletPipeline = TResource<IArdaRHIMeshletPipeline, FArdaRHIMeshletPipelineDesc, nvrhi::MeshletPipelineHandle, EArdaRHIResourceType::MeshletPipeline>;
        using FRayTracingPipeline = TResource<IArdaRHIRayTracingPipeline, FArdaRHIRayTracingPipelineDesc, nvrhi::rt::PipelineHandle, EArdaRHIResourceType::RayTracingPipeline>;

        class FTextureReference final : public FResource, public IArdaRHITextureReference
        {
        public:
            FTextureReference(FArdaRHITextureRef Texture, const void* Owner)
                : FResource(EArdaRHIResourceType::TextureReference, "TextureReference", Owner)
                , mTexture(std::move(Texture)) {}
            const FArdaRHITextureRef& GetTexture() const noexcept override { return mTexture; }
            FArdaRHITextureRef mTexture;
        };

        class FUniformBuffer final : public FResource, public IArdaRHIUniformBuffer
        {
        public:
            FUniformBuffer(FArdaRHIUniformBufferDesc Desc,
                FArdaRHIBufferRef Buffer, const void* Owner)
                : FResource(EArdaRHIResourceType::UniformBuffer, Desc.mDebugName, Owner)
                , mDesc(std::move(Desc))
                , mBuffer(std::move(Buffer)) {}
            const FArdaRHIUniformBufferDesc& GetDesc() const noexcept override { return mDesc; }
            const FArdaRHIBufferRef& GetBuffer() const noexcept override { return mBuffer; }
            FArdaRHIUniformBufferDesc mDesc;
            FArdaRHIBufferRef mBuffer;
        };

        template <typename Interface, typename Desc, EArdaRHIResourceType Type>
        class TLogicalState final : public FResource, public Interface
        {
        public:
            TLogicalState(const Desc& Descriptor, const void* Owner)
                : FResource(Type, "CachedState", Owner), mDesc(Descriptor) {}
            const Desc& GetDesc() const noexcept override { return mDesc; }
        private:
            Desc mDesc;
        };
        using FRasterState = TLogicalState<IArdaRHIRasterState,
            FArdaRHIRasterState, EArdaRHIResourceType::RasterState>;
        using FBlendState = TLogicalState<IArdaRHIBlendState,
            FArdaRHIBlendState, EArdaRHIResourceType::BlendState>;
        using FDepthStencilState = TLogicalState<IArdaRHIDepthStencilState,
            FArdaRHIDepthStencilState, EArdaRHIResourceType::DepthStencilState>;

        template <typename Interface, typename Handle, EArdaRHIResourceType Type>
        class TOpaqueResource final : public FResource, public Interface
        {
        public:
            TOpaqueResource(const char* Name, Handle NativeHandle, const void* Owner)
                : FResource(Type, Name ? Name : "", Owner), mHandle(std::move(NativeHandle)) {}
            Handle mHandle;
        };

        using FEventQuery = TOpaqueResource<IArdaRHIEventQuery, nvrhi::EventQueryHandle, EArdaRHIResourceType::EventQuery>;
        using FTimerQuery = TOpaqueResource<IArdaRHITimerQuery, nvrhi::TimerQueryHandle, EArdaRHIResourceType::TimerQuery>;
        using FGpuFence = TOpaqueResource<IArdaRHIGpuFence, nvrhi::EventQueryHandle, EArdaRHIResourceType::GpuFence>;
        using FShaderLibrary = TOpaqueResource<IArdaRHIShaderLibrary, nvrhi::ShaderLibraryHandle, EArdaRHIResourceType::ShaderLibrary>;

        class FShader final : public FResource, public IArdaRHIShader
        {
        public:
            FShader(const FArdaRHIShaderDesc& Desc, nvrhi::ShaderHandle Handle, const void* Owner)
                : FResource(EArdaRHIResourceType::Shader, Desc.mDebugName, Owner), mStage(Desc.mStage), mHandle(std::move(Handle)) {}
            EArdaRHIShaderStage GetStage() const noexcept override { return mStage; }
            EArdaRHIShaderStage mStage;
            nvrhi::ShaderHandle mHandle;
        };

        class FDescriptorTable final : public FResource, public IArdaRHIDescriptorTable
        {
        public:
            FDescriptorTable(FArdaRHIBindingLayoutRef Layout, nvrhi::DescriptorTableHandle Handle, const void* Owner)
                : FResource(EArdaRHIResourceType::DescriptorTable, "DescriptorTable", Owner),
                  mLayout(std::move(Layout)), mHandle(std::move(Handle))
            {
                mDesc.mLayout = mLayout;
            }
            const FArdaRHIBindingSetDesc& GetDesc() const noexcept override { return mDesc; }
            uint32_t GetCapacity() const noexcept override { return mHandle->getCapacity(); }
            uint32_t GetFirstDescriptorIndexInHeap() const noexcept override { return mHandle->getFirstDescriptorIndexInHeap(); }
            FArdaRHIBindingLayoutRef mLayout;
            nvrhi::DescriptorTableHandle mHandle;
        private:
            FArdaRHIBindingSetDesc mDesc;
        };

        class FOpacityMicromap final : public FResource, public IArdaRHIOpacityMicromap
        {
        public:
            FOpacityMicromap(FArdaRHIOpacityMicromapDesc Desc, nvrhi::rt::OpacityMicromapHandle Handle, const void* Owner)
                : FResource(EArdaRHIResourceType::OpacityMicromap, Desc.mDebugName, Owner), mDesc(std::move(Desc)), mHandle(std::move(Handle)) {}
            const FArdaRHIOpacityMicromapDesc& GetDesc() const noexcept override { return mDesc; }
            bool IsCompacted() const noexcept override { return mHandle->isCompacted(); }
            uint64_t GetDeviceAddress() const noexcept override { return mHandle->getDeviceAddress(); }
            FArdaRHIOpacityMicromapDesc mDesc;
            nvrhi::rt::OpacityMicromapHandle mHandle;
        };

        class FAccelStruct final : public FResource, public IArdaRHIAccelStruct
        {
        public:
            FAccelStruct(FArdaRHIAccelStructDesc Desc, nvrhi::rt::AccelStructHandle Handle, const void* Owner)
                : FResource(EArdaRHIResourceType::AccelStruct, Desc.mDebugName, Owner), mDesc(std::move(Desc)), mHandle(std::move(Handle)) {}
            const FArdaRHIAccelStructDesc& GetDesc() const noexcept override { return mDesc; }
            bool IsCompacted() const noexcept override { return mHandle->isCompacted(); }
            uint64_t GetDeviceAddress() const noexcept override { return mHandle->getDeviceAddress(); }
            const void* GetPhysicalIdentity() const noexcept override { return mHandle.Get(); }
            FArdaRHIAccelStructDesc mDesc;
            nvrhi::rt::AccelStructHandle mHandle;
        };

        class FShaderTable final : public FResource, public IArdaRHIShaderTable
        {
        public:
            FShaderTable(FArdaRHIShaderTableDesc Desc, nvrhi::rt::ShaderTableHandle Handle, const void* Owner)
                : FResource(EArdaRHIResourceType::ShaderTable, Desc.mDebugName, Owner), mDesc(std::move(Desc)), mHandle(std::move(Handle)) {}
            const FArdaRHIShaderTableDesc& GetDesc() const noexcept override { return mDesc; }
            uint32_t GetEntryCount() const noexcept override { return mHandle->getNumEntries(); }
            FArdaRHIShaderTableDesc mDesc;
            nvrhi::rt::ShaderTableHandle mHandle;
        };

        class FSamplerFeedbackTexture final : public FResource, public IArdaRHISamplerFeedbackTexture
        {
        public:
            FSamplerFeedbackTexture(FArdaRHISamplerFeedbackTextureDesc Desc, FArdaRHITextureRef Paired,
                nvrhi::SamplerFeedbackTextureHandle Handle, const void* Owner)
                : FResource(EArdaRHIResourceType::SamplerFeedbackTexture, Desc.mDebugName, Owner),
                  mDesc(std::move(Desc)), mPaired(std::move(Paired)), mHandle(std::move(Handle)) {}
            const FArdaRHISamplerFeedbackTextureDesc& GetDesc() const noexcept override { return mDesc; }
            const FArdaRHITextureRef& GetPairedTexture() const noexcept override { return mPaired; }
            FArdaRHISamplerFeedbackTextureDesc mDesc;
            FArdaRHITextureRef mPaired;
            nvrhi::SamplerFeedbackTextureHandle mHandle;
        };

        class FInputLayout final : public FResource, public IArdaRHIInputLayout
        {
        public:
            FInputLayout(FArdaRHIInputLayoutDesc Desc, nvrhi::InputLayoutHandle Handle, const void* Owner)
                : FResource(EArdaRHIResourceType::InputLayout, "InputLayout", Owner), mDesc(std::move(Desc)), mHandle(std::move(Handle)) {}
            const FArdaRHIInputLayoutDesc& GetDesc() const noexcept override { return mDesc; }
            FArdaRHIInputLayoutDesc mDesc;
            nvrhi::InputLayoutHandle mHandle;
        };

        template <typename Interface, EArdaRHIResourceType Type>
        class TView final : public FResource, public Interface
        {
        public:
            TView(TArdaRHIRef<IArdaRHIResource> Resource, const FArdaRHIViewDesc& Desc, const void* Owner)
                : FResource(Type, Type == EArdaRHIResourceType::ShaderResourceView ? "SRV" : "UAV", Owner),
                  mResource(std::move(Resource)), mDesc(Desc) {}
            IArdaRHIResource* GetResource() const noexcept override { return mResource.Get(); }
            const FArdaRHIViewDesc& GetDesc() const noexcept override { return mDesc; }
            TArdaRHIRef<IArdaRHIResource> mResource;
            FArdaRHIViewDesc mDesc;
        };

        using FShaderResourceView = TView<IArdaRHIShaderResourceView, EArdaRHIResourceType::ShaderResourceView>;
        using FUnorderedAccessView = TView<IArdaRHIUnorderedAccessView, EArdaRHIResourceType::UnorderedAccessView>;

        template <typename T>
        T* Cast(IArdaRHIResource* Resource) noexcept { return dynamic_cast<T*>(Resource); }
        template <typename T>
        const T* Cast(const IArdaRHIResource* Resource) noexcept { return dynamic_cast<const T*>(Resource); }

        nvrhi::ResourceType ToNvrhiBinding(EArdaRHIBindingType Type) noexcept
        {
            switch (Type)
            {
            case EArdaRHIBindingType::TextureSRV: return nvrhi::ResourceType::Texture_SRV;
            case EArdaRHIBindingType::TextureUAV: return nvrhi::ResourceType::Texture_UAV;
            case EArdaRHIBindingType::TypedBufferSRV: return nvrhi::ResourceType::TypedBuffer_SRV;
            case EArdaRHIBindingType::TypedBufferUAV: return nvrhi::ResourceType::TypedBuffer_UAV;
            case EArdaRHIBindingType::StructuredBufferSRV: return nvrhi::ResourceType::StructuredBuffer_SRV;
            case EArdaRHIBindingType::StructuredBufferUAV: return nvrhi::ResourceType::StructuredBuffer_UAV;
            case EArdaRHIBindingType::RawBufferSRV: return nvrhi::ResourceType::RawBuffer_SRV;
            case EArdaRHIBindingType::RawBufferUAV: return nvrhi::ResourceType::RawBuffer_UAV;
            case EArdaRHIBindingType::ConstantBuffer: return nvrhi::ResourceType::ConstantBuffer;
            case EArdaRHIBindingType::VolatileConstantBuffer: return nvrhi::ResourceType::VolatileConstantBuffer;
            case EArdaRHIBindingType::Sampler: return nvrhi::ResourceType::Sampler;
            case EArdaRHIBindingType::PushConstants: return nvrhi::ResourceType::PushConstants;
            case EArdaRHIBindingType::RayTracingAccelStruct: return nvrhi::ResourceType::RayTracingAccelStruct;
            case EArdaRHIBindingType::SamplerFeedbackTextureUAV: return nvrhi::ResourceType::SamplerFeedbackTexture_UAV;
            }
            return nvrhi::ResourceType::None;
        }

        nvrhi::PrimitiveType ToNvrhiPrimitive(EArdaRHIPrimitiveTopology V) noexcept
        {
            switch (V)
            {
            case EArdaRHIPrimitiveTopology::PointList: return nvrhi::PrimitiveType::PointList;
            case EArdaRHIPrimitiveTopology::LineList: return nvrhi::PrimitiveType::LineList;
            case EArdaRHIPrimitiveTopology::LineStrip: return nvrhi::PrimitiveType::LineStrip;
            case EArdaRHIPrimitiveTopology::TriangleList: return nvrhi::PrimitiveType::TriangleList;
            case EArdaRHIPrimitiveTopology::TriangleStrip: return nvrhi::PrimitiveType::TriangleStrip;
            case EArdaRHIPrimitiveTopology::PatchList: return nvrhi::PrimitiveType::PatchList;
            }
            return nvrhi::PrimitiveType::TriangleList;
        }

        nvrhi::BlendFactor ToNvrhiBlend(EArdaRHIBlendFactor V) noexcept
        {
            switch (V)
            {
            case EArdaRHIBlendFactor::Zero: return nvrhi::BlendFactor::Zero;
            case EArdaRHIBlendFactor::One: return nvrhi::BlendFactor::One;
            case EArdaRHIBlendFactor::SourceColor: return nvrhi::BlendFactor::SrcColor;
            case EArdaRHIBlendFactor::InverseSourceColor: return nvrhi::BlendFactor::InvSrcColor;
            case EArdaRHIBlendFactor::SourceAlpha: return nvrhi::BlendFactor::SrcAlpha;
            case EArdaRHIBlendFactor::InverseSourceAlpha: return nvrhi::BlendFactor::InvSrcAlpha;
            case EArdaRHIBlendFactor::DestinationAlpha: return nvrhi::BlendFactor::DstAlpha;
            case EArdaRHIBlendFactor::InverseDestinationAlpha: return nvrhi::BlendFactor::InvDstAlpha;
            case EArdaRHIBlendFactor::DestinationColor: return nvrhi::BlendFactor::DstColor;
            case EArdaRHIBlendFactor::InverseDestinationColor: return nvrhi::BlendFactor::InvDstColor;
            }
            return nvrhi::BlendFactor::One;
        }

        nvrhi::BindingSetItem LowerBinding(const FArdaRHIBindingItem& Item)
        {
            IArdaRHIResource* Resource = Item.mResource.Get();
            if (auto* View = Cast<FShaderResourceView>(Resource))
                Resource = View->mResource.Get();
            else if (auto* View = Cast<FUnorderedAccessView>(Resource))
                Resource = View->mResource.Get();

            nvrhi::BindingSetItem Result = nvrhi::BindingSetItem::None(Item.mSlot);
            if (auto* Texture = Cast<FTexture>(Resource))
            {
                if (Item.mType == EArdaRHIBindingType::TextureUAV)
                    Result = nvrhi::BindingSetItem::Texture_UAV(Item.mSlot, Texture->mHandle, ToNvrhi(Item.mView.mFormat), ToNvrhi(Item.mView.mTextureRange), ToNvrhi(Item.mView.mDimension));
                else
                    Result = nvrhi::BindingSetItem::Texture_SRV(Item.mSlot, Texture->mHandle, ToNvrhi(Item.mView.mFormat), ToNvrhi(Item.mView.mTextureRange), ToNvrhi(Item.mView.mDimension));
            }
            else if (auto* Buffer = Cast<FBuffer>(Resource))
            {
                const auto Range = ToNvrhi(Item.mView.mBufferRange);
                switch (Item.mType)
                {
                case EArdaRHIBindingType::TypedBufferSRV: Result = nvrhi::BindingSetItem::TypedBuffer_SRV(Item.mSlot, Buffer->mHandle, ToNvrhi(Item.mView.mFormat), Range); break;
                case EArdaRHIBindingType::TypedBufferUAV: Result = nvrhi::BindingSetItem::TypedBuffer_UAV(Item.mSlot, Buffer->mHandle, ToNvrhi(Item.mView.mFormat), Range); break;
                case EArdaRHIBindingType::StructuredBufferSRV: Result = nvrhi::BindingSetItem::StructuredBuffer_SRV(Item.mSlot, Buffer->mHandle, nvrhi::Format::UNKNOWN, Range); break;
                case EArdaRHIBindingType::StructuredBufferUAV: Result = nvrhi::BindingSetItem::StructuredBuffer_UAV(Item.mSlot, Buffer->mHandle, nvrhi::Format::UNKNOWN, Range); break;
                case EArdaRHIBindingType::RawBufferSRV: Result = nvrhi::BindingSetItem::RawBuffer_SRV(Item.mSlot, Buffer->mHandle, Range); break;
                case EArdaRHIBindingType::RawBufferUAV: Result = nvrhi::BindingSetItem::RawBuffer_UAV(Item.mSlot, Buffer->mHandle, Range); break;
                case EArdaRHIBindingType::ConstantBuffer:
                case EArdaRHIBindingType::VolatileConstantBuffer: Result = nvrhi::BindingSetItem::ConstantBuffer(Item.mSlot, Buffer->mHandle, Range); break;
                default: break;
                }
            }
            else if (auto* Sampler = Cast<FSampler>(Resource))
                Result = nvrhi::BindingSetItem::Sampler(Item.mSlot, Sampler->mHandle);
            else if (auto* AccelStruct = Cast<FAccelStruct>(Resource))
                Result = nvrhi::BindingSetItem::RayTracingAccelStruct(Item.mSlot, AccelStruct->mHandle);
            else if (auto* Feedback = Cast<FSamplerFeedbackTexture>(Resource))
                Result = nvrhi::BindingSetItem::SamplerFeedbackTexture_UAV(Item.mSlot, Feedback->mHandle);
            else if (Item.mType == EArdaRHIBindingType::PushConstants)
                Result = nvrhi::BindingSetItem::PushConstants(Item.mSlot, static_cast<uint32_t>(Item.mView.mBufferRange.mByteSize));
            Result.setArrayElement(Item.mArrayElement);
            return Result;
        }

        nvrhi::TextureSlice ToNvrhiSlice(const FArdaRHITextureSlice& V)
        {
            return nvrhi::TextureSlice().setOrigin(V.mX, V.mY, V.mZ)
                .setSize(V.mWidth, V.mHeight, V.mDepth)
                .setMipLevel(V.mMipLevel).setArraySlice(V.mArraySlice);
        }

        nvrhi::rt::OpacityMicromapDesc LowerOpacityMicromapDesc(const FArdaRHIOpacityMicromapDesc& D)
        {
            nvrhi::rt::OpacityMicromapDesc N;
            N.debugName = D.mDebugName.c_str();
            N.trackLiveness = D.mbTrackLiveness;
            N.flags = static_cast<nvrhi::rt::OpacityMicromapBuildFlags>(D.mFlags);
            for (const auto& C : D.mCounts)
                N.counts.push_back({ C.mCount, C.mSubdivisionLevel, static_cast<nvrhi::rt::OpacityMicromapFormat>(C.mFormat) });
            if (auto* B = Cast<FBuffer>(D.mInputBuffer.Get())) N.inputBuffer = B->mHandle;
            N.inputBufferOffset = D.mInputBufferOffset;
            if (auto* B = Cast<FBuffer>(D.mPerMicromapDescBuffer.Get())) N.perOmmDescs = B->mHandle;
            N.perOmmDescsOffset = D.mPerMicromapDescBufferOffset;
            return N;
        }

        nvrhi::rt::GeometryDesc LowerGeometry(const FArdaRHIRayTracingGeometryDesc& D)
        {
            nvrhi::rt::GeometryDesc N;
            N.flags = static_cast<nvrhi::rt::GeometryFlags>(D.mFlags);
            if (D.mType == EArdaRHIRayTracingGeometryType::AABBs)
            {
                nvrhi::rt::GeometryAABBs A;
                if (auto* B = Cast<FBuffer>(D.mVertexOrAABBBuffer.Get())) A.buffer = B->mHandle;
                A.offset = D.mVertexOrAABBOffset; A.count = D.mVertexOrAABBCount; A.stride = D.mStride;
                N.setAABBs(A);
            }
            else
            {
                nvrhi::rt::GeometryTriangles T;
                if (auto* B = Cast<FBuffer>(D.mIndexBuffer.Get())) T.indexBuffer = B->mHandle;
                if (auto* B = Cast<FBuffer>(D.mVertexOrAABBBuffer.Get())) T.vertexBuffer = B->mHandle;
                T.indexFormat = ToNvrhi(D.mIndexFormat); T.vertexFormat = ToNvrhi(D.mVertexFormat);
                T.indexOffset = D.mIndexOffset; T.vertexOffset = D.mVertexOrAABBOffset;
                T.indexCount = D.mIndexCount; T.vertexCount = D.mVertexOrAABBCount; T.vertexStride = D.mStride;
                if (auto* O = Cast<FOpacityMicromap>(D.mOpacityMicromap.Get())) T.opacityMicromap = O->mHandle;
                if (auto* B = Cast<FBuffer>(D.mOpacityMicromapIndexBuffer.Get())) T.ommIndexBuffer = B->mHandle;
                T.ommIndexBufferOffset = D.mOpacityMicromapIndexOffset;
                T.ommIndexFormat = ToNvrhi(D.mOpacityMicromapIndexFormat);
                N.setTriangles(T);
            }
            return N;
        }

        FArdaRHIStatus ValidateGeometryBasics(
            const FArdaRHIRayTracingGeometryDesc& D)
        {
            if (!D.mVertexOrAABBBuffer || !D.mVertexOrAABBCount || !D.mStride)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "Ray-tracing geometry requires a vertex/AABB buffer, count, and stride.");
            if (D.mType == EArdaRHIRayTracingGeometryType::Triangles &&
                D.mIndexCount && (!D.mIndexBuffer ||
                    (D.mIndexFormat != EArdaRHIFormat::R16UInt &&
                     D.mIndexFormat != EArdaRHIFormat::R32UInt)))
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "Indexed triangle geometry requires an index buffer and R16UInt or R32UInt format.");
            if (D.mOpacityMicromap && !D.mOpacityMicromapIndexBuffer)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "Opacity-micromap geometry requires an index buffer.");
            return {};
        }

        class FCommandList final : public FResource, public IArdaRHICommandList
        {
        public:
            FCommandList(nvrhi::CommandListHandle Handle, EArdaRHIQueueType Queue, const void* Owner,
                IArdaRHIDevice* Device, bool Meshlets, bool RayTracing,
                bool SamplerFeedback, bool OpacityMicromap)
                : FResource(EArdaRHIResourceType::CommandList, "CommandList", Owner), mHandle(std::move(Handle)),
                  mDevice(Device), mQueue(Queue), mbMeshlets(Meshlets), mbRayTracing(RayTracing),
                  mbSamplerFeedback(SamplerFeedback), mbOpacityMicromap(OpacityMicromap) {}

            IArdaRHIDevice* GetDevice() const noexcept override { return mDevice; }
            EArdaRHIQueueType GetQueueType() const noexcept override { return mQueue; }
            FArdaRHIStatus Open() override { if (mbOpen) return Error("Command list is already open."); mHandle->open(); mbOpen = true; return {}; }
            FArdaRHIStatus Close() override { if (!mbOpen) return Error("Command list is not open."); mHandle->close(); mbOpen = false; return {}; }
            FArdaRHIStatus Reset() override
            {
                if (mbOpen) return Error("An open command list must be closed before reset.");
                mHandle->open();
                mbOpen = true;
                return {};
            }
            FArdaRHIStatus WriteBuffer(IArdaRHIBuffer& B, const void* Data, size_t Size, uint64_t Offset) override
            {
                auto* Native = Cast<FBuffer>(&B);
                if (!Native || !Owns(Native)) return Wrong();
                if (!Data || !Size || Offset + Size > B.GetDesc().mByteSize) return Invalid("Buffer write range is invalid.");
                mHandle->writeBuffer(Native->mHandle, Data, Size, Offset); return {};
            }
            FArdaRHIStatus CopyBuffer(IArdaRHIBuffer& D, uint64_t DO, IArdaRHIBuffer& S, uint64_t SO, uint64_t Size) override
            {
                auto* DN = Cast<FBuffer>(&D); auto* SN = Cast<FBuffer>(&S);
                if (!DN || !SN || !Owns(DN) || !Owns(SN)) return Wrong();
                if (!Size || DO + Size > D.GetDesc().mByteSize || SO + Size > S.GetDesc().mByteSize) return Invalid("Buffer copy range is invalid.");
                mHandle->copyBuffer(DN->mHandle, DO, SN->mHandle, SO, Size); return {};
            }
            FArdaRHIStatus CopyTextureToStaging(IArdaRHIStagingTexture& D, const FArdaRHITextureSlice& DS, IArdaRHITexture& S, const FArdaRHITextureSlice& SS) override
            {
                auto* DN = Cast<FStagingTexture>(&D); auto* SN = Cast<FTexture>(&S);
                if (!DN || !SN || !Owns(DN) || !Owns(SN)) return Wrong();
                mHandle->copyTexture(DN->mHandle, ToNvrhiSlice(DS), SN->mHandle, ToNvrhiSlice(SS)); return {};
            }
            FArdaRHIStatus CopyTextureFromStaging(IArdaRHITexture& D, const FArdaRHITextureSlice& DS, IArdaRHIStagingTexture& S, const FArdaRHITextureSlice& SS) override
            {
                auto* DN = Cast<FTexture>(&D); auto* SN = Cast<FStagingTexture>(&S);
                if (!DN || !SN || !Owns(DN) || !Owns(SN)) return Wrong();
                mHandle->copyTexture(DN->mHandle, ToNvrhiSlice(DS), SN->mHandle, ToNvrhiSlice(SS)); return {};
            }
            FArdaRHIStatus ClearTexture(IArdaRHITexture& T, const FArdaRHITextureSubresourceRange& R, const FArdaRHIColor& C) override
            {
                auto* N = Cast<FTexture>(&T); if (!N || !Owns(N)) return Wrong();
                mHandle->clearTextureFloat(N->mHandle, ToNvrhi(R), { C.mR, C.mG, C.mB, C.mA }); return {};
            }
            FArdaRHIStatus SetTextureState(IArdaRHITexture& T, const FArdaRHITextureSubresourceRange& R, EArdaRHIResourceState S) override
            {
                auto* N = Cast<FTexture>(&T); if (!N || !Owns(N)) return Wrong();
                mHandle->setTextureState(N->mHandle, ToNvrhi(R), ToNvrhi(S)); return {};
            }
            FArdaRHIStatus SetBufferState(IArdaRHIBuffer& B, EArdaRHIResourceState S) override
            {
                auto* N = Cast<FBuffer>(&B); if (!N || !Owns(N)) return Wrong();
                mHandle->setBufferState(N->mHandle, ToNvrhi(S)); return {};
            }
            FArdaRHIStatus SetAccelStructState(IArdaRHIAccelStruct& A, EArdaRHIResourceState S) override
            {
                auto* N = Cast<FAccelStruct>(&A); if (!N || !Owns(N)) return Wrong();
                if (!mbRayTracing) return Unsupported("Acceleration structures are unsupported.");
                mHandle->setAccelStructState(N->mHandle, ToNvrhi(S)); return {};
            }
            void SetAutomaticBarriers(bool bEnabled) override { mHandle->setEnableAutomaticBarriers(bEnabled); }
            FArdaRHIStatus BeginTrackingTextureState(IArdaRHITexture& T, const FArdaRHITextureSubresourceRange& R, EArdaRHIResourceState S) override
            {
                auto* N = Cast<FTexture>(&T); if (!N || !Owns(N)) return Wrong();
                mHandle->beginTrackingTextureState(N->mHandle, ToNvrhi(R), ToNvrhi(S)); return {};
            }
            FArdaRHIStatus BeginTrackingBufferState(IArdaRHIBuffer& B, EArdaRHIResourceState S) override
            {
                auto* N = Cast<FBuffer>(&B); if (!N || !Owns(N)) return Wrong();
                mHandle->beginTrackingBufferState(N->mHandle, ToNvrhi(S)); return {};
            }
            FArdaRHIStatus SetUAVBarriersForTexture(IArdaRHITexture& T, bool bEnabled) override
            {
                auto* N = Cast<FTexture>(&T); if (!N || !Owns(N)) return Wrong();
                mHandle->setEnableUavBarriersForTexture(N->mHandle, bEnabled); return {};
            }
            FArdaRHIStatus SetUAVBarriersForBuffer(IArdaRHIBuffer& B, bool bEnabled) override
            {
                auto* N = Cast<FBuffer>(&B); if (!N || !Owns(N)) return Wrong();
                mHandle->setEnableUavBarriersForBuffer(N->mHandle, bEnabled); return {};
            }
            void CommitBarriers() override { mHandle->commitBarriers(); }
            FArdaRHIStatus ClearTextureUInt(IArdaRHITexture& T, const FArdaRHITextureSubresourceRange& R, uint32_t V) override
            {
                auto* N = Cast<FTexture>(&T); if (!N || !Owns(N)) return Wrong();
                mHandle->clearTextureUInt(N->mHandle, ToNvrhi(R), V); return {};
            }
            FArdaRHIStatus ClearDepthStencilTexture(IArdaRHITexture& T, const FArdaRHITextureSubresourceRange& R, bool bDepth, float Depth, bool bStencil, uint8_t Stencil) override
            {
                auto* N = Cast<FTexture>(&T); if (!N || !Owns(N)) return Wrong();
                mHandle->clearDepthStencilTexture(N->mHandle, ToNvrhi(R), bDepth, Depth, bStencil, Stencil); return {};
            }
            FArdaRHIStatus ClearBufferUInt(IArdaRHIBuffer& B, uint32_t V) override
            {
                auto* N = Cast<FBuffer>(&B); if (!N || !Owns(N)) return Wrong();
                mHandle->clearBufferUInt(N->mHandle, V); return {};
            }
            FArdaRHIStatus SetGraphicsState(const FArdaRHIGraphicsState& S) override
            {
                auto* P = Cast<FGraphicsPipeline>(S.mPipeline.Get()); auto* F = Cast<FFramebuffer>(S.mFramebuffer.Get());
                if (!P || !F || !Owns(P) || !Owns(F)) return Wrong();
                nvrhi::GraphicsState N; N.pipeline = P->mHandle; N.framebuffer = F->mHandle;
                for (const auto& B : S.mBindings) { auto* V = Cast<FBindingSet>(B.Get()); auto* T = Cast<FDescriptorTable>(B.Get()); if ((!V && !T) || !Owns(Cast<FResource>(B.Get()))) return Wrong(); N.addBindingSet(V ? static_cast<nvrhi::IBindingSet*>(V->mHandle.Get()) : T->mHandle.Get()); }
                for (const auto& B : S.mVertexBuffers) { auto* V = Cast<FBuffer>(B.mBuffer.Get()); if (!V || !Owns(V)) return Wrong(); N.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(V->mHandle).setSlot(B.mSlot).setOffset(B.mOffset)); }
                if (S.mIndexBuffer) { auto* V = Cast<FBuffer>(S.mIndexBuffer.Get()); if (!V || !Owns(V)) return Wrong(); N.setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(V->mHandle).setFormat(ToNvrhi(S.mIndexFormat)).setOffset(S.mIndexOffset)); }
                for (const auto& V : S.mViewports) N.viewport.addViewport({ V.mMinX, V.mMaxX, V.mMinY, V.mMaxY, V.mMinZ, V.mMaxZ });
                for (const auto& R : S.mScissors) N.viewport.addScissorRect({ R.mMinX, R.mMaxX, R.mMinY, R.mMaxY });
                mHandle->setGraphicsState(N); return {};
            }
            FArdaRHIStatus SetComputeState(const FArdaRHIComputeState& S) override
            {
                auto* P = Cast<FComputePipeline>(S.mPipeline.Get()); if (!P || !Owns(P)) return Wrong();
                nvrhi::ComputeState N; N.pipeline = P->mHandle;
                for (const auto& B : S.mBindings) { auto* V = Cast<FBindingSet>(B.Get()); auto* T = Cast<FDescriptorTable>(B.Get()); if ((!V && !T) || !Owns(Cast<FResource>(B.Get()))) return Wrong(); N.addBindingSet(V ? static_cast<nvrhi::IBindingSet*>(V->mHandle.Get()) : T->mHandle.Get()); }
                mHandle->setComputeState(N); return {};
            }
            FArdaRHIStatus SetMeshletState(const FArdaRHIMeshletState& S) override
            {
                if (!mbMeshlets) return Unsupported("Mesh shaders are unsupported.");
                auto* P = Cast<FMeshletPipeline>(S.mPipeline.Get()); auto* F = Cast<FFramebuffer>(S.mFramebuffer.Get());
                if (!P || !F || !Owns(P) || !Owns(F)) return Wrong();
                nvrhi::MeshletState N; N.pipeline = P->mHandle; N.framebuffer = F->mHandle;
                for (const auto& B : S.mBindings) { auto* V = Cast<FBindingSet>(B.Get()); auto* T = Cast<FDescriptorTable>(B.Get()); if ((!V && !T) || !Owns(Cast<FResource>(B.Get()))) return Wrong(); N.addBindingSet(V ? static_cast<nvrhi::IBindingSet*>(V->mHandle.Get()) : T->mHandle.Get()); }
                for (const auto& V : S.mViewports) N.viewport.addViewport({ V.mMinX, V.mMaxX, V.mMinY, V.mMaxY, V.mMinZ, V.mMaxZ });
                for (const auto& R : S.mScissors) N.viewport.addScissorRect({ R.mMinX, R.mMaxX, R.mMinY, R.mMaxY });
                mHandle->setMeshletState(N); return {};
            }
            FArdaRHIStatus SetRayTracingState(const FArdaRHIRayTracingState& S) override
            {
                if (!mbRayTracing) return Unsupported("Ray tracing pipelines are unsupported.");
                auto* T = Cast<FShaderTable>(S.mShaderTable.Get()); if (!T || !Owns(T)) return Wrong();
                nvrhi::rt::State N; N.shaderTable = T->mHandle;
                for (const auto& B : S.mBindings) { auto* V = Cast<FBindingSet>(B.Get()); auto* D = Cast<FDescriptorTable>(B.Get()); if ((!V && !D) || !Owns(Cast<FResource>(B.Get()))) return Wrong(); N.addBindingSet(V ? static_cast<nvrhi::IBindingSet*>(V->mHandle.Get()) : D->mHandle.Get()); }
                mHandle->setRayTracingState(N); return {};
            }
            void SetPushConstants(const void* D, size_t S) override { mHandle->setPushConstants(D, S); }
            void Draw(const FArdaRHIDrawArguments& A) override { mHandle->draw(ToDraw(A)); }
            void DrawIndexed(const FArdaRHIDrawArguments& A) override { mHandle->drawIndexed(ToDraw(A)); }
            void Dispatch(uint32_t X, uint32_t Y, uint32_t Z) override { mHandle->dispatch(X, Y, Z); }
            FArdaRHIStatus DispatchMesh(uint32_t X, uint32_t Y, uint32_t Z) override
            {
                if (!mbMeshlets) return Unsupported("Mesh shaders are unsupported.");
                if (!X || !Y || !Z) return Invalid("Mesh dispatch dimensions must be non-zero.");
                mHandle->dispatchMesh(X, Y, Z); return {};
            }
            FArdaRHIStatus DispatchRays(uint32_t X, uint32_t Y, uint32_t Z) override
            {
                if (!mbRayTracing) return Unsupported("Ray tracing pipelines are unsupported.");
                if (!X || !Y || !Z) return Invalid("Ray dispatch dimensions must be non-zero.");
                mHandle->dispatchRays(nvrhi::rt::DispatchRaysArguments().setDimensions(X, Y, Z)); return {};
            }
            FArdaRHIStatus BuildBottomLevelAccelStruct(IArdaRHIAccelStruct& A, const eastl::vector<FArdaRHIRayTracingGeometryDesc>& G, EArdaRHIAccelStructBuildFlags Flags) override
            {
                if (!mbRayTracing) return Unsupported("Acceleration structures are unsupported.");
                auto* N = Cast<FAccelStruct>(&A); if (!N || !Owns(N) || N->mDesc.mbTopLevel) return Invalid("BLAS build requires an owned bottom-level acceleration structure.");
                eastl::vector<nvrhi::rt::GeometryDesc> Native; Native.reserve(G.size());
                eastl::vector<eastl::vector<nvrhi::rt::OpacityMicromapUsageCount>> UsageCounts; UsageCounts.reserve(G.size());
                for (const auto& V : G) { if (auto S = ValidateGeometryBasics(V); !S) return S; if (!ValidateGeometry(V)) return Wrong(); Native.push_back(LowerGeometry(V)); UsageCounts.emplace_back(); for (const auto& C : V.mOpacityMicromapUsageCounts) UsageCounts.back().push_back({ C.mCount, C.mSubdivisionLevel, static_cast<nvrhi::rt::OpacityMicromapFormat>(C.mFormat) }); if (!UsageCounts.back().empty() && V.mType == EArdaRHIRayTracingGeometryType::Triangles) { Native.back().geometryData.triangles.pOmmUsageCounts = UsageCounts.back().data(); Native.back().geometryData.triangles.numOmmUsageCounts = static_cast<uint32_t>(UsageCounts.back().size()); } }
                if (Native.empty()) return Invalid("BLAS build requires geometry.");
                mHandle->buildBottomLevelAccelStruct(N->mHandle, Native.data(), Native.size(), static_cast<nvrhi::rt::AccelStructBuildFlags>(Flags)); return {};
            }
            FArdaRHIStatus BuildTopLevelAccelStruct(IArdaRHIAccelStruct& A, const eastl::vector<FArdaRHIRayTracingInstanceDesc>& I, EArdaRHIAccelStructBuildFlags Flags) override
            {
                if (!mbRayTracing) return Unsupported("Acceleration structures are unsupported.");
                auto* N = Cast<FAccelStruct>(&A); if (!N || !Owns(N) || !N->mDesc.mbTopLevel) return Invalid("TLAS build requires an owned top-level acceleration structure.");
                eastl::vector<nvrhi::rt::InstanceDesc> Native; Native.reserve(I.size());
                for (const auto& V : I) { auto* B = Cast<FAccelStruct>(V.mBottomLevelAccelStruct.Get()); if (!B || !Owns(B) || B->mDesc.mbTopLevel) return Wrong(); nvrhi::rt::InstanceDesc D; std::memcpy(D.transform, V.mTransform, sizeof(D.transform)); D.instanceID = V.mInstanceId; D.instanceMask = V.mInstanceMask; D.instanceContributionToHitGroupIndex = V.mHitGroupContribution; D.flags = static_cast<nvrhi::rt::InstanceFlags>(V.mFlags); D.bottomLevelAS = B->mHandle; Native.push_back(D); }
                mHandle->buildTopLevelAccelStruct(N->mHandle, Native.data(), Native.size(), static_cast<nvrhi::rt::AccelStructBuildFlags>(Flags)); return {};
            }
            FArdaRHIStatus BuildTopLevelAccelStructFromBuffer(IArdaRHIAccelStruct& A, IArdaRHIBuffer& B, uint64_t O, size_t Count, EArdaRHIAccelStructBuildFlags Flags) override
            {
                if (!mbRayTracing) return Unsupported("Acceleration structures are unsupported.");
                auto* N = Cast<FAccelStruct>(&A); auto* Buffer = Cast<FBuffer>(&B);
                if (!N || !Buffer || !Owns(N) || !Owns(Buffer)) return Wrong();
                if (!N->mDesc.mbTopLevel || !Count || O >= B.GetDesc().mByteSize) return Invalid("Buffered TLAS build arguments are invalid.");
                mHandle->buildTopLevelAccelStructFromBuffer(N->mHandle, Buffer->mHandle, O, Count, static_cast<nvrhi::rt::AccelStructBuildFlags>(Flags)); return {};
            }
            FArdaRHIStatus BuildOpacityMicromap(IArdaRHIOpacityMicromap& O) override
            {
                if (!mbOpacityMicromap) return Unsupported("Opacity micromaps are unsupported.");
                auto* N = Cast<FOpacityMicromap>(&O); if (!N || !Owns(N)) return Wrong();
                auto D = LowerOpacityMicromapDesc(N->mDesc); mHandle->buildOpacityMicromap(N->mHandle, D); return {};
            }
            FArdaRHIStatus ClearSamplerFeedbackTexture(IArdaRHISamplerFeedbackTexture& T) override
            {
                if (!mbSamplerFeedback) return Unsupported("Sampler feedback is unsupported.");
                auto* N = Cast<FSamplerFeedbackTexture>(&T); if (!N || !Owns(N)) return Wrong(); mHandle->clearSamplerFeedbackTexture(N->mHandle); return {};
            }
            FArdaRHIStatus DecodeSamplerFeedbackTexture(IArdaRHIBuffer& B, IArdaRHISamplerFeedbackTexture& T, EArdaRHIFormat F) override
            {
                if (!mbSamplerFeedback) return Unsupported("Sampler feedback is unsupported.");
                auto* BN = Cast<FBuffer>(&B); auto* TN = Cast<FSamplerFeedbackTexture>(&T); if (!BN || !TN || !Owns(BN) || !Owns(TN)) return Wrong();
                mHandle->decodeSamplerFeedbackTexture(BN->mHandle, TN->mHandle, ToNvrhi(F)); return {};
            }
            FArdaRHIStatus SetSamplerFeedbackTextureState(IArdaRHISamplerFeedbackTexture& T, EArdaRHIResourceState S) override
            {
                if (!mbSamplerFeedback) return Unsupported("Sampler feedback is unsupported.");
                auto* N = Cast<FSamplerFeedbackTexture>(&T); if (!N || !Owns(N)) return Wrong(); mHandle->setSamplerFeedbackTextureState(N->mHandle, ToNvrhi(S)); return {};
            }
            FArdaRHIStatus BeginTimerQuery(IArdaRHITimerQuery& Q) override { auto* N = Cast<FTimerQuery>(&Q); if (!N || !Owns(N)) return Wrong(); mHandle->beginTimerQuery(N->mHandle); return {}; }
            FArdaRHIStatus EndTimerQuery(IArdaRHITimerQuery& Q) override { auto* N = Cast<FTimerQuery>(&Q); if (!N || !Owns(N)) return Wrong(); mHandle->endTimerQuery(N->mHandle); return {}; }
            void BeginMarker(const char* N) override { mHandle->beginMarker(N ? N : ""); }
            void EndMarker() override { mHandle->endMarker(); }

            nvrhi::CommandListHandle mHandle;
        private:
            static nvrhi::DrawArguments ToDraw(const FArdaRHIDrawArguments& A)
            {
                return nvrhi::DrawArguments().setVertexCount(A.mVertexCount).setInstanceCount(A.mInstanceCount)
                    .setStartIndexLocation(A.mStartIndex).setStartVertexLocation(A.mStartVertex).setStartInstanceLocation(A.mStartInstance);
            }
            bool Owns(const FResource* R) const noexcept { return R->GetOwner() == GetOwner(); }
            static FArdaRHIStatus Error(const char* M) { return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, M); }
            static FArdaRHIStatus Invalid(const char* M) { return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, M); }
            static FArdaRHIStatus Wrong() { return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice, "Resource belongs to another RHI device or implementation."); }
            static FArdaRHIStatus Unsupported(const char* M) { return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, M); }
            bool ValidateGeometry(const FArdaRHIRayTracingGeometryDesc& G) const
            {
                auto Owned = [this](const IArdaRHIResource* R) { return !R || (Cast<FResource>(R) && Owns(Cast<FResource>(R))); };
                return Owned(G.mIndexBuffer.Get()) && Owned(G.mVertexOrAABBBuffer.Get()) &&
                    Owned(G.mOpacityMicromap.Get()) && Owned(G.mOpacityMicromapIndexBuffer.Get());
            }
            IArdaRHIDevice* mDevice = nullptr;
            EArdaRHIQueueType mQueue;
            bool mbOpen = false;
            bool mbMeshlets = false, mbRayTracing = false, mbSamplerFeedback = false, mbOpacityMicromap = false;
        };

        template <typename Desc, typename Ref>
        class TDescriptorCache
        {
        public:
            [[nodiscard]] Ref Find(const Desc& D) const
            {
                for (const auto& Entry : mEntries)
                    if (Entry.mDesc == D) return Entry.mResource;
                return {};
            }

            template <typename Predicate>
            [[nodiscard]] bool ContainsIf(Predicate Matches) const
            {
                for (const auto& Entry : mEntries)
                    if (Matches(Entry.mDesc)) return true;
                return false;
            }

            void Insert(const Desc& D, const Ref& Resource)
            {
                if (mEntries.size() >= mCapacity)
                    mEntries.erase(mEntries.begin());
                mEntries.push_back({ D, Resource });
            }

            void Clear() { mEntries.clear(); }
            [[nodiscard]] size_t Size() const noexcept { return mEntries.size(); }

        private:
            struct FEntry
            {
                Desc mDesc;
                Ref mResource;
            };
            static constexpr size_t mCapacity = 64;
            eastl::vector<FEntry> mEntries;
        };

        class FDevice final : public FResource, public IArdaRHIDevice
        {
        public:
            explicit FDevice(nvrhi::DeviceHandle Device, eastl::shared_ptr<void> BackendLifetime)
                : FResource(EArdaRHIResourceType::Device, "ArdaNvrhiDevice", nullptr)
                , mBackendLifetime(std::move(BackendLifetime))
                , mDevice(std::move(Device))
            {
                SetOwner(this);
                mCapabilities.mbComputeQueue = mDevice->queryFeatureSupport(nvrhi::Feature::ComputeQueue);
                mCapabilities.mbCopyQueue = mDevice->queryFeatureSupport(nvrhi::Feature::CopyQueue);
                mCapabilities.mbConservativeRasterization = mDevice->queryFeatureSupport(nvrhi::Feature::ConservativeRasterization);
                mCapabilities.mbMeshShaders = mDevice->queryFeatureSupport(nvrhi::Feature::Meshlets);
                mCapabilities.mbRayTracing = mDevice->queryFeatureSupport(nvrhi::Feature::RayTracingPipeline);
                mCapabilities.mbSamplerFeedback = mDevice->queryFeatureSupport(nvrhi::Feature::SamplerFeedback);
                mCapabilities.mbVariableRateShading = mDevice->queryFeatureSupport(nvrhi::Feature::VariableRateShading);
                mCapabilities.mbVirtualResources = mDevice->queryFeatureSupport(nvrhi::Feature::VirtualResources);
                mCapabilities.mbHeaps = mCapabilities.mbVirtualResources;
                mCapabilities.mbBindless = mDevice->getGraphicsAPI() != nvrhi::GraphicsAPI::D3D11;
                mCapabilities.mbRayTracingAccelStruct = mDevice->queryFeatureSupport(nvrhi::Feature::RayTracingAccelStruct);
                mCapabilities.mbRayTracingOpacityMicromap = mDevice->queryFeatureSupport(nvrhi::Feature::RayTracingOpacityMicromap);
                mCapabilities.mbTiledTextures =
                    mCapabilities.mbVirtualResources &&
                    mDevice->getGraphicsAPI() == nvrhi::GraphicsAPI::D3D12;
            }

            const FArdaRHICapabilities& GetCapabilities() const noexcept override { return mCapabilities; }

            TArdaRHIResult<FArdaRHITextureRef> CreateTexture(const FArdaRHITextureDesc& D) override
            {
                if (auto S = Validate(D); !S) return { {}, S };
                if (D.mbVirtual && !mCapabilities.mbVirtualResources)
                    return Unsupported<FArdaRHITextureRef>("Virtual textures are unsupported.");
                if (D.mbTiled && !mCapabilities.mbTiledTextures)
                    return Unsupported<FArdaRHITextureRef>("Tiled textures are unsupported.");
                if (D.mbTiled && !D.mbVirtual)
                    return Invalid<FArdaRHITextureRef>("Tiled textures must also be virtual.");
                auto H = mDevice->createTexture(ToNvrhi(D)); if (!H) return Fail<FArdaRHITextureRef>("NVRHI failed to create texture.");
                return Ok(FArdaRHITextureRef(new FTexture(D, H, this)));
            }
            TArdaRHIResult<FArdaRHITextureReferenceRef> CreateTextureReference(
                const FArdaRHITextureRef& Texture) override
            {
                if (Texture && !Owns(Texture.Get()))
                    return Wrong<FArdaRHITextureReferenceRef>();
                return Ok(FArdaRHITextureReferenceRef(
                    new FTextureReference(Texture, this)));
            }
            FArdaRHIStatus SetTextureReference(
                const FArdaRHITextureReferenceRef& Reference,
                const FArdaRHITextureRef& Texture) override
            {
                auto* Native = Cast<FTextureReference>(Reference.Get());
                if (!Native || !Owns(Native) || (Texture && !Owns(Texture.Get())))
                    return WrongStatus();
                Native->mTexture = Texture;
                return {};
            }
            TArdaRHIResult<FArdaRHIBufferRef> CreateBuffer(const FArdaRHIBufferDesc& D) override
            {
                if (auto S = Validate(D); !S) return { {}, S };
                if (D.mbVirtual && !mCapabilities.mbVirtualResources)
                    return Unsupported<FArdaRHIBufferRef>("Virtual buffers are unsupported.");
                auto H = mDevice->createBuffer(ToNvrhi(D)); if (!H) return Fail<FArdaRHIBufferRef>("NVRHI failed to create buffer.");
                return Ok(FArdaRHIBufferRef(new FBuffer(D, H, this)));
            }
            TArdaRHIResult<FArdaRHIUniformBufferRef> CreateUniformBuffer(
                const FArdaRHIUniformBufferDesc& D,
                const void* InitialData) override
            {
                if (!D.mByteSize || !D.mMaxVersions)
                    return Invalid<FArdaRHIUniformBufferRef>("Uniform buffers require a non-zero size and version count.");
                FArdaRHIBufferDesc BufferDesc;
                BufferDesc.mByteSize = D.mByteSize;
                BufferDesc.mMaxVersions = D.mMaxVersions;
                BufferDesc.mUsage = EArdaRHIBufferUsage::Constant |
                    (D.mMaxVersions > 1 ? EArdaRHIBufferUsage::Volatile :
                        EArdaRHIBufferUsage::None);
                BufferDesc.mInitialState = EArdaRHIResourceState::ConstantBuffer;
                BufferDesc.mbKeepInitialState = true;
                BufferDesc.mDebugName = D.mDebugName;
                auto BufferResult = CreateBuffer(BufferDesc);
                if (!BufferResult) return { {}, BufferResult.mStatus };
                if (InitialData)
                {
                    auto* Buffer = Cast<FBuffer>(BufferResult.mValue.Get());
                    auto Commands = mDevice->createCommandList(
                        nvrhi::CommandListParameters().setEnableImmediateExecution(true));
                    if (!Commands)
                        return Fail<FArdaRHIUniformBufferRef>("Failed to create uniform-buffer upload commands.");
                    Commands->open();
                    Commands->writeBuffer(Buffer->mHandle, InitialData, D.mByteSize);
                    Commands->close();
                    if (!mDevice->executeCommandList(
                        Commands, nvrhi::CommandQueue::Graphics))
                        return Fail<FArdaRHIUniformBufferRef>("Failed to upload uniform-buffer contents.");
                }
                return Ok(FArdaRHIUniformBufferRef(new FUniformBuffer(
                    D, BufferResult.mValue, this)));
            }
            TArdaRHIResult<FArdaRHITextureRef> ImportNativeTexture(
                const FArdaRHINativeTextureImportDesc& D) override
            {
                if (!D.mNativeObject) return Invalid<FArdaRHITextureRef>("Native texture object is null.");
                if (D.mOwnership == EArdaRHINativeOwnership::Transferred)
                    return Unsupported<FArdaRHITextureRef>("Portable transferred ownership is not supported; import as Borrowed.");
                if (auto S = Validate(D.mTexture); !S) return { {}, S };
                std::lock_guard<std::mutex> Lock(mCacheMutex);
                if (auto Existing = mImportedTextures.Find(D)) return Ok(Existing);
                if (mImportedTextures.ContainsIf([&D](const auto& Existing)
                    {
                        return Existing.mNativeObject == D.mNativeObject &&
                            Existing.mNativeType == D.mNativeType;
                    }))
                    return Invalid<FArdaRHITextureRef>("Native texture was already imported with a conflicting descriptor or state.");

                nvrhi::ObjectType Type = 0;
                nvrhi::Object Object(static_cast<uint64_t>(0));
                const auto Api = mDevice->getGraphicsAPI();
#if defined(_WIN32)
                if (Api == nvrhi::GraphicsAPI::D3D12 &&
                    D.mNativeType == EArdaRHINativeResourceType::D3D12Resource)
                {
                    Type = nvrhi::ObjectTypes::D3D12_Resource;
                    Object = nvrhi::Object(reinterpret_cast<void*>(D.mNativeObject));
                }
                else
#endif
                if (Api == nvrhi::GraphicsAPI::VULKAN &&
                    D.mNativeType == EArdaRHINativeResourceType::VulkanImage)
                {
                    Type = nvrhi::ObjectTypes::VK_Image;
                    Object = nvrhi::Object(static_cast<uint64_t>(D.mNativeObject));
                }
                else return Unsupported<FArdaRHITextureRef>("Native texture type does not match the active backend.");

                FArdaRHITextureDesc Desc = D.mTexture;
                Desc.mInitialState = D.mInitialState;
                auto H = mDevice->createHandleForNativeTexture(Type, Object, ToNvrhi(Desc));
                if (!H) return Fail<FArdaRHITextureRef>("NVRHI failed to import the native texture.");
                FArdaRHITextureRef Result(new FTexture(Desc, H, this));
                mImportedTextures.Insert(D, Result);
                return Ok(Result);
            }
            TArdaRHIResult<FArdaRHIBufferRef> ImportNativeBuffer(
                const FArdaRHINativeBufferImportDesc& D) override
            {
                if (!D.mNativeObject) return Invalid<FArdaRHIBufferRef>("Native buffer object is null.");
                if (D.mOwnership == EArdaRHINativeOwnership::Transferred)
                    return Unsupported<FArdaRHIBufferRef>("Portable transferred ownership is not supported; import as Borrowed.");
                if (auto S = Validate(D.mBuffer); !S) return { {}, S };
                std::lock_guard<std::mutex> Lock(mCacheMutex);
                if (auto Existing = mImportedBuffers.Find(D)) return Ok(Existing);
                if (mImportedBuffers.ContainsIf([&D](const auto& Existing)
                    {
                        return Existing.mNativeObject == D.mNativeObject &&
                            Existing.mNativeType == D.mNativeType;
                    }))
                    return Invalid<FArdaRHIBufferRef>("Native buffer was already imported with a conflicting descriptor or state.");

                nvrhi::ObjectType Type = 0;
                nvrhi::Object Object(static_cast<uint64_t>(0));
                const auto Api = mDevice->getGraphicsAPI();
#if defined(_WIN32)
                if (Api == nvrhi::GraphicsAPI::D3D12 &&
                    D.mNativeType == EArdaRHINativeResourceType::D3D12Resource)
                {
                    Type = nvrhi::ObjectTypes::D3D12_Resource;
                    Object = nvrhi::Object(reinterpret_cast<void*>(D.mNativeObject));
                }
                else
#endif
                if (Api == nvrhi::GraphicsAPI::VULKAN &&
                    D.mNativeType == EArdaRHINativeResourceType::VulkanBuffer)
                {
                    Type = nvrhi::ObjectTypes::VK_Buffer;
                    Object = nvrhi::Object(static_cast<uint64_t>(D.mNativeObject));
                }
                else return Unsupported<FArdaRHIBufferRef>("Native buffer type does not match the active backend.");

                FArdaRHIBufferDesc Desc = D.mBuffer;
                Desc.mInitialState = D.mInitialState;
                auto H = mDevice->createHandleForNativeBuffer(Type, Object, ToNvrhi(Desc));
                if (!H) return Fail<FArdaRHIBufferRef>("NVRHI failed to import the native buffer.");
                FArdaRHIBufferRef Result(new FBuffer(Desc, H, this));
                mImportedBuffers.Insert(D, Result);
                return Ok(Result);
            }
            TArdaRHIResult<FArdaRHIHeapRef> CreateHeap(const FArdaRHIHeapDesc& D) override
            {
                if (!mCapabilities.mbHeaps) return Unsupported<FArdaRHIHeapRef>("Explicit heaps require virtual-resource support.");
                if (!D.mCapacity) return Invalid<FArdaRHIHeapRef>("Heap capacity must be non-zero.");
                nvrhi::HeapDesc N; N.capacity = D.mCapacity; N.type = static_cast<nvrhi::HeapType>(D.mType); N.debugName = D.mDebugName.c_str();
                auto H = mDevice->createHeap(N); if (!H) return Fail<FArdaRHIHeapRef>("NVRHI failed to create heap.");
                return Ok(FArdaRHIHeapRef(new FHeap(D, H, this)));
            }
            TArdaRHIResult<FArdaRHIStagingTextureRef> CreateStagingTexture(const FArdaRHIStagingTextureDesc& D) override
            {
                if (auto S = Validate(D.mTexture); !S) return { {}, S };
                if (D.mCpuAccess == EArdaRHICpuAccess::None) return Invalid<FArdaRHIStagingTextureRef>("Staging textures require read or write CPU access.");
                auto H = mDevice->createStagingTexture(ToNvrhi(D.mTexture), static_cast<nvrhi::CpuAccessMode>(D.mCpuAccess));
                if (!H) return Fail<FArdaRHIStagingTextureRef>("NVRHI failed to create staging texture.");
                return Ok(FArdaRHIStagingTextureRef(new FStagingTexture(D, H, this)));
            }
            TArdaRHIResult<FArdaRHIStagingTextureMapping> MapStagingTexture(const FArdaRHIStagingTextureRef& T, const FArdaRHITextureSlice& S, EArdaRHICpuAccess A) override
            {
                auto* N = Cast<FStagingTexture>(T.Get()); if (!N || !Owns(N)) return Wrong<FArdaRHIStagingTextureMapping>();
                if (A == EArdaRHICpuAccess::None || A != N->GetDesc().mCpuAccess) return Invalid<FArdaRHIStagingTextureMapping>("Map access must match staging texture CPU access.");
                size_t Pitch = 0; void* Data = mDevice->mapStagingTexture(N->mHandle, ToNvrhiSlice(S), static_cast<nvrhi::CpuAccessMode>(A), &Pitch);
                if (!Data) return Fail<FArdaRHIStagingTextureMapping>("NVRHI failed to map staging texture.");
                return Ok(FArdaRHIStagingTextureMapping{ Data, Pitch });
            }
            FArdaRHIStatus UnmapStagingTexture(const FArdaRHIStagingTextureRef& T) override
            {
                auto* N = Cast<FStagingTexture>(T.Get()); if (!N || !Owns(N)) return WrongStatus();
                mDevice->unmapStagingTexture(N->mHandle); return {};
            }
            TArdaRHIResult<FArdaRHIShaderResourceViewRef> CreateShaderResourceView(const TArdaRHIRef<IArdaRHIResource>& R, const FArdaRHIViewDesc& D) override
            {
                if (!IsViewable(R.Get()) || !Owns(R.Get())) return Invalid<FArdaRHIShaderResourceViewRef>("SRV resource is null, foreign, or not viewable.");
                if (auto S = Validate(D); !S) return { {}, S };
                return Ok(FArdaRHIShaderResourceViewRef(new FShaderResourceView(R, D, this)));
            }
            TArdaRHIResult<FArdaRHIUnorderedAccessViewRef> CreateUnorderedAccessView(const TArdaRHIRef<IArdaRHIResource>& R, const FArdaRHIViewDesc& D) override
            {
                if (!IsViewable(R.Get()) || !Owns(R.Get())) return Invalid<FArdaRHIUnorderedAccessViewRef>("UAV resource is null, foreign, or not viewable.");
                if (auto S = Validate(D); !S) return { {}, S };
                return Ok(FArdaRHIUnorderedAccessViewRef(new FUnorderedAccessView(R, D, this)));
            }
            TArdaRHIResult<FArdaRHISamplerRef> CreateSampler(const FArdaRHISamplerDesc& D) override
            {
                if (auto S = Validate(D); !S) return { {}, S };
                std::lock_guard<std::mutex> Lock(mCacheMutex);
                if (auto Existing = mSamplers.Find(D)) return Ok(Existing);
                auto H = mDevice->createSampler(ToNvrhi(D)); if (!H) return Fail<FArdaRHISamplerRef>("NVRHI failed to create sampler.");
                FArdaRHISamplerRef Result(new FSampler(D, H, this));
                mSamplers.Insert(D, Result);
                return Ok(Result);
            }
            TArdaRHIResult<FArdaRHIShaderRef> CreateShader(const FArdaRHIShaderDesc& D) override
            {
                if (D.mStage == EArdaRHIShaderStage::None || !D.mBytecode || !D.mBytecodeSize) return Invalid<FArdaRHIShaderRef>("Shader stage and bytecode are required.");
                nvrhi::ShaderDesc N; N.shaderType = ToNvrhi(D.mStage); N.entryName = D.mEntryPoint.c_str(); N.debugName = D.mDebugName.c_str();
                auto H = mDevice->createShader(N, D.mBytecode, D.mBytecodeSize); if (!H) return Fail<FArdaRHIShaderRef>("NVRHI failed to create shader.");
                return Ok(FArdaRHIShaderRef(new FShader(D, H, this)));
            }
            TArdaRHIResult<FArdaRHIShaderLibraryRef> CreateShaderLibrary(const void* B, size_t S, const char* Name) override
            {
                if (!B || !S) return Invalid<FArdaRHIShaderLibraryRef>("Shader library bytecode is required.");
                auto H = mDevice->createShaderLibrary(B, S);
                if (!H) return Unsupported<FArdaRHIShaderLibraryRef>("The active NVRHI backend rejected shader-library creation.");
                return Ok(FArdaRHIShaderLibraryRef(new FShaderLibrary(Name ? Name : "ShaderLibrary", H, this)));
            }
            TArdaRHIResult<FArdaRHIShaderRef> GetShaderFromLibrary(const FArdaRHIShaderLibraryRef& L, const char* E, EArdaRHIShaderStage S, const char* Name) override
            {
                auto* N = Cast<FShaderLibrary>(L.Get()); if (!N || !Owns(N)) return Wrong<FArdaRHIShaderRef>();
                if (!E || !*E || S == EArdaRHIShaderStage::None) return Invalid<FArdaRHIShaderRef>("Library shader entry point and stage are required.");
                auto H = N->mHandle->getShader(E, ToNvrhi(S)); if (!H) return Fail<FArdaRHIShaderRef>("NVRHI failed to retrieve shader-library entry.");
                FArdaRHIShaderDesc D; D.mStage = S; D.mEntryPoint = E; D.mDebugName = Name ? Name : E;
                return Ok(FArdaRHIShaderRef(new FShader(D, H, this)));
            }
            TArdaRHIResult<FArdaRHIInputLayoutRef> CreateInputLayout(const eastl::vector<FArdaRHIVertexAttributeDesc>& A, const FArdaRHIShaderRef& VS) override
            {
                FArdaRHIInputLayoutDesc Key{ A, VS };
                if (auto Status = Validate(Key); !Status) return { {}, Status };
                eastl::vector<nvrhi::VertexAttributeDesc> N; N.reserve(A.size());
                for (const auto& V : A) { nvrhi::VertexAttributeDesc D; D.name = V.mSemanticName.c_str(); D.format = ToNvrhi(V.mFormat); D.arraySize = V.mArraySize; D.bufferIndex = V.mBufferIndex; D.offset = V.mOffset; D.elementStride = V.mElementStride; D.isInstanced = V.mbInstanced; N.push_back(D); }
                auto* S = Cast<FShader>(VS.Get()); if (VS && (!S || !Owns(S))) return Wrong<FArdaRHIInputLayoutRef>();
                std::lock_guard<std::mutex> Lock(mCacheMutex);
                if (auto Existing = mInputLayouts.Find(Key)) return Ok(Existing);
                auto H = mDevice->createInputLayout(N.data(), static_cast<uint32_t>(N.size()), S ? S->mHandle.Get() : nullptr);
                if (!H) return Fail<FArdaRHIInputLayoutRef>("NVRHI failed to create input layout.");
                FArdaRHIInputLayoutRef Result(new FInputLayout(Key, H, this));
                mInputLayouts.Insert(Key, Result);
                return Ok(Result);
            }
            TArdaRHIResult<FArdaRHIBindingLayoutRef> CreateBindingLayout(const FArdaRHIBindingLayoutDesc& D) override
            {
                if (auto S = Validate(D); !S) return { {}, S };
                std::lock_guard<std::mutex> Lock(mCacheMutex);
                if (auto Existing = mBindingLayouts.Find(D)) return Ok(Existing);
                nvrhi::BindingLayoutDesc N; N.visibility = ToNvrhi(D.mVisibility); N.registerSpace = D.mRegisterSpace; N.registerSpaceIsDescriptorSet = D.mbRegisterSpaceIsDescriptorSet;
                for (const auto& I : D.mItems) { nvrhi::BindingLayoutItem V{}; V.slot = I.mSlot; V.type = ToNvrhiBinding(I.mType); V.size = static_cast<uint16_t>(I.mArraySize); N.addItem(V); }
                auto H = mDevice->createBindingLayout(N); if (!H) return Fail<FArdaRHIBindingLayoutRef>("NVRHI failed to create binding layout.");
                FArdaRHIBindingLayoutRef Result(new FBindingLayout(D, H, this));
                mBindingLayouts.Insert(D, Result);
                return Ok(Result);
            }
            TArdaRHIResult<FArdaRHIBindingLayoutRef> CreateBindlessLayout(const FArdaRHIBindlessLayoutDesc& D) override
            {
                if (!mCapabilities.mbBindless) return Unsupported<FArdaRHIBindingLayoutRef>("Bindless layouts are unsupported.");
                if (D.mVisibility == EArdaRHIShaderStage::None || !D.mMaxCapacity || D.mRegisterSpaces.empty())
                    return Invalid<FArdaRHIBindingLayoutRef>("Bindless layout requires visibility, capacity, and register spaces.");
                nvrhi::BindlessLayoutDesc N; N.visibility = ToNvrhi(D.mVisibility); N.firstSlot = D.mFirstSlot;
                N.maxCapacity = D.mMaxCapacity; N.layoutType = static_cast<nvrhi::BindlessLayoutDesc::LayoutType>(D.mLayoutType);
                for (const auto& I : D.mRegisterSpaces)
                {
                    if (!I.mArraySize || I.mArraySize > 65535)
                        return Invalid<FArdaRHIBindingLayoutRef>("Bindless register-space array size must be between 1 and 65535.");
                    nvrhi::BindingLayoutItem V{}; V.slot = I.mSlot; V.type = ToNvrhiBinding(I.mType); V.size = static_cast<uint16_t>(I.mArraySize); N.addRegisterSpace(V);
                }
                auto H = mDevice->createBindlessLayout(N); if (!H) return Fail<FArdaRHIBindingLayoutRef>("NVRHI failed to create bindless layout.");
                FArdaRHIBindingLayoutDesc Public; Public.mVisibility = D.mVisibility; Public.mDebugName = D.mDebugName;
                return Ok(FArdaRHIBindingLayoutRef(new FBindingLayout(
                    Public, H, this, D.mbAllowUnsafeDescriptorTableLifetime)));
            }
            TArdaRHIResult<FArdaRHIBindingSetRef> CreateBindingSet(const FArdaRHIBindingSetDesc& D) override
            {
                auto* L = Cast<FBindingLayout>(D.mLayout.Get()); if (!L || !Owns(L)) return Wrong<FArdaRHIBindingSetRef>();
                nvrhi::BindingSetDesc N; for (const auto& I : D.mItems) { if (I.mResource && !Owns(I.mResource.Get())) return Wrong<FArdaRHIBindingSetRef>(); N.addItem(LowerBinding(I)); }
                auto H = mDevice->createBindingSet(N, L->mHandle); if (!H) return Fail<FArdaRHIBindingSetRef>("NVRHI failed to create binding set.");
                return Ok(FArdaRHIBindingSetRef(new FBindingSet(D, H, this)));
            }
            TArdaRHIResult<FArdaRHIDescriptorTableRef> CreateDescriptorTable(const FArdaRHIBindingLayoutRef& L) override
            {
                if (!mCapabilities.mbBindless) return Unsupported<FArdaRHIDescriptorTableRef>("Descriptor tables are unsupported.");
                auto* N = Cast<FBindingLayout>(L.Get()); if (!N || !Owns(N)) return Wrong<FArdaRHIDescriptorTableRef>();
                if (!N->mbUnsafeDescriptorTables)
                    return Invalid<FArdaRHIDescriptorTableRef>("Descriptor tables require explicit unsafe lifetime opt-in on the bindless layout.");
                auto H = mDevice->createDescriptorTable(N->mHandle); if (!H) return Fail<FArdaRHIDescriptorTableRef>("NVRHI failed to create descriptor table.");
                return Ok(FArdaRHIDescriptorTableRef(new FDescriptorTable(L, H, this)));
            }
            FArdaRHIStatus ResizeDescriptorTable(const FArdaRHIDescriptorTableRef& T, uint32_t S, bool Keep) override
            {
                auto* N = Cast<FDescriptorTable>(T.Get()); if (!N || !Owns(N)) return WrongStatus();
                if (!S) return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Descriptor table size must be non-zero.");
                mDevice->resizeDescriptorTable(N->mHandle, S, Keep); return {};
            }
            FArdaRHIStatus WriteDescriptorTable(const FArdaRHIDescriptorTableRef& T, const FArdaRHIBindingItem& I) override
            {
                auto* N = Cast<FDescriptorTable>(T.Get()); if (!N || !Owns(N)) return WrongStatus();
                if (I.mResource && !Owns(I.mResource.Get())) return WrongStatus();
                if (I.mSlot >= N->GetCapacity()) return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Descriptor table slot is outside its capacity.");
                return mDevice->writeDescriptorTable(N->mHandle, LowerBinding(I))
                    ? FArdaRHIStatus{} : FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "NVRHI failed to write descriptor table.");
            }
            TArdaRHIResult<FArdaRHIFramebufferRef> CreateFramebuffer(const FArdaRHIFramebufferDesc& D) override
            {
                if (D.mColorAttachments.empty() && !D.mDepthAttachment.mTexture) return Invalid<FArdaRHIFramebufferRef>("Framebuffer requires at least one attachment.");
                if (D.mColorAttachments.size() > ArdaRHIMaxRenderTargets) return Invalid<FArdaRHIFramebufferRef>("Framebuffer has too many color attachments.");
                nvrhi::FramebufferDesc N;
                for (const auto& A : D.mColorAttachments) { auto* T = Cast<FTexture>(A.mTexture.Get()); if (!T || !Owns(T)) return Wrong<FArdaRHIFramebufferRef>(); N.addColorAttachment(nvrhi::FramebufferAttachment().setTexture(T->mHandle).setSubresources(ToNvrhi(A.mAttachment.mSubresources)).setFormat(ToNvrhi(A.mAttachment.mFormat)).setReadOnly(A.mAttachment.mbReadOnly)); }
                if (D.mDepthAttachment.mTexture) { auto* T = Cast<FTexture>(D.mDepthAttachment.mTexture.Get()); if (!T || !Owns(T)) return Wrong<FArdaRHIFramebufferRef>(); N.setDepthAttachment(nvrhi::FramebufferAttachment().setTexture(T->mHandle).setSubresources(ToNvrhi(D.mDepthAttachment.mAttachment.mSubresources)).setFormat(ToNvrhi(D.mDepthAttachment.mAttachment.mFormat)).setReadOnly(D.mDepthAttachment.mAttachment.mbReadOnly)); }
                auto H = mDevice->createFramebuffer(N); if (!H) return Fail<FArdaRHIFramebufferRef>("NVRHI failed to create framebuffer.");
                return Ok(FArdaRHIFramebufferRef(new FFramebuffer(D, H, this)));
            }
            TArdaRHIResult<FArdaRHIGraphicsPipelineRef> CreateGraphicsPipeline(const FArdaRHIGraphicsPipelineDesc& D) override
            {
                if (auto S = Validate(D); !S) return { {}, S };
                auto* VS = Cast<FShader>(D.mVertexShader.Get()); if (!VS || !Owns(VS)) return Invalid<FArdaRHIGraphicsPipelineRef>("Graphics pipeline requires an owned vertex shader.");
                nvrhi::GraphicsPipelineDesc N; N.primType = ToNvrhiPrimitive(D.mTopology); N.patchControlPoints = D.mPatchControlPoints; N.VS = VS->mHandle;
                if (auto* V = Cast<FInputLayout>(D.mInputLayout.Get())) N.inputLayout = V->mHandle; else if (D.mInputLayout) return Wrong<FArdaRHIGraphicsPipelineRef>();
                if (!SetShader(N.PS, D.mPixelShader) || !SetShader(N.HS, D.mHullShader) || !SetShader(N.DS, D.mDomainShader) || !SetShader(N.GS, D.mGeometryShader)) return Wrong<FArdaRHIGraphicsPipelineRef>();
                for (const auto& B : D.mBindingLayouts) { auto* V = Cast<FBindingLayout>(B.Get()); if (!V || !Owns(V)) return Wrong<FArdaRHIGraphicsPipelineRef>(); N.addBindingLayout(V->mHandle); }
                N.renderState.rasterState.fillMode = static_cast<nvrhi::RasterFillMode>(D.mRasterState.mFillMode);
                N.renderState.rasterState.cullMode = static_cast<nvrhi::RasterCullMode>(D.mRasterState.mCullMode);
                N.renderState.rasterState.frontCounterClockwise = D.mRasterState.mbFrontCounterClockwise; N.renderState.rasterState.depthClipEnable = D.mRasterState.mbDepthClip; N.renderState.rasterState.scissorEnable = D.mRasterState.mbScissor;
                N.renderState.depthStencilState.depthTestEnable = D.mDepthStencilState.mbDepthTest; N.renderState.depthStencilState.depthWriteEnable = D.mDepthStencilState.mbDepthWrite; N.renderState.depthStencilState.depthFunc = static_cast<nvrhi::ComparisonFunc>(static_cast<uint8_t>(D.mDepthStencilState.mDepthFunc) + 1);
                N.renderState.blendState.alphaToCoverageEnable = D.mBlendState.mbAlphaToCoverage;
                for (uint32_t I = 0; I < ArdaRHIMaxRenderTargets; ++I)
                {
                    const auto& Source = D.mBlendState.mTargets[I];
                    auto& Target = N.renderState.blendState.targets[I];
                    Target.blendEnable = Source.mbEnable;
                    Target.srcBlend = ToNvrhiBlend(Source.mSourceColor);
                    Target.destBlend = ToNvrhiBlend(Source.mDestinationColor);
                    Target.srcBlendAlpha = ToNvrhiBlend(Source.mSourceAlpha);
                    Target.destBlendAlpha = ToNvrhiBlend(Source.mDestinationAlpha);
                }
                nvrhi::FramebufferInfo F; for (auto Format : D.mColorFormats) F.colorFormats.push_back(ToNvrhi(Format)); F.depthFormat = ToNvrhi(D.mDepthFormat); F.sampleCount = D.mSampleCount;
                auto H = mDevice->createGraphicsPipeline(N, F); if (!H) return Fail<FArdaRHIGraphicsPipelineRef>("NVRHI failed to create graphics pipeline.");
                return Ok(FArdaRHIGraphicsPipelineRef(new FGraphicsPipeline(D, H, this)));
            }
            TArdaRHIResult<FArdaRHIComputePipelineRef> CreateComputePipeline(const FArdaRHIComputePipelineDesc& D) override
            {
                if (auto S = Validate(D); !S) return { {}, S };
                auto* CS = Cast<FShader>(D.mComputeShader.Get()); if (!CS || !Owns(CS) || CS->mStage != EArdaRHIShaderStage::Compute) return Invalid<FArdaRHIComputePipelineRef>("Compute pipeline requires an owned compute shader.");
                nvrhi::ComputePipelineDesc N; N.CS = CS->mHandle;
                for (const auto& B : D.mBindingLayouts) { auto* V = Cast<FBindingLayout>(B.Get()); if (!V || !Owns(V)) return Wrong<FArdaRHIComputePipelineRef>(); N.addBindingLayout(V->mHandle); }
                auto H = mDevice->createComputePipeline(N); if (!H) return Fail<FArdaRHIComputePipelineRef>("NVRHI failed to create compute pipeline.");
                return Ok(FArdaRHIComputePipelineRef(new FComputePipeline(D, H, this)));
            }
            TArdaRHIResult<FArdaRHIMeshletPipelineRef> CreateMeshletPipeline(const FArdaRHIMeshletPipelineDesc& D) override
            {
                if (!mCapabilities.mbMeshShaders) return Unsupported<FArdaRHIMeshletPipelineRef>("Mesh shaders are unsupported.");
                if (auto S = Validate(D); !S) return { {}, S };
                auto* MS = Cast<FShader>(D.mMeshShader.Get()); if (!MS || !Owns(MS) || MS->mStage != EArdaRHIShaderStage::Mesh)
                    return Invalid<FArdaRHIMeshletPipelineRef>("Meshlet pipeline requires an owned mesh shader.");
                std::lock_guard<std::mutex> Lock(mCacheMutex);
                if (auto Existing = mMeshletPipelines.Find(D)) return Ok(Existing);
                nvrhi::MeshletPipelineDesc N; N.primType = ToNvrhiPrimitive(D.mTopology); N.MS = MS->mHandle;
                if (!SetShader(N.AS, D.mAmplificationShader) || !SetShader(N.PS, D.mPixelShader)) return Wrong<FArdaRHIMeshletPipelineRef>();
                for (const auto& B : D.mBindingLayouts) { auto* V = Cast<FBindingLayout>(B.Get()); if (!V || !Owns(V)) return Wrong<FArdaRHIMeshletPipelineRef>(); N.addBindingLayout(V->mHandle); }
                N.renderState.rasterState.fillMode = static_cast<nvrhi::RasterFillMode>(D.mRasterState.mFillMode);
                N.renderState.rasterState.cullMode = static_cast<nvrhi::RasterCullMode>(D.mRasterState.mCullMode);
                N.renderState.rasterState.frontCounterClockwise = D.mRasterState.mbFrontCounterClockwise; N.renderState.rasterState.depthClipEnable = D.mRasterState.mbDepthClip; N.renderState.rasterState.scissorEnable = D.mRasterState.mbScissor;
                N.renderState.depthStencilState.depthTestEnable = D.mDepthStencilState.mbDepthTest; N.renderState.depthStencilState.depthWriteEnable = D.mDepthStencilState.mbDepthWrite; N.renderState.depthStencilState.depthFunc = static_cast<nvrhi::ComparisonFunc>(static_cast<uint8_t>(D.mDepthStencilState.mDepthFunc) + 1);
                N.renderState.blendState.alphaToCoverageEnable = D.mBlendState.mbAlphaToCoverage;
                for (uint32_t I = 0; I < ArdaRHIMaxRenderTargets; ++I)
                {
                    const auto& Source = D.mBlendState.mTargets[I];
                    auto& Target = N.renderState.blendState.targets[I];
                    Target.blendEnable = Source.mbEnable;
                    Target.srcBlend = ToNvrhiBlend(Source.mSourceColor);
                    Target.destBlend = ToNvrhiBlend(Source.mDestinationColor);
                    Target.srcBlendAlpha = ToNvrhiBlend(Source.mSourceAlpha);
                    Target.destBlendAlpha = ToNvrhiBlend(Source.mDestinationAlpha);
                }
                nvrhi::FramebufferInfo F; for (auto Format : D.mColorFormats) F.colorFormats.push_back(ToNvrhi(Format)); F.depthFormat = ToNvrhi(D.mDepthFormat); F.sampleCount = D.mSampleCount;
                auto H = mDevice->createMeshletPipeline(N, F); if (!H) return Fail<FArdaRHIMeshletPipelineRef>("NVRHI failed to create meshlet pipeline.");
                FArdaRHIMeshletPipelineRef Result(new FMeshletPipeline(D, H, this));
                mMeshletPipelines.Insert(D, Result);
                return Ok(Result);
            }
            TArdaRHIResult<FArdaRHIRasterStateRef> CreateRasterState(
                const FArdaRHIRasterState& D) override
            {
                std::lock_guard<std::mutex> Lock(mCacheMutex);
                if (auto Existing = mRasterStates.Find(D)) return Ok(Existing);
                FArdaRHIRasterStateRef Result(new FRasterState(D, this));
                mRasterStates.Insert(D, Result);
                return Ok(Result);
            }
            TArdaRHIResult<FArdaRHIBlendStateRef> CreateBlendState(
                const FArdaRHIBlendState& D) override
            {
                std::lock_guard<std::mutex> Lock(mCacheMutex);
                if (auto Existing = mBlendStates.Find(D)) return Ok(Existing);
                FArdaRHIBlendStateRef Result(new FBlendState(D, this));
                mBlendStates.Insert(D, Result);
                return Ok(Result);
            }
            TArdaRHIResult<FArdaRHIDepthStencilStateRef> CreateDepthStencilState(
                const FArdaRHIDepthStencilState& D) override
            {
                std::lock_guard<std::mutex> Lock(mCacheMutex);
                if (auto Existing = mDepthStencilStates.Find(D)) return Ok(Existing);
                FArdaRHIDepthStencilStateRef Result(new FDepthStencilState(D, this));
                mDepthStencilStates.Insert(D, Result);
                return Ok(Result);
            }
            TArdaRHIResult<FArdaRHIAccelStructRef> CreateAccelStruct(const FArdaRHIAccelStructDesc& D) override
            {
                if (!mCapabilities.mbRayTracingAccelStruct) return Unsupported<FArdaRHIAccelStructRef>("Ray tracing acceleration structures are unsupported.");
                if (D.mbVirtual && !mCapabilities.mbVirtualResources)
                    return Unsupported<FArdaRHIAccelStructRef>("Virtual acceleration structures are unsupported.");
                if (!D.mbTrackLiveness && !D.mbAllowUnsafeLivenessOptOut)
                    return Invalid<FArdaRHIAccelStructRef>("Disabling acceleration-structure liveness tracking requires explicit unsafe opt-in.");
                if (D.mbTopLevel ? !D.mTopLevelMaxInstances : D.mBottomLevelGeometries.empty())
                    return Invalid<FArdaRHIAccelStructRef>("Acceleration structure descriptor has no capacity or geometry.");
                nvrhi::rt::AccelStructDesc N; N.topLevelMaxInstances = D.mTopLevelMaxInstances; N.buildFlags = static_cast<nvrhi::rt::AccelStructBuildFlags>(D.mBuildFlags);
                N.debugName = D.mDebugName.c_str(); N.trackLiveness = D.mbTrackLiveness; N.isTopLevel = D.mbTopLevel; N.isVirtual = D.mbVirtual;
                eastl::vector<eastl::vector<nvrhi::rt::OpacityMicromapUsageCount>> UsageCounts; UsageCounts.reserve(D.mBottomLevelGeometries.size());
                for (const auto& G : D.mBottomLevelGeometries) { if (auto S = ValidateGeometryBasics(G); !S) return { {}, S }; if (!ValidateGeometryOwnership(G)) return Wrong<FArdaRHIAccelStructRef>(); N.bottomLevelGeometries.push_back(LowerGeometry(G)); UsageCounts.emplace_back(); for (const auto& C : G.mOpacityMicromapUsageCounts) UsageCounts.back().push_back({ C.mCount, C.mSubdivisionLevel, static_cast<nvrhi::rt::OpacityMicromapFormat>(C.mFormat) }); if (!UsageCounts.back().empty() && G.mType == EArdaRHIRayTracingGeometryType::Triangles) { N.bottomLevelGeometries.back().geometryData.triangles.pOmmUsageCounts = UsageCounts.back().data(); N.bottomLevelGeometries.back().geometryData.triangles.numOmmUsageCounts = static_cast<uint32_t>(UsageCounts.back().size()); } }
                auto H = mDevice->createAccelStruct(N); if (!H) return Fail<FArdaRHIAccelStructRef>("NVRHI failed to create acceleration structure.");
                return Ok(FArdaRHIAccelStructRef(new FAccelStruct(D, H, this)));
            }
            TArdaRHIResult<FArdaRHIOpacityMicromapRef> CreateOpacityMicromap(const FArdaRHIOpacityMicromapDesc& D) override
            {
                if (!mCapabilities.mbRayTracingOpacityMicromap) return Unsupported<FArdaRHIOpacityMicromapRef>("Ray tracing opacity micromaps are unsupported.");
                if (!D.mbTrackLiveness && !D.mbAllowUnsafeLivenessOptOut)
                    return Invalid<FArdaRHIOpacityMicromapRef>("Disabling opacity-micromap liveness tracking requires explicit unsafe opt-in.");
                if (D.mCounts.empty() || !D.mInputBuffer || !D.mPerMicromapDescBuffer) return Invalid<FArdaRHIOpacityMicromapRef>("Opacity micromap counts and input buffers are required.");
                if (!Owns(D.mInputBuffer.Get()) || !Owns(D.mPerMicromapDescBuffer.Get())) return Wrong<FArdaRHIOpacityMicromapRef>();
                if (D.mInputBufferOffset >= D.mInputBuffer->GetDesc().mByteSize ||
                    D.mPerMicromapDescBufferOffset >=
                        D.mPerMicromapDescBuffer->GetDesc().mByteSize)
                    return Invalid<FArdaRHIOpacityMicromapRef>("Opacity-micromap buffer offsets are out of bounds.");
                for (const auto& Count : D.mCounts)
                    if (!Count.mCount)
                        return Invalid<FArdaRHIOpacityMicromapRef>("Opacity-micromap usage counts must be non-zero.");
                auto N = LowerOpacityMicromapDesc(D); auto H = mDevice->createOpacityMicromap(N);
                if (!H) return Fail<FArdaRHIOpacityMicromapRef>("NVRHI failed to create opacity micromap.");
                return Ok(FArdaRHIOpacityMicromapRef(new FOpacityMicromap(D, H, this)));
            }
            TArdaRHIResult<FArdaRHIRayTracingPipelineRef> CreateRayTracingPipeline(const FArdaRHIRayTracingPipelineDesc& D) override
            {
                if (!mCapabilities.mbRayTracing) return Unsupported<FArdaRHIRayTracingPipelineRef>("Ray tracing pipelines are unsupported.");
                if (D.mbAllowOpacityMicromaps && !mCapabilities.mbRayTracingOpacityMicromap)
                    return Unsupported<FArdaRHIRayTracingPipelineRef>("Opacity micromaps are unsupported.");
                if (auto S = Validate(D); !S) return { {}, S };
                std::lock_guard<std::mutex> Lock(mCacheMutex);
                if (auto Existing = mRayTracingPipelines.Find(D))
                    return Ok(Existing);
                nvrhi::rt::PipelineDesc N; N.maxPayloadSize = D.mMaxPayloadSize; N.maxAttributeSize = D.mMaxAttributeSize; N.maxRecursionDepth = D.mMaxRecursionDepth; N.allowOpacityMicromaps = D.mbAllowOpacityMicromaps;
                for (const auto& S : D.mShaders) { auto* Shader = Cast<FShader>(S.mShader.Get()); auto* Layout = Cast<FBindingLayout>(S.mLocalBindingLayout.Get()); if (!Shader || !Owns(Shader) || (S.mLocalBindingLayout && (!Layout || !Owns(Layout)))) return Wrong<FArdaRHIRayTracingPipelineRef>(); nvrhi::rt::PipelineShaderDesc V; V.exportName = S.mExportName.c_str(); V.shader = Shader->mHandle; if (Layout) V.bindingLayout = Layout->mHandle; N.addShader(V); }
                for (const auto& H : D.mHitGroups) { nvrhi::rt::PipelineHitGroupDesc V; V.exportName = H.mExportName.c_str(); if (!SetShader(V.closestHitShader, H.mClosestHitShader) || !SetShader(V.anyHitShader, H.mAnyHitShader) || !SetShader(V.intersectionShader, H.mIntersectionShader)) return Wrong<FArdaRHIRayTracingPipelineRef>(); auto* L = Cast<FBindingLayout>(H.mLocalBindingLayout.Get()); if (H.mLocalBindingLayout && (!L || !Owns(L))) return Wrong<FArdaRHIRayTracingPipelineRef>(); if (L) V.bindingLayout = L->mHandle; V.isProceduralPrimitive = H.mbProceduralPrimitive; N.addHitGroup(V); }
                for (const auto& B : D.mGlobalBindingLayouts) { auto* V = Cast<FBindingLayout>(B.Get()); if (!V || !Owns(V)) return Wrong<FArdaRHIRayTracingPipelineRef>(); N.addBindingLayout(V->mHandle); }
                auto H = mDevice->createRayTracingPipeline(N); if (!H) return Fail<FArdaRHIRayTracingPipelineRef>("NVRHI failed to create ray tracing pipeline.");
                FArdaRHIRayTracingPipelineRef Result(
                    new FRayTracingPipeline(D, H, this));
                mRayTracingPipelines.Insert(D, Result);
                return Ok(Result);
            }
            TArdaRHIResult<FArdaRHIShaderTableRef> CreateShaderTable(const FArdaRHIRayTracingPipelineRef& P, const FArdaRHIShaderTableDesc& D) override
            {
                if (!mCapabilities.mbRayTracing) return Unsupported<FArdaRHIShaderTableRef>("Ray tracing pipelines are unsupported.");
                auto* N = Cast<FRayTracingPipeline>(P.Get()); if (!N || !Owns(N)) return Wrong<FArdaRHIShaderTableRef>();
                if (D.mbCached && !D.mMaxEntries) return Invalid<FArdaRHIShaderTableRef>("Cached shader tables require a non-zero maximum entry count.");
                nvrhi::rt::ShaderTableDesc S; S.isCached = D.mbCached; S.maxEntries = D.mMaxEntries; S.debugName = D.mDebugName.c_str();
                auto H = N->mHandle->createShaderTable(S); if (!H) return Fail<FArdaRHIShaderTableRef>("NVRHI failed to create shader table.");
                return Ok(FArdaRHIShaderTableRef(new FShaderTable(D, H, this)));
            }
            FArdaRHIStatus SetShaderTableRayGeneration(const FArdaRHIShaderTableRef& T, const char* E, const FArdaRHIBindingSetRef& B) override
            {
                auto* N = Cast<FShaderTable>(T.Get()); nvrhi::IBindingSet* Native = nullptr; if (!GetBinding(B, Native)) return WrongStatus();
                if (!N || !Owns(N) || !E || !*E) return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Owned shader table and export name are required.");
                N->mHandle->setRayGenerationShader(E, Native); return {};
            }
            TArdaRHIResult<int> AddShaderTableMiss(const FArdaRHIShaderTableRef& T, const char* E, const FArdaRHIBindingSetRef& B) override { return AddShaderTableEntry(T, E, B, 0); }
            TArdaRHIResult<int> AddShaderTableHitGroup(const FArdaRHIShaderTableRef& T, const char* E, const FArdaRHIBindingSetRef& B) override { return AddShaderTableEntry(T, E, B, 1); }
            TArdaRHIResult<int> AddShaderTableCallable(const FArdaRHIShaderTableRef& T, const char* E, const FArdaRHIBindingSetRef& B) override { return AddShaderTableEntry(T, E, B, 2); }
            TArdaRHIResult<FArdaRHISamplerFeedbackTextureRef> CreateSamplerFeedbackTexture(const FArdaRHITextureRef& T, const FArdaRHISamplerFeedbackTextureDesc& D) override
            {
                if (!mCapabilities.mbSamplerFeedback) return Unsupported<FArdaRHISamplerFeedbackTextureRef>("Sampler feedback is unsupported.");
                auto* N = Cast<FTexture>(T.Get()); if (!N || !Owns(N)) return Wrong<FArdaRHISamplerFeedbackTextureRef>();
                nvrhi::SamplerFeedbackTextureDesc S; S.samplerFeedbackFormat = static_cast<nvrhi::SamplerFeedbackFormat>(D.mFormat); S.samplerFeedbackMipRegionX = D.mMipRegionX; S.samplerFeedbackMipRegionY = D.mMipRegionY; S.samplerFeedbackMipRegionZ = D.mMipRegionZ; S.initialState = ToNvrhi(D.mInitialState); S.keepInitialState = D.mbKeepInitialState;
                auto H = mDevice->createSamplerFeedbackTexture(N->mHandle, S); if (!H) return Fail<FArdaRHISamplerFeedbackTextureRef>("NVRHI failed to create sampler feedback texture.");
                return Ok(FArdaRHISamplerFeedbackTextureRef(new FSamplerFeedbackTexture(D, T, H, this)));
            }
            TArdaRHIResult<FArdaRHIEventQueryRef> CreateEventQuery() override
            {
                auto H = mDevice->createEventQuery(); if (!H) return Fail<FArdaRHIEventQueryRef>("NVRHI failed to create event query.");
                return Ok(FArdaRHIEventQueryRef(new FEventQuery("EventQuery", H, this)));
            }
            TArdaRHIResult<FArdaRHITimerQueryRef> CreateTimerQuery() override
            {
                auto H = mDevice->createTimerQuery(); if (!H) return Fail<FArdaRHITimerQueryRef>("NVRHI failed to create timer query.");
                return Ok(FArdaRHITimerQueryRef(new FTimerQuery("TimerQuery", H, this)));
            }
            TArdaRHIResult<FArdaRHIGpuFenceRef> CreateGpuFence() override
            {
                auto H = mDevice->createEventQuery(); if (!H) return Fail<FArdaRHIGpuFenceRef>("NVRHI failed to create GPU fence event.");
                return Ok(FArdaRHIGpuFenceRef(new FGpuFence("GpuFence", H, this)));
            }
            FArdaRHIStatus SignalEventQuery(const FArdaRHIEventQueryRef& Q, EArdaRHIQueueType Queue) override
            {
                auto* N = Cast<FEventQuery>(Q.Get()); if (!N || !Owns(N)) return WrongStatus();
                if (!mCapabilities.IsQueueSupported(Queue)) return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Requested event-query queue is unavailable.");
                mDevice->setEventQuery(N->mHandle, ToNvrhi(Queue)); return {};
            }
            TArdaRHIResult<bool> PollEventQuery(const FArdaRHIEventQueryRef& Q) override { auto* N = Cast<FEventQuery>(Q.Get()); return (!N || !Owns(N)) ? Wrong<bool>() : Ok(mDevice->pollEventQuery(N->mHandle)); }
            FArdaRHIStatus WaitEventQuery(const FArdaRHIEventQueryRef& Q) override { auto* N = Cast<FEventQuery>(Q.Get()); if (!N || !Owns(N)) return WrongStatus(); mDevice->waitEventQuery(N->mHandle); return {}; }
            FArdaRHIStatus ResetEventQuery(const FArdaRHIEventQueryRef& Q) override { auto* N = Cast<FEventQuery>(Q.Get()); if (!N || !Owns(N)) return WrongStatus(); mDevice->resetEventQuery(N->mHandle); return {}; }
            TArdaRHIResult<bool> PollTimerQuery(const FArdaRHITimerQueryRef& Q) override { auto* N = Cast<FTimerQuery>(Q.Get()); return (!N || !Owns(N)) ? Wrong<bool>() : Ok(mDevice->pollTimerQuery(N->mHandle)); }
            TArdaRHIResult<float> GetTimerQuerySeconds(const FArdaRHITimerQueryRef& Q) override { auto* N = Cast<FTimerQuery>(Q.Get()); return (!N || !Owns(N)) ? Wrong<float>() : Ok(mDevice->getTimerQueryTime(N->mHandle)); }
            FArdaRHIStatus ResetTimerQuery(const FArdaRHITimerQueryRef& Q) override { auto* N = Cast<FTimerQuery>(Q.Get()); if (!N || !Owns(N)) return WrongStatus(); mDevice->resetTimerQuery(N->mHandle); return {}; }
            FArdaRHIStatus SignalGpuFence(const FArdaRHIGpuFenceRef& F, EArdaRHIQueueType Queue) override
            {
                auto* N = Cast<FGpuFence>(F.Get()); if (!N || !Owns(N)) return WrongStatus();
                if (!mCapabilities.IsQueueSupported(Queue)) return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Requested GPU-fence queue is unavailable.");
                mDevice->setEventQuery(N->mHandle, ToNvrhi(Queue)); return {};
            }
            TArdaRHIResult<bool> PollGpuFence(const FArdaRHIGpuFenceRef& F) override { auto* N = Cast<FGpuFence>(F.Get()); return (!N || !Owns(N)) ? Wrong<bool>() : Ok(mDevice->pollEventQuery(N->mHandle)); }
            FArdaRHIStatus WaitGpuFence(const FArdaRHIGpuFenceRef& F) override { auto* N = Cast<FGpuFence>(F.Get()); if (!N || !Owns(N)) return WrongStatus(); mDevice->waitEventQuery(N->mHandle); return {}; }
            FArdaRHIStatus ResetGpuFence(const FArdaRHIGpuFenceRef& F) override { auto* N = Cast<FGpuFence>(F.Get()); if (!N || !Owns(N)) return WrongStatus(); mDevice->resetEventQuery(N->mHandle); return {}; }
            TArdaRHIResult<FArdaRHICommandListRef> CreateCommandList(
                EArdaRHIQueueType Q,
                bool bImmediateExecution) override
            {
                if (!mCapabilities.IsQueueSupported(Q)) return { {}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Requested command queue is unavailable.") };
                auto H = mDevice->createCommandList(
                    nvrhi::CommandListParameters()
                        .setQueueType(ToNvrhi(Q))
                        .setEnableImmediateExecution(bImmediateExecution));
                if (!H) return Fail<FArdaRHICommandListRef>("NVRHI failed to create command list.");
                return Ok(FArdaRHICommandListRef(new FCommandList(H, Q, this, this,
                    mCapabilities.mbMeshShaders, mCapabilities.mbRayTracing,
                    mCapabilities.mbSamplerFeedback, mCapabilities.mbRayTracingOpacityMicromap)));
            }
            TArdaRHIResult<uint64_t> ExecuteCommandList(const FArdaRHICommandListRef& C) override
            {
                auto* N = Cast<FCommandList>(C.Get()); if (!N || !Owns(N)) return Wrong<uint64_t>();
                const uint64_t I = mDevice->executeCommandList(N->mHandle, ToNvrhi(N->GetQueueType()));
                if (!I) return Fail<uint64_t>("NVRHI failed to execute command list."); return Ok(I);
            }
            TArdaRHIResult<uint64_t> ExecuteCommandLists(const eastl::vector<FArdaRHICommandListRef>& Lists, EArdaRHIQueueType Q) override
            {
                eastl::vector<nvrhi::ICommandList*> Native;
                Native.reserve(Lists.size());
                for (const auto& C : Lists)
                {
                    auto* N = Cast<FCommandList>(C.Get());
                    if (!N || !Owns(N) || N->GetQueueType() != Q) return Wrong<uint64_t>();
                    Native.push_back(N->mHandle.Get());
                }
                if (Native.empty()) return Invalid<uint64_t>("At least one command list is required.");
                const uint64_t I = mDevice->executeCommandLists(Native.data(), Native.size(), ToNvrhi(Q));
                if (!I) return Fail<uint64_t>("NVRHI failed to execute command lists.");
                return Ok(I);
            }
            FArdaRHIStatus QueueWait(EArdaRHIQueueType W, EArdaRHIQueueType E, uint64_t I) override
            {
                if (!I) return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Queue wait instance must be non-zero.");
                mDevice->queueWaitForCommandList(ToNvrhi(W), ToNvrhi(E), I); return {};
            }
            TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(const FArdaRHITextureRef& T) override
            {
                auto* N = Cast<FTexture>(T.Get()); if (!N || !Owns(N)) return Wrong<FArdaRHIMemoryRequirements>();
                const auto R = mDevice->getTextureMemoryRequirements(N->mHandle);
                return Ok(FArdaRHIMemoryRequirements{ R.size, R.alignment });
            }
            TArdaRHIResult<FArdaRHIMemoryRequirements> GetBufferMemoryRequirements(const FArdaRHIBufferRef& B) override
            {
                auto* N = Cast<FBuffer>(B.Get()); if (!N || !Owns(N)) return Wrong<FArdaRHIMemoryRequirements>();
                const auto R = mDevice->getBufferMemoryRequirements(N->mHandle);
                return Ok(FArdaRHIMemoryRequirements{ R.size, R.alignment });
            }
            TArdaRHIResult<FArdaRHIMemoryRequirements> GetAccelStructMemoryRequirements(const FArdaRHIAccelStructRef& A) override
            {
                auto* N = Cast<FAccelStruct>(A.Get()); if (!N || !Owns(N)) return Wrong<FArdaRHIMemoryRequirements>();
                if (!mCapabilities.mbRayTracingAccelStruct) return Unsupported<FArdaRHIMemoryRequirements>("Acceleration structures are unsupported.");
                const auto R = mDevice->getAccelStructMemoryRequirements(N->mHandle);
                return Ok(FArdaRHIMemoryRequirements{ R.size, R.alignment });
            }
            FArdaRHIStatus BindTextureMemory(const FArdaRHITextureRef& T, const FArdaRHIHeapRef& H, uint64_t O) override
            {
                auto* N = Cast<FTexture>(T.Get()); auto* Heap = Cast<FHeap>(H.Get()); if (!N || !Heap || !Owns(N) || !Owns(Heap)) return WrongStatus();
                if (!N->GetDesc().mbVirtual) return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Only virtual textures can be placed in heaps.");
                return mDevice->bindTextureMemory(N->mHandle, Heap->mHandle, O) ? FArdaRHIStatus{} : FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "NVRHI failed to bind texture memory.");
            }
            FArdaRHIStatus BindBufferMemory(const FArdaRHIBufferRef& B, const FArdaRHIHeapRef& H, uint64_t O) override
            {
                auto* N = Cast<FBuffer>(B.Get()); auto* Heap = Cast<FHeap>(H.Get()); if (!N || !Heap || !Owns(N) || !Owns(Heap)) return WrongStatus();
                if (!N->GetDesc().mbVirtual) return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Only virtual buffers can be placed in heaps.");
                return mDevice->bindBufferMemory(N->mHandle, Heap->mHandle, O) ? FArdaRHIStatus{} : FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "NVRHI failed to bind buffer memory.");
            }
            FArdaRHIStatus BindAccelStructMemory(const FArdaRHIAccelStructRef& A, const FArdaRHIHeapRef& H, uint64_t O) override
            {
                auto* N = Cast<FAccelStruct>(A.Get()); auto* Heap = Cast<FHeap>(H.Get()); if (!N || !Heap || !Owns(N) || !Owns(Heap)) return WrongStatus();
                if (!N->GetDesc().mbVirtual) return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Only virtual acceleration structures can be placed in heaps.");
                return mDevice->bindAccelStructMemory(N->mHandle, Heap->mHandle, O) ? FArdaRHIStatus{} : FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "NVRHI failed to bind acceleration-structure memory.");
            }
            TArdaRHIResult<FArdaRHITextureTiling> GetTextureTiling(const FArdaRHITextureRef& T) override
            {
                if (!mCapabilities.mbTiledTextures) return Unsupported<FArdaRHITextureTiling>("Tiled textures are unsupported.");
                auto* N = Cast<FTexture>(T.Get()); if (!N || !Owns(N)) return Wrong<FArdaRHITextureTiling>();
                if (!N->GetDesc().mbTiled) return Invalid<FArdaRHITextureTiling>("Texture was not created as tiled.");
                nvrhi::PackedMipDesc P; nvrhi::TileShape Shape; uint32_t Tiles = 0, Count = 0;
                mDevice->getTextureTiling(N->mHandle, &Tiles, &P, &Shape, &Count, nullptr);
                eastl::vector<nvrhi::SubresourceTiling> Native(Count);
                if (Count) mDevice->getTextureTiling(N->mHandle, &Tiles, &P, &Shape, &Count, Native.data());
                FArdaRHITextureTiling R; R.mTileCount = Tiles; R.mPackedMips = { P.numStandardMips, P.numPackedMips, P.numTilesForPackedMips, P.startTileIndexInOverallResource }; R.mTileShape = { Shape.widthInTexels, Shape.heightInTexels, Shape.depthInTexels };
                for (const auto& S : Native) R.mSubresources.push_back({ S.widthInTiles, S.heightInTiles, S.depthInTiles, S.startTileIndexInOverallResource });
                return Ok(std::move(R));
            }
            FArdaRHIStatus UpdateTextureTileMappings(const FArdaRHITextureRef& T, const eastl::vector<FArdaRHITextureTileMapping>& M, EArdaRHIQueueType Q) override
            {
                if (!mCapabilities.mbTiledTextures) return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Tiled textures are unsupported.");
                auto* N = Cast<FTexture>(T.Get()); if (!N || !Owns(N)) return WrongStatus();
                if (!N->GetDesc().mbTiled || M.empty()) return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "A tiled texture and at least one mapping are required.");
                eastl::vector<nvrhi::TextureTilesMapping> Native; Native.reserve(M.size());
                eastl::vector<eastl::vector<nvrhi::TiledTextureCoordinate>> Coordinates(M.size());
                eastl::vector<eastl::vector<nvrhi::TiledTextureRegion>> Regions(M.size());
                for (size_t Index = 0; Index < M.size(); ++Index) { const auto& V = M[Index]; auto* Heap = Cast<FHeap>(V.mHeap.Get()); if (!Heap || !Owns(Heap)) return WrongStatus(); if (V.mCoordinates.empty() || V.mCoordinates.size() != V.mRegions.size() || V.mCoordinates.size() != V.mByteOffsets.size()) return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Tile mapping arrays must be non-empty and have equal lengths."); for (const auto& C : V.mCoordinates) Coordinates[Index].push_back({ C.mMipLevel, C.mArrayLevel, C.mX, C.mY, C.mZ }); for (const auto& R : V.mRegions) Regions[Index].push_back({ R.mTileCount, R.mWidth, R.mHeight, R.mDepth }); nvrhi::TextureTilesMapping X; X.tiledTextureCoordinates = Coordinates[Index].data(); X.tiledTextureRegions = Regions[Index].data(); X.byteOffsets = const_cast<uint64_t*>(V.mByteOffsets.data()); X.numTextureRegions = static_cast<uint32_t>(V.mCoordinates.size()); X.heap = Heap->mHandle; Native.push_back(X); }
                mDevice->updateTextureTileMappings(N->mHandle, Native.data(), static_cast<uint32_t>(Native.size()), ToNvrhi(Q)); return {};
            }
            FArdaRHIStatus QueryWorkGraphSupport() const override { return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Work graphs are not exposed by this NVRHI revision."); }
            FArdaRHIStatus QueryShaderBundleSupport() const override { return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Shader bundles are not exposed by this NVRHI revision."); }
            FArdaRHIStatus QueryCustomPresentSupport() const override { return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Custom present is not exposed by the backend-neutral RHI."); }
            FArdaRHIStatus QueryStreamSourceSupport() const override { return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Stream-source output is not exposed by this NVRHI revision."); }
            void TrimDescriptorCaches() override
            {
                std::lock_guard<std::mutex> Lock(mCacheMutex);
                mSamplers.Clear();
                mBindingLayouts.Clear();
                mInputLayouts.Clear();
                mMeshletPipelines.Clear();
                mRayTracingPipelines.Clear();
                mRasterStates.Clear();
                mBlendStates.Clear();
                mDepthStencilStates.Clear();
                mImportedTextures.Clear();
                mImportedBuffers.Clear();
            }
            FArdaRHICacheStats GetDescriptorCacheStats() const noexcept override
            {
                std::lock_guard<std::mutex> Lock(mCacheMutex);
                return {
                    mSamplers.Size(),
                    mBindingLayouts.Size(),
                    mInputLayouts.Size(),
                    0,
                    0,
                    mMeshletPipelines.Size(),
                    mRayTracingPipelines.Size(),
                    mRasterStates.Size(),
                    mBlendStates.Size(),
                    mDepthStencilStates.Size()
                };
            }
            FArdaRHIStatus WaitForIdle() override { return mDevice->waitForIdle() ? FArdaRHIStatus{} : FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "NVRHI waitForIdle failed."); }
            void RunGarbageCollection() override { mDevice->runGarbageCollection(); }

        private:
            template <typename T> static TArdaRHIResult<T> Ok(T V) { return { std::move(V), {} }; }
            template <typename T> static TArdaRHIResult<T> Fail(const char* M) { return { {}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, M) }; }
            template <typename T> static TArdaRHIResult<T> Invalid(const char* M) { return { {}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, M) }; }
            template <typename T> static TArdaRHIResult<T> Unsupported(const char* M) { return { {}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, M) }; }
            template <typename T> static TArdaRHIResult<T> Wrong() { return { {}, FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice, "Resource belongs to another RHI device or implementation.") }; }
            static FArdaRHIStatus WrongStatus() { return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice, "Resource belongs to another RHI device or implementation."); }
            bool Owns(const IArdaRHIResource* R) const noexcept { auto* V = Cast<FResource>(R); return V && V->GetOwner() == this; }
            bool IsViewable(const IArdaRHIResource* R) const noexcept { return Cast<FTexture>(R) || Cast<FBuffer>(R); }
            bool SetShader(nvrhi::ShaderHandle& Out, const FArdaRHIShaderRef& In) const { if (!In) return true; auto* V = Cast<FShader>(In.Get()); if (!V || !Owns(V)) return false; Out = V->mHandle; return true; }
            bool ValidateGeometryOwnership(const FArdaRHIRayTracingGeometryDesc& G) const
            {
                auto Owned = [this](const IArdaRHIResource* R) { return !R || Owns(R); };
                return Owned(G.mIndexBuffer.Get()) && Owned(G.mVertexOrAABBBuffer.Get()) &&
                    Owned(G.mOpacityMicromap.Get()) && Owned(G.mOpacityMicromapIndexBuffer.Get());
            }
            bool GetBinding(const FArdaRHIBindingSetRef& B, nvrhi::IBindingSet*& Out) const
            {
                Out = nullptr; if (!B) return true;
                if (auto* V = Cast<FBindingSet>(B.Get())) { if (!Owns(V)) return false; Out = V->mHandle; return true; }
                if (auto* V = Cast<FDescriptorTable>(B.Get())) { if (!Owns(V)) return false; Out = V->mHandle; return true; }
                return false;
            }
            TArdaRHIResult<int> AddShaderTableEntry(const FArdaRHIShaderTableRef& T, const char* E, const FArdaRHIBindingSetRef& B, uint32_t Kind)
            {
                auto* N = Cast<FShaderTable>(T.Get()); nvrhi::IBindingSet* Native = nullptr;
                if (!N || !Owns(N)) return Wrong<int>();
                if (!E || !*E) return Invalid<int>("Shader-table export name is required.");
                if (!GetBinding(B, Native)) return Wrong<int>();
                int Index = Kind == 0 ? N->mHandle->addMissShader(E, Native) :
                    Kind == 1 ? N->mHandle->addHitGroup(E, Native) : N->mHandle->addCallableShader(E, Native);
                return Index >= 0 ? Ok(Index) : Fail<int>("NVRHI failed to add shader-table entry.");
            }
            eastl::shared_ptr<void> mBackendLifetime;
            nvrhi::DeviceHandle mDevice;
            FArdaRHICapabilities mCapabilities;
            mutable std::mutex mCacheMutex;
            TDescriptorCache<FArdaRHISamplerDesc, FArdaRHISamplerRef> mSamplers;
            TDescriptorCache<FArdaRHIBindingLayoutDesc, FArdaRHIBindingLayoutRef> mBindingLayouts;
            TDescriptorCache<FArdaRHIInputLayoutDesc, FArdaRHIInputLayoutRef> mInputLayouts;
            TDescriptorCache<FArdaRHIMeshletPipelineDesc, FArdaRHIMeshletPipelineRef> mMeshletPipelines;
            TDescriptorCache<FArdaRHIRayTracingPipelineDesc, FArdaRHIRayTracingPipelineRef> mRayTracingPipelines;
            TDescriptorCache<FArdaRHIRasterState, FArdaRHIRasterStateRef> mRasterStates;
            TDescriptorCache<FArdaRHIBlendState, FArdaRHIBlendStateRef> mBlendStates;
            TDescriptorCache<FArdaRHIDepthStencilState, FArdaRHIDepthStencilStateRef> mDepthStencilStates;
            TDescriptorCache<FArdaRHINativeTextureImportDesc, FArdaRHITextureRef> mImportedTextures;
            TDescriptorCache<FArdaRHINativeBufferImportDesc, FArdaRHIBufferRef> mImportedBuffers;
        };
    }

    FArdaRHIDeviceRef CreateArdaNvrhiDevice(
        nvrhi::DeviceHandle Device,
        eastl::shared_ptr<void> BackendLifetime)
    {
        return Device
            ? FArdaRHIDeviceRef(new FDevice(
                std::move(Device), std::move(BackendLifetime)))
            : FArdaRHIDeviceRef{};
    }

    FArdaRHIFramebufferRef CreateArdaNvrhiFramebuffer(
        const FArdaRHIDeviceRef& Device,
        nvrhi::FramebufferHandle Framebuffer,
        const FArdaRHIFramebufferDesc& Desc)
    {
        auto* Owner = Cast<FDevice>(Device.Get());
        return Owner && Framebuffer
            ? FArdaRHIFramebufferRef(new FFramebuffer(Desc, std::move(Framebuffer), Owner))
            : FArdaRHIFramebufferRef{};
    }
}
