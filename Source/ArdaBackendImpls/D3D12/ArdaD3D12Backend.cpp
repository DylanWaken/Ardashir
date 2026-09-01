#include "RHI/ArdaRHIProvider.h"
#include "RHI/ArdaRHIProviderPipelineCache.h"
#include "ArdaBackendProvider.h"
#include "ArdaExternalInterop.h"
#include "ArdaSwapChain.h"

#if !defined(_WIN32)
#error The native D3D12 backend is Windows-only.
#endif

#include <d3d12.h>
#include <d3dx12/d3dx12_state_object.h>
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
        using namespace rhi::provider;

        constexpr uint32_t D3D12ResourceDescriptorHeapCapacity = 65536;
        constexpr uint32_t D3D12SamplerDescriptorHeapCapacity = 2048;

        template <typename T, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type>
        struct alignas(void*) TD3D12PipelineSubobject
        {
            D3D12_PIPELINE_STATE_SUBOBJECT_TYPE mType = Type;
            T mValue{};
        };

        struct FMeshPipelineStream
        {
            TD3D12PipelineSubobject<ID3D12RootSignature*,
                D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE> mRoot;
            TD3D12PipelineSubobject<D3D12_SHADER_BYTECODE,
                D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS> mAmplificationShader;
            TD3D12PipelineSubobject<D3D12_SHADER_BYTECODE,
                D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS> mMeshShader;
            TD3D12PipelineSubobject<D3D12_SHADER_BYTECODE,
                D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS> mPixelShader;
            TD3D12PipelineSubobject<D3D12_BLEND_DESC,
                D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND> mBlend;
            TD3D12PipelineSubobject<UINT,
                D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK> mSampleMask;
            TD3D12PipelineSubobject<D3D12_RASTERIZER_DESC,
                D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER> mRasterizer;
            TD3D12PipelineSubobject<D3D12_DEPTH_STENCIL_DESC,
                D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL> mDepthStencil;
            TD3D12PipelineSubobject<D3D12_PRIMITIVE_TOPOLOGY_TYPE,
                D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY> mTopology;
            TD3D12PipelineSubobject<D3D12_RT_FORMAT_ARRAY,
                D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS> mRenderTargets;
            TD3D12PipelineSubobject<DXGI_FORMAT,
                D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT> mDepthFormat;
            TD3D12PipelineSubobject<DXGI_SAMPLE_DESC,
                D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC> mSampleDesc;
        };

        FArdaRHIStatus D3D12Failure(const char* Message, HRESULT Result = E_FAIL)
        {
            eastl::string Text = Message ? Message : "D3D12 operation failed.";
            char Suffix[32]{};
            std::snprintf(Suffix, sizeof(Suffix), " (HRESULT 0x%08X)",
                static_cast<unsigned>(Result));
            Text += Suffix;
            return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Text.c_str());
        }

        FArdaRHIStatus D3D12FailureWithInfo(
            ID3D12Device* Device, const char* Message, HRESULT Result)
        {
            auto Status = D3D12Failure(Message, Result);
            if (Device && (Result == DXGI_ERROR_DEVICE_REMOVED ||
                    Result == DXGI_ERROR_DEVICE_RESET ||
                    Result == DXGI_ERROR_DEVICE_HUNG))
            {
                const HRESULT RemovedReason = Device->GetDeviceRemovedReason();
                char Suffix[48]{};
                std::snprintf(Suffix, sizeof(Suffix),
                    " (device removed reason 0x%08X)",
                    static_cast<unsigned>(RemovedReason));
                Status.mMessage += Suffix;
            }
            ComPtr<ID3D12InfoQueue> Queue;
            if (!Device || FAILED(Device->QueryInterface(IID_PPV_ARGS(&Queue))))
                return Status;
            const UINT64 Count = Queue->GetNumStoredMessagesAllowedByRetrievalFilter();
            if (!Count)
                return Status;
            SIZE_T Bytes = 0;
            Queue->GetMessage(Count - 1, nullptr, &Bytes);
            eastl::vector<uint8_t> Storage(Bytes);
            auto* NativeMessage = reinterpret_cast<D3D12_MESSAGE*>(Storage.data());
            if (Bytes && SUCCEEDED(Queue->GetMessage(
                    Count - 1, NativeMessage, &Bytes)) &&
                NativeMessage->pDescription)
            {
                Status.mMessage += ": ";
                Status.mMessage += NativeMessage->pDescription;
            }
            return Status;
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
            if (HasAnyFlags(State, EArdaRHIResourceState::CpuRead)) Result |= D3D12_RESOURCE_STATE_COPY_DEST;
            if (HasAnyFlags(State, EArdaRHIResourceState::ShadingRateSource)) Result |= D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
            if (HasAnyFlags(State,
                    EArdaRHIResourceState::AccelStructBuildInput) ||
                HasAnyFlags(State,
                    EArdaRHIResourceState::AccelStructBuildBlas))
                Result |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            return Result;
        }

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS
        ToD3D12BuildFlags(EArdaRHIAccelStructBuildFlags Flags) noexcept
        {
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS Result =
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
            if (HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::AllowUpdate))
                Result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
            if (HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::AllowCompaction))
                Result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
            if (HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::PreferFastTrace))
                Result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
            if (HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::PreferFastBuild))
                Result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
            if (HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::MinimizeMemory))
                Result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY;
            if (HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::PerformUpdate))
                Result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
            return Result;
        }

        D3D12_RAYTRACING_GEOMETRY_FLAGS ToD3D12GeometryFlags(
            EArdaRHIRayTracingGeometryFlags Flags) noexcept
        {
            D3D12_RAYTRACING_GEOMETRY_FLAGS Result =
                D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
            if (HasAnyFlags(Flags, EArdaRHIRayTracingGeometryFlags::Opaque))
                Result |= D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            if (HasAnyFlags(Flags,
                    EArdaRHIRayTracingGeometryFlags::NoDuplicateAnyHitInvocation))
                Result |= D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;
            return Result;
        }

        TArdaRHIResult<ComPtr<ID3D12Resource>> CreateD3D12BufferResource(
            ID3D12Device* Device,
            uint64_t Size,
            D3D12_HEAP_TYPE HeapType,
            D3D12_RESOURCE_STATES State,
            D3D12_RESOURCE_FLAGS Flags = D3D12_RESOURCE_FLAG_NONE)
        {
            D3D12_HEAP_PROPERTIES Heap{};
            Heap.Type = HeapType;
            Heap.CreationNodeMask = 1;
            Heap.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC Desc{};
            Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            Desc.Width = eastl::max<uint64_t>(Size, 1);
            Desc.Height = 1;
            Desc.DepthOrArraySize = 1;
            Desc.MipLevels = 1;
            Desc.SampleDesc.Count = 1;
            Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            Desc.Flags = Flags;
            ComPtr<ID3D12Resource> Resource;
            const HRESULT Result = Device->CreateCommittedResource(
                &Heap, D3D12_HEAP_FLAG_NONE, &Desc, State, nullptr,
                IID_PPV_ARGS(&Resource));
            if (FAILED(Result))
                return Fail<ComPtr<ID3D12Resource>>(D3D12Failure(
                    "Failed to allocate a D3D12 ray-tracing buffer.", Result));
            return {eastl::move(Resource), {}};
        }

        size_t D3D12TextureStateCount(
            const FArdaRHITextureDesc& Desc) noexcept
        {
            return static_cast<size_t>(Desc.mMipLevels) * Desc.mArraySize *
                GetArdaRHIFormatPlaneCount(Desc.mFormat);
        }

        constexpr uint64_t D3D12SubmissionQueueShift = 62;
        constexpr uint64_t D3D12SubmissionValueMask =
            (uint64_t{1} << D3D12SubmissionQueueShift) - 1;

        uint64_t EncodeD3D12Submission(
            EArdaRHIQueueType Queue, uint64_t QueueValue) noexcept
        {
            return (static_cast<uint64_t>(GetArdaRHIQueueIndex(Queue)) <<
                D3D12SubmissionQueueShift) | QueueValue;
        }

        uint64_t DecodeD3D12SubmissionValue(uint64_t Submission) noexcept
        {
            return Submission & D3D12SubmissionValueMask;
        }

        D3D12_RESOURCE_DESC ToD3D12ResourceDesc(
            const FArdaRHITextureDesc& Desc) noexcept
        {
            D3D12_RESOURCE_DESC Resource{};
            Resource.Dimension =
                Desc.mDimension == EArdaRHITextureDimension::Texture3D
                    ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                    : (Desc.mDimension == EArdaRHITextureDimension::Texture1D ||
                       Desc.mDimension == EArdaRHITextureDimension::Texture1DArray
                        ? D3D12_RESOURCE_DIMENSION_TEXTURE1D
                        : D3D12_RESOURCE_DIMENSION_TEXTURE2D);
            Resource.Width = Desc.mWidth;
            Resource.Height = Desc.mHeight;
            Resource.DepthOrArraySize = static_cast<UINT16>(
                Resource.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                    ? Desc.mDepth : Desc.mArraySize);
            Resource.MipLevels = static_cast<UINT16>(Desc.mMipLevels);
            Resource.Format = ToDxgi(Desc.mFormat);
            Resource.SampleDesc.Count = Desc.mSampleCount;
            Resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::RenderTarget))
                Resource.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::DepthStencil))
                Resource.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::UnorderedAccess))
                Resource.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            return Resource;
        }

        D3D12_RESOURCE_DESC ToD3D12ResourceDesc(
            const FArdaRHIBufferDesc& Desc) noexcept
        {
            D3D12_RESOURCE_DESC Resource{};
            Resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            Resource.Width = Desc.mByteSize;
            Resource.Height = 1;
            Resource.DepthOrArraySize = 1;
            Resource.MipLevels = 1;
            Resource.SampleDesc.Count = 1;
            Resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (HasAnyFlags(Desc.mUsage, EArdaRHIBufferUsage::UnorderedAccess))
                Resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            return Resource;
        }

        uint32_t ArdaD3D12CalcSubresource(
            uint32_t MipLevel,
            uint32_t ArraySlice,
            uint32_t Plane,
            uint32_t MipLevels,
            uint32_t ArraySize) noexcept
        {
            return MipLevel + ArraySlice * MipLevels +
                Plane * MipLevels * ArraySize;
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

        class FD3D12Texture : public IArdaProviderObject
        {
        public:
            const void* GetIdentity() const noexcept override { return mResource.Get(); }
            ComPtr<ID3D12Resource> mResource;
            FArdaRHITextureDesc mDesc;
            mutable std::mutex mStateMutex;
            eastl::vector<EArdaRHIResourceState> mAbstractStates;
            eastl::vector<D3D12_RESOURCE_STATES> mNativeStates;
            ComPtr<ID3D12DescriptorHeap> mRtvHeap;
            ComPtr<ID3D12DescriptorHeap> mDsvHeap;
            D3D12_CPU_DESCRIPTOR_HANDLE mRtv{};
            D3D12_CPU_DESCRIPTOR_HANDLE mDsv{};
            FArdaProviderObjectRef mHeap;
            eastl::vector<FArdaProviderObjectRef> mSparseHeaps;
            ComPtr<ID3D12Heap> mReservedCommitHeap;
            uint64_t mCommittedBytes = 0;
        };

        class FD3D12Buffer final : public IArdaProviderObject
        {
        public:
            const void* GetIdentity() const noexcept override { return mResource.Get(); }
            ComPtr<ID3D12Resource> mResource;
            FArdaRHIBufferDesc mDesc;
            mutable std::mutex mStateMutex;
            EArdaRHIResourceState mAbstractState = EArdaRHIResourceState::Unknown;
            D3D12_RESOURCE_STATES mNativeState = D3D12_RESOURCE_STATE_COMMON;
            bool mbStateKnown = false;
            FArdaProviderObjectRef mHeap;
            eastl::vector<FArdaProviderObjectRef> mSparseHeaps;
            ComPtr<ID3D12Heap> mReservedCommitHeap;
            uint64_t mCommittedBytes = 0;
        };

        class FD3D12AccelStruct final : public IArdaProviderObject
        {
        public:
            const void* GetIdentity() const noexcept override
            {
                return mResource.Get();
            }
            FArdaRHIAccelStructDesc mDesc;
            FArdaRHIAccelStructMemoryRequirements mRequirements;
            ComPtr<ID3D12Resource> mResource;
            ComPtr<ID3D12Resource> mCompactedSizeGpu;
            ComPtr<ID3D12Resource> mCompactedSizeReadback;
            mutable std::mutex mStateMutex;
            EArdaRHIResourceState mAbstractState =
                EArdaRHIResourceState::AccelStructRead;
            EArdaRHIAccelStructBuildState mBuildState =
                EArdaRHIAccelStructBuildState::Unbuilt;
            bool mbCompactedSizePending = false;
        };

        class FD3D12Heap final : public IArdaProviderObject
        {
        public:
            const void* GetIdentity() const noexcept override
            {
                return mHeap.Get();
            }
            ComPtr<ID3D12Heap> mHeap;
            FArdaRHIHeapDesc mDesc;
        };

        class FD3D12StagingTexture final : public IArdaProviderObject
        {
        public:
            const void* GetIdentity() const noexcept override
            {
                return mResource.Get();
            }
            ComPtr<ID3D12Resource> mResource;
            FArdaRHIStagingTextureDesc mDesc;
            eastl::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> mFootprints;
            eastl::vector<UINT> mRowCounts;
            eastl::vector<UINT64> mRowSizes;
            std::mutex mMapMutex;
            bool mbMapped = false;
        };

        class FD3D12Sampler final : public IArdaProviderObject
        {
        public:
            const void* GetIdentity() const noexcept override { return this; }
            D3D12_SAMPLER_DESC mDesc{};
        };

        class FD3D12Shader final : public IArdaProviderObject
        {
        public:
            const void* GetIdentity() const noexcept override { return this; }
            eastl::vector<uint8_t> mBytecode;
            EArdaRHIShaderStage mStage = EArdaRHIShaderStage::None;
        };

        class FD3D12BindingLayout final : public IArdaProviderObject
        {
        public:
            const void* GetIdentity() const noexcept override { return this; }
            FArdaRHIBindingLayoutDesc mDesc;
            bool mbBindless = false;
            FArdaRHIBindlessLayoutDesc mBindlessDesc;
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
                mResourceFree.push_back(
                    { 0, mResourceHeap->GetDesc().NumDescriptors });
                mSamplerFree.push_back(
                    { 0, mSamplerHeap->GetDesc().NumDescriptors });
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

        class FD3D12SamplerFeedbackTexture final : public FD3D12Texture
        {
        public:
            ~FD3D12SamplerFeedbackTexture() override
            {
                if (mAllocator && mDescriptor.mCount)
                    mAllocator->Free(mDescriptor);
            }
            FArdaRHISamplerFeedbackTextureDesc mFeedbackDesc;
            FArdaProviderObjectRef mPairedTexture;
            eastl::shared_ptr<FD3D12DescriptorAllocator> mAllocator;
            FD3D12DescriptorAllocation mDescriptor;
            ComPtr<ID3D12DescriptorHeap> mCpuDescriptorHeap;
            D3D12_CPU_DESCRIPTOR_HANDLE mCpuDescriptor{};
        };

        class FD3D12BindingSet final : public IArdaProviderObject
        {
        public:
            ~FD3D12BindingSet() override
            {
                if (mAllocator)
                    for (const FD3D12DescriptorAllocation& Allocation : mAllocations)
                        mAllocator->Free(Allocation);
            }
            const void* GetIdentity() const noexcept override { return this; }
            uint32_t GetDescriptorBaseIndex() const noexcept override
            {
                return mAllocations.empty() ? 0u : mAllocations.front().mOffset;
            }
            eastl::vector<FD3D12DescriptorTable> mTables;
            eastl::vector<FArdaProviderObjectRef> mRetainedObjects;
            eastl::shared_ptr<FD3D12DescriptorAllocator> mAllocator;
            eastl::vector<FD3D12DescriptorAllocation> mAllocations;
        };

        class FD3D12Framebuffer final : public IArdaProviderObject
        {
        public:
            const void* GetIdentity() const noexcept override { return this; }
            eastl::vector<D3D12_CPU_DESCRIPTOR_HANDLE> mRtvs;
            D3D12_CPU_DESCRIPTOR_HANDLE mDsv{};
            bool mbHasDepth = false;
            eastl::vector<FArdaProviderObjectRef> mRetainedTextures;
        };

        class FD3D12Pipeline final : public IArdaProviderObject
        {
        public:
            const void* GetIdentity() const noexcept override { return mPipeline.Get(); }
            ComPtr<ID3D12PipelineState> mPipeline;
            ComPtr<ID3D12RootSignature> mRootSignature;
            eastl::vector<uint32_t> mLayoutItemCounts;
            eastl::vector<int32_t> mPushConstantRoots;
            D3D12_PRIMITIVE_TOPOLOGY mTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        };

        class FD3D12RayTracingPipeline final : public IArdaProviderObject
        {
        public:
            const void* GetIdentity() const noexcept override
            {
                return mStateObject.Get();
            }
            ComPtr<ID3D12StateObject> mStateObject;
            ComPtr<ID3D12StateObjectProperties> mProperties;
            FD3D12Pipeline mGlobalBindings;
            eastl::vector<ComPtr<ID3D12RootSignature>> mLocalRootSignatures;
        };

        class FD3D12WorkGraph final : public IArdaProviderObject
        {
        public:
            const void* GetIdentity() const noexcept override
            {
                return mStateObject.Get();
            }
            uint64_t GetWorkGraphBackingMemorySize()
                const noexcept override
            {
                return mMemoryRequirements.MaxSizeInBytes;
            }
            ComPtr<ID3D12StateObject> mStateObject;
            ComPtr<ID3D12StateObjectProperties1> mStateProperties;
            ComPtr<ID3D12WorkGraphProperties> mWorkGraphProperties;
            FD3D12Pipeline mGlobalBindings;
            ComPtr<ID3D12Resource> mBackingMemory;
            D3D12_WORK_GRAPH_MEMORY_REQUIREMENTS mMemoryRequirements{};
            D3D12_PROGRAM_IDENTIFIER mProgramIdentifier{};
            UINT mEntrypointIndex = 0;
            UINT mEntrypointRecordSize = 0;
            std::atomic<bool> mbInitialized{false};
        };

        class FD3D12ShaderTable final : public IArdaProviderObject
        {
        public:
            struct FRecord
            {
                bool mbWritten = false;
                EArdaRHIShaderTableRecordType mType =
                    EArdaRHIShaderTableRecordType::RayGeneration;
                eastl::string mExportName;
                eastl::vector<uint8_t> mLocalArguments;
                uint32_t mUserData = 0;
                uint32_t mGeometrySegment = 0;
                FArdaProviderObjectRef mBindings;
                FArdaProviderObjectRef mGeometry;
            };
            const void* GetIdentity() const noexcept override { return this; }
            FArdaProviderObjectRef mPipelineObject;
            FD3D12RayTracingPipeline* mPipeline = nullptr;
            uint32_t mMaxEntries = 0;
            uint32_t mMaxLocalArgumentBytes = 0;
            eastl::vector<FRecord> mRecords;
            eastl::string mRayGeneration;
            eastl::vector<eastl::string> mMiss;
            eastl::vector<eastl::string> mHitGroups;
            eastl::vector<eastl::string> mCallable;
            ComPtr<ID3D12Resource> mBuffer;
            D3D12_GPU_VIRTUAL_ADDRESS_RANGE mRayGenerationRange{};
            D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE mMissRange{};
            D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE mHitGroupRange{};
            D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE mCallableRange{};
        };

        struct FD3D12SubmissionLifetime
        {
            ComPtr<ID3D12CommandAllocator> mAllocator;
            ComPtr<ID3D12GraphicsCommandList> mCommandList;
            eastl::vector<ComPtr<ID3D12Resource>> mResources;
            eastl::vector<ComPtr<ID3D12CommandSignature>> mCommandSignatures;
            eastl::vector<FArdaProviderObjectRef> mObjects;
        };

        class FArdaD3D12ProviderDevice;

        class FArdaD3D12CommandList final : public IArdaProviderCommandList
        {
        public:
            FArdaD3D12CommandList(
                FArdaD3D12ProviderDevice& Device,
                D3D12_COMMAND_LIST_TYPE Type);
            FArdaRHIStatus Initialize();
            FArdaRHIStatus Open() override;
            FArdaRHIStatus Close() override;
            FArdaRHIStatus Reset() override;
            FArdaRHIStatus WriteBuffer(const FArdaProviderObjectRef&, const FArdaRHIBufferDesc&, const void*, size_t, uint64_t) override;
            FArdaRHIStatus CopyBuffer(const FArdaProviderObjectRef&, uint64_t, const FArdaProviderObjectRef&, uint64_t, uint64_t) override;
            FArdaRHIStatus CopyTexture(const FArdaProviderObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureSlice&, const FArdaProviderObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureSlice&) override;
            FArdaRHIStatus ResolveTexture(const FArdaProviderObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureSlice&, const FArdaProviderObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureSlice&) override;
            FArdaRHIStatus CopyTextureToStaging(const FArdaProviderObjectRef&, const FArdaRHIStagingTextureDesc&, const FArdaRHITextureSlice&, const FArdaProviderObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureSlice&) override;
            FArdaRHIStatus CopyTextureFromStaging(const FArdaProviderObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureSlice&, const FArdaProviderObjectRef&, const FArdaRHIStagingTextureDesc&, const FArdaRHITextureSlice&) override;
            FArdaRHIStatus ClearTexture(const FArdaProviderObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureSubresourceRange&, const FArdaRHIColor&) override;
            FArdaRHIStatus ClearDepthStencilTexture(const FArdaProviderObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureSubresourceRange&, bool, float, bool, uint8_t) override;
            FArdaRHIStatus SetTextureState(const FArdaProviderObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureSubresourceRange&, EArdaRHIResourceState) override;
            FArdaRHIStatus SetBufferState(const FArdaProviderObjectRef&, const FArdaRHIBufferDesc&, EArdaRHIResourceState) override;
            FArdaRHIStatus TransitionTexture(const FArdaProviderObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureTransitionDesc&) override;
            FArdaRHIStatus TransitionBuffer(const FArdaProviderObjectRef&, const FArdaRHIBufferDesc&, const FArdaRHIBufferTransitionDesc&) override;
            void SetAutomaticBarriers(bool bEnabled) override { mbAutomaticBarriers = bEnabled; }
            FArdaRHIStatus BeginTrackingTextureState(const FArdaProviderObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureSubresourceRange&, EArdaRHIResourceState) override;
            FArdaRHIStatus BeginTrackingBufferState(const FArdaProviderObjectRef&, const FArdaRHIBufferDesc&, EArdaRHIResourceState) override;
            TArdaRHIResult<FArdaRHINativeResourceState> QueryTextureState(
                const FArdaProviderObjectRef&,
                const FArdaRHITextureDesc&,
                const FArdaRHITextureSubresourceRange&) const override;
            TArdaRHIResult<FArdaRHINativeResourceState> QueryBufferState(
                const FArdaProviderObjectRef&,
                const FArdaRHIBufferDesc&) const override;
            FArdaRHIStatus SetUAVBarriersForTexture(const FArdaProviderObjectRef&, bool) override;
            FArdaRHIStatus SetUAVBarriersForBuffer(const FArdaProviderObjectRef&, bool) override;
            void CommitBarriers() override {}
            FArdaRHIStatus AliasingBarrier(const FArdaProviderObjectRef&, const FArdaProviderObjectRef&) override;
            FArdaRHIStatus SetGraphicsState(const FArdaProviderGraphicsState&) override;
            FArdaRHIStatus SetComputeState(const FArdaProviderComputeState&) override;
            FArdaRHIStatus SetMeshletState(const FArdaProviderMeshletState&) override;
            FArdaRHIStatus SetRayTracingState(const FArdaProviderRayTracingState&) override;
            void SetPushConstants(const void*, size_t) override;
            void Draw(const FArdaRHIDrawArguments&) override;
            void DrawIndexed(const FArdaRHIDrawArguments&) override;
            FArdaRHIStatus DrawIndirect(const FArdaProviderObjectRef&, uint64_t, uint32_t, uint32_t) override;
            FArdaRHIStatus DrawIndexedIndirect(const FArdaProviderObjectRef&, uint64_t, uint32_t, uint32_t) override;
            void Dispatch(uint32_t, uint32_t, uint32_t) override;
            FArdaRHIStatus DispatchIndirect(const FArdaProviderObjectRef&, uint64_t) override;
            FArdaRHIStatus DispatchMesh(uint32_t, uint32_t, uint32_t) override;
            FArdaRHIStatus DispatchRays(uint32_t, uint32_t, uint32_t) override;
            FArdaRHIStatus DispatchRaysIndirect(
                const FArdaProviderObjectRef&, uint64_t) override;
            FArdaRHIStatus DispatchWorkGraph(
                const FArdaProviderObjectRef&, const void*, uint32_t,
                uint32_t,
                const eastl::vector<FArdaProviderObjectRef>&) override;
            FArdaRHIStatus ClearSamplerFeedbackTexture(
                const FArdaProviderObjectRef&) override;
            FArdaRHIStatus DecodeSamplerFeedbackTexture(
                const FArdaProviderObjectRef&, const FArdaRHITextureDesc&,
                const FArdaProviderObjectRef&, EArdaRHIFormat) override;
            FArdaRHIStatus SetSamplerFeedbackTextureState(
                const FArdaProviderObjectRef&,
                EArdaRHIResourceState) override;
            TArdaRHIResult<FArdaRHINativeResourceState>
                QuerySamplerFeedbackTextureState(
                    const FArdaProviderObjectRef&) const override;
            FArdaRHIStatus SetAccelStructState(
                const FArdaProviderObjectRef&, EArdaRHIResourceState) override;
            TArdaRHIResult<FArdaRHINativeResourceState>
                QueryAccelStructState(const FArdaProviderObjectRef&) const override;
            FArdaRHIStatus BuildBottomLevelAccelStruct(
                const FArdaProviderObjectRef&,
                const eastl::vector<FArdaProviderRayTracingGeometry>&,
                EArdaRHIAccelStructBuildFlags) override;
            FArdaRHIStatus BuildTopLevelAccelStruct(
                const FArdaProviderObjectRef&,
                const eastl::vector<FArdaProviderRayTracingInstance>&,
                EArdaRHIAccelStructBuildFlags) override;
            FArdaRHIStatus BuildTopLevelAccelStructFromBuffer(
                const FArdaProviderObjectRef&, const FArdaProviderObjectRef&,
                uint64_t, size_t, EArdaRHIAccelStructBuildFlags) override;
            FArdaRHIStatus CompactAccelStruct(
                const FArdaProviderObjectRef&,
                const FArdaProviderObjectRef&) override;
            void BeginMarker(const char*) override {}
            void EndMarker() override {}

            ID3D12CommandList* GetSubmitList() const noexcept { return mCommandList.Get(); }
            [[nodiscard]] FD3D12SubmissionLifetime
                CaptureSubmissionLifetime() const
            {
                FD3D12SubmissionLifetime Lifetime;
                Lifetime.mAllocator = mAllocator;
                Lifetime.mCommandList = mCommandList;
                Lifetime.mResources = mUploadResources;
                Lifetime.mCommandSignatures = mCommandSignatures;
                Lifetime.mObjects = mRetainedObjects;
                return Lifetime;
            }
            [[nodiscard]] FArdaRHIStatus ValidateTrackedStartStates() const;
            void CommitTrackedStates();

        private:
            struct FTextureTracking
            {
                eastl::vector<EArdaRHIResourceState> mAbstractStates;
                eastl::vector<D3D12_RESOURCE_STATES> mNativeStates;
                eastl::vector<EArdaRHIResourceState> mExpectedStartStates;
            };
            struct FBufferTracking
            {
                EArdaRHIResourceState mAbstractState =
                    EArdaRHIResourceState::Unknown;
                D3D12_RESOURCE_STATES mNativeState =
                    D3D12_RESOURCE_STATE_COMMON;
                bool mbKnown = false;
                EArdaRHIResourceState mExpectedStartState =
                    EArdaRHIResourceState::Unknown;
                bool mbExpectedStartState = false;
            };
            struct FAccelStructTracking
            {
                EArdaRHIResourceState mAbstractState =
                    EArdaRHIResourceState::AccelStructRead;
                EArdaRHIAccelStructBuildState mBuildState =
                    EArdaRHIAccelStructBuildState::Unbuilt;
            };
            void Retain(const FArdaProviderObjectRef& Object)
            {
                if (Object) mRetainedObjects.push_back(Object);
            }
            FArdaRHIStatus TransitionBuffer(
                const FArdaProviderObjectRef&,
                EArdaRHIResourceState);
            FArdaRHIStatus TransitionTexture(
                const FArdaProviderObjectRef&,
                const FArdaRHITextureDesc&,
                const FArdaRHITextureSubresourceRange&,
                EArdaRHIResourceState);
            FTextureTracking& GetTextureTracking(FD3D12Texture& Texture);
            FBufferTracking& GetBufferTracking(FD3D12Buffer& Buffer);
            FArdaRHIStatus BindDescriptorSets(const FD3D12Pipeline&, const eastl::vector<FArdaProviderObjectRef>&, bool);
            FArdaRHIStatus RecordAccelStructBuild(
                const FArdaProviderObjectRef&,
                const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS&,
                EArdaRHIAccelStructBuildFlags);
            FArdaD3D12ProviderDevice& mDevice;
            D3D12_COMMAND_LIST_TYPE mType = D3D12_COMMAND_LIST_TYPE_DIRECT;
            ComPtr<ID3D12CommandAllocator> mAllocator;
            ComPtr<ID3D12GraphicsCommandList> mCommandList;
            eastl::vector<ComPtr<ID3D12Resource>> mUploadResources;
            eastl::vector<ComPtr<ID3D12CommandSignature>> mCommandSignatures;
            eastl::vector<FArdaProviderObjectRef> mRetainedObjects;
            std::unordered_map<FD3D12Texture*, FTextureTracking>
                mTextureStates;
            std::unordered_map<FD3D12Buffer*, FBufferTracking>
                mBufferStates;
            std::unordered_map<FD3D12AccelStruct*, FAccelStructTracking>
                mAccelStructStates;
            FD3D12Pipeline* mBoundGraphicsPipeline = nullptr;
            FD3D12Pipeline* mBoundComputePipeline = nullptr;
            FD3D12ShaderTable* mBoundShaderTable = nullptr;
            bool mbOpen = false;
            bool mbAutomaticBarriers = true;
        };

        class FArdaD3D12ProviderDevice final : public IArdaRHIProviderDevice
        {
        public:
            FArdaD3D12ProviderDevice(
                ComPtr<ID3D12Device> Device,
                ComPtr<ID3D12CommandQueue> GraphicsQueue,
                ComPtr<ID3D12CommandQueue> ComputeQueue,
                ComPtr<ID3D12CommandQueue> CopyQueue,
                std::filesystem::path PipelineCacheDirectory,
                IArdaDiagnosticCallback* DiagnosticCallback,
                eastl::shared_ptr<void> LifetimeToken)
                : mD3DDevice(eastl::move(Device))
                , mQueue(eastl::move(GraphicsQueue))
                , mComputeQueue(eastl::move(ComputeQueue))
                , mCopyQueue(eastl::move(CopyQueue))
                , mPipelineCacheDirectory(eastl::move(PipelineCacheDirectory))
                , mDiagnosticCallback(DiagnosticCallback)
                , mLifetimeToken(eastl::move(LifetimeToken)) {}
            ~FArdaD3D12ProviderDevice() override;
            FArdaRHIStatus Initialize();
            const FArdaRHICapabilities& GetCapabilities() const noexcept override { return mCapabilities; }
            EArdaRHINativeResourceType GetTextureImportType() const noexcept override { return EArdaRHINativeResourceType::D3D12Resource; }
            EArdaRHINativeResourceType GetBufferImportType() const noexcept override { return EArdaRHINativeResourceType::D3D12Resource; }
            FArdaProviderObjectResult CreateTexture(const FArdaRHITextureDesc&) override;
            FArdaProviderObjectResult CreateSamplerFeedbackTexture(
                const FArdaProviderObjectRef&,
                const FArdaRHITextureDesc&,
                const FArdaRHISamplerFeedbackTextureDesc&) override;
            FArdaProviderObjectResult CreateBuffer(const FArdaRHIBufferDesc&) override;
            FArdaProviderObjectResult CreateHeap(const FArdaRHIHeapDesc&) override;
            TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(const FArdaProviderObjectRef&, const FArdaRHITextureDesc&) override;
            TArdaRHIResult<FArdaRHIMemoryRequirements> GetBufferMemoryRequirements(const FArdaProviderObjectRef&, const FArdaRHIBufferDesc&) override;
            FArdaRHIStatus BindTextureMemory(const FArdaProviderObjectRef&, const FArdaRHITextureDesc&, const FArdaProviderObjectRef&, uint64_t) override;
            FArdaRHIStatus BindBufferMemory(const FArdaProviderObjectRef&, const FArdaRHIBufferDesc&, const FArdaProviderObjectRef&, uint64_t) override;
            TArdaRHIResult<FArdaRHITextureTiling> GetTextureTiling(
                const FArdaProviderObjectRef&) override;
            FArdaRHIStatus UpdateTextureTileMappings(
                const FArdaProviderObjectRef&,
                const eastl::vector<FArdaProviderTextureTileMapping>&,
                EArdaRHIQueueType) override;
            FArdaRHIStatus UpdateBufferTileMappings(
                const FArdaProviderObjectRef&,
                const eastl::vector<FArdaProviderBufferTileMapping>&,
                EArdaRHIQueueType) override;
            FArdaRHIStatus CommitReservedResource(
                const FArdaProviderObjectRef&, bool, uint64_t,
                EArdaRHIQueueType) override;
            TArdaRHIResult<FArdaRHIStreamingBudget>
                QueryStreamingBudget(bool) const override;
            FArdaRHIStatus SetStreamingBudgetReservation(
                uint64_t, bool) override;
            FArdaProviderObjectResult CreateStagingTexture(const FArdaRHIStagingTextureDesc&) override;
            TArdaRHIResult<FArdaRHIStagingTextureMapping> MapStagingTexture(const FArdaProviderObjectRef&, const FArdaRHITextureSlice&, EArdaRHICpuAccess) override;
            FArdaRHIStatus UnmapStagingTexture(const FArdaProviderObjectRef&) override;
            TArdaRHIResult<void*> MapBuffer(
                const FArdaProviderObjectRef&, uint64_t, size_t) override;
            void UnmapBuffer(const FArdaProviderObjectRef&) noexcept override;
            FArdaProviderObjectResult ImportTexture(const FArdaRHINativeTextureImportDesc&) override;
            FArdaProviderObjectResult ImportBuffer(const FArdaRHINativeBufferImportDesc&) override;
            FArdaProviderObjectResult CreateSampler(const FArdaRHISamplerDesc&) override;
            FArdaProviderObjectResult CreateShader(const FArdaRHIShaderDesc&) override;
            FArdaProviderObjectResult CreateBindingLayout(const FArdaRHIBindingLayoutDesc&) override;
            FArdaProviderObjectResult CreateBindlessLayout(
                const FArdaRHIBindlessLayoutDesc&,
                const FArdaRHIBindingLayoutDesc&) override;
            FArdaProviderObjectResult CreateBindingSet(const FArdaRHIBindingSetDesc&, const FArdaProviderObjectRef&, const eastl::vector<FArdaProviderBinding>&) override;
            FArdaProviderObjectResult CreateFramebuffer(const FArdaProviderFramebufferCreateInfo&) override;
            FArdaProviderObjectResult CreateGraphicsPipeline(const FArdaProviderGraphicsPipelineCreateInfo&) override;
            FArdaProviderObjectResult CreateComputePipeline(const FArdaProviderComputePipelineCreateInfo&) override;
            FArdaProviderObjectResult CreateMeshletPipeline(const FArdaProviderMeshletPipelineCreateInfo&) override;
            FArdaProviderObjectResult CreateRayTracingPipeline(const FArdaProviderRayTracingPipelineCreateInfo&) override;
            FArdaProviderObjectResult CreateWorkGraphPipeline(
                const FArdaProviderWorkGraphPipelineCreateInfo&) override;
            FArdaProviderObjectResult CreateShaderTable(const FArdaProviderObjectRef&, const FArdaRHIShaderTableDesc&) override;
            TArdaRHIResult<FArdaRHIAccelStructMemoryRequirements>
                GetAccelStructBuildMemoryRequirements(
                    const FArdaRHIAccelStructDesc&,
                    const eastl::vector<FArdaProviderRayTracingGeometry>&) override;
            FArdaProviderObjectResult CreateAccelStruct(
                const FArdaRHIAccelStructDesc&,
                const FArdaRHIAccelStructMemoryRequirements&) override;
            TArdaRHIResult<uint64_t> GetAccelStructCompactedSize(
                const FArdaProviderObjectRef&) override;
            uint64_t GetAccelStructDeviceAddress(
                const FArdaProviderObjectRef&) const noexcept override;
            FArdaRHIStatus SetShaderTableRecord(
                const FArdaProviderObjectRef&,
                const FArdaRHIShaderTableRecordDesc&,
                const FArdaProviderObjectRef&,
                const FArdaProviderObjectRef&) override;
            FArdaRHIStatus CommitShaderTable(
                const FArdaProviderObjectRef&) override;
            FArdaRHIStatus SetShaderTableRayGeneration(const FArdaProviderObjectRef&, const char*, const FArdaProviderObjectRef&) override;
            FArdaRHIStatus AddShaderTableEntry(const FArdaProviderObjectRef&, const char*, const FArdaProviderObjectRef&, uint32_t) override;
            TArdaRHIResult<eastl::unique_ptr<IArdaProviderCommandList>> CreateCommandList(EArdaRHIQueueType, bool) override;
            TArdaRHIResult<uint64_t> ExecuteCommandList(IArdaProviderCommandList&, EArdaRHIQueueType) override;
            FArdaRHIStatus QueueWait(
                EArdaRHIQueueType, EArdaRHIQueueType, uint64_t) override;
            FArdaRHIStatus WaitForSubmission(uint64_t) override;
            FArdaRHIStatus WaitForIdle() override;
            void RunGarbageCollection() override;
            FArdaProviderLifetimeStats GetLifetimeStats() const noexcept override
            {
                FArdaProviderLifetimeStats Stats;
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
                const eastl::vector<FArdaProviderObjectRef>& Layouts,
                eastl::vector<uint32_t>& OutItemCounts,
                eastl::vector<int32_t>& OutPushConstantRoots,
                bool bLocal = false);
            [[nodiscard]] ID3D12CommandQueue* GetQueue(
                EArdaRHIQueueType Queue) const noexcept;
            void InitializePipelineCache();
            FArdaRHIStatus RebuildShaderTable(FD3D12ShaderTable&);

            FArdaRHICapabilities mCapabilities;
            ComPtr<ID3D12Device> mD3DDevice;
            ComPtr<ID3D12CommandQueue> mQueue;
            ComPtr<ID3D12CommandQueue> mComputeQueue;
            ComPtr<ID3D12CommandQueue> mCopyQueue;
            ComPtr<ID3D12DescriptorHeap> mResourceHeap;
            ComPtr<ID3D12DescriptorHeap> mSamplerHeap;
            struct FPendingSubmission
            {
                EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
                uint64_t mQueueValue = 0;
                FD3D12SubmissionLifetime mLifetime;
            };
            eastl::array<ComPtr<ID3D12Fence>,
                ArdaRHIQueueTypeCount> mQueueFences;
            eastl::array<std::atomic<uint64_t>,
                ArdaRHIQueueTypeCount> mQueueFenceValues{};
            ComPtr<IDXGIAdapter3> mDxgiAdapter;
            HANDLE mFenceEvent = nullptr;
            eastl::shared_ptr<void> mLifetimeToken;
            eastl::shared_ptr<FD3D12DescriptorAllocator> mDescriptorAllocator;
            uint32_t mResourceDescriptorSize = 0;
            uint32_t mSamplerDescriptorSize = 0;
            std::mutex mSubmissionMutex;
            eastl::vector<FPendingSubmission> mPendingSubmissions;
            ComPtr<ID3D12PipelineLibrary> mPipelineLibrary;
            std::vector<uint8_t> mPipelineCacheSource;
            std::filesystem::path mPipelineCacheDirectory;
            IArdaDiagnosticCallback* mDiagnosticCallback = nullptr;
            std::mutex mPipelineCacheMutex;
            bool mbPipelineCacheDirty = false;
        };

        FArdaD3D12ProviderDevice::~FArdaD3D12ProviderDevice()
        {
            (void)WaitForIdle();
            if (mFenceEvent)
                CloseHandle(mFenceEvent);
        }

        FArdaRHIStatus FArdaD3D12ProviderDevice::Initialize()
        {
            D3D12_DESCRIPTOR_HEAP_DESC HeapDesc{};
            HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            HeapDesc.NumDescriptors = D3D12ResourceDescriptorHeapCapacity;
            HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            HRESULT Result = mD3DDevice->CreateDescriptorHeap(
                &HeapDesc, IID_PPV_ARGS(&mResourceHeap));
            if (FAILED(Result)) return D3D12Failure("Failed to create the D3D12 resource descriptor heap.", Result);
            HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            HeapDesc.NumDescriptors = D3D12SamplerDescriptorHeapCapacity;
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
            for (auto& Fence : mQueueFences)
            {
                Result = mD3DDevice->CreateFence(
                    0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence));
                if (FAILED(Result))
                    return D3D12Failure(
                        "Failed to create a D3D12 queue fence.", Result);
            }
            mFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!mFenceEvent)
                return D3D12Failure("Failed to create the D3D12 queue fence event.", HRESULT_FROM_WIN32(GetLastError()));
            ComPtr<IDXGIFactory4> Factory;
            if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&Factory))))
            {
                const LUID DeviceLuid = mD3DDevice->GetAdapterLuid();
                for (UINT Index = 0;; ++Index)
                {
                    ComPtr<IDXGIAdapter1> Candidate;
                    if (Factory->EnumAdapters1(Index, &Candidate) ==
                        DXGI_ERROR_NOT_FOUND)
                        break;
                    DXGI_ADAPTER_DESC1 AdapterDesc{};
                    if (SUCCEEDED(Candidate->GetDesc1(&AdapterDesc)) &&
                        AdapterDesc.AdapterLuid.HighPart == DeviceLuid.HighPart &&
                        AdapterDesc.AdapterLuid.LowPart == DeviceLuid.LowPart)
                    {
                        (void)Candidate.As(&mDxgiAdapter);
                        break;
                    }
                }
            }
            mCapabilities.mQueues.mbGraphics = true;
            mCapabilities.mQueues.mbCompute = mComputeQueue != nullptr;
            mCapabilities.mQueues.mbCopy = mCopyQueue != nullptr;
            mCapabilities.mQueues.mbDedicatedComputeFamily =
                mComputeQueue != nullptr;
            mCapabilities.mQueues.mbDedicatedCopyFamily =
                mCopyQueue != nullptr;
            mCapabilities.mQueues.mbGpuWaits = true;
            mCapabilities.mbStagingTextures = true;
            mCapabilities.mbTextureCopies = true;
            mCapabilities.mbTextureResolve = true;
            mCapabilities.mbExplicitTransitions = true;
            mCapabilities.mbSplitTransitions = true;
            mCapabilities.mbIndirectCommands = true;
            mCapabilities.mbAliasingBarriers = true;
            mCapabilities.mbQueries = true;
            mCapabilities.mbVirtualResources = true;
            mCapabilities.mbHeaps = true;
            mCapabilities.mbResourceCollections = true;
            mCapabilities.mbCustomPresent = true;
            mCapabilities.mbShaderBundleDispatch = true;
            D3D12_FEATURE_DATA_D3D12_OPTIONS Options{};
            if (SUCCEEDED(mD3DDevice->CheckFeatureSupport(
                    D3D12_FEATURE_D3D12_OPTIONS,
                    &Options, sizeof(Options))))
            {
                auto& Residency = mCapabilities.mResidency;
                Residency.mbSparseBinding =
                    Options.TiledResourcesTier !=
                    D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED;
                Residency.mbReservedBuffers = Residency.mbSparseBinding;
                Residency.mbReservedTexture2D = Residency.mbSparseBinding;
                Residency.mbReservedTexture3D =
                    Options.TiledResourcesTier >=
                    D3D12_TILED_RESOURCES_TIER_3;
                Residency.mbAliasedMappings = Residency.mbSparseBinding;
                Residency.mTileSizeInBytes =
                    D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
                mCapabilities.mQueues.mbSparseBindingQueue =
                    Residency.mbSparseBinding;
            }
            mCapabilities.mResidency.mbStreamingBudget =
                mDxgiAdapter != nullptr;
            mCapabilities.mResidency.mbBudgetReservation =
                mDxgiAdapter != nullptr;
            mCapabilities.mDescriptors.mbBindless = true;
            // Half of the physical heap is the safe logical maximum so a
            // table can be versioned while its previous allocation remains
            // live for update-after-bind command lists.
            mCapabilities.mDescriptors.mMaxResourceDescriptors =
                mResourceHeap->GetDesc().NumDescriptors / 2;
            mCapabilities.mDescriptors.mMaxSamplerDescriptors =
                mSamplerHeap->GetDesc().NumDescriptors;
            mCapabilities.mDescriptors.mbRuntimeDescriptorArrays = true;
            mCapabilities.mDescriptors.mbUnboundedArrays = true;
            mCapabilities.mDescriptors.mbPartiallyBound = true;
            mCapabilities.mDescriptors.mbUpdateAfterBind = true;
            mCapabilities.mDescriptors.mbUpdateUnusedWhilePending = true;
            mCapabilities.mDescriptors.mbVariableDescriptorCount = true;
            D3D12_FEATURE_DATA_SHADER_MODEL ShaderModel{
                D3D_SHADER_MODEL_6_6};
            if (SUCCEEDED(mD3DDevice->CheckFeatureSupport(
                    D3D12_FEATURE_SHADER_MODEL, &ShaderModel,
                    sizeof(ShaderModel))))
            {
                mCapabilities.mDescriptors.mbDirectResourceHeapIndexing = true;
                mCapabilities.mDescriptors.mbDirectSamplerHeapIndexing = true;
            }
            mCapabilities.mbShaderLibraries = true;
            D3D12_FEATURE_DATA_D3D12_OPTIONS5 Options5{};
            const bool bRayTracing = SUCCEEDED(mD3DDevice->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5,
                &Options5,
                sizeof(Options5))) &&
                Options5.RaytracingTier !=
                    D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
            if (bRayTracing)
            {
                auto& Ray = mCapabilities.mRayTracing;
                Ray.mbInfrastructure = true;
                Ray.mbHardwareAccelerated = true;
                Ray.mbPipelineShaders = true;
                Ray.mbAccelerationStructures = true;
                Ray.mbBottomLevel = true;
                Ray.mbTopLevel = true;
                Ray.mbBuildUpdate = true;
                Ray.mbCompaction = true;
                Ray.mbIndirectDispatch =
                    Options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
                Ray.mbInlineRayQueries =
                    Options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1;
                // Do not advertise provider/hardware abilities that the
                // public RHI cannot execute. D3D12 opacity-micromap objects
                // and indirect TLAS builds have no facade implementation yet.
                Ray.mbOpacityMicromaps = false;
                Ray.mbIndirectTopLevelBuild = false;
                Ray.mbLocalShaderTableArguments = true;
                Ray.mbPersistentShaderTables = true;
                Ray.mShaderIdentifierSize =
                    D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
                Ray.mShaderRecordAlignment =
                    D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;
                Ray.mShaderTableAlignment =
                    D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
                Ray.mAccelerationStructureAlignment =
                    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
                Ray.mMaxRecursionDepth =
                    D3D12_RAYTRACING_MAX_DECLARABLE_TRACE_RECURSION_DEPTH;
                Ray.mMaxRayDispatchInvocations =
                    D3D12_RAYTRACING_MAX_RAY_GENERATION_SHADER_THREADS;
            }
            D3D12_FEATURE_DATA_D3D12_OPTIONS7 Options7{};
            const bool bMeshShaders = SUCCEEDED(mD3DDevice->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS7,
                &Options7,
                sizeof(Options7))) &&
                Options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
            mCapabilities.mMeshShaderTier = bMeshShaders
                ? EArdaRHIMeshShaderTier::MeshAndAmplificationShaders
                : EArdaRHIMeshShaderTier::None;
            mCapabilities.mSamplerFeedbackTier =
                Options7.SamplerFeedbackTier >=
                    D3D12_SAMPLER_FEEDBACK_TIER_1_0
                ? EArdaRHISamplerFeedbackTier::UnrestrictedAddressingAndViews
                : Options7.SamplerFeedbackTier >=
                        D3D12_SAMPLER_FEEDBACK_TIER_0_9
                    ? EArdaRHISamplerFeedbackTier::RestrictedAddressingAndViews
                    : EArdaRHISamplerFeedbackTier::None;
            D3D12_FEATURE_DATA_D3D12_OPTIONS21 Options21{};
            if (SUCCEEDED(mD3DDevice->CheckFeatureSupport(
                    D3D12_FEATURE_D3D12_OPTIONS21,
                    &Options21, sizeof(Options21))) &&
                Options21.WorkGraphsTier !=
                    D3D12_WORK_GRAPHS_TIER_NOT_SUPPORTED)
                mCapabilities.mWorkGraphTier =
                    EArdaRHIWorkGraphTier::ComputeNodes;
            D3D12_FEATURE_DATA_D3D12_OPTIONS1 Options1{};
            if (SUCCEEDED(mD3DDevice->CheckFeatureSupport(
                    D3D12_FEATURE_D3D12_OPTIONS1,
                    &Options1, sizeof(Options1))))
            {
                mCapabilities.mMachineLearning.mbSubgroupOperations =
                    Options1.WaveOps != FALSE;
                mCapabilities.mMachineLearning.mSubgroupMinSize =
                    Options1.WaveLaneCountMin;
                mCapabilities.mMachineLearning.mSubgroupMaxSize =
                    Options1.WaveLaneCountMax;
            }
            D3D12_FEATURE_DATA_D3D12_OPTIONS4 Options4{};
            if (SUCCEEDED(mD3DDevice->CheckFeatureSupport(
                    D3D12_FEATURE_D3D12_OPTIONS4,
                    &Options4, sizeof(Options4))))
            {
                mCapabilities.mMachineLearning.mbNativeFloat16 =
                    Options4.Native16BitShaderOpsSupported != FALSE;
            }
            mCapabilities.mMachineLearning.mbBufferDeviceAddress = true;
            InitializePipelineCache();
            mCapabilities.mbPipelineCachePersistence = mPipelineLibrary != nullptr;
            return {};
        }

        void FArdaD3D12ProviderDevice::InitializePipelineCache()
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
                Path, BackendName, mPipelineCacheSource);
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
        FArdaD3D12ProviderDevice::AllocateDescriptors(bool bSampler, uint32_t Count)
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

        FArdaRHIStatus FArdaD3D12ProviderDevice::CreateTextureViews(FD3D12Texture& Texture)
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

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::CreateTexture(
            const FArdaRHITextureDesc& Desc)
        {
            auto Texture = eastl::make_shared<FD3D12Texture>();
            Texture->mDesc = Desc;
            Texture->mAbstractStates.assign(
                D3D12TextureStateCount(Desc), Desc.mInitialState);
            Texture->mNativeStates.assign(
                D3D12TextureStateCount(Desc),
                ToD3D12State(Desc.mInitialState));
            D3D12_HEAP_PROPERTIES Heap{};
            Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            Heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            Heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            Heap.CreationNodeMask = 1;
            Heap.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC Resource = ToD3D12ResourceDesc(Desc);
            if (Desc.mbTiled)
                Resource.Layout =
                    D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
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
            if (Desc.mbVirtual && Desc.mbTiled)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "A D3D12 texture cannot be both virtual/placed and tiled/reserved."));
            if (Desc.mbTiled)
            {
                HRESULT Result = mD3DDevice->CreateReservedResource(
                    &Resource, ToD3D12State(Desc.mInitialState), ClearPtr,
                    IID_PPV_ARGS(&Texture->mResource));
                if (FAILED(Result))
                    return Fail<FArdaProviderObjectRef>(D3D12Failure(
                        "Failed to create a D3D12 reserved texture.", Result));
                if (auto Status = CreateTextureViews(*Texture); !Status)
                    return Fail<FArdaProviderObjectRef>(eastl::move(Status));
                return {Texture, {}};
            }
            if (Desc.mbVirtual)
                return { Texture, {} };
            HRESULT Result = mD3DDevice->CreateCommittedResource(
                &Heap, D3D12_HEAP_FLAG_NONE, &Resource,
                ToD3D12State(Desc.mInitialState), ClearPtr,
                IID_PPV_ARGS(&Texture->mResource));
            if (FAILED(Result)) return Fail<FArdaProviderObjectRef>(
                D3D12Failure("Failed to create a D3D12 texture.", Result));
            if (auto Status = CreateTextureViews(*Texture); !Status)
                return Fail<FArdaProviderObjectRef>(eastl::move(Status));
            return { Texture, {} };
        }

        FArdaProviderObjectResult
        FArdaD3D12ProviderDevice::CreateSamplerFeedbackTexture(
            const FArdaProviderObjectRef& PairedObject,
            const FArdaRHITextureDesc& PairedDesc,
            const FArdaRHISamplerFeedbackTextureDesc& Desc)
        {
            if (mCapabilities.mSamplerFeedbackTier ==
                EArdaRHISamplerFeedbackTier::None)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "D3D12 sampler feedback is unsupported by this device."));
            auto* Paired = dynamic_cast<FD3D12Texture*>(
                PairedObject.get());
            if (!Paired || !Paired->mResource)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "The D3D12 paired sampler-feedback texture is invalid."));
            ComPtr<ID3D12Device8> Device8;
            HRESULT Result = mD3DDevice.As(&Device8);
            if (FAILED(Result))
                return Fail<FArdaProviderObjectRef>(D3D12Failure(
                    "The D3D12 device does not expose sampler-feedback APIs.",
                    Result));

            auto Feedback =
                eastl::make_shared<FD3D12SamplerFeedbackTexture>();
            Feedback->mFeedbackDesc = Desc;
            Feedback->mPairedTexture = PairedObject;
            Feedback->mAllocator = mDescriptorAllocator;
            Feedback->mDesc.mWidth = PairedDesc.mWidth;
            Feedback->mDesc.mHeight = PairedDesc.mHeight;
            Feedback->mDesc.mDepth = 1;
            Feedback->mDesc.mArraySize = PairedDesc.mArraySize;
            Feedback->mDesc.mMipLevels = PairedDesc.mMipLevels;
            Feedback->mDesc.mSampleCount = 1;
            Feedback->mDesc.mDimension =
                EArdaRHITextureDimension::Texture2D;
            Feedback->mDesc.mUsage = EArdaRHITextureUsage::UnorderedAccess;
            Feedback->mDesc.mInitialState = Desc.mInitialState;
            Feedback->mDesc.mbKeepInitialState = Desc.mbKeepInitialState;
            Feedback->mDesc.mDebugName = Desc.mDebugName;

            D3D12_RESOURCE_DESC1 Resource{};
            Resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            Resource.Width = Feedback->mDesc.mWidth;
            Resource.Height = Feedback->mDesc.mHeight;
            Resource.DepthOrArraySize = static_cast<UINT16>(
                Feedback->mDesc.mArraySize);
            Resource.MipLevels = static_cast<UINT16>(
                Feedback->mDesc.mMipLevels);
            Resource.Format = Desc.mFormat ==
                    EArdaRHISamplerFeedbackFormat::MinMipOpaque
                ? DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE
                : DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE;
            Resource.SampleDesc.Count = 1;
            Resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            Resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            Resource.SamplerFeedbackMipRegion = {
                Desc.mMipRegionX, Desc.mMipRegionY, Desc.mMipRegionZ};
            D3D12_HEAP_PROPERTIES Heap{};
            Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            Heap.CreationNodeMask = 1;
            Heap.VisibleNodeMask = 1;
            Result = Device8->CreateCommittedResource2(
                &Heap, D3D12_HEAP_FLAG_NONE, &Resource,
                ToD3D12State(Desc.mInitialState), nullptr, nullptr,
                IID_PPV_ARGS(&Feedback->mResource));
            if (FAILED(Result))
                return Fail<FArdaProviderObjectRef>(D3D12Failure(
                    "Failed to create a D3D12 sampler-feedback resource.",
                    Result));

            auto Descriptor = AllocateDescriptors(false, 1);
            if (!Descriptor)
                return Fail<FArdaProviderObjectRef>(
                    eastl::move(Descriptor.mStatus));
            Feedback->mDescriptor = Descriptor.mValue;
            D3D12_DESCRIPTOR_HEAP_DESC CpuHeapDesc{};
            CpuHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            CpuHeapDesc.NumDescriptors = 1;
            Result = mD3DDevice->CreateDescriptorHeap(
                &CpuHeapDesc,
                IID_PPV_ARGS(&Feedback->mCpuDescriptorHeap));
            if (FAILED(Result))
                return Fail<FArdaProviderObjectRef>(D3D12Failure(
                    "Failed to create the D3D12 sampler-feedback CPU descriptor heap.",
                    Result));
            Feedback->mCpuDescriptor = Feedback->mCpuDescriptorHeap->
                GetCPUDescriptorHandleForHeapStart();
            ComPtr<ID3D12Device15> Device15;
            if (SUCCEEDED(mD3DDevice.As(&Device15)))
            {
                Result = Device15->TryCreateSamplerFeedbackUnorderedAccessView(
                    nullptr, Feedback->mResource.Get(),
                    Feedback->mCpuDescriptor);
                if (FAILED(Result))
                    return Fail<FArdaProviderObjectRef>(D3D12Failure(
                        "The D3D12 sampler-feedback resource cannot form a null feedback UAV.",
                        Result));
                Result = Device15->TryCreateSamplerFeedbackUnorderedAccessView(
                    Paired->mResource.Get(), Feedback->mResource.Get(),
                    Feedback->mCpuDescriptor);
                if (FAILED(Result))
                    return Fail<FArdaProviderObjectRef>(D3D12FailureWithInfo(
                        mD3DDevice.Get(),
                        "Failed to create the D3D12 sampler-feedback UAV.",
                        Result));
            }
            else
            {
                Device8->CreateSamplerFeedbackUnorderedAccessView(
                    Paired->mResource.Get(), Feedback->mResource.Get(),
                    Feedback->mCpuDescriptor);
                Result = mD3DDevice->GetDeviceRemovedReason();
                if (FAILED(Result))
                    return Fail<FArdaProviderObjectRef>(D3D12Failure(
                        "D3D12 rejected the sampler-feedback UAV.", Result));
            }
            mD3DDevice->CopyDescriptorsSimple(
                1, Feedback->mDescriptor.mCpu,
                Feedback->mCpuDescriptor,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            Feedback->mAbstractStates.assign(
                Feedback->mDesc.mMipLevels *
                    Feedback->mDesc.mArraySize,
                Desc.mInitialState);
            Feedback->mNativeStates.assign(
                Feedback->mDesc.mMipLevels *
                    Feedback->mDesc.mArraySize,
                ToD3D12State(Desc.mInitialState));
            return {Feedback, {}};
        }

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::CreateBuffer(
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
            const D3D12_RESOURCE_DESC Resource = ToD3D12ResourceDesc(Desc);
            D3D12_RESOURCE_STATES State = ToD3D12State(Desc.mInitialState);
            if (Heap.Type == D3D12_HEAP_TYPE_UPLOAD) State = D3D12_RESOURCE_STATE_GENERIC_READ;
            if (Heap.Type == D3D12_HEAP_TYPE_READBACK) State = D3D12_RESOURCE_STATE_COPY_DEST;
            Buffer->mAbstractState = Desc.mInitialState;
            Buffer->mNativeState = State;
            Buffer->mbStateKnown = true;
            if (Desc.mbVirtual && Desc.mbTiled)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "A D3D12 buffer cannot be both virtual/placed and tiled/reserved."));
            if (Desc.mbTiled)
            {
                if (Desc.mCpuAccess != EArdaRHICpuAccess::None)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidArgument,
                        "A D3D12 reserved buffer cannot be CPU visible."));
                HRESULT Result = mD3DDevice->CreateReservedResource(
                    &Resource, State, nullptr,
                    IID_PPV_ARGS(&Buffer->mResource));
                if (FAILED(Result))
                    return Fail<FArdaProviderObjectRef>(D3D12Failure(
                        "Failed to create a D3D12 reserved buffer.", Result));
                return {Buffer, {}};
            }
            if (Desc.mbVirtual)
                return { Buffer, {} };
            HRESULT Result = mD3DDevice->CreateCommittedResource(
                &Heap, D3D12_HEAP_FLAG_NONE, &Resource, State, nullptr,
                IID_PPV_ARGS(&Buffer->mResource));
            if (FAILED(Result)) return Fail<FArdaProviderObjectRef>(
                D3D12Failure("Failed to create a D3D12 buffer.", Result));
            return { Buffer, {} };
        }

        TArdaRHIResult<FArdaRHIAccelStructMemoryRequirements>
        FArdaD3D12ProviderDevice::GetAccelStructBuildMemoryRequirements(
            const FArdaRHIAccelStructDesc& Desc,
            const eastl::vector<FArdaProviderRayTracingGeometry>& Geometries)
        {
            ComPtr<ID3D12Device5> Device5;
            if (FAILED(mD3DDevice.As(&Device5)))
                return Fail<FArdaRHIAccelStructMemoryRequirements>(
                    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                        "The D3D12 device does not expose DXR."));
            eastl::vector<D3D12_RAYTRACING_GEOMETRY_DESC> NativeGeometries;
            NativeGeometries.reserve(Geometries.size());
            for (const auto& Geometry : Geometries)
            {
                D3D12_RAYTRACING_GEOMETRY_DESC Native{};
                Native.Flags = ToD3D12GeometryFlags(Geometry.mDesc.mFlags);
                auto* Vertex = dynamic_cast<FD3D12Buffer*>(
                    Geometry.mVertexOrAABBBuffer.get());
                if (!Vertex || !Vertex->mResource)
                    return Fail<FArdaRHIAccelStructMemoryRequirements>(
                        FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                            "A D3D12 BLAS geometry buffer is invalid."));
                if (Geometry.mDesc.mType ==
                    EArdaRHIRayTracingGeometryType::Triangles)
                {
                    Native.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
                    Native.Triangles.VertexBuffer.StartAddress =
                        Vertex->mResource->GetGPUVirtualAddress() +
                        Geometry.mDesc.mVertexOrAABBOffset;
                    Native.Triangles.VertexBuffer.StrideInBytes =
                        Geometry.mDesc.mStride;
                    Native.Triangles.VertexCount =
                        Geometry.mDesc.mVertexOrAABBCount;
                    Native.Triangles.VertexFormat = ToDxgi(
                        Geometry.mDesc.mVertexFormat);
                    if (Geometry.mIndexBuffer)
                    {
                        auto* Index = dynamic_cast<FD3D12Buffer*>(
                            Geometry.mIndexBuffer.get());
                        if (!Index || !Index->mResource)
                            return Fail<FArdaRHIAccelStructMemoryRequirements>(
                                FArdaRHIStatus::Error(
                                    EArdaRHIResult::WrongDevice,
                                    "A D3D12 BLAS index buffer is invalid."));
                        Native.Triangles.IndexBuffer =
                            Index->mResource->GetGPUVirtualAddress() +
                            Geometry.mDesc.mIndexOffset;
                        Native.Triangles.IndexCount =
                            Geometry.mDesc.mIndexCount;
                        Native.Triangles.IndexFormat = ToDxgi(
                            Geometry.mDesc.mIndexFormat);
                    }
                }
                else
                {
                    Native.Type =
                        D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
                    Native.AABBs.AABBCount =
                        Geometry.mDesc.mVertexOrAABBCount;
                    Native.AABBs.AABBs.StartAddress =
                        Vertex->mResource->GetGPUVirtualAddress() +
                        Geometry.mDesc.mVertexOrAABBOffset;
                    Native.AABBs.AABBs.StrideInBytes =
                        Geometry.mDesc.mStride;
                }
                NativeGeometries.push_back(Native);
            }
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS Inputs{};
            Inputs.Type = Desc.mbTopLevel
                ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL
                : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            Inputs.Flags = ToD3D12BuildFlags(Desc.mBuildFlags);
            Inputs.DescsLayout =
                D3D12_ELEMENTS_LAYOUT_ARRAY;
            Inputs.NumDescs = Desc.mbTopLevel
                ? static_cast<UINT>(Desc.mTopLevelMaxInstances)
                : static_cast<UINT>(NativeGeometries.size());
            Inputs.pGeometryDescs = Desc.mbTopLevel
                ? nullptr : NativeGeometries.data();
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO Info{};
            Device5->GetRaytracingAccelerationStructurePrebuildInfo(
                &Inputs, &Info);
            if (!Info.ResultDataMaxSizeInBytes)
                return Fail<FArdaRHIAccelStructMemoryRequirements>(
                    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                        "D3D12 rejected the acceleration-structure build description."));
            FArdaRHIAccelStructMemoryRequirements Result;
            Result.mResultSize = Desc.mResultSizeOverride
                ? Desc.mResultSizeOverride
                : Info.ResultDataMaxSizeInBytes;
            Result.mBuildScratchSize = Info.ScratchDataSizeInBytes;
            Result.mUpdateScratchSize = Info.UpdateScratchDataSizeInBytes;
            Result.mResultAlignment =
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
            Result.mScratchAlignment =
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
            return {Result, {}};
        }

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::CreateAccelStruct(
            const FArdaRHIAccelStructDesc& Desc,
            const FArdaRHIAccelStructMemoryRequirements& Requirements)
        {
            auto AccelStruct = eastl::make_shared<FD3D12AccelStruct>();
            AccelStruct->mDesc = Desc;
            AccelStruct->mRequirements = Requirements;
            if (Desc.mbVirtual)
                return {AccelStruct, {}};
            auto Resource = CreateD3D12BufferResource(
                mD3DDevice.Get(), Requirements.mResultSize,
                D3D12_HEAP_TYPE_DEFAULT,
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            if (!Resource)
                return Fail<FArdaProviderObjectRef>(
                    eastl::move(Resource.mStatus));
            AccelStruct->mResource = eastl::move(Resource.mValue);
            if (HasAnyFlags(Desc.mBuildFlags,
                    EArdaRHIAccelStructBuildFlags::AllowCompaction))
            {
                auto Gpu = CreateD3D12BufferResource(
                    mD3DDevice.Get(), sizeof(uint64_t),
                    D3D12_HEAP_TYPE_DEFAULT,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
                if (!Gpu) return Fail<FArdaProviderObjectRef>(Gpu.mStatus);
                auto Readback = CreateD3D12BufferResource(
                    mD3DDevice.Get(), sizeof(uint64_t),
                    D3D12_HEAP_TYPE_READBACK,
                    D3D12_RESOURCE_STATE_COPY_DEST);
                if (!Readback)
                    return Fail<FArdaProviderObjectRef>(Readback.mStatus);
                AccelStruct->mCompactedSizeGpu = eastl::move(Gpu.mValue);
                AccelStruct->mCompactedSizeReadback =
                    eastl::move(Readback.mValue);
            }
            return {AccelStruct, {}};
        }

        uint64_t FArdaD3D12ProviderDevice::GetAccelStructDeviceAddress(
            const FArdaProviderObjectRef& Object) const noexcept
        {
            auto* AccelStruct = dynamic_cast<FD3D12AccelStruct*>(Object.get());
            return AccelStruct && AccelStruct->mResource
                ? AccelStruct->mResource->GetGPUVirtualAddress() : 0;
        }

        TArdaRHIResult<uint64_t>
        FArdaD3D12ProviderDevice::GetAccelStructCompactedSize(
            const FArdaProviderObjectRef& Object)
        {
            auto* AccelStruct = dynamic_cast<FD3D12AccelStruct*>(Object.get());
            if (!AccelStruct || !AccelStruct->mCompactedSizeReadback)
                return Fail<uint64_t>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "The D3D12 acceleration structure has no compacted-size query."));
            if (auto Status = WaitForIdle(); !Status)
                return Fail<uint64_t>(Status);
            void* Mapped = nullptr;
            D3D12_RANGE Range{0, sizeof(uint64_t)};
            const HRESULT Result = AccelStruct->mCompactedSizeReadback->Map(
                0, &Range, &Mapped);
            if (FAILED(Result))
                return Fail<uint64_t>(D3D12Failure(
                    "Failed to map D3D12 compacted-size readback.", Result));
            const uint64_t Size = *static_cast<const uint64_t*>(Mapped);
            D3D12_RANGE Written{0, 0};
            AccelStruct->mCompactedSizeReadback->Unmap(0, &Written);
            if (!Size)
                return Fail<uint64_t>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "The D3D12 compacted-size query has not completed."));
            return {Size, {}};
        }

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::CreateHeap(
            const FArdaRHIHeapDesc& Desc)
        {
            auto Heap = eastl::make_shared<FD3D12Heap>();
            Heap->mDesc = Desc;
            D3D12_HEAP_DESC NativeDesc{};
            NativeDesc.SizeInBytes = Desc.mCapacity;
            NativeDesc.Properties.Type =
                Desc.mType == EArdaRHIHeapType::Upload
                    ? D3D12_HEAP_TYPE_UPLOAD
                    : (Desc.mType == EArdaRHIHeapType::Readback
                        ? D3D12_HEAP_TYPE_READBACK
                        : D3D12_HEAP_TYPE_DEFAULT);
            NativeDesc.Properties.CreationNodeMask = 1;
            NativeDesc.Properties.VisibleNodeMask = 1;
            NativeDesc.Flags = Desc.mType == EArdaRHIHeapType::DeviceLocal
                ? D3D12_HEAP_FLAG_NONE
                : D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
            const HRESULT Result = mD3DDevice->CreateHeap(
                &NativeDesc, IID_PPV_ARGS(&Heap->mHeap));
            if (FAILED(Result))
                return Fail<FArdaProviderObjectRef>(D3D12Failure(
                    "Failed to create a D3D12 explicit resource heap.", Result));
            return { Heap, {} };
        }

        TArdaRHIResult<FArdaRHIMemoryRequirements>
        FArdaD3D12ProviderDevice::GetTextureMemoryRequirements(
            const FArdaProviderObjectRef& Object,
            const FArdaRHITextureDesc& Desc)
        {
            if (!dynamic_cast<FD3D12Texture*>(Object.get()))
                return Fail<FArdaRHIMemoryRequirements>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 texture memory requirements have the wrong resource type."));
            const D3D12_RESOURCE_DESC Resource = ToD3D12ResourceDesc(Desc);
            const D3D12_RESOURCE_ALLOCATION_INFO Info =
                mD3DDevice->GetResourceAllocationInfo(0, 1, &Resource);
            if (!Info.SizeInBytes || Info.SizeInBytes == UINT64_MAX)
                return Fail<FArdaRHIMemoryRequirements>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure,
                    "D3D12 could not determine texture allocation requirements."));
            return {{Info.SizeInBytes, Info.Alignment, 0xffffffffu}, {}};
        }

        TArdaRHIResult<FArdaRHIMemoryRequirements>
        FArdaD3D12ProviderDevice::GetBufferMemoryRequirements(
            const FArdaProviderObjectRef& Object,
            const FArdaRHIBufferDesc& Desc)
        {
            if (!dynamic_cast<FD3D12Buffer*>(Object.get()))
                return Fail<FArdaRHIMemoryRequirements>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 buffer memory requirements have the wrong resource type."));
            const D3D12_RESOURCE_DESC Resource = ToD3D12ResourceDesc(Desc);
            const D3D12_RESOURCE_ALLOCATION_INFO Info =
                mD3DDevice->GetResourceAllocationInfo(0, 1, &Resource);
            if (!Info.SizeInBytes || Info.SizeInBytes == UINT64_MAX)
                return Fail<FArdaRHIMemoryRequirements>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure,
                    "D3D12 could not determine buffer allocation requirements."));
            return {{Info.SizeInBytes, Info.Alignment, 0xffffffffu}, {}};
        }

        FArdaRHIStatus FArdaD3D12ProviderDevice::BindTextureMemory(
            const FArdaProviderObjectRef& Object,
            const FArdaRHITextureDesc& Desc,
            const FArdaProviderObjectRef& HeapObject,
            uint64_t Offset)
        {
            auto* Texture = dynamic_cast<FD3D12Texture*>(Object.get());
            auto* Heap = dynamic_cast<FD3D12Heap*>(HeapObject.get());
            if (!Texture || !Heap || !Heap->mHeap)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 texture heap binding has the wrong resource type.");
            if (Heap->mDesc.mType != EArdaRHIHeapType::DeviceLocal)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "D3D12 textures require a device-local explicit heap.");
            std::lock_guard<std::mutex> Lock(Texture->mStateMutex);
            if (Texture->mResource)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "D3D12 virtual texture memory is already bound.");
            const D3D12_RESOURCE_DESC Resource = ToD3D12ResourceDesc(Desc);
            D3D12_CLEAR_VALUE Clear{};
            D3D12_CLEAR_VALUE* ClearPtr = nullptr;
            if (Desc.mbUseClearValue ||
                HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::DepthStencil))
            {
                Clear.Format = Resource.Format;
                if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::DepthStencil))
                {
                    Clear.DepthStencil.Depth = Desc.mbUseClearValue
                        ? Desc.mClearValue.mR : 1.f;
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
            const HRESULT Result = mD3DDevice->CreatePlacedResource(
                Heap->mHeap.Get(),
                Offset,
                &Resource,
                ToD3D12State(Desc.mInitialState),
                ClearPtr,
                IID_PPV_ARGS(&Texture->mResource));
            if (FAILED(Result))
                return D3D12Failure(
                    "Failed to bind a D3D12 placed texture.", Result);
            Texture->mHeap = HeapObject;
            if (auto Status = CreateTextureViews(*Texture); !Status)
            {
                Texture->mResource.Reset();
                Texture->mHeap.reset();
                return Status;
            }
            return {};
        }

        FArdaRHIStatus FArdaD3D12ProviderDevice::BindBufferMemory(
            const FArdaProviderObjectRef& Object,
            const FArdaRHIBufferDesc& Desc,
            const FArdaProviderObjectRef& HeapObject,
            uint64_t Offset)
        {
            auto* Buffer = dynamic_cast<FD3D12Buffer*>(Object.get());
            auto* Heap = dynamic_cast<FD3D12Heap*>(HeapObject.get());
            if (!Buffer || !Heap || !Heap->mHeap)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 buffer heap binding has the wrong resource type.");
            std::lock_guard<std::mutex> Lock(Buffer->mStateMutex);
            if (Buffer->mResource)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "D3D12 virtual buffer memory is already bound.");
            const D3D12_RESOURCE_DESC Resource = ToD3D12ResourceDesc(Desc);
            const D3D12_RESOURCE_STATES InitialState =
                Heap->mDesc.mType == EArdaRHIHeapType::Upload
                    ? D3D12_RESOURCE_STATE_GENERIC_READ
                    : (Heap->mDesc.mType == EArdaRHIHeapType::Readback
                        ? D3D12_RESOURCE_STATE_COPY_DEST
                        : ToD3D12State(Desc.mInitialState));
            const HRESULT Result = mD3DDevice->CreatePlacedResource(
                Heap->mHeap.Get(),
                Offset,
                &Resource,
                InitialState,
                nullptr,
                IID_PPV_ARGS(&Buffer->mResource));
            if (FAILED(Result))
                return D3D12Failure(
                    "Failed to bind a D3D12 placed buffer.", Result);
            Buffer->mNativeState = InitialState;
            Buffer->mHeap = HeapObject;
            return {};
        }

        TArdaRHIResult<FArdaRHITextureTiling>
        FArdaD3D12ProviderDevice::GetTextureTiling(
            const FArdaProviderObjectRef& Object)
        {
            auto* Texture = dynamic_cast<FD3D12Texture*>(Object.get());
            if (!Texture || !Texture->mResource || !Texture->mDesc.mbTiled)
                return Fail<FArdaRHITextureTiling>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 texture tiling requires a reserved texture."));
            UINT TileCount = 0;
            D3D12_PACKED_MIP_INFO Packed{};
            D3D12_TILE_SHAPE Shape{};
            UINT SubresourceCount = static_cast<UINT>(
                Texture->mDesc.mMipLevels * Texture->mDesc.mArraySize *
                GetArdaRHIFormatPlaneCount(Texture->mDesc.mFormat));
            eastl::vector<D3D12_SUBRESOURCE_TILING> Native(
                SubresourceCount);
            UINT FirstSubresource = 0;
            mD3DDevice->GetResourceTiling(
                Texture->mResource.Get(), &TileCount, &Packed, &Shape,
                &SubresourceCount, FirstSubresource, Native.data());
            FArdaRHITextureTiling Result;
            Result.mTileCount = TileCount;
            Result.mPackedMips = {
                Packed.NumStandardMips,
                Packed.NumPackedMips,
                Packed.NumTilesForPackedMips,
                Packed.StartTileIndexInOverallResource};
            Result.mTileShape = {
                Shape.WidthInTexels,
                Shape.HeightInTexels,
                Shape.DepthInTexels};
            Result.mSubresources.reserve(SubresourceCount);
            for (UINT Index = 0; Index < SubresourceCount; ++Index)
            {
                const auto& Entry = Native[Index];
                Result.mSubresources.push_back({
                    Entry.WidthInTiles,
                    Entry.HeightInTiles,
                    Entry.DepthInTiles,
                    Entry.StartTileIndexInOverallResource});
            }
            return {eastl::move(Result), {}};
        }

        FArdaRHIStatus FArdaD3D12ProviderDevice::UpdateTextureTileMappings(
            const FArdaProviderObjectRef& Object,
            const eastl::vector<FArdaProviderTextureTileMapping>& Mappings,
            EArdaRHIQueueType QueueType)
        {
            auto* Texture = dynamic_cast<FD3D12Texture*>(Object.get());
            ID3D12CommandQueue* Queue = GetQueue(QueueType);
            if (!Texture || !Texture->mResource || !Texture->mDesc.mbTiled ||
                !Queue)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "D3D12 texture tile mapping has an invalid resource or queue.");
            constexpr uint64_t TileBytes =
                D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            for (const auto& Mapping : Mappings)
            {
                auto* Heap = Mapping.mHeap
                    ? dynamic_cast<FD3D12Heap*>(Mapping.mHeap.get())
                    : nullptr;
                if (Mapping.mHeap && (!Heap || !Heap->mHeap))
                    return FArdaRHIStatus::Error(
                        EArdaRHIResult::WrongDevice,
                        "D3D12 texture tile mapping has an invalid heap.");
                eastl::vector<D3D12_TILED_RESOURCE_COORDINATE> Coordinates;
                eastl::vector<D3D12_TILE_REGION_SIZE> Regions;
                eastl::vector<D3D12_TILE_RANGE_FLAGS> RangeFlags;
                eastl::vector<UINT> RangeOffsets;
                eastl::vector<UINT> RangeCounts;
                Coordinates.reserve(Mapping.mCoordinates.size());
                Regions.reserve(Mapping.mRegions.size());
                for (size_t Index = 0;
                     Index < Mapping.mCoordinates.size(); ++Index)
                {
                    if (Mapping.mByteOffsets[Index] % TileBytes)
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument,
                            "D3D12 tile heap offsets must be 64 KiB aligned.");
                    const auto& SourceCoordinate =
                        Mapping.mCoordinates[Index];
                    const auto& SourceRegion = Mapping.mRegions[Index];
                    D3D12_TILED_RESOURCE_COORDINATE Coordinate{};
                    Coordinate.X = SourceCoordinate.mX;
                    Coordinate.Y = SourceCoordinate.mY;
                    Coordinate.Z = SourceCoordinate.mZ;
                    Coordinate.Subresource = SourceCoordinate.mMipLevel +
                        SourceCoordinate.mArrayLevel *
                            Texture->mDesc.mMipLevels;
                    Coordinates.push_back(Coordinate);
                    D3D12_TILE_REGION_SIZE Region{};
                    Region.UseBox = SourceRegion.mWidth &&
                        SourceRegion.mHeight && SourceRegion.mDepth;
                    Region.Width = eastl::max(1u, SourceRegion.mWidth);
                    Region.Height = static_cast<UINT16>(
                        eastl::max(1u, SourceRegion.mHeight));
                    Region.Depth = static_cast<UINT16>(
                        eastl::max(1u, SourceRegion.mDepth));
                    Region.NumTiles = SourceRegion.mTileCount
                        ? SourceRegion.mTileCount
                        : Region.Width * Region.Height * Region.Depth;
                    Regions.push_back(Region);
                    RangeFlags.push_back(Heap
                        ? D3D12_TILE_RANGE_FLAG_NONE
                        : D3D12_TILE_RANGE_FLAG_NULL);
                    RangeOffsets.push_back(static_cast<UINT>(
                        Mapping.mByteOffsets[Index] / TileBytes));
                    RangeCounts.push_back(Region.NumTiles);
                }
                Queue->UpdateTileMappings(
                    Texture->mResource.Get(),
                    static_cast<UINT>(Coordinates.size()),
                    Coordinates.data(), Regions.data(),
                    Heap ? Heap->mHeap.Get() : nullptr,
                    static_cast<UINT>(RangeFlags.size()),
                    RangeFlags.data(), RangeOffsets.data(),
                    RangeCounts.data(), D3D12_TILE_MAPPING_FLAG_NONE);
                if (Mapping.mHeap)
                    Texture->mSparseHeaps.push_back(Mapping.mHeap);
            }
            return WaitForIdle();
        }

        FArdaRHIStatus FArdaD3D12ProviderDevice::UpdateBufferTileMappings(
            const FArdaProviderObjectRef& Object,
            const eastl::vector<FArdaProviderBufferTileMapping>& Mappings,
            EArdaRHIQueueType QueueType)
        {
            auto* Buffer = dynamic_cast<FD3D12Buffer*>(Object.get());
            ID3D12CommandQueue* Queue = GetQueue(QueueType);
            if (!Buffer || !Buffer->mResource || !Buffer->mDesc.mbTiled ||
                !Queue)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "D3D12 buffer tile mapping has an invalid resource or queue.");
            constexpr uint64_t TileBytes =
                D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            for (const auto& Mapping : Mappings)
            {
                if (!Mapping.mByteSize || Mapping.mBufferOffset % TileBytes ||
                    Mapping.mByteSize % TileBytes ||
                    Mapping.mHeapOffset % TileBytes)
                    return FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidArgument,
                        "D3D12 buffer tile mappings must use 64 KiB-aligned non-empty ranges.");
                auto* Heap = Mapping.mHeap
                    ? dynamic_cast<FD3D12Heap*>(Mapping.mHeap.get())
                    : nullptr;
                if (Mapping.mbCommit && (!Heap || !Heap->mHeap))
                    return FArdaRHIStatus::Error(
                        EArdaRHIResult::WrongDevice,
                        "D3D12 committed buffer tiles require a valid heap.");
                D3D12_TILED_RESOURCE_COORDINATE Coordinate{};
                Coordinate.X = static_cast<UINT>(
                    Mapping.mBufferOffset / TileBytes);
                D3D12_TILE_REGION_SIZE Region{};
                Region.NumTiles = static_cast<UINT>(
                    Mapping.mByteSize / TileBytes);
                D3D12_TILE_RANGE_FLAGS Flag = Mapping.mbCommit
                    ? D3D12_TILE_RANGE_FLAG_NONE
                    : D3D12_TILE_RANGE_FLAG_NULL;
                const UINT HeapOffset = static_cast<UINT>(
                    Mapping.mHeapOffset / TileBytes);
                Queue->UpdateTileMappings(
                    Buffer->mResource.Get(), 1, &Coordinate, &Region,
                    Mapping.mbCommit ? Heap->mHeap.Get() : nullptr,
                    1, &Flag, &HeapOffset, &Region.NumTiles,
                    D3D12_TILE_MAPPING_FLAG_NONE);
                if (Mapping.mbCommit)
                    Buffer->mSparseHeaps.push_back(Mapping.mHeap);
            }
            return WaitForIdle();
        }

        FArdaRHIStatus FArdaD3D12ProviderDevice::CommitReservedResource(
            const FArdaProviderObjectRef& Object,
            bool bTexture,
            uint64_t RequestedBytes,
            EArdaRHIQueueType QueueType)
        {
            ID3D12Resource* Resource = nullptr;
            ComPtr<ID3D12Heap>* CommitHeap = nullptr;
            uint64_t* CommittedBytes = nullptr;
            bool bBuffer = false;
            if (bTexture)
            {
                auto* Texture = dynamic_cast<FD3D12Texture*>(Object.get());
                if (!Texture || !Texture->mDesc.mbTiled)
                    return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                        "D3D12 reserved commit requires a tiled texture.");
                Resource = Texture->mResource.Get();
                CommitHeap = &Texture->mReservedCommitHeap;
                CommittedBytes = &Texture->mCommittedBytes;
            }
            else
            {
                auto* Buffer = dynamic_cast<FD3D12Buffer*>(Object.get());
                if (!Buffer || !Buffer->mDesc.mbTiled)
                    return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                        "D3D12 reserved commit requires a tiled buffer.");
                Resource = Buffer->mResource.Get();
                CommitHeap = &Buffer->mReservedCommitHeap;
                CommittedBytes = &Buffer->mCommittedBytes;
                bBuffer = true;
            }
            UINT TotalTiles = 0;
            mD3DDevice->GetResourceTiling(
                Resource, &TotalTiles, nullptr, nullptr, nullptr, 0, nullptr);
            const uint64_t TileBytes =
                D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            const uint64_t TotalBytes =
                static_cast<uint64_t>(TotalTiles) * TileBytes;
            const uint64_t Bytes = eastl::min(
                TotalBytes, (RequestedBytes + TileBytes - 1) & ~(TileBytes - 1));
            const uint64_t Tiles = Bytes / TileBytes;
            ID3D12CommandQueue* Queue = GetQueue(QueueType);
            if (!Queue)
                return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                    "The requested D3D12 residency queue is unavailable.");
            ComPtr<ID3D12Heap> ReplacementHeap;
            if (Tiles)
            {
                D3D12_HEAP_DESC HeapDesc{};
                HeapDesc.SizeInBytes = Bytes;
                HeapDesc.Alignment = TileBytes;
                HeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
                HeapDesc.Properties.CreationNodeMask = 1;
                HeapDesc.Properties.VisibleNodeMask = 1;
                HeapDesc.Flags = bBuffer
                    ? D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS
                    : D3D12_HEAP_FLAG_NONE;
                const HRESULT Result = mD3DDevice->CreateHeap(
                    &HeapDesc, IID_PPV_ARGS(&ReplacementHeap));
                if (FAILED(Result)) return D3D12Failure(
                    "Failed to allocate reserved-resource backing tiles.",
                    Result);
                D3D12_TILED_RESOURCE_COORDINATE Coordinate{};
                D3D12_TILE_REGION_SIZE Region{};
                Region.NumTiles = static_cast<UINT>(Tiles);
                D3D12_TILE_RANGE_FLAGS Flag = D3D12_TILE_RANGE_FLAG_NONE;
                UINT Offset = 0;
                UINT Count = static_cast<UINT>(Tiles);
                Queue->UpdateTileMappings(
                    Resource, 1, &Coordinate, &Region,
                    ReplacementHeap.Get(),
                    1, &Flag, &Offset, &Count,
                    D3D12_TILE_MAPPING_FLAG_NONE);
            }
            if (Tiles < TotalTiles)
            {
                D3D12_TILED_RESOURCE_COORDINATE Coordinate{};
                Coordinate.X = static_cast<UINT>(Tiles);
                D3D12_TILE_REGION_SIZE Region{};
                Region.NumTiles = static_cast<UINT>(TotalTiles - Tiles);
                D3D12_TILE_RANGE_FLAGS Flag = D3D12_TILE_RANGE_FLAG_NULL;
                UINT Offset = 0;
                UINT Count = Region.NumTiles;
                Queue->UpdateTileMappings(
                    Resource, 1, &Coordinate, &Region, nullptr,
                    1, &Flag, &Offset, &Count,
                    D3D12_TILE_MAPPING_FLAG_NONE);
            }
            // UpdateTileMappings is ordered on the queue. Complete it before
            // replacing or releasing the heap that backed the prior mapping.
            if (const FArdaRHIStatus Status = WaitForIdle(); !Status)
                return Status;
            *CommitHeap = eastl::move(ReplacementHeap);
            *CommittedBytes = Bytes;
            return {};
        }

        TArdaRHIResult<FArdaRHIStreamingBudget>
        FArdaD3D12ProviderDevice::QueryStreamingBudget(
            bool bLocalMemory) const
        {
            if (!mDxgiAdapter)
                return Fail<FArdaRHIStreamingBudget>(FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "DXGI memory-budget telemetry is unavailable."));
            DXGI_QUERY_VIDEO_MEMORY_INFO Info{};
            const HRESULT Result = mDxgiAdapter->QueryVideoMemoryInfo(
                0, bLocalMemory
                    ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL
                    : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
                &Info);
            if (FAILED(Result))
                return Fail<FArdaRHIStreamingBudget>(D3D12Failure(
                    "Failed to query the DXGI streaming budget.", Result));
            FArdaRHIStreamingBudget Budget;
            Budget.mBudgetBytes = Info.Budget;
            Budget.mCurrentUsageBytes = Info.CurrentUsage;
            Budget.mAvailableForReservationBytes =
                Info.AvailableForReservation;
            Budget.mCurrentReservationBytes = Info.CurrentReservation;
            Budget.mbLocalMemory = bLocalMemory;
            return {Budget, {}};
        }

        FArdaRHIStatus
        FArdaD3D12ProviderDevice::SetStreamingBudgetReservation(
            uint64_t Bytes, bool bLocalMemory)
        {
            if (!mDxgiAdapter)
                return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                    "DXGI memory-budget reservation is unavailable.");
            const HRESULT Result = mDxgiAdapter->SetVideoMemoryReservation(
                0, bLocalMemory
                    ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL
                    : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
                Bytes);
            return FAILED(Result)
                ? D3D12Failure(
                    "Failed to set the DXGI streaming reservation.", Result)
                : FArdaRHIStatus{};
        }

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::CreateStagingTexture(
            const FArdaRHIStagingTextureDesc& Desc)
        {
            auto Texture = eastl::make_shared<FD3D12StagingTexture>();
            Texture->mDesc = Desc;
            const FArdaRHITextureDesc& TextureDesc = Desc.mTexture;
            D3D12_RESOURCE_DESC Resource{};
            Resource.Dimension =
                TextureDesc.mDimension == EArdaRHITextureDimension::Texture3D
                    ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                    : (TextureDesc.mDimension == EArdaRHITextureDimension::Texture1D ||
                       TextureDesc.mDimension == EArdaRHITextureDimension::Texture1DArray
                        ? D3D12_RESOURCE_DIMENSION_TEXTURE1D
                        : D3D12_RESOURCE_DIMENSION_TEXTURE2D);
            Resource.Width = TextureDesc.mWidth;
            Resource.Height = TextureDesc.mHeight;
            Resource.DepthOrArraySize = static_cast<UINT16>(
                Resource.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                    ? TextureDesc.mDepth
                    : TextureDesc.mArraySize);
            Resource.MipLevels = static_cast<UINT16>(TextureDesc.mMipLevels);
            Resource.Format = ToDxgi(TextureDesc.mFormat);
            Resource.SampleDesc.Count = TextureDesc.mSampleCount;
            Resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            const uint32_t SubresourceCount =
                D3D12TextureStateCount(TextureDesc);
            Texture->mFootprints.resize(SubresourceCount);
            Texture->mRowCounts.resize(SubresourceCount);
            Texture->mRowSizes.resize(SubresourceCount);
            UINT64 TotalBytes = 0;
            mD3DDevice->GetCopyableFootprints(
                &Resource,
                0,
                SubresourceCount,
                0,
                Texture->mFootprints.data(),
                Texture->mRowCounts.data(),
                Texture->mRowSizes.data(),
                &TotalBytes);
            if (!TotalBytes)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "D3D12 staging texture has no copyable footprint."));
            D3D12_HEAP_PROPERTIES Heap{};
            Heap.Type = Desc.mCpuAccess == EArdaRHICpuAccess::Read
                ? D3D12_HEAP_TYPE_READBACK
                : D3D12_HEAP_TYPE_UPLOAD;
            Heap.CreationNodeMask = 1;
            Heap.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC BufferDesc{};
            BufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            BufferDesc.Width = TotalBytes;
            BufferDesc.Height = 1;
            BufferDesc.DepthOrArraySize = 1;
            BufferDesc.MipLevels = 1;
            BufferDesc.SampleDesc.Count = 1;
            BufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            const D3D12_RESOURCE_STATES State =
                Desc.mCpuAccess == EArdaRHICpuAccess::Read
                    ? D3D12_RESOURCE_STATE_COPY_DEST
                    : D3D12_RESOURCE_STATE_GENERIC_READ;
            const HRESULT Result = mD3DDevice->CreateCommittedResource(
                &Heap,
                D3D12_HEAP_FLAG_NONE,
                &BufferDesc,
                State,
                nullptr,
                IID_PPV_ARGS(&Texture->mResource));
            if (FAILED(Result))
                return Fail<FArdaProviderObjectRef>(D3D12Failure(
                    "Failed to create a D3D12 staging texture buffer.", Result));
            return { Texture, {} };
        }

        TArdaRHIResult<FArdaRHIStagingTextureMapping>
        FArdaD3D12ProviderDevice::MapStagingTexture(
            const FArdaProviderObjectRef& Object,
            const FArdaRHITextureSlice& Slice,
            EArdaRHICpuAccess Access)
        {
            auto* Texture = dynamic_cast<FD3D12StagingTexture*>(Object.get());
            if (!Texture || !Texture->mResource)
                return Fail<FArdaRHIStagingTextureMapping>(
                    FArdaRHIStatus::Error(
                        EArdaRHIResult::WrongDevice,
                        "D3D12 staging mapping has the wrong resource type."));
            const FArdaRHITextureDesc& Desc = Texture->mDesc.mTexture;
            if (Access != Texture->mDesc.mCpuAccess ||
                Slice.mMipLevel >= Desc.mMipLevels ||
                Slice.mArraySlice >= Desc.mArraySize ||
                Slice.mPlane >= GetArdaRHIFormatPlaneCount(Desc.mFormat) ||
                Slice.mX || Slice.mY || Slice.mZ)
            {
                return Fail<FArdaRHIStagingTextureMapping>(
                    FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidArgument,
                        "D3D12 staging mapping slice or access is invalid."));
            }
            std::lock_guard<std::mutex> Lock(Texture->mMapMutex);
            if (Texture->mbMapped)
                return Fail<FArdaRHIStagingTextureMapping>(
                    FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidState,
                        "D3D12 staging texture is already mapped."));
            const uint32_t Subresource = ArdaD3D12CalcSubresource(
                Slice.mMipLevel,
                Slice.mArraySlice,
                Slice.mPlane,
                Desc.mMipLevels,
                Desc.mArraySize);
            const auto& Footprint = Texture->mFootprints[Subresource];
            const SIZE_T RowCount = static_cast<SIZE_T>(
                Texture->mRowCounts[Subresource]) *
                Footprint.Footprint.Depth;
            const SIZE_T ByteSize = RowCount
                ? static_cast<SIZE_T>(Footprint.Footprint.RowPitch) *
                        (RowCount - 1) +
                    static_cast<SIZE_T>(Texture->mRowSizes[Subresource])
                : 0;
            const D3D12_RANGE ReadRange =
                Access == EArdaRHICpuAccess::Read
                    ? D3D12_RANGE{
                        static_cast<SIZE_T>(Footprint.Offset),
                        static_cast<SIZE_T>(Footprint.Offset) + ByteSize}
                    : D3D12_RANGE{0, 0};
            void* Data = nullptr;
            const HRESULT Result = Texture->mResource->Map(
                0, &ReadRange, &Data);
            if (FAILED(Result))
                return Fail<FArdaRHIStagingTextureMapping>(D3D12Failure(
                    "Failed to map a D3D12 staging texture.", Result));
            Texture->mbMapped = true;
            FArdaRHIStagingTextureMapping Mapping;
            Mapping.mData = static_cast<uint8_t*>(Data) + Footprint.Offset;
            Mapping.mRowPitch = Footprint.Footprint.RowPitch;
            return { Mapping, {} };
        }

        FArdaRHIStatus FArdaD3D12ProviderDevice::UnmapStagingTexture(
            const FArdaProviderObjectRef& Object)
        {
            auto* Texture = dynamic_cast<FD3D12StagingTexture*>(Object.get());
            if (!Texture || !Texture->mResource)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 staging unmap has the wrong resource type.");
            std::lock_guard<std::mutex> Lock(Texture->mMapMutex);
            if (!Texture->mbMapped)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "D3D12 staging texture is not mapped.");
            Texture->mResource->Unmap(0, nullptr);
            Texture->mbMapped = false;
            return {};
        }

        TArdaRHIResult<void*> FArdaD3D12ProviderDevice::MapBuffer(
            const FArdaProviderObjectRef& Object,
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
                return Fail<void*>(D3D12FailureWithInfo(
                    mD3DDevice.Get(),
                    "Failed to map a D3D12 host-visible buffer.", Result));
            return { static_cast<uint8_t*>(Data) + Offset, {} };
        }

        void FArdaD3D12ProviderDevice::UnmapBuffer(
            const FArdaProviderObjectRef& Object) noexcept
        {
            auto* Buffer = dynamic_cast<FD3D12Buffer*>(Object.get());
            if (!Buffer || !Buffer->mResource) return;
            const D3D12_RANGE WrittenRange{ 0, 0 };
            Buffer->mResource->Unmap(0, &WrittenRange);
        }

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::ImportTexture(
            const FArdaRHINativeTextureImportDesc& Desc)
        {
            auto Texture = eastl::make_shared<FD3D12Texture>();
            Texture->mResource = reinterpret_cast<ID3D12Resource*>(Desc.mNativeObject);
            Texture->mDesc = Desc.mTexture;
            Texture->mAbstractStates.assign(
                D3D12TextureStateCount(Desc.mTexture),
                Desc.mTexture.mInitialState);
            Texture->mNativeStates.assign(
                D3D12TextureStateCount(Desc.mTexture),
                ToD3D12State(Desc.mTexture.mInitialState));
            if (!Texture->mResource)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument, "Native D3D12 texture is null."));
            if (auto Status = CreateTextureViews(*Texture); !Status)
                return Fail<FArdaProviderObjectRef>(eastl::move(Status));
            return { Texture, {} };
        }

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::ImportBuffer(
            const FArdaRHINativeBufferImportDesc& Desc)
        {
            auto Buffer = eastl::make_shared<FD3D12Buffer>();
            Buffer->mResource = reinterpret_cast<ID3D12Resource*>(Desc.mNativeObject);
            Buffer->mDesc = Desc.mBuffer;
            Buffer->mAbstractState = Desc.mBuffer.mInitialState;
            Buffer->mNativeState = ToD3D12State(Desc.mBuffer.mInitialState);
            Buffer->mbStateKnown = true;
            if (!Buffer->mResource)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument, "Native D3D12 buffer is null."));
            return { Buffer, {} };
        }

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::CreateSampler(
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

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::CreateShader(
            const FArdaRHIShaderDesc& Desc)
        {
            auto Shader = eastl::make_shared<FD3D12Shader>();
            const auto* Begin = static_cast<const uint8_t*>(Desc.mBytecode);
            Shader->mBytecode.assign(Begin, Begin + Desc.mBytecodeSize);
            Shader->mStage = Desc.mStage;
            return { Shader, {} };
        }

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::CreateBindingLayout(
            const FArdaRHIBindingLayoutDesc& Desc)
        {
            auto Layout = eastl::make_shared<FD3D12BindingLayout>();
            Layout->mDesc = Desc;
            return { Layout, {} };
        }

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::CreateBindlessLayout(
            const FArdaRHIBindlessLayoutDesc& Desc,
            const FArdaRHIBindingLayoutDesc& NativeDesc)
        {
            auto Layout = eastl::make_shared<FD3D12BindingLayout>();
            Layout->mDesc = NativeDesc;
            Layout->mbBindless = true;
            Layout->mBindlessDesc = Desc;
            return {Layout, {}};
        }

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::CreateBindingSet(
            const FArdaRHIBindingSetDesc& Desc,
            const FArdaProviderObjectRef& LayoutObject,
            const eastl::vector<FArdaProviderBinding>& Bindings)
        {
            auto* Layout = dynamic_cast<FD3D12BindingLayout*>(LayoutObject.get());
            if (!Layout)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
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
                const uint32_t DescriptorCount = Layout->mbBindless &&
                    Layout->mBindlessDesc.mbVariableDescriptorCount
                    ? eastl::max(1u, Desc.mVariableDescriptorCount)
                    : eastl::max(1u, Item.mArraySize);
                auto Allocation = AllocateDescriptors(
                    bSampler, DescriptorCount);
                if (!Allocation) return Fail<FArdaProviderObjectRef>(eastl::move(Allocation.mStatus));
                Set->mAllocations.push_back(Allocation.mValue);
                Set->mTables.push_back({ Item.mType, Allocation.mValue.mGpu });
                const uint32_t Increment = bSampler ? mSamplerDescriptorSize : mResourceDescriptorSize;
                for (uint32_t Element = 0;
                     Element < DescriptorCount; ++Element)
                {
                    const FArdaProviderBinding* Binding = nullptr;
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
                    D3D12_CPU_DESCRIPTOR_HANDLE Destination = Allocation.mValue.mCpu;
                    Destination.ptr += static_cast<SIZE_T>(Element) * Increment;
                    if (!Binding)
                    {
                        switch (Item.mType)
                        {
                        case EArdaRHIBindingType::Sampler:
                        {
                            D3D12_SAMPLER_DESC NullSampler{};
                            NullSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
                            NullSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                            NullSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                            NullSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                            NullSampler.MaxLOD = D3D12_FLOAT32_MAX;
                            mD3DDevice->CreateSampler(
                                &NullSampler, Destination);
                            break;
                        }
                        case EArdaRHIBindingType::TextureSRV:
                        {
                            D3D12_SHADER_RESOURCE_VIEW_DESC View{};
                            View.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                            View.Shader4ComponentMapping =
                                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                            View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                            View.Texture2D.MipLevels = 1;
                            mD3DDevice->CreateShaderResourceView(
                                nullptr, &View, Destination);
                            break;
                        }
                        case EArdaRHIBindingType::TextureUAV:
                        {
                            D3D12_UNORDERED_ACCESS_VIEW_DESC View{};
                            View.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                            View.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                            mD3DDevice->CreateUnorderedAccessView(
                                nullptr, nullptr, &View, Destination);
                            break;
                        }
                        case EArdaRHIBindingType::SamplerFeedbackTextureUAV:
                        {
                            ComPtr<ID3D12Device8> Device8;
                            if (FAILED(mD3DDevice.As(&Device8)))
                                return Fail<FArdaProviderObjectRef>(
                                    FArdaRHIStatus::Error(
                                        EArdaRHIResult::Unsupported,
                                        "D3D12 sampler-feedback descriptors are unsupported."));
                            D3D12_DESCRIPTOR_HEAP_DESC CpuHeapDesc{};
                            CpuHeapDesc.Type =
                                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                            CpuHeapDesc.NumDescriptors = 1;
                            ComPtr<ID3D12DescriptorHeap> CpuHeap;
                            const HRESULT HeapResult =
                                mD3DDevice->CreateDescriptorHeap(
                                    &CpuHeapDesc,
                                    IID_PPV_ARGS(&CpuHeap));
                            if (FAILED(HeapResult))
                                return Fail<FArdaProviderObjectRef>(
                                    D3D12Failure(
                                        "Failed to allocate a null sampler-feedback descriptor.",
                                        HeapResult));
                            const auto Cpu = CpuHeap->
                                GetCPUDescriptorHandleForHeapStart();
                            Device8->CreateSamplerFeedbackUnorderedAccessView(
                                nullptr, nullptr, Cpu);
                            mD3DDevice->CopyDescriptorsSimple(
                                1, Destination, Cpu,
                                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                            break;
                        }
                        case EArdaRHIBindingType::ConstantBuffer:
                        case EArdaRHIBindingType::VolatileConstantBuffer:
                            mD3DDevice->CreateConstantBufferView(
                                nullptr, Destination);
                            break;
                        case EArdaRHIBindingType::RayTracingAccelStruct:
                        {
                            D3D12_SHADER_RESOURCE_VIEW_DESC View{};
                            View.Format = DXGI_FORMAT_UNKNOWN;
                            View.Shader4ComponentMapping =
                                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                            View.ViewDimension =
                                D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
                            View.RaytracingAccelerationStructure.Location = 0;
                            mD3DDevice->CreateShaderResourceView(
                                nullptr, &View, Destination);
                            break;
                        }
                        case EArdaRHIBindingType::TypedBufferUAV:
                        case EArdaRHIBindingType::StructuredBufferUAV:
                        case EArdaRHIBindingType::RawBufferUAV:
                        {
                            D3D12_UNORDERED_ACCESS_VIEW_DESC View{};
                            View.Format = Item.mType ==
                                    EArdaRHIBindingType::StructuredBufferUAV
                                ? DXGI_FORMAT_UNKNOWN
                                : (Item.mType ==
                                        EArdaRHIBindingType::RawBufferUAV
                                    ? DXGI_FORMAT_R32_TYPELESS
                                    : DXGI_FORMAT_R32_UINT);
                            View.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                            View.Buffer.NumElements = 1;
                            View.Buffer.StructureByteStride = Item.mType ==
                                    EArdaRHIBindingType::StructuredBufferUAV
                                ? 4u : 0u;
                            View.Buffer.Flags = Item.mType ==
                                    EArdaRHIBindingType::RawBufferUAV
                                ? D3D12_BUFFER_UAV_FLAG_RAW
                                : D3D12_BUFFER_UAV_FLAG_NONE;
                            mD3DDevice->CreateUnorderedAccessView(
                                nullptr, nullptr, &View, Destination);
                            break;
                        }
                        default:
                        {
                            D3D12_SHADER_RESOURCE_VIEW_DESC View{};
                            View.Format = Item.mType ==
                                    EArdaRHIBindingType::StructuredBufferSRV
                                ? DXGI_FORMAT_UNKNOWN
                                : (Item.mType ==
                                        EArdaRHIBindingType::RawBufferSRV
                                    ? DXGI_FORMAT_R32_TYPELESS
                                    : DXGI_FORMAT_R32_UINT);
                            View.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                            View.Shader4ComponentMapping =
                                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                            View.Buffer.NumElements = 1;
                            View.Buffer.StructureByteStride = Item.mType ==
                                    EArdaRHIBindingType::StructuredBufferSRV
                                ? 4u : 0u;
                            View.Buffer.Flags = Item.mType ==
                                    EArdaRHIBindingType::RawBufferSRV
                                ? D3D12_BUFFER_SRV_FLAG_RAW
                                : D3D12_BUFFER_SRV_FLAG_NONE;
                            mD3DDevice->CreateShaderResourceView(
                                nullptr, &View, Destination);
                            break;
                        }
                        }
                        continue;
                    }
                    if (Item.mType == EArdaRHIBindingType::Sampler)
                    {
                        auto* Sampler = dynamic_cast<FD3D12Sampler*>(Binding->mObject.get());
                        if (!Sampler) return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument, "D3D12 sampler binding has the wrong resource type."));
                        mD3DDevice->CreateSampler(&Sampler->mDesc, Destination);
                        continue;
                    }

                    auto* Texture = dynamic_cast<FD3D12Texture*>(Binding->mObject.get());
                    auto* Feedback = dynamic_cast<
                        FD3D12SamplerFeedbackTexture*>(
                        Binding->mObject.get());
                    auto* Buffer = dynamic_cast<FD3D12Buffer*>(Binding->mObject.get());
                    auto* AccelStruct = dynamic_cast<FD3D12AccelStruct*>(
                        Binding->mObject.get());
                    switch (Item.mType)
                    {
                    case EArdaRHIBindingType::RayTracingAccelStruct:
                    {
                        if (!AccelStruct || !AccelStruct->mResource)
                            return Fail<FArdaProviderObjectRef>(
                                FArdaRHIStatus::Error(
                                    EArdaRHIResult::InvalidArgument,
                                    "D3D12 acceleration-structure SRV has the wrong resource type."));
                        D3D12_SHADER_RESOURCE_VIEW_DESC View{};
                        View.Format = DXGI_FORMAT_UNKNOWN;
                        View.Shader4ComponentMapping =
                            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        View.ViewDimension =
                            D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
                        View.RaytracingAccelerationStructure.Location =
                            AccelStruct->mResource->GetGPUVirtualAddress();
                        mD3DDevice->CreateShaderResourceView(
                            nullptr, &View, Destination);
                        break;
                    }
                    case EArdaRHIBindingType::TextureSRV:
                    {
                        if (!Texture) return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
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
                    {
                        if (!Texture) return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument, "D3D12 texture UAV has the wrong resource type."));
                        D3D12_UNORDERED_ACCESS_VIEW_DESC View{};
                        View.Format = ToDxgi(Binding->mItem.mView.mFormat == EArdaRHIFormat::Unknown
                            ? Texture->mDesc.mFormat : Binding->mItem.mView.mFormat);
                        View.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                        View.Texture2D.MipSlice = Binding->mItem.mView.mTextureRange.mBaseMipLevel;
                        mD3DDevice->CreateUnorderedAccessView(Texture->mResource.Get(), nullptr, &View, Destination);
                        break;
                    }
                    case EArdaRHIBindingType::SamplerFeedbackTextureUAV:
                    {
                        auto* Paired = Feedback
                            ? dynamic_cast<FD3D12Texture*>(
                                Feedback->mPairedTexture.get())
                            : nullptr;
                        if (!Feedback || !Paired || !Paired->mResource ||
                            !Feedback->mResource)
                            return Fail<FArdaProviderObjectRef>(
                                FArdaRHIStatus::Error(
                                    EArdaRHIResult::InvalidArgument,
                                    "D3D12 sampler-feedback UAV has the wrong resource type."));
                        mD3DDevice->CopyDescriptorsSimple(
                            1, Destination, Feedback->mCpuDescriptor,
                            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                        break;
                    }
                    case EArdaRHIBindingType::ConstantBuffer:
                    case EArdaRHIBindingType::VolatileConstantBuffer:
                    {
                        if (!Buffer) return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
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
                        if (!Buffer) return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument, "D3D12 buffer SRV has the wrong resource type."));
                        const auto Range = Binding->mItem.mView.mBufferRange.Resolve(Buffer->mDesc);
                        const bool bRaw = Item.mType == EArdaRHIBindingType::RawBufferSRV;
                        const bool bStructured = Item.mType == EArdaRHIBindingType::StructuredBufferSRV;
                        const EArdaRHIFormat Format = Binding->mItem.mView.mFormat == EArdaRHIFormat::Unknown
                            ? Buffer->mDesc.mFormat : Binding->mItem.mView.mFormat;
                        const uint32_t Stride = bStructured ? Buffer->mDesc.mStructureStride
                            : (bRaw ? 4u : eastl::max(
                                1u, GetArdaRHIFormatElementSize(Format)));
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
                        if (!Buffer) return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument, "D3D12 buffer UAV has the wrong resource type."));
                        const auto Range = Binding->mItem.mView.mBufferRange.Resolve(Buffer->mDesc);
                        const bool bRaw = Item.mType == EArdaRHIBindingType::RawBufferUAV;
                        const bool bStructured = Item.mType == EArdaRHIBindingType::StructuredBufferUAV;
                        const EArdaRHIFormat Format = Binding->mItem.mView.mFormat == EArdaRHIFormat::Unknown
                            ? Buffer->mDesc.mFormat : Binding->mItem.mView.mFormat;
                        const uint32_t Stride = bStructured ? Buffer->mDesc.mStructureStride
                            : (bRaw ? 4u : eastl::max(
                                1u, GetArdaRHIFormatElementSize(Format)));
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
                        return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::Unsupported, "The D3D12 binding type is unsupported."));
                    }
                }
            }
            return { Set, {} };
        }

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::CreateFramebuffer(
            const FArdaProviderFramebufferCreateInfo& Info)
        {
            auto Framebuffer = eastl::make_shared<FD3D12Framebuffer>();
            for (const auto& Target : Info.mColors)
            {
                auto* Texture = dynamic_cast<FD3D12Texture*>(Target.mTexture.get());
                if (!Texture || !Texture->mRtv.ptr)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidArgument,
                        "A D3D12 framebuffer color attachment is not render-target capable."));
                Framebuffer->mRtvs.push_back(Texture->mRtv);
                Framebuffer->mRetainedTextures.push_back(Target.mTexture);
            }
            if (Info.mDepth.mTexture)
            {
                auto* Texture = dynamic_cast<FD3D12Texture*>(Info.mDepth.mTexture.get());
                if (!Texture || !Texture->mDsv.ptr)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidArgument,
                        "A D3D12 framebuffer depth attachment is not depth-stencil capable."));
                Framebuffer->mDsv = Texture->mDsv;
                Framebuffer->mbHasDepth = true;
                Framebuffer->mRetainedTextures.push_back(Info.mDepth.mTexture);
            }
            return { Framebuffer, {} };
        }

        TArdaRHIResult<ComPtr<ID3D12RootSignature>>
        FArdaD3D12ProviderDevice::CreateRootSignature(
            const eastl::vector<FArdaProviderObjectRef>& Layouts,
            eastl::vector<uint32_t>& OutItemCounts,
            eastl::vector<int32_t>& OutPushConstantRoots,
            bool bLocal)
        {
            eastl::vector<D3D12_ROOT_PARAMETER1> Parameters;
            eastl::vector<D3D12_DESCRIPTOR_RANGE1> Ranges;
            size_t RangeCount = 0;
            bool bDirectHeapIndexing = false;
            for (const auto& LayoutObject : Layouts)
            {
                auto* Layout = dynamic_cast<FD3D12BindingLayout*>(LayoutObject.get());
                if (!Layout) return Fail<ComPtr<ID3D12RootSignature>>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice, "D3D12 pipeline binding layout has the wrong implementation."));
                bDirectHeapIndexing = bDirectHeapIndexing ||
                    (Layout->mbBindless &&
                     Layout->mBindlessDesc.mbDirectHeapIndexing);
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
                    D3D12_ROOT_PARAMETER1 Parameter{};
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
                        Range.NumDescriptors = Layout->mbBindless &&
                            Layout->mBindlessDesc.mbUnbounded
                            ? UINT_MAX : eastl::max(1u, Item.mArraySize);
                        Range.BaseShaderRegister = Item.mSlot;
                        Range.RegisterSpace = Layout->mDesc.mRegisterSpace;
                        Range.OffsetInDescriptorsFromTableStart = 0;
                        if (Layout->mbBindless &&
                            Layout->mBindlessDesc.mbUpdateAfterBind)
                            Range.Flags =
                                D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE |
                                (Range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
                                    ? D3D12_DESCRIPTOR_RANGE_FLAG_NONE
                                    : D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
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
            D3D12_ROOT_SIGNATURE_DESC1 Desc{};
            Desc.NumParameters = static_cast<UINT>(Parameters.size());
            Desc.pParameters = Parameters.data();
            Desc.Flags = bLocal
                ? D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE
                : D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            if (!bLocal && bDirectHeapIndexing)
                Desc.Flags = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(
                    Desc.Flags |
                    D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                    D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);
            D3D12_VERSIONED_ROOT_SIGNATURE_DESC Versioned{};
            Versioned.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
            Versioned.Desc_1_1 = Desc;
            ComPtr<ID3DBlob> Blob;
            ComPtr<ID3DBlob> Error;
            HRESULT Result = D3D12SerializeVersionedRootSignature(
                &Versioned, &Blob, &Error);
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

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::CreateGraphicsPipeline(
            const FArdaProviderGraphicsPipelineCreateInfo& Info)
        {
            auto Pipeline = eastl::make_shared<FD3D12Pipeline>();
            auto Root = CreateRootSignature(
                Info.mBindingLayouts, Pipeline->mLayoutItemCounts,
                Pipeline->mPushConstantRoots);
            if (!Root) return Fail<FArdaProviderObjectRef>(eastl::move(Root.mStatus));
            Pipeline->mRootSignature = eastl::move(Root.mValue);

            const auto Bytecode = [](const FArdaProviderObjectRef& Object)
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
                        Native.AlignedByteOffset = Attribute.mOffset + Element *
                            GetArdaRHIFormatElementSize(Attribute.mFormat);
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
            if (FAILED(Result)) return Fail<FArdaProviderObjectRef>(
                D3D12Failure("Failed to create a D3D12 graphics pipeline.", Result));
            return { Pipeline, {} };
        }

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::CreateComputePipeline(
            const FArdaProviderComputePipelineCreateInfo& Info)
        {
            auto* Shader = dynamic_cast<FD3D12Shader*>(Info.mComputeShader.get());
            if (!Shader) return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                EArdaRHIResult::InvalidArgument, "D3D12 compute shader has the wrong implementation."));
            auto Pipeline = eastl::make_shared<FD3D12Pipeline>();
            auto Root = CreateRootSignature(
                Info.mBindingLayouts, Pipeline->mLayoutItemCounts,
                Pipeline->mPushConstantRoots);
            if (!Root) return Fail<FArdaProviderObjectRef>(eastl::move(Root.mStatus));
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
            if (FAILED(Result)) return Fail<FArdaProviderObjectRef>(
                D3D12Failure("Failed to create a D3D12 compute pipeline.", Result));
            return { Pipeline, {} };
        }

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::CreateMeshletPipeline(
            const FArdaProviderMeshletPipelineCreateInfo& Info)
        {
            if (mCapabilities.mMeshShaderTier == EArdaRHIMeshShaderTier::None)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "D3D12 mesh shaders are unsupported by this device."));
            }
            auto* Mesh = dynamic_cast<FD3D12Shader*>(Info.mMeshShader.get());
            auto* Amplification = Info.mAmplificationShader
                ? dynamic_cast<FD3D12Shader*>(Info.mAmplificationShader.get())
                : nullptr;
            auto* Pixel = Info.mPixelShader
                ? dynamic_cast<FD3D12Shader*>(Info.mPixelShader.get())
                : nullptr;
            if (!Mesh || (Info.mAmplificationShader && !Amplification) ||
                (Info.mPixelShader && !Pixel))
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "D3D12 mesh pipeline shader has the wrong implementation."));
            }

            ComPtr<ID3D12Device2> Device2;
            HRESULT Result = mD3DDevice.As(&Device2);
            if (FAILED(Result))
            {
                return Fail<FArdaProviderObjectRef>(D3D12Failure(
                    "The D3D12 device cannot create pipeline-state streams.",
                    Result));
            }

            auto Pipeline = eastl::make_shared<FD3D12Pipeline>();
            auto Root = CreateRootSignature(
                Info.mBindingLayouts,
                Pipeline->mLayoutItemCounts,
                Pipeline->mPushConstantRoots);
            if (!Root)
                return Fail<FArdaProviderObjectRef>(eastl::move(Root.mStatus));
            Pipeline->mRootSignature = eastl::move(Root.mValue);

            const auto Bytecode = [](const FD3D12Shader* Shader)
            {
                return Shader
                    ? D3D12_SHADER_BYTECODE{
                        Shader->mBytecode.data(), Shader->mBytecode.size() }
                    : D3D12_SHADER_BYTECODE{};
            };
            FMeshPipelineStream Stream{};
            Stream.mRoot.mValue = Pipeline->mRootSignature.Get();
            Stream.mAmplificationShader.mValue = Bytecode(Amplification);
            Stream.mMeshShader.mValue = Bytecode(Mesh);
            Stream.mPixelShader.mValue = Bytecode(Pixel);
            Stream.mBlend.mValue.AlphaToCoverageEnable =
                Info.mDesc.mBlendState.mbAlphaToCoverage;
            Stream.mBlend.mValue.IndependentBlendEnable = TRUE;
            for (uint32_t Index = 0; Index < ArdaRHIMaxRenderTargets; ++Index)
            {
                const auto& Source = Info.mDesc.mBlendState.mTargets[Index];
                auto& Target = Stream.mBlend.mValue.RenderTarget[Index];
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
            Stream.mSampleMask.mValue = UINT_MAX;
            auto& Rasterizer = Stream.mRasterizer.mValue;
            Rasterizer.FillMode =
                Info.mDesc.mRasterState.mFillMode == EArdaRHIFillMode::Wireframe
                ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
            switch (Info.mDesc.mRasterState.mCullMode)
            {
            case EArdaRHICullMode::Front:
                Rasterizer.CullMode = D3D12_CULL_MODE_FRONT; break;
            case EArdaRHICullMode::None:
                Rasterizer.CullMode = D3D12_CULL_MODE_NONE; break;
            default:
                Rasterizer.CullMode = D3D12_CULL_MODE_BACK; break;
            }
            Rasterizer.FrontCounterClockwise =
                Info.mDesc.mRasterState.mbFrontCounterClockwise;
            Rasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
            Rasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
            Rasterizer.SlopeScaledDepthBias =
                D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
            Rasterizer.DepthClipEnable = Info.mDesc.mRasterState.mbDepthClip;
            Rasterizer.MultisampleEnable = Info.mDesc.mSampleCount > 1;
            Rasterizer.ConservativeRaster =
                D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

            auto& Depth = Stream.mDepthStencil.mValue;
            Depth.DepthEnable = Info.mDesc.mDepthStencilState.mbDepthTest;
            Depth.DepthWriteMask = Info.mDesc.mDepthStencilState.mbDepthWrite
                ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            Depth.DepthFunc = ToD3D12Comparison(
                Info.mDesc.mDepthStencilState.mDepthFunc);
            Depth.StencilEnable = FALSE;
            Depth.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
            Depth.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

            switch (Info.mDesc.mTopology)
            {
            case EArdaRHIPrimitiveTopology::PointList:
                Stream.mTopology.mValue = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
                Pipeline->mTopology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
                break;
            case EArdaRHIPrimitiveTopology::LineList:
            case EArdaRHIPrimitiveTopology::LineStrip:
                Stream.mTopology.mValue = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                Pipeline->mTopology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
                break;
            default:
                Stream.mTopology.mValue = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                Pipeline->mTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                break;
            }
            Stream.mRenderTargets.mValue.NumRenderTargets =
                static_cast<UINT>(Info.mDesc.mColorFormats.size());
            for (uint32_t Index = 0;
                 Index < Stream.mRenderTargets.mValue.NumRenderTargets;
                 ++Index)
            {
                Stream.mRenderTargets.mValue.RTFormats[Index] =
                    ToDxgi(Info.mDesc.mColorFormats[Index]);
            }
            Stream.mDepthFormat.mValue = ToDxgi(Info.mDesc.mDepthFormat);
            Stream.mSampleDesc.mValue.Count = Info.mDesc.mSampleCount;
            Stream.mSampleDesc.mValue.Quality = 0;

            const D3D12_PIPELINE_STATE_STREAM_DESC Desc{
                sizeof(Stream), &Stream };
            if (mPipelineLibrary && Info.mDesc.mPersistentCacheKey != 0)
            {
                std::lock_guard<std::mutex> Lock(mPipelineCacheMutex);
                const std::wstring Name = L"Meshlet-" +
                    std::to_wstring(Info.mDesc.mPersistentCacheKey);
                ComPtr<ID3D12PipelineLibrary1> PipelineLibrary1;
                Result = mPipelineLibrary.As(&PipelineLibrary1);
                if (SUCCEEDED(Result))
                {
                    Result = PipelineLibrary1->LoadPipeline(
                        Name.c_str(), &Desc,
                        IID_PPV_ARGS(&Pipeline->mPipeline));
                }
                if (SUCCEEDED(Result))
                {
                    pipeline_cache::Message(
                        mDiagnosticCallback,
                        EArdaDiagnosticSeverity::Info,
                        "LoadPipeline accepted a cached D3D12 meshlet PSO.");
                }
                else
                {
                    Pipeline->mPipeline.Reset();
                    Result = Device2->CreatePipelineState(
                        &Desc, IID_PPV_ARGS(&Pipeline->mPipeline));
                    if (SUCCEEDED(Result) &&
                        SUCCEEDED(mPipelineLibrary->StorePipeline(
                            Name.c_str(), Pipeline->mPipeline.Get())))
                        mbPipelineCacheDirty = true;
                }
            }
            else
            {
                Result = Device2->CreatePipelineState(
                    &Desc, IID_PPV_ARGS(&Pipeline->mPipeline));
            }
            if (FAILED(Result))
            {
                return Fail<FArdaProviderObjectRef>(D3D12Failure(
                    "Failed to create a D3D12 mesh pipeline.", Result));
            }
            return { Pipeline, {} };
        }

        FArdaProviderObjectResult
        FArdaD3D12ProviderDevice::CreateRayTracingPipeline(
            const FArdaProviderRayTracingPipelineCreateInfo& Info)
        {
            if (!mCapabilities.mRayTracing.mbPipelineShaders)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "D3D12 ray tracing is unsupported by this device."));
            }
            ComPtr<ID3D12Device5> Device5;
            HRESULT Result = mD3DDevice.As(&Device5);
            if (FAILED(Result))
                return Fail<FArdaProviderObjectRef>(D3D12Failure(
                    "The D3D12 device cannot create ray-tracing state objects.",
                    Result));

            auto Pipeline = eastl::make_shared<FD3D12RayTracingPipeline>();
            auto Root = CreateRootSignature(
                Info.mGlobalBindingLayouts,
                Pipeline->mGlobalBindings.mLayoutItemCounts,
                Pipeline->mGlobalBindings.mPushConstantRoots);
            if (!Root)
                return Fail<FArdaProviderObjectRef>(eastl::move(Root.mStatus));
            Pipeline->mGlobalBindings.mRootSignature = eastl::move(Root.mValue);

            struct FLibrary
            {
                std::wstring mExportName;
                std::wstring mSourceName;
                D3D12_EXPORT_DESC mExport{};
                D3D12_DXIL_LIBRARY_DESC mLibrary{};
            };
            std::vector<FLibrary> Libraries;
            Libraries.reserve(Info.mShaders.size() +
                Info.mHitGroups.size() * 3u);
            const auto AddLibrary = [&Libraries](
                const FArdaProviderRayTracingShader& ShaderInfo)
                -> FArdaRHIStatus
            {
                if (!ShaderInfo.mShader) return {};
                auto* Shader = dynamic_cast<FD3D12Shader*>(
                    ShaderInfo.mShader.get());
                if (!Shader)
                    return FArdaRHIStatus::Error(
                        EArdaRHIResult::WrongDevice,
                        "A D3D12 ray-tracing shader has the wrong implementation.");
                FLibrary Library;
                Library.mExportName.assign(
                    ShaderInfo.mExportName.begin(),
                    ShaderInfo.mExportName.end());
                Library.mSourceName.assign(
                    ShaderInfo.mEntryPoint.begin(),
                    ShaderInfo.mEntryPoint.end());
                Library.mLibrary.DXILLibrary = {
                    Shader->mBytecode.data(), Shader->mBytecode.size() };
                Libraries.push_back(eastl::move(Library));
                return {};
            };
            for (const auto& ShaderInfo : Info.mShaders)
                if (auto Status = AddLibrary(ShaderInfo); !Status)
                    return Fail<FArdaProviderObjectRef>(Status);
            for (const auto& Hit : Info.mHitGroups)
            {
                if (auto Status = AddLibrary(Hit.mClosestHit); !Status)
                    return Fail<FArdaProviderObjectRef>(Status);
                if (auto Status = AddLibrary(Hit.mAnyHit); !Status)
                    return Fail<FArdaProviderObjectRef>(Status);
                if (auto Status = AddLibrary(Hit.mIntersection); !Status)
                    return Fail<FArdaProviderObjectRef>(Status);
            }
            for (FLibrary& Library : Libraries)
            {
                Library.mExport.Name = Library.mExportName.c_str();
                Library.mExport.ExportToRename =
                    Library.mSourceName.empty() ||
                    Library.mSourceName == Library.mExportName
                    ? nullptr : Library.mSourceName.c_str();
                Library.mLibrary.NumExports = 1;
                Library.mLibrary.pExports = &Library.mExport;
            }

            struct FHitGroup
            {
                std::wstring mExport;
                std::wstring mClosestHit;
                std::wstring mAnyHit;
                std::wstring mIntersection;
                D3D12_HIT_GROUP_DESC mDesc{};
            };
            std::vector<FHitGroup> HitGroups;
            HitGroups.reserve(Info.mHitGroups.size());
            for (const auto& Hit : Info.mHitGroups)
            {
                FHitGroup Native;
                Native.mExport.assign(
                    Hit.mExportName.begin(), Hit.mExportName.end());
                Native.mClosestHit.assign(Hit.mClosestHit.mExportName.begin(),
                    Hit.mClosestHit.mExportName.end());
                Native.mAnyHit.assign(Hit.mAnyHit.mExportName.begin(),
                    Hit.mAnyHit.mExportName.end());
                Native.mIntersection.assign(
                    Hit.mIntersection.mExportName.begin(),
                    Hit.mIntersection.mExportName.end());
                HitGroups.push_back(eastl::move(Native));
            }
            for (size_t Index = 0; Index < HitGroups.size(); ++Index)
            {
                auto& Native = HitGroups[Index];
                const auto& Source = Info.mHitGroups[Index];
                Native.mDesc.HitGroupExport = Native.mExport.c_str();
                Native.mDesc.ClosestHitShaderImport =
                    Native.mClosestHit.empty() ? nullptr
                                               : Native.mClosestHit.c_str();
                Native.mDesc.AnyHitShaderImport =
                    Native.mAnyHit.empty() ? nullptr : Native.mAnyHit.c_str();
                Native.mDesc.IntersectionShaderImport =
                    Native.mIntersection.empty() ? nullptr
                                                 : Native.mIntersection.c_str();
                Native.mDesc.Type = Source.mbProceduralPrimitive
                    ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE
                    : D3D12_HIT_GROUP_TYPE_TRIANGLES;
            }

            D3D12_GLOBAL_ROOT_SIGNATURE GlobalRoot{
                Pipeline->mGlobalBindings.mRootSignature.Get() };
            D3D12_RAYTRACING_SHADER_CONFIG ShaderConfig{
                Info.mDesc.mMaxPayloadSize,
                Info.mDesc.mMaxAttributeSize };
            D3D12_RAYTRACING_PIPELINE_CONFIG PipelineConfig{
                Info.mDesc.mMaxRecursionDepth };

            struct FLocalAssociation
            {
                std::wstring mExport;
                const wchar_t* mExportPointer = nullptr;
                D3D12_LOCAL_ROOT_SIGNATURE mRoot{};
                D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION mAssociation{};
                size_t mRootSubobject = 0;
            };
            std::vector<FLocalAssociation> LocalAssociations;
            LocalAssociations.reserve(
                Info.mShaders.size() + Info.mHitGroups.size());
            const auto AddLocalRoot = [this, &Pipeline, &LocalAssociations](
                const eastl::string& Export,
                const FArdaProviderObjectRef& Layout) -> FArdaRHIStatus
            {
                if (!Layout) return {};
                eastl::vector<FArdaProviderObjectRef> Layouts{Layout};
                eastl::vector<uint32_t> ItemCounts;
                eastl::vector<int32_t> PushRoots;
                auto Root = CreateRootSignature(
                    Layouts, ItemCounts, PushRoots, true);
                if (!Root) return Root.mStatus;
                Pipeline->mLocalRootSignatures.push_back(
                    eastl::move(Root.mValue));
                FLocalAssociation Association;
                Association.mExport.assign(Export.begin(), Export.end());
                Association.mRoot.pLocalRootSignature =
                    Pipeline->mLocalRootSignatures.back().Get();
                LocalAssociations.push_back(eastl::move(Association));
                return {};
            };
            for (const auto& Shader : Info.mShaders)
                if (auto Status = AddLocalRoot(
                        Shader.mExportName, Shader.mLocalBindingLayout);
                    !Status)
                    return Fail<FArdaProviderObjectRef>(Status);
            for (const auto& Hit : Info.mHitGroups)
                if (auto Status = AddLocalRoot(
                        Hit.mExportName, Hit.mLocalBindingLayout); !Status)
                    return Fail<FArdaProviderObjectRef>(Status);

            std::vector<D3D12_STATE_SUBOBJECT> Subobjects;
            Subobjects.reserve(Libraries.size() + HitGroups.size() + 3 +
                LocalAssociations.size() * 2);
            for (FLibrary& Library : Libraries)
            {
                Subobjects.push_back({
                    D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY,
                    &Library.mLibrary });
            }
            for (FHitGroup& HitGroup : HitGroups)
                Subobjects.push_back({
                    D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP,
                    &HitGroup.mDesc });
            Subobjects.push_back({
                D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE,
                &GlobalRoot });
            Subobjects.push_back({
                D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG,
                &ShaderConfig });
            Subobjects.push_back({
                D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG,
                &PipelineConfig });
            for (FLocalAssociation& Local : LocalAssociations)
            {
                Local.mRootSubobject = Subobjects.size();
                Subobjects.push_back({
                    D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE,
                    &Local.mRoot });
            }
            for (FLocalAssociation& Local : LocalAssociations)
            {
                Local.mExportPointer = Local.mExport.c_str();
                Local.mAssociation.pSubobjectToAssociate =
                    &Subobjects[Local.mRootSubobject];
                Local.mAssociation.NumExports = 1;
                Local.mAssociation.pExports = &Local.mExportPointer;
                Subobjects.push_back({
                    D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION,
                    &Local.mAssociation });
            }
            D3D12_STATE_OBJECT_DESC Desc{};
            Desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
            Desc.NumSubobjects = static_cast<UINT>(Subobjects.size());
            Desc.pSubobjects = Subobjects.data();
            Result = Device5->CreateStateObject(
                &Desc, IID_PPV_ARGS(&Pipeline->mStateObject));
            if (FAILED(Result))
                return Fail<FArdaProviderObjectRef>(D3D12Failure(
                    "Failed to create a D3D12 ray-tracing pipeline.", Result));
            Result = Pipeline->mStateObject.As(&Pipeline->mProperties);
            if (FAILED(Result))
                return Fail<FArdaProviderObjectRef>(D3D12Failure(
                    "Failed to query D3D12 ray-tracing pipeline properties.",
                    Result));
            return { Pipeline, {} };
        }

        FArdaProviderObjectResult
        FArdaD3D12ProviderDevice::CreateWorkGraphPipeline(
            const FArdaProviderWorkGraphPipelineCreateInfo& Info)
        {
            if (mCapabilities.mWorkGraphTier ==
                EArdaRHIWorkGraphTier::None)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "D3D12 work graphs are unsupported by this device."));
            ComPtr<ID3D12Device5> Device5;
            HRESULT Result = mD3DDevice.As(&Device5);
            if (FAILED(Result))
                return Fail<FArdaProviderObjectRef>(D3D12Failure(
                    "The D3D12 device cannot create executable state objects.",
                    Result));
            auto Pipeline = eastl::make_shared<FD3D12WorkGraph>();
            auto Root = CreateRootSignature(
                Info.mBindingLayouts,
                Pipeline->mGlobalBindings.mLayoutItemCounts,
                Pipeline->mGlobalBindings.mPushConstantRoots);
            if (!Root)
                return Fail<FArdaProviderObjectRef>(
                    eastl::move(Root.mStatus));
            Pipeline->mGlobalBindings.mRootSignature =
                eastl::move(Root.mValue);

            CD3DX12_STATE_OBJECT_DESC State(
                D3D12_STATE_OBJECT_TYPE_EXECUTABLE);
            for (const auto& ShaderObject : Info.mShaders)
            {
                auto* Shader = dynamic_cast<FD3D12Shader*>(
                    ShaderObject.get());
                if (!Shader)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::WrongDevice,
                        "A D3D12 work-graph shader has the wrong implementation."));
                auto* Library = State.CreateSubobject<
                    CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
                D3D12_SHADER_BYTECODE Bytecode{
                    Shader->mBytecode.data(), Shader->mBytecode.size()};
                Library->SetDXILLibrary(&Bytecode);
            }
            auto* RootSubobject = State.CreateSubobject<
                CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
            RootSubobject->SetRootSignature(
                Pipeline->mGlobalBindings.mRootSignature.Get());
            const std::wstring ProgramName(
                Info.mDesc.mProgramName.begin(),
                Info.mDesc.mProgramName.end());
            const std::wstring Entrypoint(
                Info.mDesc.mEntryPoint.begin(),
                Info.mDesc.mEntryPoint.end());
            auto* WorkGraph = State.CreateSubobject<
                CD3DX12_WORK_GRAPH_SUBOBJECT>();
            WorkGraph->SetProgramName(ProgramName.c_str());
            WorkGraph->IncludeAllAvailableNodes();
            if (!Entrypoint.empty())
                WorkGraph->AddEntrypoint({Entrypoint.c_str(), 0});
            Result = Device5->CreateStateObject(
                State, IID_PPV_ARGS(&Pipeline->mStateObject));
            if (FAILED(Result))
                return Fail<FArdaProviderObjectRef>(D3D12Failure(
                    "Failed to create a D3D12 work-graph state object.",
                    Result));
            Result = Pipeline->mStateObject.As(
                &Pipeline->mStateProperties);
            if (FAILED(Result))
                return Fail<FArdaProviderObjectRef>(D3D12Failure(
                    "Failed to query D3D12 work-graph program properties.",
                    Result));
            Result = Pipeline->mStateObject.As(
                &Pipeline->mWorkGraphProperties);
            if (FAILED(Result))
                return Fail<FArdaProviderObjectRef>(D3D12Failure(
                    "Failed to query D3D12 work-graph topology properties.",
                    Result));
            Pipeline->mProgramIdentifier =
                Pipeline->mStateProperties->GetProgramIdentifier(
                    ProgramName.c_str());
            const UINT GraphIndex =
                Pipeline->mWorkGraphProperties->GetWorkGraphIndex(
                    ProgramName.c_str());
            if (GraphIndex == UINT_MAX)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure,
                    "The D3D12 state object did not expose the requested work graph."));
            if (!Entrypoint.empty())
                Pipeline->mEntrypointIndex =
                    Pipeline->mWorkGraphProperties->GetEntrypointIndex(
                        GraphIndex, {Entrypoint.c_str(), 0});
            Pipeline->mEntrypointRecordSize =
                Pipeline->mWorkGraphProperties->
                    GetEntrypointRecordSizeInBytes(
                        GraphIndex, Pipeline->mEntrypointIndex);
            Pipeline->mWorkGraphProperties->GetWorkGraphMemoryRequirements(
                GraphIndex, &Pipeline->mMemoryRequirements);
            if (Pipeline->mMemoryRequirements.MaxSizeInBytes)
            {
                auto Backing = CreateD3D12BufferResource(
                    mD3DDevice.Get(),
                    Pipeline->mMemoryRequirements.MaxSizeInBytes,
                    D3D12_HEAP_TYPE_DEFAULT,
                    D3D12_RESOURCE_STATE_COMMON,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
                if (!Backing)
                    return Fail<FArdaProviderObjectRef>(
                        eastl::move(Backing.mStatus));
                Pipeline->mBackingMemory =
                    eastl::move(Backing.mValue);
            }
            return {Pipeline, {}};
        }

        FArdaProviderObjectResult FArdaD3D12ProviderDevice::CreateShaderTable(
            const FArdaProviderObjectRef& PipelineObject,
            const FArdaRHIShaderTableDesc& Desc)
        {
            auto* Pipeline = dynamic_cast<FD3D12RayTracingPipeline*>(
                PipelineObject.get());
            if (!Pipeline)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "The D3D12 shader table has the wrong pipeline type."));
            auto Table = eastl::make_shared<FD3D12ShaderTable>();
            Table->mPipelineObject = PipelineObject;
            Table->mPipeline = Pipeline;
            Table->mMaxEntries = Desc.mMaxEntries;
            Table->mMaxLocalArgumentBytes = Desc.mMaxLocalArgumentBytes;
            Table->mRecords.resize(Desc.mMaxEntries);
            return { Table, {} };
        }

        FArdaRHIStatus FArdaD3D12ProviderDevice::RebuildShaderTable(
            FD3D12ShaderTable& Table)
        {
            eastl::vector<const FD3D12ShaderTable::FRecord*> RayRecords;
            eastl::vector<const FD3D12ShaderTable::FRecord*> MissRecords;
            eastl::vector<const FD3D12ShaderTable::FRecord*> HitRecords;
            eastl::vector<const FD3D12ShaderTable::FRecord*> CallableRecords;
            for (const auto& Record : Table.mRecords)
            {
                if (!Record.mbWritten) continue;
                switch (Record.mType)
                {
                case EArdaRHIShaderTableRecordType::RayGeneration:
                    RayRecords.push_back(&Record); break;
                case EArdaRHIShaderTableRecordType::Miss:
                    MissRecords.push_back(&Record); break;
                case EArdaRHIShaderTableRecordType::HitGroup:
                    HitRecords.push_back(&Record); break;
                case EArdaRHIShaderTableRecordType::Callable:
                    CallableRecords.push_back(&Record); break;
                }
            }
            if (RayRecords.size() > 1)
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                    "A D3D12 shader table may contain one ray-generation record.");
            const bool bStructured = !RayRecords.empty() ||
                !MissRecords.empty() || !HitRecords.empty() ||
                !CallableRecords.empty();
            const uint32_t EntryCount = bStructured
                ? static_cast<uint32_t>(RayRecords.size() +
                    MissRecords.size() + HitRecords.size() +
                    CallableRecords.size())
                : (Table.mRayGeneration.empty() ? 0u : 1u) +
                    static_cast<uint32_t>(Table.mMiss.size()) +
                    static_cast<uint32_t>(Table.mHitGroups.size()) +
                    static_cast<uint32_t>(Table.mCallable.size());
            if (EntryCount > Table.mMaxEntries)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "The D3D12 shader table exceeds its entry capacity.");
            Table.mBuffer.Reset();
            Table.mRayGenerationRange = {};
            Table.mMissRange = {};
            Table.mHitGroupRange = {};
            Table.mCallableRange = {};
            if (!EntryCount) return {};

            constexpr uint64_t IdentifierSize =
                D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
            const auto Align = [](uint64_t Value, uint64_t Alignment)
            {
                return (Value + Alignment - 1u) & ~(Alignment - 1u);
            };
            uint64_t MaxPayload = 0;
            if (bStructured)
            {
                for (const auto& Record : Table.mRecords)
                {
                    if (!Record.mbWritten) continue;
                    uint64_t Size = Record.mLocalArguments.size();
                    if (auto* Bindings = dynamic_cast<FD3D12BindingSet*>(
                            Record.mBindings.get()))
                        Size += Bindings->mTables.size() * sizeof(uint64_t);
                    if (Record.mGeometry) Size += sizeof(uint64_t);
                    if (Record.mUserData || Record.mGeometrySegment)
                        Size += sizeof(uint32_t) * 2u;
                    MaxPayload = eastl::max(MaxPayload, Size);
                }
            }
            const uint64_t RecordStride = Align(
                IdentifierSize + MaxPayload,
                D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
            uint64_t Cursor = 0;
            const uint64_t RayCount = bStructured
                ? RayRecords.size()
                : (Table.mRayGeneration.empty() ? 0u : 1u);
            const uint64_t MissCount = bStructured
                ? MissRecords.size() : Table.mMiss.size();
            const uint64_t HitCount = bStructured
                ? HitRecords.size() : Table.mHitGroups.size();
            const uint64_t CallableCount = bStructured
                ? CallableRecords.size() : Table.mCallable.size();
            const uint64_t RayOffset = !RayCount
                ? 0 : Align(Cursor,
                    D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
            if (RayCount) Cursor = RayOffset + RayCount * RecordStride;
            const uint64_t MissOffset = !MissCount
                ? 0 : Align(Cursor,
                    D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
            if (MissCount) Cursor = MissOffset + MissCount * RecordStride;
            const uint64_t HitOffset = !HitCount
                ? 0 : Align(Cursor,
                    D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
            if (HitCount) Cursor = HitOffset + HitCount * RecordStride;
            const uint64_t CallableOffset = !CallableCount
                ? 0 : Align(Cursor,
                    D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
            if (CallableCount)
                Cursor = CallableOffset + CallableCount * RecordStride;

            D3D12_HEAP_PROPERTIES Heap{};
            Heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            Heap.CreationNodeMask = 1;
            Heap.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC Resource{};
            Resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            Resource.Width = Cursor;
            Resource.Height = 1;
            Resource.DepthOrArraySize = 1;
            Resource.MipLevels = 1;
            Resource.SampleDesc.Count = 1;
            Resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            HRESULT Result = mD3DDevice->CreateCommittedResource(
                &Heap,
                D3D12_HEAP_FLAG_NONE,
                &Resource,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&Table.mBuffer));
            if (FAILED(Result))
                return D3D12Failure(
                    "Failed to allocate a D3D12 shader table.", Result);
            void* Mapped = nullptr;
            D3D12_RANGE ReadRange{ 0, 0 };
            Result = Table.mBuffer->Map(0, &ReadRange, &Mapped);
            if (FAILED(Result))
                return D3D12Failure(
                    "Failed to map a D3D12 shader table.", Result);
            std::memset(Mapped, 0, static_cast<size_t>(Cursor));
            const auto WriteRecord = [&](
                const eastl::string& Name,
                uint64_t Offset,
                const FD3D12ShaderTable::FRecord* Record = nullptr)
                -> FArdaRHIStatus
            {
                const std::wstring Wide(Name.begin(), Name.end());
                const void* Identifier =
                    Table.mPipeline->mProperties->GetShaderIdentifier(
                        Wide.c_str());
                if (!Identifier)
                    return FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidArgument,
                        "A shader-table export is absent from the D3D12 pipeline.");
                std::memcpy(
                    static_cast<uint8_t*>(Mapped) + Offset,
                    Identifier,
                    IdentifierSize);
                if (Record)
                {
                    uint8_t* Payload = static_cast<uint8_t*>(Mapped) +
                        Offset + IdentifierSize;
                    if (auto* Bindings = dynamic_cast<FD3D12BindingSet*>(
                            Record->mBindings.get()))
                    {
                        for (const auto& Descriptor : Bindings->mTables)
                        {
                            const uint64_t Handle = Descriptor.mGpu.ptr;
                            std::memcpy(Payload, &Handle, sizeof(Handle));
                            Payload += sizeof(Handle);
                        }
                    }
                    if (!Record->mLocalArguments.empty())
                    {
                        std::memcpy(Payload,
                            Record->mLocalArguments.data(),
                            Record->mLocalArguments.size());
                        Payload += Record->mLocalArguments.size();
                    }
                    if (auto* Geometry = dynamic_cast<FD3D12AccelStruct*>(
                            Record->mGeometry.get()))
                    {
                        const uint64_t Address = Geometry->mResource
                            ? Geometry->mResource->GetGPUVirtualAddress() : 0;
                        std::memcpy(Payload, &Address, sizeof(Address));
                        Payload += sizeof(Address);
                    }
                    if (Record->mUserData || Record->mGeometrySegment)
                    {
                        std::memcpy(Payload, &Record->mUserData,
                            sizeof(Record->mUserData));
                        Payload += sizeof(Record->mUserData);
                        std::memcpy(Payload, &Record->mGeometrySegment,
                            sizeof(Record->mGeometrySegment));
                    }
                }
                return {};
            };
            FArdaRHIStatus Status;
            if (bStructured && !RayRecords.empty())
                Status = WriteRecord(RayRecords.front()->mExportName,
                    RayOffset, RayRecords.front());
            else if (!Table.mRayGeneration.empty())
                Status = WriteRecord(Table.mRayGeneration, RayOffset, nullptr);
            const auto WriteSection = [&](
                const eastl::vector<eastl::string>& Names,
                uint64_t Offset) -> FArdaRHIStatus
            {
                for (size_t Index = 0; Index < Names.size(); ++Index)
                    if (auto ItemStatus = WriteRecord(
                            Names[Index], Offset + Index * RecordStride,
                            nullptr);
                        !ItemStatus)
                        return ItemStatus;
                return {};
            };
            const auto WriteStructuredSection = [&WriteRecord, RecordStride](
                const eastl::vector<const FD3D12ShaderTable::FRecord*>& Records,
                uint64_t Offset) -> FArdaRHIStatus
            {
                for (size_t Index = 0; Index < Records.size(); ++Index)
                    if (auto ItemStatus = WriteRecord(
                            Records[Index]->mExportName,
                            Offset + Index * RecordStride,
                            Records[Index]); !ItemStatus)
                        return ItemStatus;
                return {};
            };
            if (bStructured)
            {
                if (Status) Status = WriteStructuredSection(
                    MissRecords, MissOffset);
                if (Status) Status = WriteStructuredSection(
                    HitRecords, HitOffset);
                if (Status) Status = WriteStructuredSection(
                    CallableRecords, CallableOffset);
            }
            else
            {
                if (Status) Status = WriteSection(Table.mMiss, MissOffset);
                if (Status) Status = WriteSection(Table.mHitGroups, HitOffset);
                if (Status) Status = WriteSection(Table.mCallable, CallableOffset);
            }
            Table.mBuffer->Unmap(0, nullptr);
            if (!Status)
            {
                Table.mBuffer.Reset();
                return Status;
            }
            const D3D12_GPU_VIRTUAL_ADDRESS Address =
                Table.mBuffer->GetGPUVirtualAddress();
            if (RayCount)
                Table.mRayGenerationRange = {
                    Address + RayOffset, RecordStride };
            if (MissCount)
                Table.mMissRange = {
                    Address + MissOffset,
                    MissCount * RecordStride,
                    RecordStride };
            if (HitCount)
                Table.mHitGroupRange = {
                    Address + HitOffset,
                    HitCount * RecordStride,
                    RecordStride };
            if (CallableCount)
                Table.mCallableRange = {
                    Address + CallableOffset,
                    CallableCount * RecordStride,
                    RecordStride };
            return {};
        }

        FArdaRHIStatus
        FArdaD3D12ProviderDevice::SetShaderTableRecord(
            const FArdaProviderObjectRef& TableObject,
            const FArdaRHIShaderTableRecordDesc& Record,
            const FArdaProviderObjectRef& LocalBindings,
            const FArdaProviderObjectRef& Geometry)
        {
            auto* Table = dynamic_cast<FD3D12ShaderTable*>(TableObject.get());
            if (!Table || Record.mRecordIndex >= Table->mRecords.size())
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "The D3D12 shader-table record is invalid.");
            if (LocalBindings &&
                !dynamic_cast<FD3D12BindingSet*>(LocalBindings.get()))
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "D3D12 local shader-table bindings are invalid.");
            if (Geometry &&
                !dynamic_cast<FD3D12AccelStruct*>(Geometry.get()))
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "D3D12 shader-table geometry is invalid.");
            auto& Native = Table->mRecords[Record.mRecordIndex];
            Native.mbWritten = true;
            Native.mType = Record.mType;
            Native.mExportName = Record.mExportName;
            Native.mLocalArguments = Record.mLocalArguments;
            Native.mUserData = Record.mUserData;
            Native.mGeometrySegment = Record.mGeometrySegment;
            Native.mBindings = LocalBindings;
            Native.mGeometry = Geometry;
            return {};
        }

        FArdaRHIStatus FArdaD3D12ProviderDevice::CommitShaderTable(
            const FArdaProviderObjectRef& TableObject)
        {
            auto* Table = dynamic_cast<FD3D12ShaderTable*>(TableObject.get());
            if (!Table)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "The D3D12 shader table has the wrong implementation.");
            return RebuildShaderTable(*Table);
        }

        FArdaRHIStatus
        FArdaD3D12ProviderDevice::SetShaderTableRayGeneration(
            const FArdaProviderObjectRef& TableObject,
            const char* ExportName,
            const FArdaProviderObjectRef& LocalBindings)
        {
            auto* Table = dynamic_cast<FD3D12ShaderTable*>(TableObject.get());
            if (!Table)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "The D3D12 shader table has the wrong implementation.");
            if (LocalBindings)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "Local D3D12 shader-table bindings are not implemented yet.");
            const eastl::string Previous = Table->mRayGeneration;
            Table->mRayGeneration = ExportName;
            const FArdaRHIStatus Status = RebuildShaderTable(*Table);
            if (!Status) Table->mRayGeneration = Previous;
            return Status;
        }

        FArdaRHIStatus FArdaD3D12ProviderDevice::AddShaderTableEntry(
            const FArdaProviderObjectRef& TableObject,
            const char* ExportName,
            const FArdaProviderObjectRef& LocalBindings,
            uint32_t Category)
        {
            auto* Table = dynamic_cast<FD3D12ShaderTable*>(TableObject.get());
            if (!Table)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "The D3D12 shader table has the wrong implementation.");
            if (LocalBindings)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "Local D3D12 shader-table bindings are not implemented yet.");
            eastl::vector<eastl::string>* Entries = nullptr;
            switch (Category)
            {
            case 0: Entries = &Table->mMiss; break;
            case 1: Entries = &Table->mHitGroups; break;
            case 2: Entries = &Table->mCallable; break;
            default:
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "The shader-table entry category is invalid.");
            }
            Entries->push_back(ExportName);
            const FArdaRHIStatus Status = RebuildShaderTable(*Table);
            if (!Status) Entries->pop_back();
            return Status;
        }

        FArdaD3D12CommandList::FArdaD3D12CommandList(
            FArdaD3D12ProviderDevice& Device,
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
            mCommandSignatures.clear();
            mRetainedObjects.clear();
            mTextureStates.clear();
            mBufferStates.clear();
            mAccelStructStates.clear();
            mBoundGraphicsPipeline = nullptr;
            mBoundComputePipeline = nullptr;
            mBoundShaderTable = nullptr;
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

        FArdaD3D12CommandList::FTextureTracking&
            FArdaD3D12CommandList::GetTextureTracking(
                FD3D12Texture& Texture)
        {
            auto Existing = mTextureStates.find(&Texture);
            if (Existing != mTextureStates.end())
                return Existing->second;
            FTextureTracking Tracking;
            {
                std::lock_guard<std::mutex> Lock(Texture.mStateMutex);
                Tracking.mAbstractStates = Texture.mAbstractStates;
                Tracking.mNativeStates = Texture.mNativeStates;
            }
            Tracking.mExpectedStartStates.assign(
                Tracking.mAbstractStates.size(),
                EArdaRHIResourceState::Unknown);
            return mTextureStates.emplace(
                &Texture, eastl::move(Tracking)).first->second;
        }

        FArdaD3D12CommandList::FBufferTracking&
            FArdaD3D12CommandList::GetBufferTracking(FD3D12Buffer& Buffer)
        {
            auto Existing = mBufferStates.find(&Buffer);
            if (Existing != mBufferStates.end())
                return Existing->second;
            FBufferTracking Tracking;
            {
                std::lock_guard<std::mutex> Lock(Buffer.mStateMutex);
                Tracking.mAbstractState = Buffer.mAbstractState;
                Tracking.mNativeState = Buffer.mNativeState;
                Tracking.mbKnown = Buffer.mbStateKnown;
            }
            return mBufferStates.emplace(&Buffer, Tracking).first->second;
        }

        FArdaRHIStatus
            FArdaD3D12CommandList::ValidateTrackedStartStates() const
        {
            for (const auto& Entry : mTextureStates)
            {
                std::lock_guard<std::mutex> Lock(Entry.first->mStateMutex);
                for (size_t Index = 0;
                     Index < Entry.second.mExpectedStartStates.size();
                     ++Index)
                {
                    if (Entry.second.mExpectedStartStates[Index] !=
                            EArdaRHIResourceState::Unknown &&
                        Entry.second.mExpectedStartStates[Index] !=
                            Entry.first->mAbstractStates[Index])
                    {
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidState,
                            "D3D12 texture start state differs at submission.");
                    }
                }
            }
            for (const auto& Entry : mBufferStates)
            {
                if (!Entry.second.mbExpectedStartState)
                    continue;
                std::lock_guard<std::mutex> Lock(Entry.first->mStateMutex);
                if (Entry.second.mExpectedStartState !=
                    Entry.first->mAbstractState)
                {
                    return FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidState,
                        "D3D12 buffer start state differs at submission.");
                }
            }
            return {};
        }

        void FArdaD3D12CommandList::CommitTrackedStates()
        {
            for (auto& Entry : mTextureStates)
            {
                std::lock_guard<std::mutex> Lock(Entry.first->mStateMutex);
                Entry.first->mAbstractStates = Entry.second.mAbstractStates;
                Entry.first->mNativeStates = Entry.second.mNativeStates;
            }
            for (const auto& Entry : mBufferStates)
            {
                std::lock_guard<std::mutex> Lock(Entry.first->mStateMutex);
                Entry.first->mAbstractState = Entry.second.mAbstractState;
                Entry.first->mNativeState = Entry.second.mNativeState;
                Entry.first->mbStateKnown = Entry.second.mbKnown;
            }
            for (const auto& Entry : mAccelStructStates)
            {
                std::lock_guard<std::mutex> Lock(Entry.first->mStateMutex);
                Entry.first->mAbstractState = Entry.second.mAbstractState;
                Entry.first->mBuildState = Entry.second.mBuildState;
            }
        }

        FArdaRHIStatus FArdaD3D12CommandList::TransitionBuffer(
            const FArdaProviderObjectRef& Object,
            EArdaRHIResourceState State)
        {
            auto* Buffer = dynamic_cast<FD3D12Buffer*>(Object.get());
            if (!Buffer || !Buffer->mResource)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 buffer transition has the wrong implementation.");
            Retain(Object);
            const D3D12_RESOURCE_STATES NewState = ToD3D12State(State);
            FBufferTracking& Tracking = GetBufferTracking(*Buffer);
            if (!Tracking.mbKnown)
            {
                Tracking.mAbstractState = Buffer->mDesc.mInitialState;
                Tracking.mNativeState = ToD3D12State(
                    Buffer->mDesc.mInitialState);
                Tracking.mbKnown = true;
            }
            if (Buffer->mDesc.mCpuAccess == EArdaRHICpuAccess::None &&
                Tracking.mNativeState != NewState)
            {
                D3D12_RESOURCE_BARRIER Barrier{};
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                Barrier.Transition.pResource = Buffer->mResource.Get();
                Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                Barrier.Transition.StateBefore = Tracking.mNativeState;
                Barrier.Transition.StateAfter = NewState;
                mCommandList->ResourceBarrier(1, &Barrier);
                Tracking.mNativeState = NewState;
            }
            Tracking.mAbstractState = State;
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::TransitionTexture(
            const FArdaProviderObjectRef& Object,
            const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& InputRange,
            EArdaRHIResourceState State)
        {
            auto* Texture = dynamic_cast<FD3D12Texture*>(Object.get());
            if (!Texture || !Texture->mResource)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 texture transition has the wrong implementation.");
            Retain(Object);
            const auto Range = InputRange.Resolve(Desc);
            const D3D12_RESOURCE_STATES NewState = ToD3D12State(State);
            std::vector<D3D12_RESOURCE_BARRIER> Barriers;
            FTextureTracking& Tracking = GetTextureTracking(*Texture);
            if (Tracking.mNativeStates.empty())
            {
                Tracking.mAbstractStates.assign(
                    D3D12TextureStateCount(Desc), Desc.mInitialState);
                Tracking.mNativeStates.assign(
                    D3D12TextureStateCount(Desc),
                    ToD3D12State(Desc.mInitialState));
            }
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
                        const uint32_t Subresource = ArdaD3D12CalcSubresource(
                            MipLevel, ArraySlice, Plane,
                            Desc.mMipLevels, Desc.mArraySize);
                        if (Tracking.mNativeStates[Subresource] != NewState)
                        {
                            D3D12_RESOURCE_BARRIER Barrier{};
                            Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                            Barrier.Transition.pResource = Texture->mResource.Get();
                            Barrier.Transition.Subresource = Subresource;
                            Barrier.Transition.StateBefore =
                                Tracking.mNativeStates[Subresource];
                            Barrier.Transition.StateAfter = NewState;
                            Barriers.push_back(Barrier);
                            Tracking.mNativeStates[Subresource] = NewState;
                        }
                        Tracking.mAbstractStates[Subresource] = State;
                    }
                }
            }
            if (!Barriers.empty())
            {
                mCommandList->ResourceBarrier(
                    static_cast<UINT>(Barriers.size()), Barriers.data());
            }
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::WriteBuffer(
            const FArdaProviderObjectRef& Object,
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
            const EArdaRHIResourceState PreviousState =
                GetBufferTracking(*Buffer).mAbstractState;
            if (mbAutomaticBarriers)
                (void)TransitionBuffer(Object, EArdaRHIResourceState::CopyDest);
            mCommandList->CopyBufferRegion(Buffer->mResource.Get(), Offset, Upload.Get(), 0, Size);
            if (mbAutomaticBarriers)
                (void)TransitionBuffer(Object, PreviousState);
            mUploadResources.push_back(eastl::move(Upload));
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::CopyBuffer(
            const FArdaProviderObjectRef& Destination, uint64_t DestinationOffset,
            const FArdaProviderObjectRef& Source, uint64_t SourceOffset, uint64_t Size)
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

        FArdaRHIStatus FArdaD3D12CommandList::CopyTexture(
            const FArdaProviderObjectRef& Destination,
            const FArdaRHITextureDesc& DestinationDesc,
            const FArdaRHITextureSlice& DestinationSlice,
            const FArdaProviderObjectRef& Source,
            const FArdaRHITextureDesc& SourceDesc,
            const FArdaRHITextureSlice& SourceSlice)
        {
            auto* Dst = dynamic_cast<FD3D12Texture*>(Destination.get());
            auto* Src = dynamic_cast<FD3D12Texture*>(Source.get());
            if (!Dst || !Src || !Dst->mResource || !Src->mResource)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 texture copy has the wrong resource type.");
            FArdaRHITextureCopyExtent CopyExtent;
            if (auto Status = ResolveArdaRHITextureCopyExtent(
                    DestinationDesc, DestinationSlice,
                    SourceDesc, SourceSlice, CopyExtent);
                !Status)
                return Status;
            const uint32_t Width = CopyExtent.mWidth;
            const uint32_t Height = CopyExtent.mHeight;
            const uint32_t Depth = CopyExtent.mDepth;

            FArdaRHITextureSubresourceRange DstRange;
            DstRange.mBaseMipLevel = DestinationSlice.mMipLevel;
            DstRange.mMipLevelCount = 1;
            DstRange.mBaseArraySlice = DestinationSlice.mArraySlice;
            DstRange.mArraySliceCount = 1;
            DstRange.mBasePlane = DestinationSlice.mPlane;
            DstRange.mPlaneCount = 1;
            FArdaRHITextureSubresourceRange SrcRange;
            SrcRange.mBaseMipLevel = SourceSlice.mMipLevel;
            SrcRange.mMipLevelCount = 1;
            SrcRange.mBaseArraySlice = SourceSlice.mArraySlice;
            SrcRange.mArraySliceCount = 1;
            SrcRange.mBasePlane = SourceSlice.mPlane;
            SrcRange.mPlaneCount = 1;
            const uint32_t DstSubresource = ArdaD3D12CalcSubresource(
                DestinationSlice.mMipLevel,
                DestinationSlice.mArraySlice,
                DestinationSlice.mPlane,
                DestinationDesc.mMipLevels,
                DestinationDesc.mArraySize);
            const uint32_t SrcSubresource = ArdaD3D12CalcSubresource(
                SourceSlice.mMipLevel,
                SourceSlice.mArraySlice,
                SourceSlice.mPlane,
                SourceDesc.mMipLevels,
                SourceDesc.mArraySize);
            if (Dst == Src && DstSubresource == SrcSubresource)
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "D3D12 cannot copy a texture subresource onto itself.");
            }
            const EArdaRHIResourceState PreviousDst =
                GetTextureTracking(*Dst).mAbstractStates[DstSubresource];
            const EArdaRHIResourceState PreviousSrc =
                GetTextureTracking(*Src).mAbstractStates[SrcSubresource];
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTexture(
                        Destination, DestinationDesc, DstRange,
                        EArdaRHIResourceState::CopyDest); !Status)
                    return Status;
                if (auto Status = TransitionTexture(
                        Source, SourceDesc, SrcRange,
                        EArdaRHIResourceState::CopySource); !Status)
                    return Status;
            }
            D3D12_TEXTURE_COPY_LOCATION DstLocation{};
            DstLocation.pResource = Dst->mResource.Get();
            DstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            DstLocation.SubresourceIndex = DstSubresource;
            D3D12_TEXTURE_COPY_LOCATION SrcLocation{};
            SrcLocation.pResource = Src->mResource.Get();
            SrcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            SrcLocation.SubresourceIndex = SrcSubresource;
            const D3D12_BOX Box{
                SourceSlice.mX,
                SourceSlice.mY,
                SourceSlice.mZ,
                SourceSlice.mX + Width,
                SourceSlice.mY + Height,
                SourceSlice.mZ + Depth};
            mCommandList->CopyTextureRegion(
                &DstLocation,
                DestinationSlice.mX,
                DestinationSlice.mY,
                DestinationSlice.mZ,
                &SrcLocation,
                &Box);
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTexture(
                        Destination, DestinationDesc, DstRange, PreviousDst); !Status)
                    return Status;
                if (auto Status = TransitionTexture(
                        Source, SourceDesc, SrcRange, PreviousSrc); !Status)
                    return Status;
            }
            Retain(Destination);
            Retain(Source);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::ResolveTexture(
            const FArdaProviderObjectRef& Destination,
            const FArdaRHITextureDesc& DestinationDesc,
            const FArdaRHITextureSlice& DestinationSlice,
            const FArdaProviderObjectRef& Source,
            const FArdaRHITextureDesc& SourceDesc,
            const FArdaRHITextureSlice& SourceSlice)
        {
            auto* Dst = dynamic_cast<FD3D12Texture*>(Destination.get());
            auto* Src = dynamic_cast<FD3D12Texture*>(Source.get());
            if (!Dst || !Src || !Dst->mResource || !Src->mResource)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 texture resolve has the wrong resource type.");
            FArdaRHITextureCopyExtent Extent;
            if (auto Status = ValidateArdaRHITextureResolve(
                    DestinationDesc, DestinationSlice,
                    SourceDesc, SourceSlice, Extent); !Status)
                return Status;
            FArdaRHITextureSubresourceRange DstRange{
                DestinationSlice.mMipLevel, 1,
                DestinationSlice.mArraySlice, 1, 0, 1};
            FArdaRHITextureSubresourceRange SrcRange{
                SourceSlice.mMipLevel, 1,
                SourceSlice.mArraySlice, 1, 0, 1};
            const uint32_t DstSubresource = ArdaD3D12CalcSubresource(
                DestinationSlice.mMipLevel,
                DestinationSlice.mArraySlice,
                0,
                DestinationDesc.mMipLevels,
                DestinationDesc.mArraySize);
            const uint32_t SrcSubresource = ArdaD3D12CalcSubresource(
                SourceSlice.mMipLevel,
                SourceSlice.mArraySlice,
                0,
                SourceDesc.mMipLevels,
                SourceDesc.mArraySize);
            const EArdaRHIResourceState PreviousDst =
                GetTextureTracking(*Dst).mAbstractStates[DstSubresource];
            const EArdaRHIResourceState PreviousSrc =
                GetTextureTracking(*Src).mAbstractStates[SrcSubresource];
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTexture(
                        Destination, DestinationDesc, DstRange,
                        EArdaRHIResourceState::ResolveDest); !Status)
                    return Status;
                if (auto Status = TransitionTexture(
                        Source, SourceDesc, SrcRange,
                        EArdaRHIResourceState::ResolveSource); !Status)
                    return Status;
            }
            mCommandList->ResolveSubresource(
                Dst->mResource.Get(),
                DstSubresource,
                Src->mResource.Get(),
                SrcSubresource,
                ToDxgi(DestinationDesc.mFormat));
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTexture(
                        Destination, DestinationDesc, DstRange, PreviousDst); !Status)
                    return Status;
                if (auto Status = TransitionTexture(
                        Source, SourceDesc, SrcRange, PreviousSrc); !Status)
                    return Status;
            }
            Retain(Destination);
            Retain(Source);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::CopyTextureToStaging(
            const FArdaProviderObjectRef& Destination,
            const FArdaRHIStagingTextureDesc& DestinationDesc,
            const FArdaRHITextureSlice& DestinationSlice,
            const FArdaProviderObjectRef& Source,
            const FArdaRHITextureDesc& SourceDesc,
            const FArdaRHITextureSlice& SourceSlice)
        {
            auto* Dst = dynamic_cast<FD3D12StagingTexture*>(Destination.get());
            auto* Src = dynamic_cast<FD3D12Texture*>(Source.get());
            if (!Dst || !Src || !Dst->mResource || !Src->mResource)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 texture readback has the wrong resource type.");
            FArdaRHITextureCopyExtent CopyExtent;
            if (auto Status = ResolveArdaRHITextureCopyExtent(
                    DestinationDesc.mTexture, DestinationSlice,
                    SourceDesc, SourceSlice, CopyExtent);
                !Status)
                return Status;
            const uint32_t Width = CopyExtent.mWidth;
            const uint32_t Height = CopyExtent.mHeight;
            const uint32_t Depth = CopyExtent.mDepth;
            const uint32_t Subresource = ArdaD3D12CalcSubresource(
                SourceSlice.mMipLevel,
                SourceSlice.mArraySlice,
                SourceSlice.mPlane,
                SourceDesc.mMipLevels,
                SourceDesc.mArraySize);
            const uint32_t StagingSubresource = ArdaD3D12CalcSubresource(
                DestinationSlice.mMipLevel,
                DestinationSlice.mArraySlice,
                DestinationSlice.mPlane,
                DestinationDesc.mTexture.mMipLevels,
                DestinationDesc.mTexture.mArraySize);
            const EArdaRHIResourceState Previous =
                GetTextureTracking(*Src).mAbstractStates[Subresource];
            FArdaRHITextureSubresourceRange Range{
                SourceSlice.mMipLevel, 1,
                SourceSlice.mArraySlice, 1,
                SourceSlice.mPlane, 1};
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTexture(
                        Source, SourceDesc, Range,
                        EArdaRHIResourceState::CopySource); !Status)
                    return Status;
            }
            D3D12_TEXTURE_COPY_LOCATION DstLocation{};
            DstLocation.pResource = Dst->mResource.Get();
            DstLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            DstLocation.PlacedFootprint = Dst->mFootprints[StagingSubresource];
            D3D12_TEXTURE_COPY_LOCATION SrcLocation{};
            SrcLocation.pResource = Src->mResource.Get();
            SrcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            SrcLocation.SubresourceIndex = Subresource;
            const D3D12_BOX Box{
                SourceSlice.mX,
                SourceSlice.mY,
                SourceSlice.mZ,
                SourceSlice.mX + Width,
                SourceSlice.mY + Height,
                SourceSlice.mZ + Depth};
            mCommandList->CopyTextureRegion(
                &DstLocation,
                DestinationSlice.mX,
                DestinationSlice.mY,
                DestinationSlice.mZ,
                &SrcLocation,
                &Box);
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTexture(
                        Source, SourceDesc, Range, Previous); !Status)
                    return Status;
            }
            Retain(Destination);
            Retain(Source);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::CopyTextureFromStaging(
            const FArdaProviderObjectRef& Destination,
            const FArdaRHITextureDesc& DestinationDesc,
            const FArdaRHITextureSlice& DestinationSlice,
            const FArdaProviderObjectRef& Source,
            const FArdaRHIStagingTextureDesc& SourceDesc,
            const FArdaRHITextureSlice& SourceSlice)
        {
            auto* Dst = dynamic_cast<FD3D12Texture*>(Destination.get());
            auto* Src = dynamic_cast<FD3D12StagingTexture*>(Source.get());
            if (!Dst || !Src || !Dst->mResource || !Src->mResource)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 texture upload has the wrong resource type.");
            FArdaRHITextureCopyExtent CopyExtent;
            if (auto Status = ResolveArdaRHITextureCopyExtent(
                    DestinationDesc, DestinationSlice,
                    SourceDesc.mTexture, SourceSlice, CopyExtent);
                !Status)
                return Status;
            const uint32_t Width = CopyExtent.mWidth;
            const uint32_t Height = CopyExtent.mHeight;
            const uint32_t Depth = CopyExtent.mDepth;
            const uint32_t Subresource = ArdaD3D12CalcSubresource(
                DestinationSlice.mMipLevel,
                DestinationSlice.mArraySlice,
                DestinationSlice.mPlane,
                DestinationDesc.mMipLevels,
                DestinationDesc.mArraySize);
            const uint32_t StagingSubresource = ArdaD3D12CalcSubresource(
                SourceSlice.mMipLevel,
                SourceSlice.mArraySlice,
                SourceSlice.mPlane,
                SourceDesc.mTexture.mMipLevels,
                SourceDesc.mTexture.mArraySize);
            const EArdaRHIResourceState Previous =
                GetTextureTracking(*Dst).mAbstractStates[Subresource];
            FArdaRHITextureSubresourceRange Range{
                DestinationSlice.mMipLevel, 1,
                DestinationSlice.mArraySlice, 1,
                DestinationSlice.mPlane, 1};
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTexture(
                        Destination, DestinationDesc, Range,
                        EArdaRHIResourceState::CopyDest); !Status)
                    return Status;
            }
            D3D12_TEXTURE_COPY_LOCATION DstLocation{};
            DstLocation.pResource = Dst->mResource.Get();
            DstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            DstLocation.SubresourceIndex = Subresource;
            D3D12_TEXTURE_COPY_LOCATION SrcLocation{};
            SrcLocation.pResource = Src->mResource.Get();
            SrcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            SrcLocation.PlacedFootprint = Src->mFootprints[StagingSubresource];
            const D3D12_BOX Box{
                SourceSlice.mX,
                SourceSlice.mY,
                SourceSlice.mZ,
                SourceSlice.mX + Width,
                SourceSlice.mY + Height,
                SourceSlice.mZ + Depth};
            mCommandList->CopyTextureRegion(
                &DstLocation,
                DestinationSlice.mX,
                DestinationSlice.mY,
                DestinationSlice.mZ,
                &SrcLocation,
                &Box);
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTexture(
                        Destination, DestinationDesc, Range, Previous); !Status)
                    return Status;
            }
            Retain(Destination);
            Retain(Source);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::ClearTexture(
            const FArdaProviderObjectRef& Object, const FArdaRHITextureDesc&,
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
            const FArdaProviderObjectRef& Object, const FArdaRHITextureDesc&,
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
            const FArdaProviderObjectRef& Object, const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& Range,
            EArdaRHIResourceState State)
        {
            return TransitionTexture(Object, Desc, Range, State);
        }

        FArdaRHIStatus FArdaD3D12CommandList::SetBufferState(
            const FArdaProviderObjectRef& Object, const FArdaRHIBufferDesc&,
            EArdaRHIResourceState State)
        {
            return TransitionBuffer(Object, State);
        }

        FArdaRHIStatus FArdaD3D12CommandList::TransitionTexture(
            const FArdaProviderObjectRef& Object,
            const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureTransitionDesc& Transition)
        {
            auto* Texture = dynamic_cast<FD3D12Texture*>(Object.get());
            if (!Texture || !Texture->mResource)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 explicit texture transition has the wrong resource type.");
            const bool bBegin = HasAnyFlags(
                Transition.mFlags, EArdaRHITransitionFlags::BeginOnly);
            const bool bEnd = HasAnyFlags(
                Transition.mFlags, EArdaRHITransitionFlags::EndOnly);
            if (bBegin && bEnd)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "A D3D12 transition cannot be both begin-only and end-only.");
            if (!bBegin && !bEnd)
            {
                if (HasAnyFlags(
                        Transition.mFlags,
                        EArdaRHITransitionFlags::Discard))
                {
                    mCommandList->DiscardResource(Texture->mResource.Get(), nullptr);
                }
                return TransitionTexture(
                    Object,
                    Desc,
                    Transition.mSubresources,
                    Transition.mStateAfter);
            }

            const auto Range = Transition.mSubresources.Resolve(Desc);
            FTextureTracking& Tracking = GetTextureTracking(*Texture);
            const D3D12_RESOURCE_STATES StateAfter =
                ToD3D12State(Transition.mStateAfter);
            std::vector<D3D12_RESOURCE_BARRIER> Barriers;
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
                        const uint32_t Subresource = ArdaD3D12CalcSubresource(
                            MipLevel, ArraySlice, Plane,
                            Desc.mMipLevels, Desc.mArraySize);
                        if (Tracking.mNativeStates[Subresource] == StateAfter)
                            continue;
                        D3D12_RESOURCE_BARRIER Barrier{};
                        Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                        Barrier.Flags = bBegin
                            ? D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY
                            : D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
                        Barrier.Transition.pResource = Texture->mResource.Get();
                        Barrier.Transition.Subresource = Subresource;
                        Barrier.Transition.StateBefore =
                            Tracking.mNativeStates[Subresource];
                        Barrier.Transition.StateAfter = StateAfter;
                        Barriers.push_back(Barrier);
                        if (bEnd)
                        {
                            Tracking.mNativeStates[Subresource] = StateAfter;
                            Tracking.mAbstractStates[Subresource] =
                                Transition.mStateAfter;
                        }
                    }
                }
            }
            if (!Barriers.empty())
            {
                mCommandList->ResourceBarrier(
                    static_cast<UINT>(Barriers.size()), Barriers.data());
            }
            Retain(Object);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::TransitionBuffer(
            const FArdaProviderObjectRef& Object,
            const FArdaRHIBufferDesc& Desc,
            const FArdaRHIBufferTransitionDesc& Transition)
        {
            auto* Buffer = dynamic_cast<FD3D12Buffer*>(Object.get());
            if (!Buffer || !Buffer->mResource)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 explicit buffer transition has the wrong resource type.");
            const bool bBegin = HasAnyFlags(
                Transition.mFlags, EArdaRHITransitionFlags::BeginOnly);
            const bool bEnd = HasAnyFlags(
                Transition.mFlags, EArdaRHITransitionFlags::EndOnly);
            if (bBegin && bEnd)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "A D3D12 transition cannot be both begin-only and end-only.");
            if (!bBegin && !bEnd)
            {
                if (HasAnyFlags(
                        Transition.mFlags,
                        EArdaRHITransitionFlags::Discard))
                {
                    mCommandList->DiscardResource(Buffer->mResource.Get(), nullptr);
                }
                return TransitionBuffer(Object, Transition.mStateAfter);
            }
            if (Desc.mCpuAccess != EArdaRHICpuAccess::None)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "Host-visible D3D12 buffers cannot use split transitions.");
            FBufferTracking& Tracking = GetBufferTracking(*Buffer);
            const D3D12_RESOURCE_STATES StateAfter =
                ToD3D12State(Transition.mStateAfter);
            if (Tracking.mNativeState == StateAfter)
                return {};
            D3D12_RESOURCE_BARRIER Barrier{};
            Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            Barrier.Flags = bBegin
                ? D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY
                : D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
            Barrier.Transition.pResource = Buffer->mResource.Get();
            Barrier.Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            Barrier.Transition.StateBefore = Tracking.mNativeState;
            Barrier.Transition.StateAfter = StateAfter;
            mCommandList->ResourceBarrier(1, &Barrier);
            if (bEnd)
            {
                Tracking.mNativeState = StateAfter;
                Tracking.mAbstractState = Transition.mStateAfter;
            }
            Retain(Object);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::BeginTrackingTextureState(
            const FArdaProviderObjectRef& Object, const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& InputRange,
            EArdaRHIResourceState State)
        {
            auto* Texture = dynamic_cast<FD3D12Texture*>(Object.get());
            if (!Texture)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 texture tracking has the wrong implementation.");
            const auto Range = InputRange.Resolve(Desc);
            FTextureTracking& Tracking = GetTextureTracking(*Texture);
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
                        const uint32_t Subresource = ArdaD3D12CalcSubresource(
                            MipLevel, ArraySlice, Plane,
                            Desc.mMipLevels, Desc.mArraySize);
                        Tracking.mExpectedStartStates[Subresource] = State;
                        Tracking.mAbstractStates[Subresource] = State;
                        Tracking.mNativeStates[Subresource] =
                            ToD3D12State(State);
                    }
                }
            }
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::BeginTrackingBufferState(
            const FArdaProviderObjectRef& Object, const FArdaRHIBufferDesc&,
            EArdaRHIResourceState State)
        {
            auto* Buffer = dynamic_cast<FD3D12Buffer*>(Object.get());
            if (!Buffer)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 buffer tracking has the wrong implementation.");
            FBufferTracking& Tracking = GetBufferTracking(*Buffer);
            Tracking.mExpectedStartState = State;
            Tracking.mbExpectedStartState = true;
            Tracking.mAbstractState = State;
            if (Buffer->mDesc.mCpuAccess == EArdaRHICpuAccess::None)
                Tracking.mNativeState = ToD3D12State(State);
            Tracking.mbKnown = true;
            return {};
        }

        TArdaRHIResult<FArdaRHINativeResourceState>
            FArdaD3D12CommandList::QueryTextureState(
                const FArdaProviderObjectRef& Object,
                const FArdaRHITextureDesc& Desc,
                const FArdaRHITextureSubresourceRange& InputRange) const
        {
            auto* Texture = dynamic_cast<FD3D12Texture*>(Object.get());
            if (!Texture)
                return Fail<FArdaRHINativeResourceState>(
                    FArdaRHIStatus::Error(
                        EArdaRHIResult::WrongDevice,
                        "D3D12 texture query has the wrong implementation."));
            const auto Range = InputRange.Resolve(Desc);
            std::unique_lock<std::mutex> ResourceLock;
            const eastl::vector<EArdaRHIResourceState>* AbstractStates = nullptr;
            const eastl::vector<D3D12_RESOURCE_STATES>* NativeStates = nullptr;
            const auto Local = mTextureStates.find(Texture);
            if (Local != mTextureStates.end())
            {
                AbstractStates = &Local->second.mAbstractStates;
                NativeStates = &Local->second.mNativeStates;
            }
            else
            {
                ResourceLock = std::unique_lock<std::mutex>(Texture->mStateMutex);
                AbstractStates = &Texture->mAbstractStates;
                NativeStates = &Texture->mNativeStates;
            }
            const uint32_t First = ArdaD3D12CalcSubresource(
                Range.mBaseMipLevel, Range.mBaseArraySlice,
                Range.mBasePlane,
                Desc.mMipLevels, Desc.mArraySize);
            FArdaRHINativeResourceState Snapshot;
            Snapshot.mState = (*AbstractStates)[First];
            Snapshot.mNativeType = EArdaRHINativeResourceType::D3D12Resource;
            Snapshot.mPrimaryState = static_cast<uint64_t>(
                (*NativeStates)[First]);
            Snapshot.mbKnown = true;
            Snapshot.mbNativeCompatible =
                (*NativeStates)[First] ==
                    ToD3D12State(Snapshot.mState);
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
                        const uint32_t Subresource = ArdaD3D12CalcSubresource(
                            MipLevel, ArraySlice, Plane,
                            Desc.mMipLevels, Desc.mArraySize);
                        if ((*AbstractStates)[Subresource] !=
                                Snapshot.mState ||
                            (*NativeStates)[Subresource] !=
                                static_cast<D3D12_RESOURCE_STATES>(
                                    Snapshot.mPrimaryState))
                        {
                            return Fail<FArdaRHINativeResourceState>(
                                FArdaRHIStatus::Error(
                                    EArdaRHIResult::InvalidState,
                                    "D3D12 texture range contains mixed backend states."));
                        }
                    }
                }
            }
            return { Snapshot, {} };
        }

        TArdaRHIResult<FArdaRHINativeResourceState>
            FArdaD3D12CommandList::QueryBufferState(
                const FArdaProviderObjectRef& Object,
                const FArdaRHIBufferDesc&) const
        {
            auto* Buffer = dynamic_cast<FD3D12Buffer*>(Object.get());
            if (!Buffer)
                return Fail<FArdaRHINativeResourceState>(
                    FArdaRHIStatus::Error(
                        EArdaRHIResult::WrongDevice,
                        "D3D12 buffer query has the wrong implementation."));
            std::unique_lock<std::mutex> ResourceLock;
            FBufferTracking Tracking;
            const auto Local = mBufferStates.find(Buffer);
            if (Local != mBufferStates.end())
            {
                Tracking = Local->second;
            }
            else
            {
                ResourceLock = std::unique_lock<std::mutex>(Buffer->mStateMutex);
                Tracking.mAbstractState = Buffer->mAbstractState;
                Tracking.mNativeState = Buffer->mNativeState;
                Tracking.mbKnown = Buffer->mbStateKnown;
            }
            FArdaRHINativeResourceState Snapshot;
            Snapshot.mState = Tracking.mAbstractState;
            Snapshot.mNativeType = EArdaRHINativeResourceType::D3D12Resource;
            Snapshot.mPrimaryState = static_cast<uint64_t>(Tracking.mNativeState);
            Snapshot.mbKnown = Tracking.mbKnown;
            const D3D12_RESOURCE_STATES ExpectedNativeState =
                Buffer->mDesc.mCpuAccess == EArdaRHICpuAccess::Write
                    ? D3D12_RESOURCE_STATE_GENERIC_READ
                    : (Buffer->mDesc.mCpuAccess == EArdaRHICpuAccess::Read
                        ? D3D12_RESOURCE_STATE_COPY_DEST
                        : ToD3D12State(Tracking.mAbstractState));
            Snapshot.mbNativeCompatible = Tracking.mbKnown &&
                Tracking.mNativeState == ExpectedNativeState;
            return { Snapshot, {} };
        }

        FArdaRHIStatus FArdaD3D12CommandList::SetAccelStructState(
            const FArdaProviderObjectRef& Object,
            EArdaRHIResourceState State)
        {
            auto* AccelStruct = dynamic_cast<FD3D12AccelStruct*>(Object.get());
            if (!AccelStruct || !AccelStruct->mResource)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "D3D12 acceleration structure has the wrong implementation.");
            FAccelStructTracking Tracking;
            const auto Existing = mAccelStructStates.find(AccelStruct);
            if (Existing != mAccelStructStates.end())
                Tracking = Existing->second;
            else
            {
                std::lock_guard<std::mutex> Lock(AccelStruct->mStateMutex);
                Tracking.mAbstractState = AccelStruct->mAbstractState;
                Tracking.mBuildState = AccelStruct->mBuildState;
            }
            if (Tracking.mAbstractState != State)
            {
                D3D12_RESOURCE_BARRIER Barrier{};
                Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                Barrier.UAV.pResource = AccelStruct->mResource.Get();
                mCommandList->ResourceBarrier(1, &Barrier);
            }
            Tracking.mAbstractState = State;
            mAccelStructStates[AccelStruct] = Tracking;
            Retain(Object);
            return {};
        }

        TArdaRHIResult<FArdaRHINativeResourceState>
        FArdaD3D12CommandList::QueryAccelStructState(
            const FArdaProviderObjectRef& Object) const
        {
            auto* AccelStruct = dynamic_cast<FD3D12AccelStruct*>(Object.get());
            if (!AccelStruct || !AccelStruct->mResource)
                return Fail<FArdaRHINativeResourceState>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 acceleration-structure query has the wrong implementation."));
            FAccelStructTracking Tracking;
            const auto Existing = mAccelStructStates.find(AccelStruct);
            if (Existing != mAccelStructStates.end())
                Tracking = Existing->second;
            else
            {
                std::lock_guard<std::mutex> Lock(AccelStruct->mStateMutex);
                Tracking.mAbstractState = AccelStruct->mAbstractState;
                Tracking.mBuildState = AccelStruct->mBuildState;
            }
            FArdaRHINativeResourceState Snapshot;
            Snapshot.mState = Tracking.mAbstractState;
            Snapshot.mNativeType =
                EArdaRHINativeResourceType::D3D12AccelerationStructure;
            Snapshot.mPrimaryState =
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
            Snapshot.mbKnown = true;
            Snapshot.mbNativeCompatible =
                HasAnyFlags(Tracking.mAbstractState,
                    EArdaRHIResourceState::AccelStructRead) ||
                HasAnyFlags(Tracking.mAbstractState,
                    EArdaRHIResourceState::AccelStructWrite);
            return {Snapshot, {}};
        }

        FArdaRHIStatus FArdaD3D12CommandList::SetUAVBarriersForTexture(
            const FArdaProviderObjectRef& Object, bool bEnabled)
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
            const FArdaProviderObjectRef& Object, bool bEnabled)
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

        FArdaRHIStatus FArdaD3D12CommandList::AliasingBarrier(
            const FArdaProviderObjectRef& ResourceBefore,
            const FArdaProviderObjectRef& ResourceAfter)
        {
            const auto GetResource = [](const FArdaProviderObjectRef& Object)
                -> ID3D12Resource*
            {
                if (auto* Texture = dynamic_cast<FD3D12Texture*>(Object.get()))
                    return Texture->mResource.Get();
                if (auto* Buffer = dynamic_cast<FD3D12Buffer*>(Object.get()))
                    return Buffer->mResource.Get();
                return nullptr;
            };
            ID3D12Resource* Before = GetResource(ResourceBefore);
            ID3D12Resource* After = GetResource(ResourceAfter);
            if ((!Before && ResourceBefore) || (!After && ResourceAfter) ||
                (!Before && !After))
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 aliasing barrier has an invalid resource.");
            }
            D3D12_RESOURCE_BARRIER Barrier{};
            Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
            Barrier.Aliasing.pResourceBefore = Before;
            Barrier.Aliasing.pResourceAfter = After;
            mCommandList->ResourceBarrier(1, &Barrier);
            Retain(ResourceBefore);
            Retain(ResourceAfter);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::BindDescriptorSets(
            const FD3D12Pipeline& Pipeline,
            const eastl::vector<FArdaProviderObjectRef>& Bindings,
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
            const FArdaProviderGraphicsState& State)
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
            mBoundShaderTable = nullptr;
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
            const FArdaProviderComputeState& State)
        {
            auto* Pipeline = dynamic_cast<FD3D12Pipeline*>(State.mPipeline.get());
            if (!Pipeline) return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                "D3D12 compute state has the wrong implementation.");
            Retain(State.mPipeline);
            for (const auto& Binding : State.mBindings) Retain(Binding);
            mBoundComputePipeline = Pipeline;
            mBoundGraphicsPipeline = nullptr;
            mBoundShaderTable = nullptr;
            mCommandList->SetComputeRootSignature(Pipeline->mRootSignature.Get());
            mCommandList->SetPipelineState(Pipeline->mPipeline.Get());
            return BindDescriptorSets(*Pipeline, State.mBindings, false);
        }

        FArdaRHIStatus FArdaD3D12CommandList::SetMeshletState(
            const FArdaProviderMeshletState& State)
        {
            FArdaProviderGraphicsState Graphics;
            Graphics.mPipeline = State.mPipeline;
            Graphics.mFramebuffer = State.mFramebuffer;
            Graphics.mBindings = State.mBindings;
            Graphics.mViewports = State.mViewports;
            Graphics.mScissors = State.mScissors;
            return SetGraphicsState(Graphics);
        }

        FArdaRHIStatus FArdaD3D12CommandList::SetRayTracingState(
            const FArdaProviderRayTracingState& State)
        {
            auto* Table = dynamic_cast<FD3D12ShaderTable*>(
                State.mShaderTable.get());
            if (!Table || !Table->mPipeline || !Table->mBuffer ||
                !Table->mRayGenerationRange.StartAddress)
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "The D3D12 ray-tracing shader table is incomplete.");
            }
            ComPtr<ID3D12GraphicsCommandList4> RayCommands;
            const HRESULT Result = mCommandList.As(&RayCommands);
            if (FAILED(Result))
                return D3D12Failure(
                    "The D3D12 command list does not support ray tracing.",
                    Result);
            Retain(State.mShaderTable);
            for (const auto& Binding : State.mBindings) Retain(Binding);
            mBoundGraphicsPipeline = nullptr;
            mBoundComputePipeline = nullptr;
            mBoundShaderTable = Table;
            RayCommands->SetPipelineState1(
                Table->mPipeline->mStateObject.Get());
            mCommandList->SetComputeRootSignature(
                Table->mPipeline->mGlobalBindings.mRootSignature.Get());
            return BindDescriptorSets(
                Table->mPipeline->mGlobalBindings,
                State.mBindings,
                false);
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

        FArdaRHIStatus FArdaD3D12CommandList::DrawIndirect(
            const FArdaProviderObjectRef& Arguments,
            uint64_t Offset,
            uint32_t DrawCount,
            uint32_t Stride)
        {
            auto* Buffer = dynamic_cast<FD3D12Buffer*>(Arguments.get());
            if (!Buffer || !Buffer->mResource)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 indirect draw has the wrong buffer type.");
            D3D12_INDIRECT_ARGUMENT_DESC Argument{};
            Argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
            D3D12_COMMAND_SIGNATURE_DESC Desc{};
            Desc.ByteStride = Stride;
            Desc.NumArgumentDescs = 1;
            Desc.pArgumentDescs = &Argument;
            ComPtr<ID3D12CommandSignature> Signature;
            const HRESULT Result = mDevice.GetDevice().CreateCommandSignature(
                &Desc, nullptr, IID_PPV_ARGS(&Signature));
            if (FAILED(Result))
                return D3D12Failure(
                    "Failed to create a D3D12 indirect draw signature.", Result);
            mCommandList->ExecuteIndirect(
                Signature.Get(), DrawCount, Buffer->mResource.Get(), Offset,
                nullptr, 0);
            mCommandSignatures.push_back(eastl::move(Signature));
            Retain(Arguments);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::DrawIndexedIndirect(
            const FArdaProviderObjectRef& Arguments,
            uint64_t Offset,
            uint32_t DrawCount,
            uint32_t Stride)
        {
            auto* Buffer = dynamic_cast<FD3D12Buffer*>(Arguments.get());
            if (!Buffer || !Buffer->mResource)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 indexed indirect draw has the wrong buffer type.");
            D3D12_INDIRECT_ARGUMENT_DESC Argument{};
            Argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
            D3D12_COMMAND_SIGNATURE_DESC Desc{};
            Desc.ByteStride = Stride;
            Desc.NumArgumentDescs = 1;
            Desc.pArgumentDescs = &Argument;
            ComPtr<ID3D12CommandSignature> Signature;
            const HRESULT Result = mDevice.GetDevice().CreateCommandSignature(
                &Desc, nullptr, IID_PPV_ARGS(&Signature));
            if (FAILED(Result))
                return D3D12Failure(
                    "Failed to create a D3D12 indexed indirect draw signature.",
                    Result);
            mCommandList->ExecuteIndirect(
                Signature.Get(), DrawCount, Buffer->mResource.Get(), Offset,
                nullptr, 0);
            mCommandSignatures.push_back(eastl::move(Signature));
            Retain(Arguments);
            return {};
        }

        void FArdaD3D12CommandList::Dispatch(uint32_t X, uint32_t Y, uint32_t Z)
        {
            mCommandList->Dispatch(X, Y, Z);
        }

        FArdaRHIStatus FArdaD3D12CommandList::DispatchIndirect(
            const FArdaProviderObjectRef& Arguments,
            uint64_t Offset)
        {
            auto* Buffer = dynamic_cast<FD3D12Buffer*>(Arguments.get());
            if (!Buffer || !Buffer->mResource)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "D3D12 indirect dispatch has the wrong buffer type.");
            D3D12_INDIRECT_ARGUMENT_DESC Argument{};
            Argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
            D3D12_COMMAND_SIGNATURE_DESC Desc{};
            Desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
            Desc.NumArgumentDescs = 1;
            Desc.pArgumentDescs = &Argument;
            ComPtr<ID3D12CommandSignature> Signature;
            const HRESULT Result = mDevice.GetDevice().CreateCommandSignature(
                &Desc, nullptr, IID_PPV_ARGS(&Signature));
            if (FAILED(Result))
                return D3D12Failure(
                    "Failed to create a D3D12 indirect dispatch signature.",
                    Result);
            mCommandList->ExecuteIndirect(
                Signature.Get(), 1, Buffer->mResource.Get(), Offset,
                nullptr, 0);
            mCommandSignatures.push_back(eastl::move(Signature));
            Retain(Arguments);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::DispatchMesh(
            uint32_t X,
            uint32_t Y,
            uint32_t Z)
        {
            if (!mBoundGraphicsPipeline)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "A D3D12 mesh pipeline must be bound before dispatch.");
            ComPtr<ID3D12GraphicsCommandList6> MeshCommands;
            const HRESULT Result = mCommandList.As(&MeshCommands);
            if (FAILED(Result))
                return D3D12Failure(
                    "The D3D12 command list does not support mesh dispatch.",
                    Result);
            MeshCommands->DispatchMesh(X, Y, Z);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::RecordAccelStructBuild(
            const FArdaProviderObjectRef& Object,
            const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& Input,
            EArdaRHIAccelStructBuildFlags Flags)
        {
            auto* AccelStruct = dynamic_cast<FD3D12AccelStruct*>(Object.get());
            if (!AccelStruct || !AccelStruct->mResource)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "D3D12 acceleration-structure build has the wrong implementation.");
            ComPtr<ID3D12GraphicsCommandList4> RayCommands;
            HRESULT Result = mCommandList.As(&RayCommands);
            if (FAILED(Result))
                return D3D12Failure(
                    "The D3D12 command list does not support AS builds.", Result);
            const bool bUpdate = HasAnyFlags(Flags,
                EArdaRHIAccelStructBuildFlags::PerformUpdate);
            const uint64_t ScratchSize = bUpdate
                ? AccelStruct->mRequirements.mUpdateScratchSize
                : AccelStruct->mRequirements.mBuildScratchSize;
            auto Scratch = CreateD3D12BufferResource(
                &mDevice.GetDevice(), ScratchSize,
                D3D12_HEAP_TYPE_DEFAULT,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            if (!Scratch) return Scratch.mStatus;
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC Desc{};
            Desc.Inputs = Input;
            Desc.Inputs.Flags = ToD3D12BuildFlags(
                AccelStruct->mDesc.mBuildFlags | Flags);
            Desc.DestAccelerationStructureData =
                AccelStruct->mResource->GetGPUVirtualAddress();
            Desc.ScratchAccelerationStructureData =
                Scratch.mValue->GetGPUVirtualAddress();
            if (bUpdate)
                Desc.SourceAccelerationStructureData =
                    AccelStruct->mResource->GetGPUVirtualAddress();
            RayCommands->BuildRaytracingAccelerationStructure(
                &Desc, 0, nullptr);
            D3D12_RESOURCE_BARRIER Uav{};
            Uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            Uav.UAV.pResource = AccelStruct->mResource.Get();
            mCommandList->ResourceBarrier(1, &Uav);

            if (AccelStruct->mCompactedSizeGpu)
            {
                if (AccelStruct->mbCompactedSizePending)
                {
                    D3D12_RESOURCE_BARRIER ToUav{};
                    ToUav.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    ToUav.Transition.pResource =
                        AccelStruct->mCompactedSizeGpu.Get();
                    ToUav.Transition.StateBefore =
                        D3D12_RESOURCE_STATE_COPY_SOURCE;
                    ToUav.Transition.StateAfter =
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                    ToUav.Transition.Subresource =
                        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    mCommandList->ResourceBarrier(1, &ToUav);
                }
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC Post{};
                Post.DestBuffer =
                    AccelStruct->mCompactedSizeGpu->GetGPUVirtualAddress();
                Post.InfoType =
                    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;
                const D3D12_GPU_VIRTUAL_ADDRESS Address =
                    AccelStruct->mResource->GetGPUVirtualAddress();
                RayCommands->EmitRaytracingAccelerationStructurePostbuildInfo(
                    &Post, 1, &Address);
                D3D12_RESOURCE_BARRIER ToCopy{};
                ToCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                ToCopy.Transition.pResource =
                    AccelStruct->mCompactedSizeGpu.Get();
                ToCopy.Transition.StateBefore =
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
                ToCopy.Transition.StateAfter =
                    D3D12_RESOURCE_STATE_COPY_SOURCE;
                ToCopy.Transition.Subresource =
                    D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                mCommandList->ResourceBarrier(1, &ToCopy);
                mCommandList->CopyBufferRegion(
                    AccelStruct->mCompactedSizeReadback.Get(), 0,
                    AccelStruct->mCompactedSizeGpu.Get(), 0,
                    sizeof(uint64_t));
                AccelStruct->mbCompactedSizePending = true;
            }
            mUploadResources.push_back(eastl::move(Scratch.mValue));
            Retain(Object);
            mAccelStructStates[AccelStruct] = {
                EArdaRHIResourceState::AccelStructRead,
                bUpdate ? EArdaRHIAccelStructBuildState::Updated
                        : EArdaRHIAccelStructBuildState::Built};
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::BuildBottomLevelAccelStruct(
            const FArdaProviderObjectRef& Object,
            const eastl::vector<FArdaProviderRayTracingGeometry>& Geometries,
            EArdaRHIAccelStructBuildFlags Flags)
        {
            eastl::vector<D3D12_RAYTRACING_GEOMETRY_DESC> Native;
            Native.reserve(Geometries.size());
            for (const auto& Geometry : Geometries)
            {
                auto* Vertex = dynamic_cast<FD3D12Buffer*>(
                    Geometry.mVertexOrAABBBuffer.get());
                if (!Vertex || !Vertex->mResource)
                    return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                        "D3D12 BLAS geometry buffer is invalid.");
                D3D12_RAYTRACING_GEOMETRY_DESC Desc{};
                Desc.Flags = ToD3D12GeometryFlags(Geometry.mDesc.mFlags);
                if (Geometry.mDesc.mType ==
                    EArdaRHIRayTracingGeometryType::Triangles)
                {
                    Desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
                    Desc.Triangles.VertexBuffer.StartAddress =
                        Vertex->mResource->GetGPUVirtualAddress() +
                        Geometry.mDesc.mVertexOrAABBOffset;
                    Desc.Triangles.VertexBuffer.StrideInBytes =
                        Geometry.mDesc.mStride;
                    Desc.Triangles.VertexCount =
                        Geometry.mDesc.mVertexOrAABBCount;
                    Desc.Triangles.VertexFormat = ToDxgi(
                        Geometry.mDesc.mVertexFormat);
                    if (Geometry.mIndexBuffer)
                    {
                        auto* Index = dynamic_cast<FD3D12Buffer*>(
                            Geometry.mIndexBuffer.get());
                        if (!Index || !Index->mResource)
                            return FArdaRHIStatus::Error(
                                EArdaRHIResult::WrongDevice,
                                "D3D12 BLAS index buffer is invalid.");
                        Desc.Triangles.IndexBuffer =
                            Index->mResource->GetGPUVirtualAddress() +
                            Geometry.mDesc.mIndexOffset;
                        Desc.Triangles.IndexCount = Geometry.mDesc.mIndexCount;
                        Desc.Triangles.IndexFormat = ToDxgi(
                            Geometry.mDesc.mIndexFormat);
                        Retain(Geometry.mIndexBuffer);
                    }
                }
                else
                {
                    Desc.Type =
                        D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
                    Desc.AABBs.AABBCount =
                        Geometry.mDesc.mVertexOrAABBCount;
                    Desc.AABBs.AABBs.StartAddress =
                        Vertex->mResource->GetGPUVirtualAddress() +
                        Geometry.mDesc.mVertexOrAABBOffset;
                    Desc.AABBs.AABBs.StrideInBytes = Geometry.mDesc.mStride;
                }
                Retain(Geometry.mVertexOrAABBBuffer);
                Native.push_back(Desc);
            }
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS Inputs{};
            Inputs.Type =
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            Inputs.NumDescs = static_cast<UINT>(Native.size());
            Inputs.pGeometryDescs = Native.data();
            return RecordAccelStructBuild(Object, Inputs, Flags);
        }

        FArdaRHIStatus FArdaD3D12CommandList::BuildTopLevelAccelStruct(
            const FArdaProviderObjectRef& Object,
            const eastl::vector<FArdaProviderRayTracingInstance>& Instances,
            EArdaRHIAccelStructBuildFlags Flags)
        {
            const uint64_t Size = eastl::max<uint64_t>(1,
                Instances.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
            auto Upload = CreateD3D12BufferResource(
                &mDevice.GetDevice(), Size, D3D12_HEAP_TYPE_UPLOAD,
                D3D12_RESOURCE_STATE_GENERIC_READ);
            if (!Upload) return Upload.mStatus;
            void* Mapped = nullptr;
            D3D12_RANGE ReadRange{0, 0};
            HRESULT Result = Upload.mValue->Map(0, &ReadRange, &Mapped);
            if (FAILED(Result))
                return D3D12Failure(
                    "Failed to map D3D12 TLAS instances.", Result);
            auto* Output = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(Mapped);
            std::memset(Mapped, 0, static_cast<size_t>(Size));
            for (size_t Index = 0; Index < Instances.size(); ++Index)
            {
                const auto& Instance = Instances[Index];
                auto* BottomLevel = dynamic_cast<FD3D12AccelStruct*>(
                    Instance.mBottomLevelAccelStruct.get());
                if (!BottomLevel || !BottomLevel->mResource)
                {
                    Upload.mValue->Unmap(0, nullptr);
                    return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                        "D3D12 TLAS instance BLAS is invalid.");
                }
                std::memcpy(Output[Index].Transform, Instance.mTransform,
                    sizeof(Output[Index].Transform));
                Output[Index].InstanceID = Instance.mInstanceID & 0xffffffu;
                Output[Index].InstanceMask = Instance.mInstanceMask & 0xffu;
                Output[Index].InstanceContributionToHitGroupIndex =
                    Instance.mInstanceContributionToHitGroupIndex & 0xffffffu;
                Output[Index].Flags =
                    static_cast<D3D12_RAYTRACING_INSTANCE_FLAGS>(
                        Instance.mFlags & 0xffu);
                Output[Index].AccelerationStructure =
                    BottomLevel->mResource->GetGPUVirtualAddress();
                Retain(Instance.mBottomLevelAccelStruct);
            }
            Upload.mValue->Unmap(0, nullptr);
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS Inputs{};
            Inputs.Type =
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            Inputs.NumDescs = static_cast<UINT>(Instances.size());
            Inputs.InstanceDescs = Upload.mValue->GetGPUVirtualAddress();
            const FArdaRHIStatus Status = RecordAccelStructBuild(
                Object, Inputs, Flags);
            if (Status)
                mUploadResources.push_back(eastl::move(Upload.mValue));
            return Status;
        }

        FArdaRHIStatus
        FArdaD3D12CommandList::BuildTopLevelAccelStructFromBuffer(
            const FArdaProviderObjectRef& Object,
            const FArdaProviderObjectRef& InstanceObject,
            uint64_t Offset,
            size_t InstanceCount,
            EArdaRHIAccelStructBuildFlags Flags)
        {
            auto* Instances = dynamic_cast<FD3D12Buffer*>(InstanceObject.get());
            if (!Instances || !Instances->mResource)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "D3D12 indirect TLAS instance buffer is invalid.");
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS Inputs{};
            Inputs.Type =
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            Inputs.NumDescs = static_cast<UINT>(InstanceCount);
            Inputs.InstanceDescs =
                Instances->mResource->GetGPUVirtualAddress() + Offset;
            Retain(InstanceObject);
            return RecordAccelStructBuild(Object, Inputs, Flags);
        }

        FArdaRHIStatus FArdaD3D12CommandList::CompactAccelStruct(
            const FArdaProviderObjectRef& DestinationObject,
            const FArdaProviderObjectRef& SourceObject)
        {
            auto* Destination = dynamic_cast<FD3D12AccelStruct*>(
                DestinationObject.get());
            auto* Source = dynamic_cast<FD3D12AccelStruct*>(SourceObject.get());
            if (!Destination || !Source || !Destination->mResource ||
                !Source->mResource)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "D3D12 AS compaction resources are invalid.");
            ComPtr<ID3D12GraphicsCommandList4> RayCommands;
            const HRESULT Result = mCommandList.As(&RayCommands);
            if (FAILED(Result))
                return D3D12Failure(
                    "The D3D12 command list does not support AS compaction.",
                    Result);
            RayCommands->CopyRaytracingAccelerationStructure(
                Destination->mResource->GetGPUVirtualAddress(),
                Source->mResource->GetGPUVirtualAddress(),
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
            D3D12_RESOURCE_BARRIER Barrier{};
            Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            Barrier.UAV.pResource = Destination->mResource.Get();
            mCommandList->ResourceBarrier(1, &Barrier);
            Retain(DestinationObject);
            Retain(SourceObject);
            mAccelStructStates[Destination] = {
                EArdaRHIResourceState::AccelStructRead,
                EArdaRHIAccelStructBuildState::Compacted};
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::DispatchRays(
            uint32_t Width,
            uint32_t Height,
            uint32_t Depth)
        {
            if (!mBoundShaderTable)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "A D3D12 shader table must be bound before ray dispatch.");
            ComPtr<ID3D12GraphicsCommandList4> RayCommands;
            const HRESULT Result = mCommandList.As(&RayCommands);
            if (FAILED(Result))
                return D3D12Failure(
                    "The D3D12 command list does not support ray tracing.",
                    Result);
            D3D12_DISPATCH_RAYS_DESC Desc{};
            Desc.RayGenerationShaderRecord =
                mBoundShaderTable->mRayGenerationRange;
            Desc.MissShaderTable = mBoundShaderTable->mMissRange;
            Desc.HitGroupTable = mBoundShaderTable->mHitGroupRange;
            Desc.CallableShaderTable = mBoundShaderTable->mCallableRange;
            Desc.Width = Width;
            Desc.Height = Height;
            Desc.Depth = Depth;
            RayCommands->DispatchRays(&Desc);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::DispatchRaysIndirect(
            const FArdaProviderObjectRef& Arguments,
            uint64_t Offset)
        {
            if (!mBoundShaderTable)
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
                    "A D3D12 shader table must be bound before indirect ray dispatch.");
            auto* Buffer = dynamic_cast<FD3D12Buffer*>(Arguments.get());
            if (!Buffer || !Buffer->mResource ||
                Offset > Buffer->mDesc.mByteSize ||
                sizeof(uint32_t) * 3u >
                    Buffer->mDesc.mByteSize - Offset)
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                    "D3D12 indirect ray arguments require three portable dispatch dimensions.");

            D3D12_DISPATCH_RAYS_DESC Dispatch{};
            Dispatch.RayGenerationShaderRecord =
                mBoundShaderTable->mRayGenerationRange;
            Dispatch.MissShaderTable = mBoundShaderTable->mMissRange;
            Dispatch.HitGroupTable = mBoundShaderTable->mHitGroupRange;
            Dispatch.CallableShaderTable = mBoundShaderTable->mCallableRange;
            auto Upload = CreateD3D12BufferResource(
                &mDevice.GetDevice(), sizeof(Dispatch),
                D3D12_HEAP_TYPE_UPLOAD,
                D3D12_RESOURCE_STATE_GENERIC_READ);
            if (!Upload)
                return Upload.mStatus;
            void* Mapped = nullptr;
            D3D12_RANGE ReadRange{0, 0};
            HRESULT Result = Upload.mValue->Map(0, &ReadRange, &Mapped);
            if (FAILED(Result))
                return D3D12Failure(
                    "Failed to map the D3D12 indirect ray template.",
                    Result);
            std::memcpy(Mapped, &Dispatch, sizeof(Dispatch));
            Upload.mValue->Unmap(0, nullptr);

            auto NativeArguments = CreateD3D12BufferResource(
                &mDevice.GetDevice(), sizeof(Dispatch),
                D3D12_HEAP_TYPE_DEFAULT,
                D3D12_RESOURCE_STATE_COPY_DEST);
            if (!NativeArguments)
                return NativeArguments.mStatus;
            mCommandList->CopyBufferRegion(
                NativeArguments.mValue.Get(), 0,
                Upload.mValue.Get(), 0, sizeof(Dispatch));
            if (auto Status = TransitionBuffer(
                    Arguments, EArdaRHIResourceState::CopySource);
                !Status)
                return Status;
            mCommandList->CopyBufferRegion(
                NativeArguments.mValue.Get(),
                offsetof(D3D12_DISPATCH_RAYS_DESC, Width),
                Buffer->mResource.Get(), Offset,
                sizeof(uint32_t) * 3u);
            if (auto Status = TransitionBuffer(
                    Arguments, EArdaRHIResourceState::IndirectArgument);
                !Status)
                return Status;
            D3D12_RESOURCE_BARRIER ToIndirect{};
            ToIndirect.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            ToIndirect.Transition.pResource = NativeArguments.mValue.Get();
            ToIndirect.Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ToIndirect.Transition.StateBefore =
                D3D12_RESOURCE_STATE_COPY_DEST;
            ToIndirect.Transition.StateAfter =
                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
            mCommandList->ResourceBarrier(1, &ToIndirect);

            D3D12_INDIRECT_ARGUMENT_DESC Argument{};
            Argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS;
            D3D12_COMMAND_SIGNATURE_DESC SignatureDesc{};
            SignatureDesc.ByteStride = sizeof(D3D12_DISPATCH_RAYS_DESC);
            SignatureDesc.NumArgumentDescs = 1;
            SignatureDesc.pArgumentDescs = &Argument;
            ComPtr<ID3D12CommandSignature> Signature;
            Result = mDevice.GetDevice().CreateCommandSignature(
                &SignatureDesc, nullptr, IID_PPV_ARGS(&Signature));
            if (FAILED(Result))
                return D3D12Failure(
                    "Failed to create a D3D12 indirect ray-dispatch signature.",
                    Result);
            mCommandList->ExecuteIndirect(
                Signature.Get(), 1, NativeArguments.mValue.Get(), 0,
                nullptr, 0);
            mUploadResources.push_back(eastl::move(Upload.mValue));
            mUploadResources.push_back(eastl::move(NativeArguments.mValue));
            mCommandSignatures.push_back(eastl::move(Signature));
            Retain(Arguments);
            return {};
        }

        FArdaRHIStatus FArdaD3D12CommandList::DispatchWorkGraph(
            const FArdaProviderObjectRef& PipelineObject,
            const void* Records,
            uint32_t RecordCount,
            uint32_t RecordStride,
            const eastl::vector<FArdaProviderObjectRef>& Bindings)
        {
            auto* Pipeline = dynamic_cast<FD3D12WorkGraph*>(
                PipelineObject.get());
            if (!Pipeline || !Pipeline->mStateObject ||
                !Pipeline->mStateProperties ||
                !Pipeline->mWorkGraphProperties)
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "The D3D12 work-graph pipeline is invalid.");
            }
            if (!Records || RecordCount == 0 || RecordStride == 0 ||
                (Pipeline->mEntrypointRecordSize != 0 &&
                    RecordStride < Pipeline->mEntrypointRecordSize))
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "D3D12 work-graph input records are missing or smaller than the entrypoint record size.");
            }

            ComPtr<ID3D12GraphicsCommandList10> WorkGraphCommands;
            const HRESULT Result = mCommandList.As(&WorkGraphCommands);
            if (FAILED(Result))
                return D3D12Failure(
                    "The D3D12 command list does not support work graphs.",
                    Result);

            mCommandList->SetComputeRootSignature(
                Pipeline->mGlobalBindings.mRootSignature.Get());
            if (auto Status = BindDescriptorSets(
                    Pipeline->mGlobalBindings, Bindings, false);
                !Status)
            {
                return Status;
            }

            D3D12_SET_PROGRAM_DESC Program{};
            Program.Type = D3D12_PROGRAM_TYPE_WORK_GRAPH;
            Program.WorkGraph.ProgramIdentifier =
                Pipeline->mProgramIdentifier;
            Program.WorkGraph.Flags =
                Pipeline->mbInitialized.load(std::memory_order_acquire)
                    ? D3D12_SET_WORK_GRAPH_FLAG_NONE
                    : D3D12_SET_WORK_GRAPH_FLAG_INITIALIZE;
            if (Pipeline->mBackingMemory)
            {
                Program.WorkGraph.BackingMemory.StartAddress =
                    Pipeline->mBackingMemory->GetGPUVirtualAddress();
                Program.WorkGraph.BackingMemory.SizeInBytes =
                    Pipeline->mMemoryRequirements.MaxSizeInBytes;
            }
            WorkGraphCommands->SetProgram(&Program);

            D3D12_DISPATCH_GRAPH_DESC Dispatch{};
            Dispatch.Mode = D3D12_DISPATCH_MODE_NODE_CPU_INPUT;
            Dispatch.NodeCPUInput.EntrypointIndex =
                Pipeline->mEntrypointIndex;
            Dispatch.NodeCPUInput.NumRecords = RecordCount;
            Dispatch.NodeCPUInput.pRecords = Records;
            Dispatch.NodeCPUInput.RecordStrideInBytes = RecordStride;
            WorkGraphCommands->DispatchGraph(&Dispatch);
            Pipeline->mbInitialized.store(true, std::memory_order_release);

            Retain(PipelineObject);
            for (const auto& Binding : Bindings)
                Retain(Binding);
            mBoundGraphicsPipeline = nullptr;
            mBoundComputePipeline = nullptr;
            mBoundShaderTable = nullptr;
            return {};
        }

        FArdaRHIStatus
        FArdaD3D12CommandList::ClearSamplerFeedbackTexture(
            const FArdaProviderObjectRef& Object)
        {
            auto* Feedback = dynamic_cast<
                FD3D12SamplerFeedbackTexture*>(Object.get());
            if (!Feedback || !Feedback->mResource ||
                !Feedback->mDescriptor.mCount)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "The D3D12 sampler-feedback resource is invalid.");
            if (auto Status = TransitionTexture(
                    Object, Feedback->mDesc, {},
                    EArdaRHIResourceState::UnorderedAccess);
                !Status)
                return Status;
            ID3D12DescriptorHeap* Heaps[] = {
                mDevice.GetResourceHeap(), mDevice.GetSamplerHeap()};
            mCommandList->SetDescriptorHeaps(2, Heaps);
            const UINT ClearValue = Feedback->mFeedbackDesc.mFormat ==
                    EArdaRHISamplerFeedbackFormat::MinMipOpaque
                ? 0xffffffffu : 0u;
            const UINT Values[4] = {
                ClearValue, ClearValue, ClearValue, ClearValue};
            mCommandList->ClearUnorderedAccessViewUint(
                Feedback->mDescriptor.mGpu,
                Feedback->mCpuDescriptor,
                Feedback->mResource.Get(), Values, 0, nullptr);
            Retain(Object);
            return {};
        }

        FArdaRHIStatus
        FArdaD3D12CommandList::DecodeSamplerFeedbackTexture(
            const FArdaProviderObjectRef& DestinationObject,
            const FArdaRHITextureDesc& DestinationDesc,
            const FArdaProviderObjectRef& FeedbackObject,
            EArdaRHIFormat Format)
        {
            auto* Destination = dynamic_cast<FD3D12Texture*>(
                DestinationObject.get());
            auto* Feedback = dynamic_cast<
                FD3D12SamplerFeedbackTexture*>(FeedbackObject.get());
            if (!Destination || !Destination->mResource || !Feedback ||
                !Feedback->mResource || Format != EArdaRHIFormat::R8UInt)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "D3D12 sampler-feedback decode requires an R8UInt texture destination.");
            if (auto Status = TransitionTexture(
                    DestinationObject, DestinationDesc, {},
                    EArdaRHIResourceState::ResolveDest);
                !Status)
                return Status;
            if (auto Status = TransitionTexture(
                    FeedbackObject, Feedback->mDesc, {},
                    EArdaRHIResourceState::ResolveSource);
                !Status)
                return Status;
            ComPtr<ID3D12GraphicsCommandList1> Commands1;
            const HRESULT Result = mCommandList.As(&Commands1);
            if (FAILED(Result))
                return D3D12Failure(
                    "The D3D12 command list cannot decode sampler feedback.",
                    Result);
            Commands1->ResolveSubresourceRegion(
                Destination->mResource.Get(), 0, 0, 0,
                Feedback->mResource.Get(),
                Feedback->mFeedbackDesc.mFormat ==
                        EArdaRHISamplerFeedbackFormat::MinMipOpaque
                    ? UINT_MAX : 0u,
                nullptr,
                DXGI_FORMAT_R8_UINT,
                D3D12_RESOLVE_MODE_DECODE_SAMPLER_FEEDBACK);
            Retain(DestinationObject);
            Retain(FeedbackObject);
            return {};
        }

        FArdaRHIStatus
        FArdaD3D12CommandList::SetSamplerFeedbackTextureState(
            const FArdaProviderObjectRef& Object,
            EArdaRHIResourceState State)
        {
            auto* Feedback = dynamic_cast<
                FD3D12SamplerFeedbackTexture*>(Object.get());
            if (!Feedback)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "The D3D12 sampler-feedback resource is invalid.");
            return TransitionTexture(Object, Feedback->mDesc, {}, State);
        }

        TArdaRHIResult<FArdaRHINativeResourceState>
        FArdaD3D12CommandList::QuerySamplerFeedbackTextureState(
            const FArdaProviderObjectRef& Object) const
        {
            auto* Feedback = dynamic_cast<
                FD3D12SamplerFeedbackTexture*>(Object.get());
            if (!Feedback)
                return Fail<FArdaRHINativeResourceState>(
                    FArdaRHIStatus::Error(
                        EArdaRHIResult::WrongDevice,
                        "The D3D12 sampler-feedback resource is invalid."));
            return QueryTextureState(Object, Feedback->mDesc, {});
        }

        TArdaRHIResult<eastl::unique_ptr<IArdaProviderCommandList>>
        FArdaD3D12ProviderDevice::CreateCommandList(EArdaRHIQueueType Queue, bool)
        {
            if (!GetQueue(Queue))
                return Fail<eastl::unique_ptr<IArdaProviderCommandList>>(
                    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                        "The requested D3D12 command queue is unavailable."));
            const auto Type = Queue == EArdaRHIQueueType::Compute
                ? D3D12_COMMAND_LIST_TYPE_COMPUTE
                : Queue == EArdaRHIQueueType::Copy
                    ? D3D12_COMMAND_LIST_TYPE_COPY
                    : D3D12_COMMAND_LIST_TYPE_DIRECT;
            auto Commands = eastl::make_unique<FArdaD3D12CommandList>(*this, Type);
            if (auto Status = Commands->Initialize(); !Status)
                return Fail<eastl::unique_ptr<IArdaProviderCommandList>>(eastl::move(Status));
            return { eastl::unique_ptr<IArdaProviderCommandList>(Commands.release()), {} };
        }

        TArdaRHIResult<uint64_t> FArdaD3D12ProviderDevice::ExecuteCommandList(
            IArdaProviderCommandList& CommandList, EArdaRHIQueueType QueueType)
        {
            auto* Native = dynamic_cast<FArdaD3D12CommandList*>(&CommandList);
            if (!Native) return Fail<uint64_t>(FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "D3D12 command list has the wrong implementation."));
            ID3D12CommandQueue* Queue = GetQueue(QueueType);
            if (!Queue) return Fail<uint64_t>(FArdaRHIStatus::Error(
                EArdaRHIResult::Unsupported,
                "The requested D3D12 command queue is unavailable."));
            if (const FArdaRHIStatus Status =
                    Native->ValidateTrackedStartStates();
                !Status)
            {
                return Fail<uint64_t>(Status);
            }
            ID3D12CommandList* Lists[] = { Native->GetSubmitList() };
            Queue->ExecuteCommandLists(1, Lists);
            const size_t QueueIndex = GetArdaRHIQueueIndex(QueueType);
            const uint64_t QueueValue = mQueueFenceValues[QueueIndex].fetch_add(
                1, std::memory_order_relaxed) + 1;
            HRESULT Result = Queue->Signal(
                mQueueFences[QueueIndex].Get(), QueueValue);
            if (FAILED(Result)) return Fail<uint64_t>(
                D3D12Failure("Failed to signal the D3D12 submission fence.", Result));
            Native->CommitTrackedStates();
            {
                std::lock_guard<std::mutex> Lock(mSubmissionMutex);
                mPendingSubmissions.push_back({
                    QueueType,
                    QueueValue,
                    Native->CaptureSubmissionLifetime()});
            }
            return { EncodeD3D12Submission(QueueType, QueueValue), {} };
        }

        FArdaRHIStatus FArdaD3D12ProviderDevice::QueueWait(
            EArdaRHIQueueType WaitQueue,
            EArdaRHIQueueType ExecutionQueue,
            uint64_t Value)
        {
            ID3D12CommandQueue* Queue = GetQueue(WaitQueue);
            if (!Queue)
                return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                    "The requested D3D12 wait queue is unavailable.");
            const uint32_t EncodedQueue = static_cast<uint32_t>(
                Value >> D3D12SubmissionQueueShift);
            if (EncodedQueue != GetArdaRHIQueueIndex(ExecutionQueue))
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "The D3D12 submission does not belong to the declared execution queue.");
            const HRESULT Result = Queue->Wait(
                mQueueFences[EncodedQueue].Get(),
                DecodeD3D12SubmissionValue(Value));
            return FAILED(Result)
                ? D3D12Failure("Failed to enqueue a D3D12 GPU fence wait.", Result)
                : FArdaRHIStatus{};
        }

        FArdaRHIStatus FArdaD3D12ProviderDevice::WaitForSubmission(uint64_t Value)
        {
            const uint32_t QueueIndex = static_cast<uint32_t>(
                Value >> D3D12SubmissionQueueShift);
            if (QueueIndex >= mQueueFences.size())
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "The D3D12 submission queue encoding is invalid.");
            const uint64_t QueueValue = DecodeD3D12SubmissionValue(Value);
            ID3D12Fence* Fence = mQueueFences[QueueIndex].Get();
            if (!Fence || Fence->GetCompletedValue() >= QueueValue)
            {
                RunGarbageCollection();
                return {};
            }
            const HRESULT Result = Fence->SetEventOnCompletion(
                QueueValue, mFenceEvent);
            if (FAILED(Result)) return D3D12Failure(
                "Failed to arm the D3D12 submission fence.", Result);
            WaitForSingleObject(mFenceEvent, INFINITE);
            RunGarbageCollection();
            return {};
        }

        FArdaRHIStatus FArdaD3D12ProviderDevice::WaitForIdle()
        {
            ID3D12CommandQueue* Queues[] = {
                mQueue.Get(), mComputeQueue.Get(), mCopyQueue.Get() };
            for (uint32_t QueueIndex = 0;
                 QueueIndex < eastl::size(Queues); ++QueueIndex)
            {
                ID3D12CommandQueue* Queue = Queues[QueueIndex];
                if (!Queue) continue;
                const uint64_t Value = mQueueFenceValues[QueueIndex].fetch_add(
                    1, std::memory_order_relaxed) + 1;
                ID3D12Fence* Fence = mQueueFences[QueueIndex].Get();
                HRESULT Result = Queue->Signal(Fence, Value);
                if (FAILED(Result)) return D3D12Failure(
                    "Failed to signal the D3D12 idle fence.", Result);
                if (Fence->GetCompletedValue() < Value)
                {
                    Result = Fence->SetEventOnCompletion(Value, mFenceEvent);
                    if (FAILED(Result)) return D3D12Failure(
                        "Failed to arm the D3D12 idle fence.", Result);
                    WaitForSingleObject(mFenceEvent, INFINITE);
                }
            }
            RunGarbageCollection();
            return {};
        }

        void FArdaD3D12ProviderDevice::RunGarbageCollection()
        {
            std::lock_guard<std::mutex> Lock(mSubmissionMutex);
            mPendingSubmissions.erase(
                eastl::remove_if(
                    mPendingSubmissions.begin(),
                    mPendingSubmissions.end(),
                    [this](const FPendingSubmission& Submission)
                    {
                        const uint32_t QueueIndex =
                            GetArdaRHIQueueIndex(Submission.mQueue);
                        ID3D12Fence* Fence =
                            mQueueFences[QueueIndex].Get();
                        return !Fence || Fence->GetCompletedValue() >=
                            Submission.mQueueValue;
                    }),
                mPendingSubmissions.end());
        }

        ID3D12CommandQueue* FArdaD3D12ProviderDevice::GetQueue(
            EArdaRHIQueueType Queue) const noexcept
        {
            switch (Queue)
            {
            case EArdaRHIQueueType::Graphics: return mQueue.Get();
            case EArdaRHIQueueType::Compute: return mComputeQueue.Get();
            case EArdaRHIQueueType::Copy: return mCopyQueue.Get();
            default: return nullptr;
            }
        }

        void FArdaD3D12ProviderDevice::FlushPipelineCache() noexcept
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
                            "native-d3d12", Payload))
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
                eastl::shared_ptr<FArdaD3D12ProviderDevice> ProviderDevice,
                FArdaRHIDeviceRef ArdaDevice,
                HWND Window, uint32_t Width, uint32_t Height)
                : mFactory(eastl::move(Factory)), mQueue(eastl::move(Queue))
                , mProviderDevice(eastl::move(ProviderDevice)), mArdaDevice(eastl::move(ArdaDevice))
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
                const bool bCreated = CreateResources();
                if (bCreated && mCustomPresent)
                    mCustomPresent->OnBackBufferResize(Width, Height);
                return bCreated;
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
                const uint32_t BackBufferIndex =
                    mSwapChain->GetCurrentBackBufferIndex();
                if (mCustomPresent)
                {
                    if (!mCustomPresent->Present(
                            FArdaNativeObject(reinterpret_cast<uintptr_t>(
                                mTextures[BackBufferIndex]
                                    ? mTextures[BackBufferIndex]->
                                        GetPhysicalIdentity()
                                    : nullptr)),
                            mWidth, mHeight))
                    {
                        mError = "The D3D12 custom-present callback failed.";
                        return false;
                    }
                    if (!mCustomPresent->NeedsNativePresent())
                    {
                        mCustomPresent->PostPresent();
                        mError.clear();
                        return true;
                    }
                }
                HRESULT Result = mSwapChain->Present(1, 0);
                if (FAILED(Result))
                {
                    mError = D3D12Failure("Failed to present the D3D12 swap chain.", Result).mMessage;
                    return false;
                }
                if (mCustomPresent) mCustomPresent->PostPresent();
                mError.clear();
                return true;
            }

            void SetCustomPresent(
                eastl::shared_ptr<IArdaCustomPresent> Present) override
            {
                mCustomPresent = eastl::move(Present);
                if (mCustomPresent)
                    mCustomPresent->OnBackBufferResize(mWidth, mHeight);
            }
            eastl::shared_ptr<IArdaCustomPresent>
                GetCustomPresent() const override
            {
                return mCustomPresent;
            }

            void WaitForIdle() noexcept override
            {
                if (mProviderDevice) (void)mProviderDevice->WaitForIdle();
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
                    mTextures[Index] = Texture.mValue;
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
                for (auto& Texture : mTextures) Texture = nullptr;
                if (mArdaDevice) mArdaDevice->TrimDescriptorCaches();
            }

            ComPtr<IDXGIFactory6> mFactory;
            ComPtr<ID3D12CommandQueue> mQueue;
            eastl::shared_ptr<FArdaD3D12ProviderDevice> mProviderDevice;
            FArdaRHIDeviceRef mArdaDevice;
            HWND mWindow = nullptr;
            uint32_t mWidth = 0;
            uint32_t mHeight = 0;
            ComPtr<IDXGISwapChain3> mSwapChain;
            eastl::array<FArdaRHIFramebufferRef, BufferCount> mFramebuffers;
            eastl::array<FArdaRHITextureRef, BufferCount> mTextures;
            eastl::shared_ptr<IArdaCustomPresent> mCustomPresent;
            eastl::string mError;
        };

        class FArdaD3D12BackendRuntime final : public IArdaBackendRuntime
        {
        public:
            ~FArdaD3D12BackendRuntime() override
            {
                if (mProviderDevice) (void)mProviderDevice->WaitForIdle();
                mProviderDevice.reset();
                mCopyQueue.Reset();
                mComputeQueue.Reset();
                mQueue.Reset();
                mD3DDevice.Reset();
                mFactory.Reset();
                mLifetimeToken.reset();
            }

            static FArdaBackendDeviceCreateResult Create(
                const FArdaBackendConfiguration& Configuration,
                IArdaWindowSurface* WindowSurface,
                const IArdaExternalDeviceProvider* ExternalProvider)
            {
                FArdaBackendDeviceCreateResult Result;
                auto Runtime = eastl::make_unique<FArdaD3D12BackendRuntime>();
                Result.mResult = Runtime->Initialize(
                    Configuration, WindowSurface, ExternalProvider);
                if (Result.mResult == EArdaInitializeResult::Success)
                {
                    Result.mProviderDevice = Runtime->mProviderDevice;
                    Result.mBackendRuntime = eastl::move(Runtime);
                }
                else
                    Result.mError = eastl::move(Runtime->mError);
                return Result;
            }

            FArdaSwapChainCreateResult CreateSwapChain(
                uint32_t Width,
                uint32_t Height,
                FArdaRHIDeviceRef Device) override
            {
                FArdaSwapChainCreateResult Result;
                if (!mWindow)
                {
                    Result.mError =
                        "D3D12 presentation was not initialized with a window surface.";
                    return Result;
                }
                if (!Device)
                {
                    Result.mError =
                        "D3D12 presentation requires the core RHI device.";
                    return Result;
                }
                auto SwapChain = eastl::make_unique<FArdaD3D12SwapChain>(
                    mFactory, mQueue, mProviderDevice, eastl::move(Device),
                    mWindow, Width, Height);
                if (!SwapChain->Initialize())
                {
                    Result.mError = SwapChain->GetError();
                    return Result;
                }
                Result.mSwapChain = eastl::move(SwapChain);
                return Result;
            }

        private:
            EArdaInitializeResult Initialize(
                const FArdaBackendConfiguration& Configuration,
                IArdaWindowSurface* WindowSurface,
                const IArdaExternalDeviceProvider* ExternalProvider)
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
                mProviderDevice = eastl::make_shared<FArdaD3D12ProviderDevice>(
                    mD3DDevice, mQueue, mComputeQueue, mCopyQueue,
                    Configuration.mPipelineCacheDirectory,
                    Configuration.mMessageCallback, mLifetimeToken);
                if (auto Status = mProviderDevice->Initialize(); !Status)
                {
                    mError = Status.mMessage;
                    return EArdaInitializeResult::Failure;
                }
                const auto ProfileReport = mProviderDevice->GetCapabilities().Evaluate(
                    GetArdaRHIProfileRequirements(
                        Configuration.mRequiredDeviceProfile));
                const auto ExplicitReport = mProviderDevice->GetCapabilities().Evaluate(
                    Configuration.mRequiredFeatures);
                if (!ProfileReport.IsSupported() ||
                    !ExplicitReport.IsSupported())
                {
                    const auto& Failed = !ProfileReport.IsSupported()
                        ? ProfileReport : ExplicitReport;
                    mError = Failed.ToStatus().mMessage;
                    mProviderDevice.reset();
                    return EArdaInitializeResult::Unavailable;
                }
                mError.clear();
                return EArdaInitializeResult::Success;
            }
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
                QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
                Result = mD3DDevice->CreateCommandQueue(
                    &QueueDesc, IID_PPV_ARGS(&mCopyQueue));
                if (FAILED(Result))
                {
                    mError = D3D12Failure(
                        "Failed to create the D3D12 copy queue.", Result).mMessage;
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
                    else if (Queue.mType == EArdaRHIQueueType::Copy && Queue.mQueue)
                    {
                        mCopyQueue = Queue.mQueue.As<ID3D12CommandQueue*>();
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
                if (mCopyQueue &&
                    mCopyQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_COPY)
                {
                    mError = "The external D3D12 copy queue must be a copy command queue.";
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
            ComPtr<ID3D12CommandQueue> mCopyQueue;
            eastl::shared_ptr<FArdaD3D12ProviderDevice> mProviderDevice;
        };

        class FArdaD3D12BackendModule final : public IArdaBackendModule
        {
        public:
            FArdaD3D12BackendModule()
            {
                mDescriptor.mName = "native-d3d12";
                mDescriptor.mDisplayName = "Native Direct3D 12 (Agility SDK 1.619.5)";
                mDescriptor.mShaderBinaryFormat = EArdaShaderBinaryFormat::Dxil;
                mDescriptor.mShaderArtifactExtension = ".dxil";
                mDescriptor.mbSupportsOwnedDevice = true;
                mDescriptor.mbSupportsExternalDevice = true;
                mDescriptor.mPriority = 200;
            }
            const FArdaBackendModuleDescriptor& GetDescriptor() const noexcept override { return mDescriptor; }
            FArdaBackendDeviceCreateResult CreateDevice(
                const FArdaBackendConfiguration& Configuration,
                IArdaWindowSurface* WindowSurface,
                const IArdaExternalDeviceProvider* ExternalProvider) override
            {
                return FArdaD3D12BackendRuntime::Create(
                    Configuration, WindowSurface, ExternalProvider);
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
