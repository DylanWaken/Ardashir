#include "Common/ArdaNativeRHI.h"
#include "Common/ArdaNativePipelineCache.h"

#if !defined(_WIN32)
#error The native D3D12 backend is Windows-only.
#endif

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <windows.h>

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

extern "C"
{
    __declspec(dllexport) extern const UINT D3D12SDKVersion = 619;
    __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}

namespace arda::backend
{
    namespace
    {
        using Microsoft::WRL::ComPtr;
        using namespace rhi;
        using namespace rhi::native;

        FArdaRHIStatus D3D12Failure(const char* Message, HRESULT Result = E_FAIL)
        {
            eastl::string Text = Message ? Message : "D3D12 operation failed.";
            char Suffix[32]{};
            std::snprintf(Suffix, sizeof(Suffix), " (HRESULT 0x%08X)",
                static_cast<unsigned>(Result));
            Text += Suffix;
            return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Text.c_str());
        }

        template <typename T>
        TArdaRHIResult<T> Fail(FArdaRHIStatus Status)
        {
            return { {}, eastl::move(Status) };
        }

        DXGI_FORMAT ToDxgi(EArdaRHIFormat Format) noexcept
        {
            switch (Format)
            {
            case EArdaRHIFormat::R8UInt: return DXGI_FORMAT_R8_UINT;
            case EArdaRHIFormat::R8SInt: return DXGI_FORMAT_R8_SINT;
            case EArdaRHIFormat::R8UNorm: return DXGI_FORMAT_R8_UNORM;
            case EArdaRHIFormat::R8SNorm: return DXGI_FORMAT_R8_SNORM;
            case EArdaRHIFormat::RG8UInt: return DXGI_FORMAT_R8G8_UINT;
            case EArdaRHIFormat::RG8SInt: return DXGI_FORMAT_R8G8_SINT;
            case EArdaRHIFormat::RG8UNorm: return DXGI_FORMAT_R8G8_UNORM;
            case EArdaRHIFormat::RG8SNorm: return DXGI_FORMAT_R8G8_SNORM;
            case EArdaRHIFormat::R16UInt: return DXGI_FORMAT_R16_UINT;
            case EArdaRHIFormat::R16SInt: return DXGI_FORMAT_R16_SINT;
            case EArdaRHIFormat::R16UNorm: return DXGI_FORMAT_R16_UNORM;
            case EArdaRHIFormat::R16SNorm: return DXGI_FORMAT_R16_SNORM;
            case EArdaRHIFormat::R16Float: return DXGI_FORMAT_R16_FLOAT;
            case EArdaRHIFormat::RGBA8UInt: return DXGI_FORMAT_R8G8B8A8_UINT;
            case EArdaRHIFormat::RGBA8SInt: return DXGI_FORMAT_R8G8B8A8_SINT;
            case EArdaRHIFormat::RGBA8UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case EArdaRHIFormat::RGBA8SNorm: return DXGI_FORMAT_R8G8B8A8_SNORM;
            case EArdaRHIFormat::BGRA8UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
            case EArdaRHIFormat::SRGBA8UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            case EArdaRHIFormat::SBGRA8UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            case EArdaRHIFormat::R10G10B10A2UNorm: return DXGI_FORMAT_R10G10B10A2_UNORM;
            case EArdaRHIFormat::R11G11B10Float: return DXGI_FORMAT_R11G11B10_FLOAT;
            case EArdaRHIFormat::RG16UInt: return DXGI_FORMAT_R16G16_UINT;
            case EArdaRHIFormat::RG16SInt: return DXGI_FORMAT_R16G16_SINT;
            case EArdaRHIFormat::RG16UNorm: return DXGI_FORMAT_R16G16_UNORM;
            case EArdaRHIFormat::RG16SNorm: return DXGI_FORMAT_R16G16_SNORM;
            case EArdaRHIFormat::RG16Float: return DXGI_FORMAT_R16G16_FLOAT;
            case EArdaRHIFormat::R32UInt: return DXGI_FORMAT_R32_UINT;
            case EArdaRHIFormat::R32SInt: return DXGI_FORMAT_R32_SINT;
            case EArdaRHIFormat::R32Float: return DXGI_FORMAT_R32_FLOAT;
            case EArdaRHIFormat::RGBA16UInt: return DXGI_FORMAT_R16G16B16A16_UINT;
            case EArdaRHIFormat::RGBA16SInt: return DXGI_FORMAT_R16G16B16A16_SINT;
            case EArdaRHIFormat::RGBA16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case EArdaRHIFormat::RGBA16UNorm: return DXGI_FORMAT_R16G16B16A16_UNORM;
            case EArdaRHIFormat::RGBA16SNorm: return DXGI_FORMAT_R16G16B16A16_SNORM;
            case EArdaRHIFormat::RG32UInt: return DXGI_FORMAT_R32G32_UINT;
            case EArdaRHIFormat::RG32SInt: return DXGI_FORMAT_R32G32_SINT;
            case EArdaRHIFormat::RG32Float: return DXGI_FORMAT_R32G32_FLOAT;
            case EArdaRHIFormat::RGB32UInt: return DXGI_FORMAT_R32G32B32_UINT;
            case EArdaRHIFormat::RGB32SInt: return DXGI_FORMAT_R32G32B32_SINT;
            case EArdaRHIFormat::RGB32Float: return DXGI_FORMAT_R32G32B32_FLOAT;
            case EArdaRHIFormat::RGBA32UInt: return DXGI_FORMAT_R32G32B32A32_UINT;
            case EArdaRHIFormat::RGBA32SInt: return DXGI_FORMAT_R32G32B32A32_SINT;
            case EArdaRHIFormat::RGBA32Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case EArdaRHIFormat::D16: return DXGI_FORMAT_D16_UNORM;
            case EArdaRHIFormat::D24S8: return DXGI_FORMAT_D24_UNORM_S8_UINT;
            case EArdaRHIFormat::D32: return DXGI_FORMAT_D32_FLOAT;
            case EArdaRHIFormat::D32S8: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
            case EArdaRHIFormat::BC1UNorm: return DXGI_FORMAT_BC1_UNORM;
            case EArdaRHIFormat::BC1UNormSRGB: return DXGI_FORMAT_BC1_UNORM_SRGB;
            case EArdaRHIFormat::BC2UNorm: return DXGI_FORMAT_BC2_UNORM;
            case EArdaRHIFormat::BC2UNormSRGB: return DXGI_FORMAT_BC2_UNORM_SRGB;
            case EArdaRHIFormat::BC3UNorm: return DXGI_FORMAT_BC3_UNORM;
            case EArdaRHIFormat::BC3UNormSRGB: return DXGI_FORMAT_BC3_UNORM_SRGB;
            case EArdaRHIFormat::BC4UNorm: return DXGI_FORMAT_BC4_UNORM;
            case EArdaRHIFormat::BC4SNorm: return DXGI_FORMAT_BC4_SNORM;
            case EArdaRHIFormat::BC5UNorm: return DXGI_FORMAT_BC5_UNORM;
            case EArdaRHIFormat::BC5SNorm: return DXGI_FORMAT_BC5_SNORM;
            case EArdaRHIFormat::BC6HUFloat: return DXGI_FORMAT_BC6H_UF16;
            case EArdaRHIFormat::BC6HSFloat: return DXGI_FORMAT_BC6H_SF16;
            case EArdaRHIFormat::BC7UNorm: return DXGI_FORMAT_BC7_UNORM;
            case EArdaRHIFormat::BC7UNormSRGB: return DXGI_FORMAT_BC7_UNORM_SRGB;
            default: return DXGI_FORMAT_UNKNOWN;
            }
        }

        uint32_t FormatSize(EArdaRHIFormat Format) noexcept
        {
            switch (Format)
            {
            case EArdaRHIFormat::R8UInt:
            case EArdaRHIFormat::R8SInt:
            case EArdaRHIFormat::R8UNorm:
            case EArdaRHIFormat::R8SNorm: return 1;
            case EArdaRHIFormat::RG8UInt:
            case EArdaRHIFormat::RG8SInt:
            case EArdaRHIFormat::RG8UNorm:
            case EArdaRHIFormat::RG8SNorm:
            case EArdaRHIFormat::R16UInt:
            case EArdaRHIFormat::R16SInt:
            case EArdaRHIFormat::R16UNorm:
            case EArdaRHIFormat::R16SNorm:
            case EArdaRHIFormat::R16Float:
            case EArdaRHIFormat::D16: return 2;
            case EArdaRHIFormat::RGBA8UInt:
            case EArdaRHIFormat::RGBA8SInt:
            case EArdaRHIFormat::RGBA8UNorm:
            case EArdaRHIFormat::RGBA8SNorm:
            case EArdaRHIFormat::BGRA8UNorm:
            case EArdaRHIFormat::SRGBA8UNorm:
            case EArdaRHIFormat::SBGRA8UNorm:
            case EArdaRHIFormat::R10G10B10A2UNorm:
            case EArdaRHIFormat::R11G11B10Float:
            case EArdaRHIFormat::RG16UInt:
            case EArdaRHIFormat::RG16SInt:
            case EArdaRHIFormat::RG16UNorm:
            case EArdaRHIFormat::RG16SNorm:
            case EArdaRHIFormat::RG16Float:
            case EArdaRHIFormat::R32UInt:
            case EArdaRHIFormat::R32SInt:
            case EArdaRHIFormat::R32Float:
            case EArdaRHIFormat::D24S8:
            case EArdaRHIFormat::D32: return 4;
            case EArdaRHIFormat::RGBA16UInt:
            case EArdaRHIFormat::RGBA16SInt:
            case EArdaRHIFormat::RGBA16Float:
            case EArdaRHIFormat::RGBA16UNorm:
            case EArdaRHIFormat::RGBA16SNorm:
            case EArdaRHIFormat::RG32UInt:
            case EArdaRHIFormat::RG32SInt:
            case EArdaRHIFormat::RG32Float:
            case EArdaRHIFormat::D32S8: return 8;
            case EArdaRHIFormat::RGB32UInt:
            case EArdaRHIFormat::RGB32SInt:
            case EArdaRHIFormat::RGB32Float: return 12;
            case EArdaRHIFormat::RGBA32UInt:
            case EArdaRHIFormat::RGBA32SInt:
            case EArdaRHIFormat::RGBA32Float: return 16;
            default: return 0;
            }
        }

        D3D12_RESOURCE_STATES ToD3D12State(EArdaRHIResourceState State) noexcept
        {
            if (State == EArdaRHIResourceState::Unknown ||
                State == EArdaRHIResourceState::Common ||
                State == EArdaRHIResourceState::Present)
                return State == EArdaRHIResourceState::Present
                    ? D3D12_RESOURCE_STATE_PRESENT : D3D12_RESOURCE_STATE_COMMON;
            D3D12_RESOURCE_STATES Result = D3D12_RESOURCE_STATE_COMMON;
            if (HasAnyFlags(State, EArdaRHIResourceState::ConstantBuffer) ||
                HasAnyFlags(State, EArdaRHIResourceState::VertexBuffer))
                Result |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            if (HasAnyFlags(State, EArdaRHIResourceState::IndexBuffer)) Result |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
            if (HasAnyFlags(State, EArdaRHIResourceState::IndirectArgument)) Result |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
            if (HasAnyFlags(State, EArdaRHIResourceState::PixelShaderResource)) Result |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            if (HasAnyFlags(State, EArdaRHIResourceState::NonPixelShaderResource)) Result |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            if (HasAnyFlags(State, EArdaRHIResourceState::UnorderedAccess)) Result |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            if (HasAnyFlags(State, EArdaRHIResourceState::RenderTarget)) Result |= D3D12_RESOURCE_STATE_RENDER_TARGET;
            if (HasAnyFlags(State, EArdaRHIResourceState::DepthWrite)) Result |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
            if (HasAnyFlags(State, EArdaRHIResourceState::DepthRead)) Result |= D3D12_RESOURCE_STATE_DEPTH_READ;
            if (HasAnyFlags(State, EArdaRHIResourceState::CopyDest)) Result |= D3D12_RESOURCE_STATE_COPY_DEST;
            if (HasAnyFlags(State, EArdaRHIResourceState::CopySource)) Result |= D3D12_RESOURCE_STATE_COPY_SOURCE;
            if (HasAnyFlags(State, EArdaRHIResourceState::ResolveDest)) Result |= D3D12_RESOURCE_STATE_RESOLVE_DEST;
            if (HasAnyFlags(State, EArdaRHIResourceState::ResolveSource)) Result |= D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
            return Result;
        }

        D3D12_COMPARISON_FUNC ToD3D12Comparison(EArdaRHIComparisonFunc Value) noexcept
        {
            return static_cast<D3D12_COMPARISON_FUNC>(static_cast<uint32_t>(Value) + 1);
        }

        D3D12_BLEND ToD3D12Blend(EArdaRHIBlendFactor Value) noexcept
        {
            switch (Value)
            {
            case EArdaRHIBlendFactor::Zero: return D3D12_BLEND_ZERO;
            case EArdaRHIBlendFactor::One: return D3D12_BLEND_ONE;
            case EArdaRHIBlendFactor::SourceColor: return D3D12_BLEND_SRC_COLOR;
            case EArdaRHIBlendFactor::InverseSourceColor: return D3D12_BLEND_INV_SRC_COLOR;
            case EArdaRHIBlendFactor::SourceAlpha: return D3D12_BLEND_SRC_ALPHA;
            case EArdaRHIBlendFactor::InverseSourceAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
            case EArdaRHIBlendFactor::DestinationAlpha: return D3D12_BLEND_DEST_ALPHA;
            case EArdaRHIBlendFactor::InverseDestinationAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
            case EArdaRHIBlendFactor::DestinationColor: return D3D12_BLEND_DEST_COLOR;
            case EArdaRHIBlendFactor::InverseDestinationColor: return D3D12_BLEND_INV_DEST_COLOR;
            }
            return D3D12_BLEND_ONE;
        }

        D3D12_DESCRIPTOR_RANGE_TYPE ToRangeType(EArdaRHIBindingType Type)
        {
            switch (Type)
            {
            case EArdaRHIBindingType::TextureUAV:
            case EArdaRHIBindingType::TypedBufferUAV:
            case EArdaRHIBindingType::StructuredBufferUAV:
            case EArdaRHIBindingType::RawBufferUAV:
            case EArdaRHIBindingType::SamplerFeedbackTextureUAV:
                return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            case EArdaRHIBindingType::ConstantBuffer:
            case EArdaRHIBindingType::VolatileConstantBuffer:
                return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
            case EArdaRHIBindingType::Sampler:
                return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
            default:
                return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            }
        }

        D3D12_SHADER_VISIBILITY ToVisibility(EArdaRHIShaderStage Stage)
        {
            switch (Stage)
            {
            case EArdaRHIShaderStage::Vertex: return D3D12_SHADER_VISIBILITY_VERTEX;
            case EArdaRHIShaderStage::Hull: return D3D12_SHADER_VISIBILITY_HULL;
            case EArdaRHIShaderStage::Domain: return D3D12_SHADER_VISIBILITY_DOMAIN;
            case EArdaRHIShaderStage::Geometry: return D3D12_SHADER_VISIBILITY_GEOMETRY;
            case EArdaRHIShaderStage::Pixel: return D3D12_SHADER_VISIBILITY_PIXEL;
            default: return D3D12_SHADER_VISIBILITY_ALL;
            }
        }

        class FD3D12Texture final : public IArdaNativeObject
        {
        public:
            const void* GetIdentity() const noexcept override { return mResource.Get(); }
            ComPtr<ID3D12Resource> mResource;
            FArdaRHITextureDesc mDesc;
            ComPtr<ID3D12DescriptorHeap> mRtvHeap;
            ComPtr<ID3D12DescriptorHeap> mDsvHeap;
            D3D12_CPU_DESCRIPTOR_HANDLE mRtv{};
            D3D12_CPU_DESCRIPTOR_HANDLE mDsv{};
        };

        class FD3D12Buffer final : public IArdaNativeObject
        {
        public:
            const void* GetIdentity() const noexcept override { return mResource.Get(); }
            ComPtr<ID3D12Resource> mResource;
            FArdaRHIBufferDesc mDesc;
        };

        class FD3D12Sampler final : public IArdaNativeObject
        {
        public:
            const void* GetIdentity() const noexcept override { return this; }
            D3D12_SAMPLER_DESC mDesc{};
        };

        class FD3D12Shader final : public IArdaNativeObject
        {
        public:
            const void* GetIdentity() const noexcept override { return this; }
            eastl::vector<uint8_t> mBytecode;
            EArdaRHIShaderStage mStage = EArdaRHIShaderStage::None;
        };

        class FD3D12BindingLayout final : public IArdaNativeObject
        {
        public:
            const void* GetIdentity() const noexcept override { return this; }
            FArdaRHIBindingLayoutDesc mDesc;
        };

        struct FD3D12DescriptorTable
        {
            EArdaRHIBindingType mType = EArdaRHIBindingType::TextureSRV;
            D3D12_GPU_DESCRIPTOR_HANDLE mGpu{};
        };

        struct FD3D12DescriptorAllocation
        {
            D3D12_CPU_DESCRIPTOR_HANDLE mCpu{};
            D3D12_GPU_DESCRIPTOR_HANDLE mGpu{};
            uint32_t mOffset = 0;
            uint32_t mCount = 0;
            bool mbSampler = false;
        };

        class FD3D12DescriptorAllocator
        {
        public:
            struct FRange
            {
                uint32_t mOffset = 0;
                uint32_t mCount = 0;
            };

            void Initialize(
                ComPtr<ID3D12DescriptorHeap> ResourceHeap,
                ComPtr<ID3D12DescriptorHeap> SamplerHeap,
                uint32_t ResourceIncrement,
                uint32_t SamplerIncrement)
            {
                mResourceHeap = eastl::move(ResourceHeap);
                mSamplerHeap = eastl::move(SamplerHeap);
                mResourceIncrement = ResourceIncrement;
                mSamplerIncrement = SamplerIncrement;
                mResourceFree.push_back({ 0, 65536 });
                mSamplerFree.push_back({ 0, 2048 });
            }

            TArdaRHIResult<FD3D12DescriptorAllocation> Allocate(
                bool bSampler,
                uint32_t Count);
            void Free(const FD3D12DescriptorAllocation& Allocation) noexcept;
            [[nodiscard]] size_t GetActiveCount(bool bSampler) const noexcept
            {
                std::lock_guard<std::mutex> Lock(mMutex);
                return bSampler ? mActiveSamplers : mActiveResources;
            }

        private:
            mutable std::mutex mMutex;
            ComPtr<ID3D12DescriptorHeap> mResourceHeap;
            ComPtr<ID3D12DescriptorHeap> mSamplerHeap;
            eastl::vector<FRange> mResourceFree;
            eastl::vector<FRange> mSamplerFree;
            uint32_t mResourceIncrement = 0;
            uint32_t mSamplerIncrement = 0;
            size_t mActiveResources = 0;
            size_t mActiveSamplers = 0;
        };

        class FD3D12BindingSet final : public IArdaNativeObject
        {
        public:
            ~FD3D12BindingSet() override
            {
                if (mAllocator)
                    for (const FD3D12DescriptorAllocation& Allocation : mAllocations)
                        mAllocator->Free(Allocation);
            }
            const void* GetIdentity() const noexcept override { return this; }
            eastl::vector<FD3D12DescriptorTable> mTables;
            eastl::vector<FArdaNativeObjectRef> mRetainedObjects;
            eastl::shared_ptr<FD3D12DescriptorAllocator> mAllocator;
            eastl::vector<FD3D12DescriptorAllocation> mAllocations;
        };

        class FD3D12Framebuffer final : public IArdaNativeObject
        {
        public:
            const void* GetIdentity() const noexcept override { return this; }
            eastl::vector<D3D12_CPU_DESCRIPTOR_HANDLE> mRtvs;
            D3D12_CPU_DESCRIPTOR_HANDLE mDsv{};
            bool mbHasDepth = false;
            eastl::vector<FArdaNativeObjectRef> mRetainedTextures;
        };

        class FD3D12Pipeline final : public IArdaNativeObject
        {
        public:
            const void* GetIdentity() const noexcept override { return mPipeline.Get(); }
            ComPtr<ID3D12PipelineState> mPipeline;
            ComPtr<ID3D12RootSignature> mRootSignature;
            eastl::vector<uint32_t> mLayoutItemCounts;
            eastl::vector<int32_t> mPushConstantRoots;
            D3D12_PRIMITIVE_TOPOLOGY mTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        };

        class FArdaD3D12ApiDevice;

        class FArdaD3D12CommandList final : public IArdaNativeCommandList
        {
        public:
            FArdaD3D12CommandList(
                FArdaD3D12ApiDevice& Device,
                D3D12_COMMAND_LIST_TYPE Type);
            FArdaRHIStatus Initialize();
            FArdaRHIStatus Open() override;
            FArdaRHIStatus Close() override;
            FArdaRHIStatus Reset() override;
            FArdaRHIStatus WriteBuffer(const FArdaNativeObjectRef&, const FArdaRHIBufferDesc&, const void*, size_t, uint64_t) override;
            FArdaRHIStatus CopyBuffer(const FArdaNativeObjectRef&, uint64_t, const FArdaNativeObjectRef&, uint64_t, uint64_t) override;
            FArdaRHIStatus ClearTexture(const FArdaNativeObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureSubresourceRange&, const FArdaRHIColor&) override;
            FArdaRHIStatus ClearDepthStencilTexture(const FArdaNativeObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureSubresourceRange&, bool, float, bool, uint8_t) override;
            FArdaRHIStatus SetTextureState(const FArdaNativeObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureSubresourceRange&, EArdaRHIResourceState) override;
            FArdaRHIStatus SetBufferState(const FArdaNativeObjectRef&, const FArdaRHIBufferDesc&, EArdaRHIResourceState) override;
            void SetAutomaticBarriers(bool bEnabled) override { mbAutomaticBarriers = bEnabled; }
            FArdaRHIStatus BeginTrackingTextureState(const FArdaNativeObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureSubresourceRange&, EArdaRHIResourceState) override;
            FArdaRHIStatus BeginTrackingBufferState(const FArdaNativeObjectRef&, const FArdaRHIBufferDesc&, EArdaRHIResourceState) override;
            FArdaRHIStatus SetUAVBarriersForTexture(const FArdaNativeObjectRef&, bool) override;
            FArdaRHIStatus SetUAVBarriersForBuffer(const FArdaNativeObjectRef&, bool) override;
            void CommitBarriers() override {}
            FArdaRHIStatus SetGraphicsState(const FArdaNativeGraphicsState&) override;
            FArdaRHIStatus SetComputeState(const FArdaNativeComputeState&) override;
            void SetPushConstants(const void*, size_t) override;
            void Draw(const FArdaRHIDrawArguments&) override;
            void DrawIndexed(const FArdaRHIDrawArguments&) override;
            void Dispatch(uint32_t, uint32_t, uint32_t) override;
            void BeginMarker(const char*) override {}
            void EndMarker() override {}

            ID3D12CommandList* GetSubmitList() const noexcept { return mCommandList.Get(); }

        private:
            void Retain(const FArdaNativeObjectRef& Object)
            {
                if (Object) mRetainedObjects.push_back(Object);
            }
            FArdaRHIStatus Transition(const FArdaNativeObjectRef&, D3D12_RESOURCE_STATES);
            FArdaRHIStatus BindDescriptorSets(const FD3D12Pipeline&, const eastl::vector<FArdaNativeObjectRef>&, bool);
            FArdaD3D12ApiDevice& mDevice;
            D3D12_COMMAND_LIST_TYPE mType = D3D12_COMMAND_LIST_TYPE_DIRECT;
            ComPtr<ID3D12CommandAllocator> mAllocator;
            ComPtr<ID3D12GraphicsCommandList> mCommandList;
            eastl::vector<ComPtr<ID3D12Resource>> mUploadResources;
            eastl::vector<FArdaNativeObjectRef> mRetainedObjects;
            std::unordered_map<const void*, D3D12_RESOURCE_STATES> mStates;
            FD3D12Pipeline* mBoundGraphicsPipeline = nullptr;
            FD3D12Pipeline* mBoundComputePipeline = nullptr;
            bool mbOpen = false;
            bool mbAutomaticBarriers = true;
        };

        class FArdaD3D12ApiDevice final : public IArdaNativeApiDevice
        {
        public:
            FArdaD3D12ApiDevice(
                ComPtr<ID3D12Device> Device,
                ComPtr<ID3D12CommandQueue> GraphicsQueue,
                ComPtr<ID3D12CommandQueue> ComputeQueue,
                std::filesystem::path PipelineCacheDirectory,
                IArdaDiagnosticCallback* DiagnosticCallback,
                eastl::shared_ptr<void> LifetimeToken)
                : mD3DDevice(eastl::move(Device))
                , mQueue(eastl::move(GraphicsQueue))
                , mComputeQueue(eastl::move(ComputeQueue))
                , mPipelineCacheDirectory(eastl::move(PipelineCacheDirectory))
                , mDiagnosticCallback(DiagnosticCallback)
                , mLifetimeToken(eastl::move(LifetimeToken)) {}
            ~FArdaD3D12ApiDevice() override;
            FArdaRHIStatus Initialize();
            const FArdaRHICapabilities& GetCapabilities() const noexcept override { return mCapabilities; }
            EArdaRHINativeResourceType GetTextureImportType() const noexcept override { return EArdaRHINativeResourceType::D3D12Resource; }
            EArdaRHINativeResourceType GetBufferImportType() const noexcept override { return EArdaRHINativeResourceType::D3D12Resource; }
            FArdaNativeObjectResult CreateTexture(const FArdaRHITextureDesc&) override;
            FArdaNativeObjectResult CreateBuffer(const FArdaRHIBufferDesc&) override;
            TArdaRHIResult<void*> MapBuffer(
                const FArdaNativeObjectRef&, uint64_t, size_t) override;
            void UnmapBuffer(const FArdaNativeObjectRef&) noexcept override;
            FArdaNativeObjectResult ImportTexture(const FArdaRHINativeTextureImportDesc&) override;
            FArdaNativeObjectResult ImportBuffer(const FArdaRHINativeBufferImportDesc&) override;
            FArdaNativeObjectResult CreateSampler(const FArdaRHISamplerDesc&) override;
            FArdaNativeObjectResult CreateShader(const FArdaRHIShaderDesc&) override;
            FArdaNativeObjectResult CreateBindingLayout(const FArdaRHIBindingLayoutDesc&) override;
            FArdaNativeObjectResult CreateBindingSet(const FArdaRHIBindingSetDesc&, const FArdaNativeObjectRef&, const eastl::vector<FArdaNativeBinding>&) override;
            FArdaNativeObjectResult CreateFramebuffer(const FArdaNativeFramebufferCreateInfo&) override;
            FArdaNativeObjectResult CreateGraphicsPipeline(const FArdaNativeGraphicsPipelineCreateInfo&) override;
            FArdaNativeObjectResult CreateComputePipeline(const FArdaNativeComputePipelineCreateInfo&) override;
            TArdaRHIResult<eastl::unique_ptr<IArdaNativeCommandList>> CreateCommandList(EArdaRHIQueueType, bool) override;
            TArdaRHIResult<uint64_t> ExecuteCommandList(IArdaNativeCommandList&, EArdaRHIQueueType) override;
            FArdaRHIStatus WaitForSubmission(uint64_t) override;
            FArdaRHIStatus WaitForIdle() override;
            void RunGarbageCollection() override {}
            FArdaNativeLifetimeStats GetLifetimeStats() const noexcept override
            {
                FArdaNativeLifetimeStats Stats;
                if (mDescriptorAllocator)
                {
                    Stats.mResourceDescriptors =
                        mDescriptorAllocator->GetActiveCount(false);
                    Stats.mSamplerDescriptors =
                        mDescriptorAllocator->GetActiveCount(true);
                }
                return Stats;
            }
            void FlushPipelineCache() noexcept override;

            ID3D12Device& GetDevice() const noexcept { return *mD3DDevice.Get(); }
            ID3D12DescriptorHeap* GetResourceHeap() const noexcept { return mResourceHeap.Get(); }
            ID3D12DescriptorHeap* GetSamplerHeap() const noexcept { return mSamplerHeap.Get(); }

        private:
            TArdaRHIResult<FD3D12DescriptorAllocation> AllocateDescriptors(
                bool bSampler,
                uint32_t Count);
            FArdaRHIStatus CreateTextureViews(FD3D12Texture& Texture);
            TArdaRHIResult<ComPtr<ID3D12RootSignature>> CreateRootSignature(
                const eastl::vector<FArdaNativeObjectRef>& Layouts,
                eastl::vector<uint32_t>& OutItemCounts,
                eastl::vector<int32_t>& OutPushConstantRoots);
            [[nodiscard]] ID3D12CommandQueue* GetQueue(
                EArdaRHIQueueType Queue) const noexcept;
            void InitializePipelineCache();

            FArdaRHICapabilities mCapabilities;
            ComPtr<ID3D12Device> mD3DDevice;
            ComPtr<ID3D12CommandQueue> mQueue;
            ComPtr<ID3D12CommandQueue> mComputeQueue;
            ComPtr<ID3D12DescriptorHeap> mResourceHeap;
            ComPtr<ID3D12DescriptorHeap> mSamplerHeap;
            ComPtr<ID3D12Fence> mFence;
            HANDLE mFenceEvent = nullptr;
            eastl::shared_ptr<void> mLifetimeToken;
            eastl::shared_ptr<FD3D12DescriptorAllocator> mDescriptorAllocator;
            uint32_t mResourceDescriptorSize = 0;
            uint32_t mSamplerDescriptorSize = 0;
            std::atomic<uint64_t> mFenceValue{ 0 };
            ComPtr<ID3D12PipelineLibrary> mPipelineLibrary;
            std::vector<uint8_t> mPipelineCacheSource;
            std::filesystem::path mPipelineCacheDirectory;
            IArdaDiagnosticCallback* mDiagnosticCallback = nullptr;
            std::mutex mPipelineCacheMutex;
            bool mbPipelineCacheDirty = false;
        };

        FArdaD3D12ApiDevice::~FArdaD3D12ApiDevice()
        {
            (void)WaitForIdle();
            if (mFenceEvent)
                CloseHandle(mFenceEvent);
        }

        FArdaRHIStatus FArdaD3D12ApiDevice::Initialize()
        {
            D3D12_DESCRIPTOR_HEAP_DESC HeapDesc{};
            HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            HeapDesc.NumDescriptors = 65536;
            HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            HRESULT Result = mD3DDevice->CreateDescriptorHeap(
                &HeapDesc, IID_PPV_ARGS(&mResourceHeap));
            if (FAILED(Result)) return D3D12Failure("Failed to create the D3D12 resource descriptor heap.", Result);
            HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            HeapDesc.NumDescriptors = 2048;
            Result = mD3DDevice->CreateDescriptorHeap(
                &HeapDesc, IID_PPV_ARGS(&mSamplerHeap));
            if (FAILED(Result)) return D3D12Failure("Failed to create the D3D12 sampler descriptor heap.", Result);
            mResourceDescriptorSize = mD3DDevice->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            mSamplerDescriptorSize = mD3DDevice->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
            mDescriptorAllocator = eastl::make_shared<FD3D12DescriptorAllocator>();
            mDescriptorAllocator->Initialize(
                mResourceHeap,
                mSamplerHeap,
                mResourceDescriptorSize,
                mSamplerDescriptorSize);
            Result = mD3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence));
            if (FAILED(Result)) return D3D12Failure("Failed to create the D3D12 queue fence.", Result);
            mFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!mFenceEvent)
                return D3D12Failure("Failed to create the D3D12 queue fence event.", HRESULT_FROM_WIN32(GetLastError()));
            mCapabilities.mbGraphicsQueue = true;
            mCapabilities.mbComputeQueue = mComputeQueue != nullptr;
            mCapabilities.mbCopyQueue = false;
            mCapabilities.mbStagingTextures = true;
            mCapabilities.mbQueries = true;
            mCapabilities.mbHeaps = false;
            mCapabilities.mbBindless = false;
            mCapabilities.mbShaderLibraries = true;
            InitializePipelineCache();
            mCapabilities.mbPipelineCachePersistence = mPipelineLibrary != nullptr;
            return {};
        }

        void FArdaD3D12ApiDevice::InitializePipelineCache()
        {
            if (mPipelineCacheDirectory.empty())
                return;
            ComPtr<ID3D12Device1> Device1;
            if (FAILED(mD3DDevice.As(&Device1)))
                return;

            const eastl::string BackendName = "native-d3d12";
            const auto Path = pipeline_cache::MakePath(
                mPipelineCacheDirectory, BackendName);
            std::error_code Error;
            const bool bExists = std::filesystem::exists(Path, Error);
            const bool bValid = bExists && pipeline_cache::ReadBlob(
                Path, BackendName, EArdaBackendType::D3D12,
                mPipelineCacheSource);
            HRESULT Result = Device1->CreatePipelineLibrary(
                bValid && !mPipelineCacheSource.empty()
                    ? mPipelineCacheSource.data() : nullptr,
                bValid ? mPipelineCacheSource.size() : 0,
                IID_PPV_ARGS(&mPipelineLibrary));
            if (FAILED(Result) && bValid)
            {
                pipeline_cache::Message(mDiagnosticCallback,
                    EArdaDiagnosticSeverity::Warning,
                    "D3D12 rejected persistent pipeline library data; using an empty library.");
                mPipelineCacheSource.clear();
                Result = Device1->CreatePipelineLibrary(
                    nullptr, 0, IID_PPV_ARGS(&mPipelineLibrary));
            }
            else if (bExists && !bValid)
            {
                pipeline_cache::Message(mDiagnosticCallback,
                    EArdaDiagnosticSeverity::Warning,
                    "Ignoring a corrupt, truncated, or wrong-backend pipeline cache blob.");
            }
            if (FAILED(Result))
            {
                mPipelineLibrary.Reset();
                pipeline_cache::Message(mDiagnosticCallback,
                    EArdaDiagnosticSeverity::Warning,
                    "ID3D12PipelineLibrary is unavailable for this device.");
            }
        }

        TArdaRHIResult<FD3D12DescriptorAllocation>
        FArdaD3D12ApiDevice::AllocateDescriptors(bool bSampler, uint32_t Count)
        {
            return mDescriptorAllocator->Allocate(bSampler, Count);
        }

        TArdaRHIResult<FD3D12DescriptorAllocation>
        FD3D12DescriptorAllocator::Allocate(bool bSampler, uint32_t Count)
        {
            std::lock_guard<std::mutex> Lock(mMutex);
            eastl::vector<FRange>& FreeRanges = bSampler ? mSamplerFree : mResourceFree;
            if (Count == 0)
                return Fail<FD3D12DescriptorAllocation>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "A D3D12 descriptor allocation must contain at least one descriptor."));
            for (size_t Index = 0; Index < FreeRanges.size(); ++Index)
            {
                FRange& Range = FreeRanges[Index];
                if (Range.mCount < Count)
                    continue;
                FD3D12DescriptorAllocation Allocation;
                Allocation.mOffset = Range.mOffset;
                Allocation.mCount = Count;
                Allocation.mbSampler = bSampler;
                ID3D12DescriptorHeap* Heap = bSampler
                    ? mSamplerHeap.Get() : mResourceHeap.Get();
                const uint32_t Increment = bSampler
                    ? mSamplerIncrement : mResourceIncrement;
                Allocation.mCpu = Heap->GetCPUDescriptorHandleForHeapStart();
                Allocation.mGpu = Heap->GetGPUDescriptorHandleForHeapStart();
                Allocation.mCpu.ptr += static_cast<SIZE_T>(Range.mOffset) * Increment;
                Allocation.mGpu.ptr += static_cast<UINT64>(Range.mOffset) * Increment;
                Range.mOffset += Count;
                Range.mCount -= Count;
                if (Range.mCount == 0)
                    FreeRanges.erase(FreeRanges.begin() + Index);
                (bSampler ? mActiveSamplers : mActiveResources) += Count;
                return { Allocation, {} };
            }
            return Fail<FD3D12DescriptorAllocation>(FArdaRHIStatus::Error(
                EArdaRHIResult::BackendFailure,
                "The native D3D12 descriptor heap is exhausted."));
        }

        void FD3D12DescriptorAllocator::Free(
            const FD3D12DescriptorAllocation& Allocation) noexcept
        {
            if (Allocation.mCount == 0)
                return;
            std::lock_guard<std::mutex> Lock(mMutex);
            eastl::vector<FRange>& FreeRanges = Allocation.mbSampler
                ? mSamplerFree : mResourceFree;
            FRange Released{ Allocation.mOffset, Allocation.mCount };
            auto Position = eastl::lower_bound(
                FreeRanges.begin(), FreeRanges.end(), Released,
                [](const FRange& Left, const FRange& Right)
                {
                    return Left.mOffset < Right.mOffset;
                });
            Position = FreeRanges.insert(Position, Released);
            if (Position != FreeRanges.begin())
            {
                auto Previous = Position - 1;
                if (Previous->mOffset + Previous->mCount == Position->mOffset)
                {
                    Previous->mCount += Position->mCount;
                    Position = FreeRanges.erase(Position);
                    Position = Previous;
                }
            }
            const auto Next = Position + 1;
            if (Next != FreeRanges.end() &&
                Position->mOffset + Position->mCount == Next->mOffset)
            {
                Position->mCount += Next->mCount;
                FreeRanges.erase(Next);
            }
            size_t& Active = Allocation.mbSampler
                ? mActiveSamplers : mActiveResources;
            Active = Allocation.mCount <= Active ? Active - Allocation.mCount : 0;
        }

        FArdaRHIStatus FArdaD3D12ApiDevice::CreateTextureViews(FD3D12Texture& Texture)
        {
            if (HasAnyFlags(Texture.mDesc.mUsage, EArdaRHITextureUsage::RenderTarget))
            {
                D3D12_DESCRIPTOR_HEAP_DESC HeapDesc{};
                HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
                HeapDesc.NumDescriptors = 1;
                HRESULT Result = mD3DDevice->CreateDescriptorHeap(
                    &HeapDesc, IID_PPV_ARGS(&Texture.mRtvHeap));
                if (FAILED(Result)) return D3D12Failure("Failed to create a D3D12 RTV heap.", Result);
                Texture.mRtv = Texture.mRtvHeap->GetCPUDescriptorHandleForHeapStart();
                mD3DDevice->CreateRenderTargetView(Texture.mResource.Get(), nullptr, Texture.mRtv);
            }
            if (HasAnyFlags(Texture.mDesc.mUsage, EArdaRHITextureUsage::DepthStencil))
            {
                D3D12_DESCRIPTOR_HEAP_DESC HeapDesc{};
                HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
                HeapDesc.NumDescriptors = 1;
                HRESULT Result = mD3DDevice->CreateDescriptorHeap(
                    &HeapDesc, IID_PPV_ARGS(&Texture.mDsvHeap));
                if (FAILED(Result)) return D3D12Failure("Failed to create a D3D12 DSV heap.", Result);
                Texture.mDsv = Texture.mDsvHeap->GetCPUDescriptorHandleForHeapStart();
                mD3DDevice->CreateDepthStencilView(Texture.mResource.Get(), nullptr, Texture.mDsv);
            }
            return {};
        }

        FArdaNativeObjectResult FArdaD3D12ApiDevice::CreateTexture(
            const FArdaRHITextureDesc& Desc)
        {
            auto Texture = eastl::make_shared<FD3D12Texture>();
            Texture->mDesc = Desc;
            D3D12_HEAP_PROPERTIES Heap{};
            Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            Heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            Heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            Heap.CreationNodeMask = 1;
            Heap.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC Resource{};
            Resource.Dimension = Desc.mDimension == EArdaRHITextureDimension::Texture3D
                ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                : (Desc.mDimension == EArdaRHITextureDimension::Texture1D ||
                   Desc.mDimension == EArdaRHITextureDimension::Texture1DArray
                    ? D3D12_RESOURCE_DIMENSION_TEXTURE1D
                    : D3D12_RESOURCE_DIMENSION_TEXTURE2D);
            Resource.Alignment = 0;
            Resource.Width = Desc.mWidth;
            Resource.Height = Desc.mHeight;
            Resource.DepthOrArraySize = static_cast<UINT16>(
                Resource.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                    ? Desc.mDepth : Desc.mArraySize);
            Resource.MipLevels = static_cast<UINT16>(Desc.mMipLevels);
            Resource.Format = ToDxgi(Desc.mFormat);
            Resource.SampleDesc.Count = Desc.mSampleCount;
            Resource.SampleDesc.Quality = 0;
            Resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            Resource.Flags = D3D12_RESOURCE_FLAG_NONE;
            if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::RenderTarget))
                Resource.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::DepthStencil))
                Resource.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::UnorderedAccess))
                Resource.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            D3D12_CLEAR_VALUE Clear{};
            D3D12_CLEAR_VALUE* ClearPtr = nullptr;
            if (Desc.mbUseClearValue ||
                HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::DepthStencil))
            {
                Clear.Format = Resource.Format;
                if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::DepthStencil))
                {
                    Clear.DepthStencil.Depth = Desc.mbUseClearValue ? Desc.mClearValue.mR : 1.f;
                    Clear.DepthStencil.Stencil = Desc.mbUseClearValue
                        ? static_cast<UINT8>(Desc.mClearValue.mG) : 0;
                }
                else
                {
                    Clear.Color[0] = Desc.mClearValue.mR;
                    Clear.Color[1] = Desc.mClearValue.mG;
                    Clear.Color[2] = Desc.mClearValue.mB;
                    Clear.Color[3] = Desc.mClearValue.mA;
                }
                ClearPtr = &Clear;
            }
            HRESULT Result = mD3DDevice->CreateCommittedResource(
                &Heap, D3D12_HEAP_FLAG_NONE, &Resource,
                ToD3D12State(Desc.mInitialState), ClearPtr,
                IID_PPV_ARGS(&Texture->mResource));
            if (FAILED(Result)) return Fail<FArdaNativeObjectRef>(
                D3D12Failure("Failed to create a D3D12 texture.", Result));
            if (auto Status = CreateTextureViews(*Texture); !Status)
                return Fail<FArdaNativeObjectRef>(eastl::move(Status));
            return { Texture, {} };
        }

        FArdaNativeObjectResult FArdaD3D12ApiDevice::CreateBuffer(
            const FArdaRHIBufferDesc& Desc)
        {
            auto Buffer = eastl::make_shared<FD3D12Buffer>();
            Buffer->mDesc = Desc;
            D3D12_HEAP_PROPERTIES Heap{};
            Heap.Type = Desc.mCpuAccess == EArdaRHICpuAccess::Write
                ? D3D12_HEAP_TYPE_UPLOAD
                : (Desc.mCpuAccess == EArdaRHICpuAccess::Read
                    ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_DEFAULT);
            Heap.CreationNodeMask = 1;
            Heap.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC Resource{};
            Resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            Resource.Width = Desc.mByteSize;
            Resource.Height = 1;
            Resource.DepthOrArraySize = 1;
            Resource.MipLevels = 1;
            Resource.Format = DXGI_FORMAT_UNKNOWN;
            Resource.SampleDesc.Count = 1;
            Resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (HasAnyFlags(Desc.mUsage, EArdaRHIBufferUsage::UnorderedAccess))
                Resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            D3D12_RESOURCE_STATES State = ToD3D12State(Desc.mInitialState);
            if (Heap.Type == D3D12_HEAP_TYPE_UPLOAD) State = D3D12_RESOURCE_STATE_GENERIC_READ;
            if (Heap.Type == D3D12_HEAP_TYPE_READBACK) State = D3D12_RESOURCE_STATE_COPY_DEST;
            HRESULT Result = mD3DDevice->CreateCommittedResource(
                &Heap, D3D12_HEAP_FLAG_NONE, &Resource, State, nullptr,
                IID_PPV_ARGS(&Buffer->mResource));
            if (FAILED(Result)) return Fail<FArdaNativeObjectRef>(
                D3D12Failure("Failed to create a D3D12 buffer.", Result));
            return { Buffer, {} };
        }

        TArdaRHIResult<void*> FArdaD3D12ApiDevice::MapBuffer(
            const FArdaNativeObjectRef& Object,
            uint64_t Offset,
            size_t Size)
        {
            auto* Buffer = dynamic_cast<FD3D12Buffer*>(Object.get());
            if (!Buffer || !Buffer->mResource)
                return Fail<void*>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 buffer mapping has the wrong resource type."));
            if (Buffer->mDesc.mCpuAccess == EArdaRHICpuAccess::None ||
                Offset > Buffer->mDesc.mByteSize ||
                Size > Buffer->mDesc.mByteSize - Offset)
                return Fail<void*>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "D3D12 buffer mapping range is invalid or not host visible."));
            const D3D12_RANGE ReadRange =
                Buffer->mDesc.mCpuAccess == EArdaRHICpuAccess::Read
                    ? D3D12_RANGE{ static_cast<SIZE_T>(Offset),
                        static_cast<SIZE_T>(Offset + Size) }
                    : D3D12_RANGE{ 0, 0 };
            void* Data = nullptr;
            const HRESULT Result = Buffer->mResource->Map(0, &ReadRange, &Data);
            if (FAILED(Result))
                return Fail<void*>(D3D12Failure(
                    "Failed to map a D3D12 host-visible buffer.", Result));
            return { static_cast<uint8_t*>(Data) + Offset, {} };
        }

        void FArdaD3D12ApiDevice::UnmapBuffer(
            const FArdaNativeObjectRef& Object) noexcept
        {
            auto* Buffer = dynamic_cast<FD3D12Buffer*>(Object.get());
            if (!Buffer || !Buffer->mResource) return;
            const D3D12_RANGE WrittenRange{ 0, 0 };
            Buffer->mResource->Unmap(0, &WrittenRange);
        }

        FArdaNativeObjectResult FArdaD3D12ApiDevice::ImportTexture(
            const FArdaRHINativeTextureImportDesc& Desc)
        {
            auto Texture = eastl::make_shared<FD3D12Texture>();
            Texture->mResource = reinterpret_cast<ID3D12Resource*>(Desc.mNativeObject);
            Texture->mDesc = Desc.mTexture;
            if (!Texture->mResource)
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument, "Native D3D12 texture is null."));
            if (auto Status = CreateTextureViews(*Texture); !Status)
                return Fail<FArdaNativeObjectRef>(eastl::move(Status));
            return { Texture, {} };
        }

        FArdaNativeObjectResult FArdaD3D12ApiDevice::ImportBuffer(
            const FArdaRHINativeBufferImportDesc& Desc)
        {
            auto Buffer = eastl::make_shared<FD3D12Buffer>();
            Buffer->mResource = reinterpret_cast<ID3D12Resource*>(Desc.mNativeObject);
            Buffer->mDesc = Desc.mBuffer;
            if (!Buffer->mResource)
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument, "Native D3D12 buffer is null."));
            return { Buffer, {} };
        }

        FArdaNativeObjectResult FArdaD3D12ApiDevice::CreateSampler(
            const FArdaRHISamplerDesc& Desc)
        {
            auto Sampler = eastl::make_shared<FD3D12Sampler>();
            const bool bAnisotropic = Desc.mMaxAnisotropy > 1.f;
            if (bAnisotropic)
                Sampler->mDesc.Filter = Desc.mReduction == EArdaRHISamplerReduction::Comparison
                    ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC;
            else
            {
                const uint32_t Min = Desc.mbMinFilter ? 1u : 0u;
                const uint32_t Mag = Desc.mbMagFilter ? 1u : 0u;
                const uint32_t Mip = Desc.mbMipFilter ? 1u : 0u;
                Sampler->mDesc.Filter = static_cast<D3D12_FILTER>(
                    (Min << D3D12_MIN_FILTER_SHIFT) |
                    (Mag << D3D12_MAG_FILTER_SHIFT) |
                    (Mip << D3D12_MIP_FILTER_SHIFT) |
                    (Desc.mReduction == EArdaRHISamplerReduction::Comparison
                        ? D3D12_FILTER_REDUCTION_TYPE_COMPARISON : 0));
            }
            const auto Address = [](EArdaRHISamplerAddressMode Mode)
            {
                switch (Mode)
                {
                case EArdaRHISamplerAddressMode::Wrap: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                case EArdaRHISamplerAddressMode::Border: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
                case EArdaRHISamplerAddressMode::Mirror: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
                case EArdaRHISamplerAddressMode::MirrorOnce: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
                default: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                }
            };
            Sampler->mDesc.AddressU = Address(Desc.mAddressU);
            Sampler->mDesc.AddressV = Address(Desc.mAddressV);
            Sampler->mDesc.AddressW = Address(Desc.mAddressW);
            Sampler->mDesc.MipLODBias = Desc.mMipBias;
            Sampler->mDesc.MaxAnisotropy = static_cast<UINT>(eastl::max(1.f, Desc.mMaxAnisotropy));
            Sampler->mDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            Sampler->mDesc.BorderColor[0] = Desc.mBorderColor.mR;
            Sampler->mDesc.BorderColor[1] = Desc.mBorderColor.mG;
            Sampler->mDesc.BorderColor[2] = Desc.mBorderColor.mB;
            Sampler->mDesc.BorderColor[3] = Desc.mBorderColor.mA;
            Sampler->mDesc.MinLOD = 0.f;
            Sampler->mDesc.MaxLOD = D3D12_FLOAT32_MAX;
            return { Sampler, {} };
        }

        FArdaNativeObjectResult FArdaD3D12ApiDevice::CreateShader(
            const FArdaRHIShaderDesc& Desc)
        {
            auto Shader = eastl::make_shared<FD3D12Shader>();
            const auto* Begin = static_cast<const uint8_t*>(Desc.mBytecode);
            Shader->mBytecode.assign(Begin, Begin + Desc.mBytecodeSize);
            Shader->mStage = Desc.mStage;
            return { Shader, {} };
        }

        FArdaNativeObjectResult FArdaD3D12ApiDevice::CreateBindingLayout(
            const FArdaRHIBindingLayoutDesc& Desc)
        {
            auto Layout = eastl::make_shared<FD3D12BindingLayout>();
            Layout->mDesc = Desc;
            return { Layout, {} };
        }

        FArdaNativeObjectResult FArdaD3D12ApiDevice::CreateBindingSet(
            const FArdaRHIBindingSetDesc&,
            const FArdaNativeObjectRef& LayoutObject,
            const eastl::vector<FArdaNativeBinding>& Bindings)
        {
            auto* Layout = dynamic_cast<FD3D12BindingLayout*>(LayoutObject.get());
            if (!Layout)
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice, "D3D12 binding layout has the wrong implementation."));
            auto Set = eastl::make_shared<FD3D12BindingSet>();
            Set->mAllocator = mDescriptorAllocator;
            Set->mRetainedObjects.push_back(LayoutObject);
            for (const auto& Binding : Bindings)
                Set->mRetainedObjects.push_back(Binding.mObject);

            for (const auto& Item : Layout->mDesc.mItems)
            {
                if (Item.mType == EArdaRHIBindingType::PushConstants)
                    continue;
                const bool bSampler = Item.mType == EArdaRHIBindingType::Sampler;
                auto Allocation = AllocateDescriptors(bSampler, eastl::max(1u, Item.mArraySize));
                if (!Allocation) return Fail<FArdaNativeObjectRef>(eastl::move(Allocation.mStatus));
                Set->mAllocations.push_back(Allocation.mValue);
                Set->mTables.push_back({ Item.mType, Allocation.mValue.mGpu });
                const uint32_t Increment = bSampler ? mSamplerDescriptorSize : mResourceDescriptorSize;
                for (uint32_t Element = 0; Element < eastl::max(1u, Item.mArraySize); ++Element)
                {
                    const FArdaNativeBinding* Binding = nullptr;
                    for (const auto& Candidate : Bindings)
                    {
                        if (Candidate.mItem.mSlot == Item.mSlot &&
                            Candidate.mItem.mArrayElement == Element &&
                            Candidate.mItem.mType == Item.mType)
                        {
                            Binding = &Candidate;
                            break;
                        }
                    }
                    if (!Binding)
                        return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument,
                            "A D3D12 binding set is missing an item required by its layout."));
                    D3D12_CPU_DESCRIPTOR_HANDLE Destination = Allocation.mValue.mCpu;
                    Destination.ptr += static_cast<SIZE_T>(Element) * Increment;
                    if (Item.mType == EArdaRHIBindingType::Sampler)
                    {
                        auto* Sampler = dynamic_cast<FD3D12Sampler*>(Binding->mObject.get());
                        if (!Sampler) return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument, "D3D12 sampler binding has the wrong resource type."));
                        mD3DDevice->CreateSampler(&Sampler->mDesc, Destination);
                        continue;
                    }

                    auto* Texture = dynamic_cast<FD3D12Texture*>(Binding->mObject.get());
                    auto* Buffer = dynamic_cast<FD3D12Buffer*>(Binding->mObject.get());
                    switch (Item.mType)
                    {
                    case EArdaRHIBindingType::TextureSRV:
                    {
                        if (!Texture) return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument, "D3D12 texture SRV has the wrong resource type."));
                        D3D12_SHADER_RESOURCE_VIEW_DESC View{};
                        View.Format = ToDxgi(Binding->mItem.mView.mFormat == EArdaRHIFormat::Unknown
                            ? Texture->mDesc.mFormat : Binding->mItem.mView.mFormat);
                        View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                        View.Texture2D.MostDetailedMip = Binding->mItem.mView.mTextureRange.mBaseMipLevel;
                        View.Texture2D.MipLevels = Binding->mItem.mView.mTextureRange.mMipLevelCount == ArdaRHIAllSubresources
                            ? Texture->mDesc.mMipLevels - View.Texture2D.MostDetailedMip
                            : Binding->mItem.mView.mTextureRange.mMipLevelCount;
                        mD3DDevice->CreateShaderResourceView(Texture->mResource.Get(), &View, Destination);
                        break;
                    }
                    case EArdaRHIBindingType::TextureUAV:
                    case EArdaRHIBindingType::SamplerFeedbackTextureUAV:
                    {
                        if (!Texture) return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument, "D3D12 texture UAV has the wrong resource type."));
                        D3D12_UNORDERED_ACCESS_VIEW_DESC View{};
                        View.Format = ToDxgi(Binding->mItem.mView.mFormat == EArdaRHIFormat::Unknown
                            ? Texture->mDesc.mFormat : Binding->mItem.mView.mFormat);
                        View.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                        View.Texture2D.MipSlice = Binding->mItem.mView.mTextureRange.mBaseMipLevel;
                        mD3DDevice->CreateUnorderedAccessView(Texture->mResource.Get(), nullptr, &View, Destination);
                        break;
                    }
                    case EArdaRHIBindingType::ConstantBuffer:
                    case EArdaRHIBindingType::VolatileConstantBuffer:
                    {
                        if (!Buffer) return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument, "D3D12 constant-buffer binding has the wrong resource type."));
                        const auto Range = Binding->mItem.mView.mBufferRange.Resolve(Buffer->mDesc);
                        D3D12_CONSTANT_BUFFER_VIEW_DESC View{};
                        View.BufferLocation = Buffer->mResource->GetGPUVirtualAddress() + Range.mByteOffset;
                        View.SizeInBytes = static_cast<UINT>((Range.mByteSize + 255u) & ~255ull);
                        mD3DDevice->CreateConstantBufferView(&View, Destination);
                        break;
                    }
                    case EArdaRHIBindingType::TypedBufferSRV:
                    case EArdaRHIBindingType::StructuredBufferSRV:
                    case EArdaRHIBindingType::RawBufferSRV:
                    {
                        if (!Buffer) return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument, "D3D12 buffer SRV has the wrong resource type."));
                        const auto Range = Binding->mItem.mView.mBufferRange.Resolve(Buffer->mDesc);
                        const bool bRaw = Item.mType == EArdaRHIBindingType::RawBufferSRV;
                        const bool bStructured = Item.mType == EArdaRHIBindingType::StructuredBufferSRV;
                        const EArdaRHIFormat Format = Binding->mItem.mView.mFormat == EArdaRHIFormat::Unknown
                            ? Buffer->mDesc.mFormat : Binding->mItem.mView.mFormat;
                        const uint32_t Stride = bStructured ? Buffer->mDesc.mStructureStride
                            : (bRaw ? 4u : eastl::max(1u, FormatSize(Format)));
                        D3D12_SHADER_RESOURCE_VIEW_DESC View{};
                        View.Format = bRaw ? DXGI_FORMAT_R32_TYPELESS
                            : (bStructured ? DXGI_FORMAT_UNKNOWN : ToDxgi(Format));
                        View.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                        View.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        View.Buffer.FirstElement = Range.mByteOffset / Stride;
                        View.Buffer.NumElements = static_cast<UINT>(Range.mByteSize / Stride);
                        View.Buffer.StructureByteStride = bStructured ? Stride : 0;
                        View.Buffer.Flags = bRaw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
                        mD3DDevice->CreateShaderResourceView(Buffer->mResource.Get(), &View, Destination);
                        break;
                    }
                    case EArdaRHIBindingType::TypedBufferUAV:
                    case EArdaRHIBindingType::StructuredBufferUAV:
                    case EArdaRHIBindingType::RawBufferUAV:
                    {
                        if (!Buffer) return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument, "D3D12 buffer UAV has the wrong resource type."));
                        const auto Range = Binding->mItem.mView.mBufferRange.Resolve(Buffer->mDesc);
                        const bool bRaw = Item.mType == EArdaRHIBindingType::RawBufferUAV;
                        const bool bStructured = Item.mType == EArdaRHIBindingType::StructuredBufferUAV;
                        const EArdaRHIFormat Format = Binding->mItem.mView.mFormat == EArdaRHIFormat::Unknown
                            ? Buffer->mDesc.mFormat : Binding->mItem.mView.mFormat;
                        const uint32_t Stride = bStructured ? Buffer->mDesc.mStructureStride
                            : (bRaw ? 4u : eastl::max(1u, FormatSize(Format)));
                        D3D12_UNORDERED_ACCESS_VIEW_DESC View{};
                        View.Format = bRaw ? DXGI_FORMAT_R32_TYPELESS
                            : (bStructured ? DXGI_FORMAT_UNKNOWN : ToDxgi(Format));
                        View.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                        View.Buffer.FirstElement = Range.mByteOffset / Stride;
                        View.Buffer.NumElements = static_cast<UINT>(Range.mByteSize / Stride);
                        View.Buffer.StructureByteStride = bStructured ? Stride : 0;
                        View.Buffer.Flags = bRaw ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;
                        mD3DDevice->CreateUnorderedAccessView(Buffer->mResource.Get(), nullptr, &View, Destination);
                        break;
                    }
                    default:
                        return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::Unsupported, "The D3D12 binding type is unsupported."));
                    }
                }
            }
            return { Set, {} };
        }

        FArdaNativeObjectResult FArdaD3D12ApiDevice::CreateFramebuffer(
            const FArdaNativeFramebufferCreateInfo& Info)
        {
            auto Framebuffer = eastl::make_shared<FD3D12Framebuffer>();
            for (const auto& Target : Info.mColors)
            {
                auto* Texture = dynamic_cast<FD3D12Texture*>(Target.mTexture.get());
                if (!Texture || !Texture->mRtv.ptr)
                    return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidArgument,
                        "A D3D12 framebuffer color attachment is not render-target capable."));
                Framebuffer->mRtvs.push_back(Texture->mRtv);
                Framebuffer->mRetainedTextures.push_back(Target.mTexture);
            }
            if (Info.mDepth.mTexture)
            {
                auto* Texture = dynamic_cast<FD3D12Texture*>(Info.mDepth.mTexture.get());
                if (!Texture || !Texture->mDsv.ptr)
                    return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidArgument,
                        "A D3D12 framebuffer depth attachment is not depth-stencil capable."));
                Framebuffer->mDsv = Texture->mDsv;
                Framebuffer->mbHasDepth = true;
                Framebuffer->mRetainedTextures.push_back(Info.mDepth.mTexture);
            }
            return { Framebuffer, {} };
        }

        TArdaRHIResult<ComPtr<ID3D12RootSignature>>
        FArdaD3D12ApiDevice::CreateRootSignature(
            const eastl::vector<FArdaNativeObjectRef>& Layouts,
            eastl::vector<uint32_t>& OutItemCounts,
            eastl::vector<int32_t>& OutPushConstantRoots)
        {
            eastl::vector<D3D12_ROOT_PARAMETER> Parameters;
            eastl::vector<D3D12_DESCRIPTOR_RANGE> Ranges;
            size_t RangeCount = 0;
            for (const auto& LayoutObject : Layouts)
            {
                auto* Layout = dynamic_cast<FD3D12BindingLayout*>(LayoutObject.get());
                if (!Layout) return Fail<ComPtr<ID3D12RootSignature>>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice, "D3D12 pipeline binding layout has the wrong implementation."));
                for (const auto& Item : Layout->mDesc.mItems)
                    if (Item.mType != EArdaRHIBindingType::PushConstants) ++RangeCount;
            }
            Ranges.resize(RangeCount);
            Parameters.reserve(RangeCount + Layouts.size());
            size_t RangeIndex = 0;
            for (const auto& LayoutObject : Layouts)
            {
                auto* Layout = static_cast<FD3D12BindingLayout*>(LayoutObject.get());
                uint32_t TableCount = 0;
                int32_t PushRoot = -1;
                for (const auto& Item : Layout->mDesc.mItems)
                {
                    D3D12_ROOT_PARAMETER Parameter{};
                    Parameter.ShaderVisibility = ToVisibility(Layout->mDesc.mVisibility);
                    if (Item.mType == EArdaRHIBindingType::PushConstants)
                    {
                        Parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                        Parameter.Constants.ShaderRegister = Item.mSlot;
                        Parameter.Constants.RegisterSpace = Layout->mDesc.mRegisterSpace;
                        Parameter.Constants.Num32BitValues = eastl::max(1u, (Item.mArraySize + 3u) / 4u);
                        PushRoot = static_cast<int32_t>(Parameters.size());
                    }
                    else
                    {
                        auto& Range = Ranges[RangeIndex++];
                        Range.RangeType = ToRangeType(Item.mType);
                        Range.NumDescriptors = eastl::max(1u, Item.mArraySize);
                        Range.BaseShaderRegister = Item.mSlot;
                        Range.RegisterSpace = Layout->mDesc.mRegisterSpace;
                        Range.OffsetInDescriptorsFromTableStart = 0;
                        Parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                        Parameter.DescriptorTable.NumDescriptorRanges = 1;
                        Parameter.DescriptorTable.pDescriptorRanges = &Range;
                        ++TableCount;
                    }
                    Parameters.push_back(Parameter);
                }
                OutItemCounts.push_back(TableCount);
                OutPushConstantRoots.push_back(PushRoot);
            }
            D3D12_ROOT_SIGNATURE_DESC Desc{};
            Desc.NumParameters = static_cast<UINT>(Parameters.size());
            Desc.pParameters = Parameters.data();
            Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            ComPtr<ID3DBlob> Blob;
            ComPtr<ID3DBlob> Error;
            HRESULT Result = D3D12SerializeRootSignature(
                &Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Blob, &Error);
            if (FAILED(Result))
            {
                const char* Detail = Error
                    ? static_cast<const char*>(Error->GetBufferPointer())
                    : "Failed to serialize the D3D12 root signature.";
                return Fail<ComPtr<ID3D12RootSignature>>(D3D12Failure(Detail, Result));
            }
            ComPtr<ID3D12RootSignature> Root;
            Result = mD3DDevice->CreateRootSignature(
                0, Blob->GetBufferPointer(), Blob->GetBufferSize(), IID_PPV_ARGS(&Root));
            if (FAILED(Result)) return Fail<ComPtr<ID3D12RootSignature>>(
                D3D12Failure("Failed to create the D3D12 root signature.", Result));
            return { Root, {} };
        }

        FArdaNativeObjectResult FArdaD3D12ApiDevice::CreateGraphicsPipeline(
            const FArdaNativeGraphicsPipelineCreateInfo& Info)
        {
            auto Pipeline = eastl::make_shared<FD3D12Pipeline>();
            auto Root = CreateRootSignature(
                Info.mBindingLayouts, Pipeline->mLayoutItemCounts,
                Pipeline->mPushConstantRoots);
            if (!Root) return Fail<FArdaNativeObjectRef>(eastl::move(Root.mStatus));
            Pipeline->mRootSignature = eastl::move(Root.mValue);

            const auto Bytecode = [](const FArdaNativeObjectRef& Object)
            {
                D3D12_SHADER_BYTECODE Result{};
                if (auto* Shader = dynamic_cast<FD3D12Shader*>(Object.get()))
                {
                    Result.pShaderBytecode = Shader->mBytecode.data();
                    Result.BytecodeLength = Shader->mBytecode.size();
                }
                return Result;
            };
            eastl::vector<D3D12_INPUT_ELEMENT_DESC> InputElements;
            if (Info.mInputLayout)
            {
                InputElements.reserve(Info.mInputLayout->mAttributes.size());
                for (const auto& Attribute : Info.mInputLayout->mAttributes)
                {
                    for (uint32_t Element = 0; Element < eastl::max(1u, Attribute.mArraySize); ++Element)
                    {
                        D3D12_INPUT_ELEMENT_DESC Native{};
                        Native.SemanticName = Attribute.mSemanticName.c_str();
                        Native.SemanticIndex = Element;
                        Native.Format = ToDxgi(Attribute.mFormat);
                        Native.InputSlot = Attribute.mBufferIndex;
                        Native.AlignedByteOffset = Attribute.mOffset + Element * FormatSize(Attribute.mFormat);
                        Native.InputSlotClass = Attribute.mbInstanced
                            ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                            : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                        Native.InstanceDataStepRate = Attribute.mbInstanced ? 1u : 0u;
                        InputElements.push_back(Native);
                    }
                }
            }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC Desc{};
            Desc.pRootSignature = Pipeline->mRootSignature.Get();
            Desc.VS = Bytecode(Info.mVertexShader);
            Desc.HS = Bytecode(Info.mHullShader);
            Desc.DS = Bytecode(Info.mDomainShader);
            Desc.GS = Bytecode(Info.mGeometryShader);
            Desc.PS = Bytecode(Info.mPixelShader);
            Desc.BlendState.AlphaToCoverageEnable = Info.mDesc.mBlendState.mbAlphaToCoverage;
            Desc.BlendState.IndependentBlendEnable = TRUE;
            for (uint32_t Index = 0; Index < ArdaRHIMaxRenderTargets; ++Index)
            {
                const auto& Source = Info.mDesc.mBlendState.mTargets[Index];
                auto& Target = Desc.BlendState.RenderTarget[Index];
                Target.BlendEnable = Source.mbEnable;
                Target.LogicOpEnable = FALSE;
                Target.SrcBlend = ToD3D12Blend(Source.mSourceColor);
                Target.DestBlend = ToD3D12Blend(Source.mDestinationColor);
                Target.BlendOp = D3D12_BLEND_OP_ADD;
                Target.SrcBlendAlpha = ToD3D12Blend(Source.mSourceAlpha);
                Target.DestBlendAlpha = ToD3D12Blend(Source.mDestinationAlpha);
                Target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
                Target.LogicOp = D3D12_LOGIC_OP_NOOP;
                Target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            }
            Desc.SampleMask = UINT_MAX;
            Desc.RasterizerState.FillMode = Info.mDesc.mRasterState.mFillMode == EArdaRHIFillMode::Wireframe
                ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
            switch (Info.mDesc.mRasterState.mCullMode)
            {
            case EArdaRHICullMode::Front: Desc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT; break;
            case EArdaRHICullMode::None: Desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; break;
            default: Desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; break;
            }
            Desc.RasterizerState.FrontCounterClockwise = Info.mDesc.mRasterState.mbFrontCounterClockwise;
            Desc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
            Desc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
            Desc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
            Desc.RasterizerState.DepthClipEnable = Info.mDesc.mRasterState.mbDepthClip;
            Desc.RasterizerState.MultisampleEnable = Info.mDesc.mSampleCount > 1;
            Desc.RasterizerState.AntialiasedLineEnable = FALSE;
            Desc.RasterizerState.ForcedSampleCount = 0;
            Desc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
            Desc.DepthStencilState.DepthEnable = Info.mDesc.mDepthStencilState.mbDepthTest;
            Desc.DepthStencilState.DepthWriteMask = Info.mDesc.mDepthStencilState.mbDepthWrite
                ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            Desc.DepthStencilState.DepthFunc = ToD3D12Comparison(Info.mDesc.mDepthStencilState.mDepthFunc);
            Desc.DepthStencilState.StencilEnable = FALSE;
            Desc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
            Desc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
            Desc.InputLayout = { InputElements.data(), static_cast<UINT>(InputElements.size()) };
            switch (Info.mDesc.mTopology)
            {
            case EArdaRHIPrimitiveTopology::PointList:
                Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
                Pipeline->mTopology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
                break;
            case EArdaRHIPrimitiveTopology::LineList:
                Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                Pipeline->mTopology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
                break;
            case EArdaRHIPrimitiveTopology::LineStrip:
                Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                Pipeline->mTopology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
                break;
            case EArdaRHIPrimitiveTopology::TriangleStrip:
                Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                Pipeline->mTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
                break;
            case EArdaRHIPrimitiveTopology::PatchList:
                Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
                Pipeline->mTopology = static_cast<D3D12_PRIMITIVE_TOPOLOGY>(
                    D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST +
                    eastl::max(1u, Info.mDesc.mPatchControlPoints) - 1u);
                break;
            default:
                Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                Pipeline->mTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                break;
            }
            Desc.NumRenderTargets = static_cast<UINT>(Info.mDesc.mColorFormats.size());
            for (uint32_t Index = 0; Index < Desc.NumRenderTargets; ++Index)
                Desc.RTVFormats[Index] = ToDxgi(Info.mDesc.mColorFormats[Index]);
            Desc.DSVFormat = ToDxgi(Info.mDesc.mDepthFormat);
            Desc.SampleDesc.Count = Info.mDesc.mSampleCount;
            Desc.SampleDesc.Quality = 0;
            Desc.NodeMask = 0;
            Desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
            HRESULT Result = E_FAIL;
            if (mPipelineLibrary && Info.mDesc.mPersistentCacheKey != 0)
            {
                std::lock_guard<std::mutex> Lock(mPipelineCacheMutex);
                const std::wstring Name = L"Graphics-" +
                    std::to_wstring(Info.mDesc.mPersistentCacheKey);
                Result = mPipelineLibrary->LoadGraphicsPipeline(
                    Name.c_str(), &Desc, IID_PPV_ARGS(&Pipeline->mPipeline));
                if (SUCCEEDED(Result))
                {
                    pipeline_cache::Message(mDiagnosticCallback,
                        EArdaDiagnosticSeverity::Info,
                        "LoadGraphicsPipeline accepted a cached D3D12 PSO.");
                }
                else
                {
                    Pipeline->mPipeline.Reset();
                    Result = mD3DDevice->CreateGraphicsPipelineState(
                        &Desc, IID_PPV_ARGS(&Pipeline->mPipeline));
                    if (SUCCEEDED(Result) && SUCCEEDED(mPipelineLibrary->StorePipeline(
                            Name.c_str(), Pipeline->mPipeline.Get())))
                        mbPipelineCacheDirty = true;
                }
            }
            else
            {
                Result = mD3DDevice->CreateGraphicsPipelineState(
                    &Desc, IID_PPV_ARGS(&Pipeline->mPipeline));
            }
            if (FAILED(Result)) return Fail<FArdaNativeObjectRef>(
                D3D12Failure("Failed to create a D3D12 graphics pipeline.", Result));
            return { Pipeline, {} };
        }

        FArdaNativeObjectResult FArdaD3D12ApiDevice::CreateComputePipeline(
            const FArdaNativeComputePipelineCreateInfo& Info)
        {
            auto* Shader = dynamic_cast<FD3D12Shader*>(Info.mComputeShader.get());
            if (!Shader) return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                EArdaRHIResult::InvalidArgument, "D3D12 compute shader has the wrong implementation."));
            auto Pipeline = eastl::make_shared<FD3D12Pipeline>();
            auto Root = CreateRootSignature(
                Info.mBindingLayouts, Pipeline->mLayoutItemCounts,
                Pipeline->mPushConstantRoots);
            if (!Root) return Fail<FArdaNativeObjectRef>(eastl::move(Root.mStatus));
            Pipeline->mRootSignature = eastl::move(Root.mValue);
            D3D12_COMPUTE_PIPELINE_STATE_DESC Desc{};
            Desc.pRootSignature = Pipeline->mRootSignature.Get();
            Desc.CS = { Shader->mBytecode.data(), Shader->mBytecode.size() };
            HRESULT Result = E_FAIL;
            if (mPipelineLibrary && Info.mDesc.mPersistentCacheKey != 0)
            {
                std::lock_guard<std::mutex> Lock(mPipelineCacheMutex);
                const std::wstring Name = L"Compute-" +
                    std::to_wstring(Info.mDesc.mPersistentCacheKey);
                Result = mPipelineLibrary->LoadComputePipeline(
                    Name.c_str(), &Desc, IID_PPV_ARGS(&Pipeline->mPipeline));
                if (SUCCEEDED(Result))
                {
                    pipeline_cache::Message(mDiagnosticCallback,
                        EArdaDiagnosticSeverity::Info,
                        "LoadComputePipeline accepted a cached D3D12 PSO.");
                }
                else
                {
                    Pipeline->mPipeline.Reset();
                    Result = mD3DDevice->CreateComputePipelineState(
                        &Desc, IID_PPV_ARGS(&Pipeline->mPipeline));
                    if (SUCCEEDED(Result) && SUCCEEDED(mPipelineLibrary->StorePipeline(
                            Name.c_str(), Pipeline->mPipeline.Get())))
                        mbPipelineCacheDirty = true;
                }
            }
            else
            {
                Result = mD3DDevice->CreateComputePipelineState(
                    &Desc, IID_PPV_ARGS(&Pipeline->mPipeline));
            }
            if (FAILED(Result)) return Fail<FArdaNativeObjectRef>(
                D3D12Failure("Failed to create a D3D12 compute pipeline.", Result));
            return { Pipeline, {} };
        }

        FArdaD3D12CommandList::FArdaD3D12CommandList(
            FArdaD3D12ApiDevice& Device,
            D3D12_COMMAND_LIST_TYPE Type)
            : mDevice(Device), mType(Type)
        {
        }

        FArdaRHIStatus FArdaD3D12CommandList::Initialize()
        {
            HRESULT Result = mDevice.GetDevice().CreateCommandAllocator(
                mType, IID_PPV_ARGS(&mAllocator));
            if (FAILED(Result)) return D3D12Failure("Failed to create a D3D12 command allocator.", Result);
            Result = mDevice.GetDevice().CreateCommandList(
                0, mType, mAllocator.Get(), nullptr,
                IID_PPV_ARGS(&mCommandList));
            if (FAILED(Result)) return D3D12Failure("Failed to create a D3D12 command list.", Result);
            Result = mCommandList->Close();
            if (FAILED(Result)) return D3D12Failure("Failed to close a new D3D12 command list.", Result);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::Open()
        {
            if (mbOpen) return FArdaRHIStatus::Error(
                EArdaRHIResult::InvalidState, "D3D12 command list is already open.");
            HRESULT Result = mAllocator->Reset();
            if (FAILED(Result)) return D3D12Failure("Failed to reset the D3D12 command allocator.", Result);
            Result = mCommandList->Reset(mAllocator.Get(), nullptr);
            if (FAILED(Result)) return D3D12Failure("Failed to reset the D3D12 command list.", Result);
            mUploadResources.clear();
            mRetainedObjects.clear();
            mStates.clear();
            mBoundGraphicsPipeline = nullptr;
            mBoundComputePipeline = nullptr;
            mbAutomaticBarriers = true;
            mbOpen = true;
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::Close()
        {
            if (!mbOpen) return FArdaRHIStatus::Error(
                EArdaRHIResult::InvalidState, "D3D12 command list is not open.");
            HRESULT Result = mCommandList->Close();
            if (FAILED(Result)) return D3D12Failure("Failed to close the D3D12 command list.", Result);
            mbOpen = false;
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::Reset()
        {
            if (mbOpen) return FArdaRHIStatus::Error(
                EArdaRHIResult::InvalidState,
                "An open D3D12 command list must be closed before reset.");
            return Open();
        }

        FArdaRHIStatus FArdaD3D12CommandList::Transition(
            const FArdaNativeObjectRef& Object, D3D12_RESOURCE_STATES NewState)
        {
            ID3D12Resource* Resource = nullptr;
            if (auto* Texture = dynamic_cast<FD3D12Texture*>(Object.get()))
                Resource = Texture->mResource.Get();
            else if (auto* Buffer = dynamic_cast<FD3D12Buffer*>(Object.get()))
                Resource = Buffer->mResource.Get();
            if (!Resource) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "D3D12 transition resource has the wrong implementation.");
            Retain(Object);
            const void* Identity = Resource;
            auto It = mStates.find(Identity);
            const D3D12_RESOURCE_STATES OldState = It == mStates.end()
                ? D3D12_RESOURCE_STATE_COMMON : It->second;
            if (OldState != NewState)
            {
                D3D12_RESOURCE_BARRIER Barrier{};
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                Barrier.Transition.pResource = Resource;
                Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                Barrier.Transition.StateBefore = OldState;
                Barrier.Transition.StateAfter = NewState;
                mCommandList->ResourceBarrier(1, &Barrier);
            }
            mStates[Identity] = NewState;
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::WriteBuffer(
            const FArdaNativeObjectRef& Object,
            const FArdaRHIBufferDesc& Desc,
            const void* Data, size_t Size, uint64_t Offset)
        {
            auto* Buffer = dynamic_cast<FD3D12Buffer*>(Object.get());
            if (!Buffer) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "D3D12 buffer has the wrong implementation.");
            Retain(Object);
            if (Desc.mCpuAccess == EArdaRHICpuAccess::Write)
            {
                void* Mapped = nullptr;
                D3D12_RANGE ReadRange{ 0, 0 };
                HRESULT Result = Buffer->mResource->Map(0, &ReadRange, &Mapped);
                if (FAILED(Result)) return D3D12Failure("Failed to map a D3D12 upload buffer.", Result);
                std::memcpy(static_cast<uint8_t*>(Mapped) + Offset, Data, Size);
                D3D12_RANGE Written{ static_cast<SIZE_T>(Offset), static_cast<SIZE_T>(Offset + Size) };
                Buffer->mResource->Unmap(0, &Written);
                return {};
            }
            D3D12_HEAP_PROPERTIES Heap{};
            Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            Heap.CreationNodeMask = 1;
            Heap.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC Resource{};
            Resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            Resource.Width = Size;
            Resource.Height = 1;
            Resource.DepthOrArraySize = 1;
            Resource.MipLevels = 1;
            Resource.SampleDesc.Count = 1;
            Resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ComPtr<ID3D12Resource> Upload;
            HRESULT Result = mDevice.GetDevice().CreateCommittedResource(
                &Heap, D3D12_HEAP_FLAG_NONE, &Resource,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&Upload));
            if (FAILED(Result)) return D3D12Failure("Failed to create a D3D12 upload buffer.", Result);
            void* Mapped = nullptr;
            D3D12_RANGE ReadRange{ 0, 0 };
            Result = Upload->Map(0, &ReadRange, &Mapped);
            if (FAILED(Result)) return D3D12Failure("Failed to map a D3D12 upload buffer.", Result);
            std::memcpy(Mapped, Data, Size);
            Upload->Unmap(0, nullptr);
            if (mbAutomaticBarriers)
                (void)Transition(Object, D3D12_RESOURCE_STATE_COPY_DEST);
            mCommandList->CopyBufferRegion(Buffer->mResource.Get(), Offset, Upload.Get(), 0, Size);
            if (mbAutomaticBarriers)
                (void)Transition(Object, ToD3D12State(Desc.mInitialState));
            mUploadResources.push_back(eastl::move(Upload));
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::CopyBuffer(
            const FArdaNativeObjectRef& Destination, uint64_t DestinationOffset,
            const FArdaNativeObjectRef& Source, uint64_t SourceOffset, uint64_t Size)
        {
            auto* Dst = dynamic_cast<FD3D12Buffer*>(Destination.get());
            auto* Src = dynamic_cast<FD3D12Buffer*>(Source.get());
            if (!Dst || !Src) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "D3D12 copy buffer has the wrong implementation.");
            Retain(Destination);
            Retain(Source);
            mCommandList->CopyBufferRegion(
                Dst->mResource.Get(), DestinationOffset,
                Src->mResource.Get(), SourceOffset, Size);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::ClearTexture(
            const FArdaNativeObjectRef& Object, const FArdaRHITextureDesc&,
            const FArdaRHITextureSubresourceRange&, const FArdaRHIColor& Color)
        {
            auto* Texture = dynamic_cast<FD3D12Texture*>(Object.get());
            if (!Texture || !Texture->mRtv.ptr) return FArdaRHIStatus::Error(
                EArdaRHIResult::InvalidArgument,
                "D3D12 texture clear requires a render-target texture.");
            Retain(Object);
            const float Values[] = { Color.mR, Color.mG, Color.mB, Color.mA };
            mCommandList->ClearRenderTargetView(Texture->mRtv, Values, 0, nullptr);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::ClearDepthStencilTexture(
            const FArdaNativeObjectRef& Object, const FArdaRHITextureDesc&,
            const FArdaRHITextureSubresourceRange&, bool bClearDepth,
            float Depth, bool bClearStencil, uint8_t Stencil)
        {
            auto* Texture = dynamic_cast<FD3D12Texture*>(Object.get());
            if (!Texture || !Texture->mDsv.ptr) return FArdaRHIStatus::Error(
                EArdaRHIResult::InvalidArgument,
                "D3D12 depth clear requires a depth-stencil texture.");
            Retain(Object);
            D3D12_CLEAR_FLAGS Flags = static_cast<D3D12_CLEAR_FLAGS>(0);
            if (bClearDepth) Flags |= D3D12_CLEAR_FLAG_DEPTH;
            if (bClearStencil) Flags |= D3D12_CLEAR_FLAG_STENCIL;
            mCommandList->ClearDepthStencilView(Texture->mDsv, Flags, Depth, Stencil, 0, nullptr);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::SetTextureState(
            const FArdaNativeObjectRef& Object, const FArdaRHITextureDesc&,
            const FArdaRHITextureSubresourceRange&, EArdaRHIResourceState State)
        {
            return Transition(Object, ToD3D12State(State));
        }

        FArdaRHIStatus FArdaD3D12CommandList::SetBufferState(
            const FArdaNativeObjectRef& Object, const FArdaRHIBufferDesc&,
            EArdaRHIResourceState State)
        {
            return Transition(Object, ToD3D12State(State));
        }

        FArdaRHIStatus FArdaD3D12CommandList::BeginTrackingTextureState(
            const FArdaNativeObjectRef& Object, const FArdaRHITextureDesc&,
            const FArdaRHITextureSubresourceRange&, EArdaRHIResourceState State)
        {
            mStates[Object->GetIdentity()] = ToD3D12State(State);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::BeginTrackingBufferState(
            const FArdaNativeObjectRef& Object, const FArdaRHIBufferDesc&,
            EArdaRHIResourceState State)
        {
            mStates[Object->GetIdentity()] = ToD3D12State(State);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::SetUAVBarriersForTexture(
            const FArdaNativeObjectRef& Object, bool bEnabled)
        {
            if (!bEnabled) return {};
            auto* Texture = dynamic_cast<FD3D12Texture*>(Object.get());
            if (!Texture) return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                "D3D12 UAV texture has the wrong implementation.");
            Retain(Object);
            D3D12_RESOURCE_BARRIER Barrier{};
            Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            Barrier.UAV.pResource = Texture->mResource.Get();
            mCommandList->ResourceBarrier(1, &Barrier);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::SetUAVBarriersForBuffer(
            const FArdaNativeObjectRef& Object, bool bEnabled)
        {
            if (!bEnabled) return {};
            auto* Buffer = dynamic_cast<FD3D12Buffer*>(Object.get());
            if (!Buffer) return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                "D3D12 UAV buffer has the wrong implementation.");
            Retain(Object);
            D3D12_RESOURCE_BARRIER Barrier{};
            Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            Barrier.UAV.pResource = Buffer->mResource.Get();
            mCommandList->ResourceBarrier(1, &Barrier);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::BindDescriptorSets(
            const FD3D12Pipeline& Pipeline,
            const eastl::vector<FArdaNativeObjectRef>& Bindings,
            bool bGraphics)
        {
            ID3D12DescriptorHeap* Heaps[] = {
                mDevice.GetResourceHeap(), mDevice.GetSamplerHeap()
            };
            mCommandList->SetDescriptorHeaps(2, Heaps);
            uint32_t RootIndex = 0;
            for (size_t LayoutIndex = 0; LayoutIndex < Pipeline.mLayoutItemCounts.size(); ++LayoutIndex)
            {
                FD3D12BindingSet* Set = LayoutIndex < Bindings.size()
                    ? dynamic_cast<FD3D12BindingSet*>(Bindings[LayoutIndex].get())
                    : nullptr;
                const uint32_t TableCount = Pipeline.mLayoutItemCounts[LayoutIndex];
                if (Set && Set->mTables.size() < TableCount)
                    return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                        "D3D12 binding set does not match the pipeline layout.");
                for (uint32_t Table = 0; Table < TableCount; ++Table, ++RootIndex)
                {
                    if (!Set) continue;
                    if (bGraphics)
                        mCommandList->SetGraphicsRootDescriptorTable(RootIndex, Set->mTables[Table].mGpu);
                    else
                        mCommandList->SetComputeRootDescriptorTable(RootIndex, Set->mTables[Table].mGpu);
                }
                if (LayoutIndex < Pipeline.mPushConstantRoots.size() &&
                    Pipeline.mPushConstantRoots[LayoutIndex] >= 0)
                    ++RootIndex;
            }
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::SetGraphicsState(
            const FArdaNativeGraphicsState& State)
        {
            auto* Pipeline = dynamic_cast<FD3D12Pipeline*>(State.mPipeline.get());
            auto* Framebuffer = dynamic_cast<FD3D12Framebuffer*>(State.mFramebuffer.get());
            if (!Pipeline || !Framebuffer)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "D3D12 graphics state has the wrong implementation.");
            Retain(State.mPipeline);
            Retain(State.mFramebuffer);
            for (const auto& Binding : State.mBindings) Retain(Binding);
            for (const auto& Binding : State.mVertexBuffers) Retain(Binding.mBuffer);
            Retain(State.mIndexBuffer);
            mBoundGraphicsPipeline = Pipeline;
            mBoundComputePipeline = nullptr;
            mCommandList->SetGraphicsRootSignature(Pipeline->mRootSignature.Get());
            mCommandList->SetPipelineState(Pipeline->mPipeline.Get());
            if (auto Status = BindDescriptorSets(*Pipeline, State.mBindings, true); !Status)
                return Status;
            mCommandList->IASetPrimitiveTopology(Pipeline->mTopology);
            for (const auto& Binding : State.mVertexBuffers)
            {
                auto* Buffer = dynamic_cast<FD3D12Buffer*>(Binding.mBuffer.get());
                if (!Buffer) return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "D3D12 vertex buffer has the wrong implementation.");
                D3D12_VERTEX_BUFFER_VIEW View{};
                View.BufferLocation = Buffer->mResource->GetGPUVirtualAddress() + Binding.mOffset;
                View.SizeInBytes = static_cast<UINT>(eastl::min<uint64_t>(
                    UINT_MAX, Binding.mSize - eastl::min(Binding.mOffset, Binding.mSize)));
                View.StrideInBytes = Binding.mStride;
                mCommandList->IASetVertexBuffers(Binding.mSlot, 1, &View);
            }
            if (State.mIndexBuffer)
            {
                auto* Buffer = dynamic_cast<FD3D12Buffer*>(State.mIndexBuffer.get());
                if (!Buffer) return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "D3D12 index buffer has the wrong implementation.");
                D3D12_INDEX_BUFFER_VIEW View{};
                View.BufferLocation = Buffer->mResource->GetGPUVirtualAddress() + State.mIndexOffset;
                View.SizeInBytes = static_cast<UINT>(eastl::min<uint64_t>(
                    UINT_MAX, Buffer->mDesc.mByteSize - eastl::min(State.mIndexOffset, Buffer->mDesc.mByteSize)));
                View.Format = ToDxgi(State.mIndexFormat);
                mCommandList->IASetIndexBuffer(&View);
            }
            eastl::vector<D3D12_VIEWPORT> Viewports;
            for (const auto& Viewport : State.mViewports)
                Viewports.push_back({ Viewport.mMinX, Viewport.mMinY,
                    Viewport.mMaxX - Viewport.mMinX,
                    Viewport.mMaxY - Viewport.mMinY,
                    Viewport.mMinZ, Viewport.mMaxZ });
            if (!Viewports.empty())
                mCommandList->RSSetViewports(static_cast<UINT>(Viewports.size()), Viewports.data());
            eastl::vector<D3D12_RECT> Scissors;
            for (const auto& Scissor : State.mScissors)
                Scissors.push_back({ Scissor.mMinX, Scissor.mMinY,
                    Scissor.mMaxX, Scissor.mMaxY });
            if (!Scissors.empty())
                mCommandList->RSSetScissorRects(static_cast<UINT>(Scissors.size()), Scissors.data());
            mCommandList->OMSetRenderTargets(
                static_cast<UINT>(Framebuffer->mRtvs.size()),
                Framebuffer->mRtvs.data(), FALSE,
                Framebuffer->mbHasDepth ? &Framebuffer->mDsv : nullptr);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::SetComputeState(
            const FArdaNativeComputeState& State)
        {
            auto* Pipeline = dynamic_cast<FD3D12Pipeline*>(State.mPipeline.get());
            if (!Pipeline) return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                "D3D12 compute state has the wrong implementation.");
            Retain(State.mPipeline);
            for (const auto& Binding : State.mBindings) Retain(Binding);
            mBoundComputePipeline = Pipeline;
            mBoundGraphicsPipeline = nullptr;
            mCommandList->SetComputeRootSignature(Pipeline->mRootSignature.Get());
            mCommandList->SetPipelineState(Pipeline->mPipeline.Get());
            return BindDescriptorSets(*Pipeline, State.mBindings, false);
        }

        void FArdaD3D12CommandList::SetPushConstants(const void* Data, size_t Size)
        {
            if (!Data || Size == 0) return;
            FD3D12Pipeline* Pipeline = mBoundGraphicsPipeline
                ? mBoundGraphicsPipeline : mBoundComputePipeline;
            if (!Pipeline) return;
            for (int32_t Root : Pipeline->mPushConstantRoots)
            {
                if (Root < 0) continue;
                const UINT Count = static_cast<UINT>((Size + 3u) / 4u);
                if (mBoundGraphicsPipeline)
                    mCommandList->SetGraphicsRoot32BitConstants(Root, Count, Data, 0);
                else
                    mCommandList->SetComputeRoot32BitConstants(Root, Count, Data, 0);
                break;
            }
        }

        void FArdaD3D12CommandList::Draw(const FArdaRHIDrawArguments& Arguments)
        {
            mCommandList->DrawInstanced(
                Arguments.mVertexCount, Arguments.mInstanceCount,
                Arguments.mStartVertex, Arguments.mStartInstance);
        }

        void FArdaD3D12CommandList::DrawIndexed(const FArdaRHIDrawArguments& Arguments)
        {
            mCommandList->DrawIndexedInstanced(
                Arguments.mVertexCount, Arguments.mInstanceCount,
                Arguments.mStartIndex, static_cast<INT>(Arguments.mStartVertex),
                Arguments.mStartInstance);
        }

        void FArdaD3D12CommandList::Dispatch(uint32_t X, uint32_t Y, uint32_t Z)
        {
            mCommandList->Dispatch(X, Y, Z);
        }

        TArdaRHIResult<eastl::unique_ptr<IArdaNativeCommandList>>
        FArdaD3D12ApiDevice::CreateCommandList(EArdaRHIQueueType Queue, bool)
        {
            if (!GetQueue(Queue))
                return Fail<eastl::unique_ptr<IArdaNativeCommandList>>(
                    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                        "The requested D3D12 command queue is unavailable."));
            const auto Type = Queue == EArdaRHIQueueType::Compute
                ? D3D12_COMMAND_LIST_TYPE_COMPUTE
                : D3D12_COMMAND_LIST_TYPE_DIRECT;
            auto Commands = eastl::make_unique<FArdaD3D12CommandList>(*this, Type);
            if (auto Status = Commands->Initialize(); !Status)
                return Fail<eastl::unique_ptr<IArdaNativeCommandList>>(eastl::move(Status));
            return { eastl::unique_ptr<IArdaNativeCommandList>(Commands.release()), {} };
        }

        TArdaRHIResult<uint64_t> FArdaD3D12ApiDevice::ExecuteCommandList(
            IArdaNativeCommandList& CommandList, EArdaRHIQueueType QueueType)
        {
            auto* Native = dynamic_cast<FArdaD3D12CommandList*>(&CommandList);
            if (!Native) return Fail<uint64_t>(FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "D3D12 command list has the wrong implementation."));
            ID3D12CommandQueue* Queue = GetQueue(QueueType);
            if (!Queue) return Fail<uint64_t>(FArdaRHIStatus::Error(
                EArdaRHIResult::Unsupported,
                "The requested D3D12 command queue is unavailable."));
            ID3D12CommandList* Lists[] = { Native->GetSubmitList() };
            Queue->ExecuteCommandLists(1, Lists);
            const uint64_t Value = mFenceValue.fetch_add(1, std::memory_order_relaxed) + 1;
            HRESULT Result = Queue->Signal(mFence.Get(), Value);
            if (FAILED(Result)) return Fail<uint64_t>(
                D3D12Failure("Failed to signal the D3D12 submission fence.", Result));
            if (mFence->GetCompletedValue() < Value)
            {
                Result = mFence->SetEventOnCompletion(Value, mFenceEvent);
                if (FAILED(Result)) return Fail<uint64_t>(
                    D3D12Failure("Failed to arm the D3D12 submission fence.", Result));
                WaitForSingleObject(mFenceEvent, INFINITE);
            }
            return { Value, {} };
        }

        FArdaRHIStatus FArdaD3D12ApiDevice::WaitForSubmission(uint64_t Value)
        {
            if (!mFence || mFence->GetCompletedValue() >= Value) return {};
            const HRESULT Result = mFence->SetEventOnCompletion(Value, mFenceEvent);
            if (FAILED(Result)) return D3D12Failure(
                "Failed to arm the D3D12 submission fence.", Result);
            WaitForSingleObject(mFenceEvent, INFINITE);
            return {};
        }

        FArdaRHIStatus FArdaD3D12ApiDevice::WaitForIdle()
        {
            if (!mFence) return {};
            ID3D12CommandQueue* Queues[] = { mQueue.Get(), mComputeQueue.Get() };
            for (ID3D12CommandQueue* Queue : Queues)
            {
                if (!Queue) continue;
                const uint64_t Value =
                    mFenceValue.fetch_add(1, std::memory_order_relaxed) + 1;
                HRESULT Result = Queue->Signal(mFence.Get(), Value);
                if (FAILED(Result)) return D3D12Failure(
                    "Failed to signal the D3D12 idle fence.", Result);
                if (mFence->GetCompletedValue() < Value)
                {
                    Result = mFence->SetEventOnCompletion(Value, mFenceEvent);
                    if (FAILED(Result)) return D3D12Failure(
                        "Failed to arm the D3D12 idle fence.", Result);
                    WaitForSingleObject(mFenceEvent, INFINITE);
                }
            }
            return {};
        }

        ID3D12CommandQueue* FArdaD3D12ApiDevice::GetQueue(
            EArdaRHIQueueType Queue) const noexcept
        {
            switch (Queue)
            {
            case EArdaRHIQueueType::Graphics: return mQueue.Get();
            case EArdaRHIQueueType::Compute: return mComputeQueue.Get();
            default: return nullptr;
            }
        }

        void FArdaD3D12ApiDevice::FlushPipelineCache() noexcept
        {
            std::lock_guard<std::mutex> Lock(mPipelineCacheMutex);
            if (!mPipelineLibrary)
                return;
            if (mbPipelineCacheDirty)
            {
                const SIZE_T Size = mPipelineLibrary->GetSerializedSize();
                if (Size <= pipeline_cache::MaxPayloadSize)
                {
                    std::vector<uint8_t> Payload(Size);
                    if (SUCCEEDED(mPipelineLibrary->Serialize(
                            Payload.data(), Payload.size())) &&
                        !pipeline_cache::WriteBlob(
                            pipeline_cache::MakePath(
                                mPipelineCacheDirectory, "native-d3d12"),
                            "native-d3d12", EArdaBackendType::D3D12, Payload))
                    {
                        pipeline_cache::Message(mDiagnosticCallback,
                            EArdaDiagnosticSeverity::Warning,
                            "Failed to atomically save the D3D12 pipeline library.");
                    }
                }
            }
            mPipelineLibrary.Reset();
            mPipelineCacheSource.clear();
            mbPipelineCacheDirty = false;
            mPipelineCacheDirectory.clear();
            mDiagnosticCallback = nullptr;
            mCapabilities.mbPipelineCachePersistence = false;
        }

        class FArdaD3D12SwapChain final : public IArdaSwapChain
        {
        public:
            FArdaD3D12SwapChain(
                ComPtr<IDXGIFactory6> Factory,
                ComPtr<ID3D12CommandQueue> Queue,
                eastl::shared_ptr<FArdaD3D12ApiDevice> ApiDevice,
                FArdaRHIDeviceRef ArdaDevice,
                HWND Window, uint32_t Width, uint32_t Height)
                : mFactory(eastl::move(Factory)), mQueue(eastl::move(Queue))
                , mApiDevice(eastl::move(ApiDevice)), mArdaDevice(eastl::move(ArdaDevice))
                , mWindow(Window), mWidth(Width), mHeight(Height) {}

            ~FArdaD3D12SwapChain() override
            {
                WaitForIdle();
                ReleaseResources();
            }

            bool Initialize()
            {
                DXGI_SWAP_CHAIN_DESC1 Desc{};
                Desc.Width = mWidth;
                Desc.Height = mHeight;
                Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                Desc.SampleDesc.Count = 1;
                Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                Desc.BufferCount = BufferCount;
                Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                ComPtr<IDXGISwapChain1> Base;
                HRESULT Result = mFactory->CreateSwapChainForHwnd(
                    mQueue.Get(), mWindow, &Desc, nullptr, nullptr, &Base);
                if (FAILED(Result))
                {
                    mError = D3D12Failure("Failed to create the D3D12 swap chain.", Result).mMessage;
                    return false;
                }
                (void)mFactory->MakeWindowAssociation(mWindow, DXGI_MWA_NO_ALT_ENTER);
                Result = Base.As(&mSwapChain);
                if (FAILED(Result))
                {
                    mError = "The D3D12 swap chain does not expose IDXGISwapChain3.";
                    return false;
                }
                return CreateResources();
            }

            bool Resize(uint32_t Width, uint32_t Height) override
            {
                if (Width == 0 || Height == 0 ||
                    (Width == mWidth && Height == mHeight)) return true;
                WaitForIdle();
                ReleaseResources();
                HRESULT Result = mSwapChain->ResizeBuffers(
                    BufferCount, Width, Height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
                if (FAILED(Result))
                {
                    mError = D3D12Failure("Failed to resize the D3D12 swap chain.", Result).mMessage;
                    return false;
                }
                mWidth = Width;
                mHeight = Height;
                return CreateResources();
            }

            bool AcquireFrame(FArdaRHIFramebufferRef& OutFramebuffer) override
            {
                if (!mSwapChain)
                {
                    mError = "The D3D12 swap chain is not initialized.";
                    OutFramebuffer = nullptr;
                    return false;
                }
                OutFramebuffer = mFramebuffers[mSwapChain->GetCurrentBackBufferIndex()];
                return static_cast<bool>(OutFramebuffer);
            }

            void PrepareSubmit() override {}

            bool Present() override
            {
                HRESULT Result = mSwapChain->Present(1, 0);
                if (FAILED(Result))
                {
                    mError = D3D12Failure("Failed to present the D3D12 swap chain.", Result).mMessage;
                    return false;
                }
                mError.clear();
                return true;
            }

            void WaitForIdle() noexcept override
            {
                if (mApiDevice) (void)mApiDevice->WaitForIdle();
            }
            EArdaRHIFormat GetFormat() const noexcept override { return EArdaRHIFormat::BGRA8UNorm; }
            uint32_t GetWidth() const noexcept override { return mWidth; }
            uint32_t GetHeight() const noexcept override { return mHeight; }
            const eastl::string& GetError() const noexcept override { return mError; }

        private:
            static constexpr uint32_t BufferCount = 2;

            bool CreateResources()
            {
                for (uint32_t Index = 0; Index < BufferCount; ++Index)
                {
                    ComPtr<ID3D12Resource> Resource;
                    HRESULT Result = mSwapChain->GetBuffer(Index, IID_PPV_ARGS(&Resource));
                    if (FAILED(Result))
                    {
                        mError = D3D12Failure("Failed to get a D3D12 swap-chain image.", Result).mMessage;
                        ReleaseResources();
                        return false;
                    }
                    FArdaRHINativeTextureImportDesc Import;
                    Import.mNativeObject = reinterpret_cast<uintptr_t>(Resource.Get());
                    Import.mNativeType = EArdaRHINativeResourceType::D3D12Resource;
                    Import.mOwnership = EArdaRHINativeOwnership::Borrowed;
                    Import.mInitialState = EArdaRHIResourceState::Present;
                    Import.mTexture.mWidth = mWidth;
                    Import.mTexture.mHeight = mHeight;
                    Import.mTexture.mFormat = EArdaRHIFormat::BGRA8UNorm;
                    Import.mTexture.mUsage = EArdaRHITextureUsage::RenderTarget;
                    Import.mTexture.mInitialState = EArdaRHIResourceState::Present;
                    Import.mTexture.mbKeepInitialState = true;
                    Import.mTexture.mDebugName = "D3D12 swap-chain image";
                    auto Texture = mArdaDevice->ImportNativeTexture(Import);
                    if (!Texture)
                    {
                        mError = Texture.mStatus.mMessage;
                        ReleaseResources();
                        return false;
                    }
                    FArdaRHIFramebufferDesc FramebufferDesc;
                    FramebufferDesc.mColorAttachments.push_back({ Texture.mValue, {} });
                    auto Framebuffer = mArdaDevice->CreateFramebuffer(FramebufferDesc);
                    if (!Framebuffer)
                    {
                        mError = Framebuffer.mStatus.mMessage;
                        ReleaseResources();
                        return false;
                    }
                    mFramebuffers[Index] = eastl::move(Framebuffer.mValue);
                }
                mError.clear();
                return true;
            }

            void ReleaseResources()
            {
                for (auto& Framebuffer : mFramebuffers) Framebuffer = nullptr;
                if (mArdaDevice) mArdaDevice->TrimDescriptorCaches();
            }

            ComPtr<IDXGIFactory6> mFactory;
            ComPtr<ID3D12CommandQueue> mQueue;
            eastl::shared_ptr<FArdaD3D12ApiDevice> mApiDevice;
            FArdaRHIDeviceRef mArdaDevice;
            HWND mWindow = nullptr;
            uint32_t mWidth = 0;
            uint32_t mHeight = 0;
            ComPtr<IDXGISwapChain3> mSwapChain;
            eastl::array<FArdaRHIFramebufferRef, BufferCount> mFramebuffers;
            eastl::string mError;
        };

        class FArdaD3D12BackendDevice final : public IArdaBackendDevice
        {
        public:
            ~FArdaD3D12BackendDevice() override
            {
                WaitForIdle();
                mArdaDevice = nullptr;
                mApiDevice.reset();
                mComputeQueue.Reset();
                mQueue.Reset();
                mD3DDevice.Reset();
                mFactory.Reset();
                mLifetimeToken.reset();
            }

            EArdaInitializeResult Initialize(
                const FArdaBackendConfiguration& Configuration,
                IArdaWindowSurface* WindowSurface,
                const IArdaExternalDeviceProvider* ExternalProvider) override
            {
                if (WindowSurface)
                {
                    mWindow = WindowSurface->GetD3D12WindowHandle().As<HWND>();
                    if (!mWindow)
                    {
                        mError = "The window surface did not provide a Win32 window handle.";
                        return EArdaInitializeResult::Failure;
                    }
                }
                if (Configuration.mDeviceSource == EArdaDeviceSource::ExternalProvider)
                {
                    if (!InitializeExternal(ExternalProvider))
                        return EArdaInitializeResult::Failure;
                }
                else
                {
                    const auto Result = InitializeOwned(Configuration);
                    if (Result != EArdaInitializeResult::Success) return Result;
                }
                if (!mFactory)
                {
                    HRESULT Result = CreateDXGIFactory2(0, IID_PPV_ARGS(&mFactory));
                    if (FAILED(Result))
                    {
                        mError = D3D12Failure("Failed to create a DXGI factory.", Result).mMessage;
                        return EArdaInitializeResult::Failure;
                    }
                }
                mApiDevice = eastl::make_shared<FArdaD3D12ApiDevice>(
                    mD3DDevice, mQueue, mComputeQueue,
                    Configuration.mPipelineCacheDirectory,
                    Configuration.mMessageCallback, mLifetimeToken);
                if (auto Status = mApiDevice->Initialize(); !Status)
                {
                    mError = Status.mMessage;
                    return EArdaInitializeResult::Failure;
                }
                mArdaDevice = CreateArdaNativeRHIDevice(mApiDevice);
                if (!mArdaDevice)
                {
                    mError = "Failed to create the native D3D12 RHI facade.";
                    return EArdaInitializeResult::Failure;
                }
                mQueues.mbGraphics = true;
                mQueues.mbCompute = mComputeQueue != nullptr;
                mQueues.mbCopy = false;
                mError.clear();
                return EArdaInitializeResult::Success;
            }

            eastl::unique_ptr<IArdaSwapChain> CreateSwapChain(
                uint32_t Width, uint32_t Height) override
            {
                if (!mWindow)
                {
                    mError = "D3D12 presentation was not initialized with a window surface.";
                    return {};
                }
                auto Result = eastl::make_unique<FArdaD3D12SwapChain>(
                    mFactory, mQueue, mApiDevice, mArdaDevice,
                    mWindow, Width, Height);
                if (!Result->Initialize())
                {
                    mError = Result->GetError();
                    return {};
                }
                return Result;
            }

            void WaitForIdle() noexcept override
            {
                if (mApiDevice) (void)mApiDevice->WaitForIdle();
            }
            FArdaRHIDeviceRef GetDevice() const noexcept override { return mArdaDevice; }
            FArdaQueueCapabilities GetQueueCapabilities() const noexcept override { return mQueues; }
            const eastl::string& GetError() const noexcept override { return mError; }

        private:
            EArdaInitializeResult InitializeOwned(const FArdaBackendConfiguration& Configuration)
            {
                if (Configuration.mbEnableValidation)
                {
                    ComPtr<ID3D12Debug> Debug;
                    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&Debug))))
                        Debug->EnableDebugLayer();
                }
                UINT FactoryFlags = Configuration.mbEnableValidation
                    ? DXGI_CREATE_FACTORY_DEBUG : 0;
                HRESULT Result = CreateDXGIFactory2(FactoryFlags, IID_PPV_ARGS(&mFactory));
                if (FAILED(Result) && FactoryFlags != 0)
                {
                    if (Configuration.mMessageCallback)
                    {
                        Configuration.mMessageCallback->Message(
                            EArdaDiagnosticSeverity::Warning,
                            "The DXGI debug factory is unavailable; retrying D3D12 initialization without DXGI factory debugging.");
                    }
                    mFactory.Reset();
                    Result = CreateDXGIFactory2(0, IID_PPV_ARGS(&mFactory));
                }
                if (FAILED(Result))
                {
                    mError = D3D12Failure("CreateDXGIFactory2 failed; D3D12 is unavailable.", Result).mMessage;
                    return EArdaInitializeResult::Unavailable;
                }
                ComPtr<IDXGIAdapter1> Adapter;
                for (UINT Index = 0;
                     mFactory->EnumAdapterByGpuPreference(Index,
                         DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                         IID_PPV_ARGS(&Adapter)) != DXGI_ERROR_NOT_FOUND;
                     ++Index)
                {
                    DXGI_ADAPTER_DESC1 Desc{};
                    Adapter->GetDesc1(&Desc);
                    if ((Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                        SUCCEEDED(D3D12CreateDevice(Adapter.Get(),
                            D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr)))
                        break;
                    Adapter.Reset();
                }
                if (!Adapter && FAILED(mFactory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter))))
                {
                    mError = "No D3D12 adapter or WARP adapter is available.";
                    return EArdaInitializeResult::Unavailable;
                }
                Result = D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                    IID_PPV_ARGS(&mD3DDevice));
                if (FAILED(Result))
                {
                    mError = D3D12Failure("D3D12CreateDevice failed.", Result).mMessage;
                    return EArdaInitializeResult::Unavailable;
                }
                D3D12_COMMAND_QUEUE_DESC QueueDesc{};
                QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                Result = mD3DDevice->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&mQueue));
                if (FAILED(Result))
                {
                    mError = D3D12Failure("Failed to create the D3D12 graphics queue.", Result).mMessage;
                    return EArdaInitializeResult::Failure;
                }
                QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
                Result = mD3DDevice->CreateCommandQueue(
                    &QueueDesc, IID_PPV_ARGS(&mComputeQueue));
                if (FAILED(Result))
                {
                    mError = D3D12Failure(
                        "Failed to create the D3D12 compute queue.", Result).mMessage;
                    return EArdaInitializeResult::Failure;
                }
                return EArdaInitializeResult::Success;
            }

            bool InitializeExternal(const IArdaExternalDeviceProvider* Provider)
            {
                if (!Provider)
                {
                    mError = "External D3D12 initialization requires a registered provider.";
                    return false;
                }
                FArdaExternalDeviceDesc Desc;
                if (!Provider->GetExternalDeviceDesc(Desc))
                {
                    mError = "The external provider did not supply a D3D12 device descriptor.";
                    return false;
                }
                if (Desc.mNativeApi != "d3d12" || !Desc.mDevice)
                {
                    mError = "The external device descriptor is not a valid D3D12 descriptor.";
                    return false;
                }
                mD3DDevice = Desc.mDevice.As<ID3D12Device*>();
                for (const auto& Queue : Desc.mQueues)
                {
                    if (Queue.mType == EArdaRHIQueueType::Graphics && Queue.mQueue)
                    {
                        mQueue = Queue.mQueue.As<ID3D12CommandQueue*>();
                    }
                    else if (Queue.mType == EArdaRHIQueueType::Compute && Queue.mQueue)
                    {
                        mComputeQueue = Queue.mQueue.As<ID3D12CommandQueue*>();
                    }
                }
                if (!mD3DDevice || !mQueue)
                {
                    mError = "The external D3D12 descriptor requires a device and graphics queue.";
                    return false;
                }
                if (mQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
                {
                    mError = "The external D3D12 graphics queue must be a direct command queue.";
                    return false;
                }
                if (mComputeQueue &&
                    mComputeQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_COMPUTE)
                {
                    mError = "The external D3D12 compute queue must be a compute command queue.";
                    return false;
                }
                mLifetimeToken = Provider->GetLifetimeToken();
                return true;
            }

            HWND mWindow = nullptr;
            eastl::string mError;
            eastl::shared_ptr<void> mLifetimeToken;
            ComPtr<IDXGIFactory6> mFactory;
            ComPtr<ID3D12Device> mD3DDevice;
            ComPtr<ID3D12CommandQueue> mQueue;
            ComPtr<ID3D12CommandQueue> mComputeQueue;
            eastl::shared_ptr<FArdaD3D12ApiDevice> mApiDevice;
            FArdaRHIDeviceRef mArdaDevice;
            FArdaQueueCapabilities mQueues;
        };

        class FArdaD3D12BackendModule final : public IArdaBackendModule
        {
        public:
            FArdaD3D12BackendModule()
            {
                mDescriptor.mName = "native-d3d12";
                mDescriptor.mDisplayName = "Native Direct3D 12 (Agility SDK 1.619.5)";
                mDescriptor.mBackendType = EArdaBackendType::D3D12;
                mDescriptor.mShaderBinaryFormat = EArdaShaderBinaryFormat::Dxil;
                mDescriptor.mShaderArtifactExtension = ".dxil";
                mDescriptor.mbSupportsOwnedDevice = true;
                mDescriptor.mbSupportsExternalDevice = true;
                mDescriptor.mPriority = 200;
            }
            const FArdaBackendModuleDescriptor& GetDescriptor() const noexcept override { return mDescriptor; }
            eastl::unique_ptr<IArdaBackendDevice> CreateDevice(EArdaDeviceSource) override
            {
                return eastl::make_unique<FArdaD3D12BackendDevice>();
            }
            FArdaRHIStatus ConfigureShaderCompileInvocation(
                FArdaBackendShaderCompileInvocation&) const override { return {}; }
        private:
            FArdaBackendModuleDescriptor mDescriptor;
        };

        FArdaD3D12BackendModule& GetD3D12Module()
        {
            static FArdaD3D12BackendModule Module;
            return Module;
        }
    }

    bool RegisterArdaD3D12Backend()
    {
        return RegisterBackendModule(GetD3D12Module());
    }
}
