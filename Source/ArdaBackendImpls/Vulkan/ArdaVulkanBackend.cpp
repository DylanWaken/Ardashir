#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include "RHI/ArdaRHIProvider.h"
#include "RHI/ArdaRHIProviderPipelineCache.h"
#include "ArdaBackendProvider.h"
#include "ArdaExternalInterop.h"
#include "ArdaSwapChain.h"

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

#include <atomic>
#include <array>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace arda::backend
{
    namespace
    {
        using namespace rhi;
        using namespace rhi::provider;

        constexpr uint32_t ArdaVulkanHeaderVersion = VK_HEADER_VERSION;
        constexpr uint32_t VulkanResourceDescriptorCapacity = 4096;
        constexpr uint32_t VulkanSamplerDescriptorCapacity = 1024;

        FArdaRHIStatus VulkanFailure(const char* Message, vk::Result Result = vk::Result::eErrorUnknown)
        {
            eastl::string Text = Message ? Message : "Vulkan operation failed.";
            Text += " (VkResult ";
            Text += vk::to_string(Result).c_str();
            Text += ")";
            return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Text.c_str());
        }

        VKAPI_ATTR vk::Bool32 VKAPI_CALL VulkanDebugCallback(
            vk::DebugUtilsMessageSeverityFlagBitsEXT MessageSeverity,
            vk::DebugUtilsMessageTypeFlagsEXT,
            const vk::DebugUtilsMessengerCallbackDataEXT* CallbackData,
            void* UserData) noexcept
        {
            auto* CallbackSlot =
                static_cast<std::atomic<IArdaDiagnosticCallback*>*>(UserData);
            IArdaDiagnosticCallback* Callback = CallbackSlot
                ? CallbackSlot->load(std::memory_order_acquire)
                : nullptr;
            if (!Callback)
                return VK_FALSE;
            EArdaDiagnosticSeverity Severity = EArdaDiagnosticSeverity::Info;
            if (MessageSeverity ==
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
                Severity = EArdaDiagnosticSeverity::Error;
            else if (MessageSeverity ==
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
                Severity = EArdaDiagnosticSeverity::Warning;
            try
            {
                Callback->Message(
                    Severity,
                    CallbackData && CallbackData->pMessage
                        ? CallbackData->pMessage
                        : "Vulkan validation emitted an empty diagnostic.");
            }
            catch (...)
            {
                // Diagnostics must never unwind through a Vulkan callback.
            }
            return VK_FALSE;
        }

        template <typename T>
        TArdaRHIResult<T> Fail(FArdaRHIStatus Status)
        {
            return { {}, eastl::move(Status) };
        }

        vk::Format ToVulkan(EArdaRHIFormat Format) noexcept
        {
            switch (Format)
            {
            case EArdaRHIFormat::R8UInt: return vk::Format::eR8Uint;
            case EArdaRHIFormat::R8SInt: return vk::Format::eR8Sint;
            case EArdaRHIFormat::R8UNorm: return vk::Format::eR8Unorm;
            case EArdaRHIFormat::R8SNorm: return vk::Format::eR8Snorm;
            case EArdaRHIFormat::RG8UInt: return vk::Format::eR8G8Uint;
            case EArdaRHIFormat::RG8SInt: return vk::Format::eR8G8Sint;
            case EArdaRHIFormat::RG8UNorm: return vk::Format::eR8G8Unorm;
            case EArdaRHIFormat::RG8SNorm: return vk::Format::eR8G8Snorm;
            case EArdaRHIFormat::R16UInt: return vk::Format::eR16Uint;
            case EArdaRHIFormat::R16SInt: return vk::Format::eR16Sint;
            case EArdaRHIFormat::R16UNorm: return vk::Format::eR16Unorm;
            case EArdaRHIFormat::R16SNorm: return vk::Format::eR16Snorm;
            case EArdaRHIFormat::R16Float: return vk::Format::eR16Sfloat;
            case EArdaRHIFormat::RGBA8UInt: return vk::Format::eR8G8B8A8Uint;
            case EArdaRHIFormat::RGBA8SInt: return vk::Format::eR8G8B8A8Sint;
            case EArdaRHIFormat::RGBA8UNorm: return vk::Format::eR8G8B8A8Unorm;
            case EArdaRHIFormat::RGBA8SNorm: return vk::Format::eR8G8B8A8Snorm;
            case EArdaRHIFormat::BGRA8UNorm: return vk::Format::eB8G8R8A8Unorm;
            case EArdaRHIFormat::SRGBA8UNorm: return vk::Format::eR8G8B8A8Srgb;
            case EArdaRHIFormat::SBGRA8UNorm: return vk::Format::eB8G8R8A8Srgb;
            case EArdaRHIFormat::R10G10B10A2UNorm: return vk::Format::eA2B10G10R10UnormPack32;
            case EArdaRHIFormat::R11G11B10Float: return vk::Format::eB10G11R11UfloatPack32;
            case EArdaRHIFormat::RG16UInt: return vk::Format::eR16G16Uint;
            case EArdaRHIFormat::RG16SInt: return vk::Format::eR16G16Sint;
            case EArdaRHIFormat::RG16UNorm: return vk::Format::eR16G16Unorm;
            case EArdaRHIFormat::RG16SNorm: return vk::Format::eR16G16Snorm;
            case EArdaRHIFormat::RG16Float: return vk::Format::eR16G16Sfloat;
            case EArdaRHIFormat::R32UInt: return vk::Format::eR32Uint;
            case EArdaRHIFormat::R32SInt: return vk::Format::eR32Sint;
            case EArdaRHIFormat::R32Float: return vk::Format::eR32Sfloat;
            case EArdaRHIFormat::RGBA16UInt: return vk::Format::eR16G16B16A16Uint;
            case EArdaRHIFormat::RGBA16SInt: return vk::Format::eR16G16B16A16Sint;
            case EArdaRHIFormat::RGBA16Float: return vk::Format::eR16G16B16A16Sfloat;
            case EArdaRHIFormat::RGBA16UNorm: return vk::Format::eR16G16B16A16Unorm;
            case EArdaRHIFormat::RGBA16SNorm: return vk::Format::eR16G16B16A16Snorm;
            case EArdaRHIFormat::RG32UInt: return vk::Format::eR32G32Uint;
            case EArdaRHIFormat::RG32SInt: return vk::Format::eR32G32Sint;
            case EArdaRHIFormat::RG32Float: return vk::Format::eR32G32Sfloat;
            case EArdaRHIFormat::RGB32UInt: return vk::Format::eR32G32B32Uint;
            case EArdaRHIFormat::RGB32SInt: return vk::Format::eR32G32B32Sint;
            case EArdaRHIFormat::RGB32Float: return vk::Format::eR32G32B32Sfloat;
            case EArdaRHIFormat::RGBA32UInt: return vk::Format::eR32G32B32A32Uint;
            case EArdaRHIFormat::RGBA32SInt: return vk::Format::eR32G32B32A32Sint;
            case EArdaRHIFormat::RGBA32Float: return vk::Format::eR32G32B32A32Sfloat;
            case EArdaRHIFormat::D16: return vk::Format::eD16Unorm;
            case EArdaRHIFormat::D24S8: return vk::Format::eD24UnormS8Uint;
            case EArdaRHIFormat::D32: return vk::Format::eD32Sfloat;
            case EArdaRHIFormat::D32S8: return vk::Format::eD32SfloatS8Uint;
            case EArdaRHIFormat::BC1UNorm: return vk::Format::eBc1RgbaUnormBlock;
            case EArdaRHIFormat::BC1UNormSRGB: return vk::Format::eBc1RgbaSrgbBlock;
            case EArdaRHIFormat::BC2UNorm: return vk::Format::eBc2UnormBlock;
            case EArdaRHIFormat::BC2UNormSRGB: return vk::Format::eBc2SrgbBlock;
            case EArdaRHIFormat::BC3UNorm: return vk::Format::eBc3UnormBlock;
            case EArdaRHIFormat::BC3UNormSRGB: return vk::Format::eBc3SrgbBlock;
            case EArdaRHIFormat::BC4UNorm: return vk::Format::eBc4UnormBlock;
            case EArdaRHIFormat::BC4SNorm: return vk::Format::eBc4SnormBlock;
            case EArdaRHIFormat::BC5UNorm: return vk::Format::eBc5UnormBlock;
            case EArdaRHIFormat::BC5SNorm: return vk::Format::eBc5SnormBlock;
            case EArdaRHIFormat::BC6HUFloat: return vk::Format::eBc6HUfloatBlock;
            case EArdaRHIFormat::BC6HSFloat: return vk::Format::eBc6HSfloatBlock;
            case EArdaRHIFormat::BC7UNorm: return vk::Format::eBc7UnormBlock;
            case EArdaRHIFormat::BC7UNormSRGB: return vk::Format::eBc7SrgbBlock;
            default: return vk::Format::eUndefined;
            }
        }

        vk::DeviceSize AlignDeviceSize(
            vk::DeviceSize Value,
            vk::DeviceSize Alignment) noexcept
        {
            return (Value + Alignment - 1) & ~(Alignment - 1);
        }

        vk::ShaderStageFlags ToStages(EArdaRHIShaderStage Stage) noexcept
        {
            vk::ShaderStageFlags Result{};
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::Vertex)) Result |= vk::ShaderStageFlagBits::eVertex;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::Hull)) Result |= vk::ShaderStageFlagBits::eTessellationControl;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::Domain)) Result |= vk::ShaderStageFlagBits::eTessellationEvaluation;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::Geometry)) Result |= vk::ShaderStageFlagBits::eGeometry;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::Pixel)) Result |= vk::ShaderStageFlagBits::eFragment;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::Compute)) Result |= vk::ShaderStageFlagBits::eCompute;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::Amplification)) Result |= vk::ShaderStageFlagBits::eTaskEXT;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::Mesh)) Result |= vk::ShaderStageFlagBits::eMeshEXT;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::RayGeneration)) Result |= vk::ShaderStageFlagBits::eRaygenKHR;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::Miss)) Result |= vk::ShaderStageFlagBits::eMissKHR;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::ClosestHit)) Result |= vk::ShaderStageFlagBits::eClosestHitKHR;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::AnyHit)) Result |= vk::ShaderStageFlagBits::eAnyHitKHR;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::Intersection)) Result |= vk::ShaderStageFlagBits::eIntersectionKHR;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::Callable)) Result |= vk::ShaderStageFlagBits::eCallableKHR;
            return Result ? Result : vk::ShaderStageFlagBits::eAll;
        }

        vk::ShaderStageFlagBits ToRayTracingStage(
            EArdaRHIShaderStage Stage) noexcept
        {
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::RayGeneration))
                return vk::ShaderStageFlagBits::eRaygenKHR;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::Miss))
                return vk::ShaderStageFlagBits::eMissKHR;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::ClosestHit))
                return vk::ShaderStageFlagBits::eClosestHitKHR;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::AnyHit))
                return vk::ShaderStageFlagBits::eAnyHitKHR;
            if (HasAnyFlags(Stage, EArdaRHIShaderStage::Intersection))
                return vk::ShaderStageFlagBits::eIntersectionKHR;
            return vk::ShaderStageFlagBits::eCallableKHR;
        }

        vk::DescriptorType ToDescriptorType(EArdaRHIBindingType Type)
        {
            switch (Type)
            {
            case EArdaRHIBindingType::Sampler: return vk::DescriptorType::eSampler;
            case EArdaRHIBindingType::ConstantBuffer:
            case EArdaRHIBindingType::VolatileConstantBuffer: return vk::DescriptorType::eUniformBuffer;
            case EArdaRHIBindingType::TextureUAV:
            case EArdaRHIBindingType::SamplerFeedbackTextureUAV: return vk::DescriptorType::eStorageImage;
            case EArdaRHIBindingType::TypedBufferUAV:
            case EArdaRHIBindingType::StructuredBufferUAV:
            case EArdaRHIBindingType::RawBufferUAV:
            case EArdaRHIBindingType::TypedBufferSRV:
            case EArdaRHIBindingType::StructuredBufferSRV:
            case EArdaRHIBindingType::RawBufferSRV: return vk::DescriptorType::eStorageBuffer;
            case EArdaRHIBindingType::RayTracingAccelStruct:
                return vk::DescriptorType::eAccelerationStructureKHR;
            default: return vk::DescriptorType::eSampledImage;
            }
        }

        uint32_t BindingOffset(EArdaRHIBindingType Type) noexcept
        {
            switch (Type)
            {
            case EArdaRHIBindingType::Sampler: return 128;
            case EArdaRHIBindingType::ConstantBuffer:
            case EArdaRHIBindingType::VolatileConstantBuffer: return 256;
            case EArdaRHIBindingType::TextureUAV:
            case EArdaRHIBindingType::TypedBufferUAV:
            case EArdaRHIBindingType::StructuredBufferUAV:
            case EArdaRHIBindingType::RawBufferUAV:
            case EArdaRHIBindingType::SamplerFeedbackTextureUAV: return 384;
            default: return 0;
            }
        }

        vk::SpirvResourceTypeFlagsEXT ToSpirvResourceMask(
            EArdaRHIBindingType Type) noexcept
        {
            switch (Type)
            {
            case EArdaRHIBindingType::Sampler:
                return vk::SpirvResourceTypeFlagBitsEXT::eSampler;
            case EArdaRHIBindingType::TextureSRV:
                return vk::SpirvResourceTypeFlagBitsEXT::eSampledImage |
                    vk::SpirvResourceTypeFlagBitsEXT::eReadOnlyImage;
            case EArdaRHIBindingType::TextureUAV:
            case EArdaRHIBindingType::SamplerFeedbackTextureUAV:
                return vk::SpirvResourceTypeFlagBitsEXT::eReadWriteImage;
            case EArdaRHIBindingType::ConstantBuffer:
            case EArdaRHIBindingType::VolatileConstantBuffer:
                return vk::SpirvResourceTypeFlagBitsEXT::eUniformBuffer;
            case EArdaRHIBindingType::TypedBufferSRV:
            case EArdaRHIBindingType::StructuredBufferSRV:
            case EArdaRHIBindingType::RawBufferSRV:
                return vk::SpirvResourceTypeFlagBitsEXT::eReadOnlyStorageBuffer;
            case EArdaRHIBindingType::TypedBufferUAV:
            case EArdaRHIBindingType::StructuredBufferUAV:
            case EArdaRHIBindingType::RawBufferUAV:
                return vk::SpirvResourceTypeFlagBitsEXT::eReadWriteStorageBuffer;
            case EArdaRHIBindingType::RayTracingAccelStruct:
                return vk::SpirvResourceTypeFlagBitsEXT::eAccelerationStructure;
            default:
                return {};
            }
        }

        vk::PrimitiveTopology ToTopology(EArdaRHIPrimitiveTopology Value) noexcept
        {
            switch (Value)
            {
            case EArdaRHIPrimitiveTopology::PointList: return vk::PrimitiveTopology::ePointList;
            case EArdaRHIPrimitiveTopology::LineList: return vk::PrimitiveTopology::eLineList;
            case EArdaRHIPrimitiveTopology::LineStrip: return vk::PrimitiveTopology::eLineStrip;
            case EArdaRHIPrimitiveTopology::TriangleStrip: return vk::PrimitiveTopology::eTriangleStrip;
            case EArdaRHIPrimitiveTopology::PatchList: return vk::PrimitiveTopology::ePatchList;
            default: return vk::PrimitiveTopology::eTriangleList;
            }
        }

        vk::CompareOp ToCompare(EArdaRHIComparisonFunc Value) noexcept
        {
            return static_cast<vk::CompareOp>(Value);
        }

        vk::BlendFactor ToBlend(EArdaRHIBlendFactor Value) noexcept
        {
            switch (Value)
            {
            case EArdaRHIBlendFactor::Zero: return vk::BlendFactor::eZero;
            case EArdaRHIBlendFactor::One: return vk::BlendFactor::eOne;
            case EArdaRHIBlendFactor::SourceColor: return vk::BlendFactor::eSrcColor;
            case EArdaRHIBlendFactor::InverseSourceColor: return vk::BlendFactor::eOneMinusSrcColor;
            case EArdaRHIBlendFactor::SourceAlpha: return vk::BlendFactor::eSrcAlpha;
            case EArdaRHIBlendFactor::InverseSourceAlpha: return vk::BlendFactor::eOneMinusSrcAlpha;
            case EArdaRHIBlendFactor::DestinationAlpha: return vk::BlendFactor::eDstAlpha;
            case EArdaRHIBlendFactor::InverseDestinationAlpha: return vk::BlendFactor::eOneMinusDstAlpha;
            case EArdaRHIBlendFactor::DestinationColor: return vk::BlendFactor::eDstColor;
            case EArdaRHIBlendFactor::InverseDestinationColor: return vk::BlendFactor::eOneMinusDstColor;
            }
            return vk::BlendFactor::eOne;
        }

        vk::ImageAspectFlags ImageAspect(EArdaRHIFormat Format) noexcept
        {
            if (Format == EArdaRHIFormat::D24S8 || Format == EArdaRHIFormat::D32S8)
                return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
            if (Format == EArdaRHIFormat::D16 || Format == EArdaRHIFormat::D32)
                return vk::ImageAspectFlagBits::eDepth;
            return vk::ImageAspectFlagBits::eColor;
        }

        vk::ImageAspectFlags ImageAspect(
            EArdaRHIFormat Format,
            uint32_t Plane) noexcept
        {
            const vk::ImageAspectFlags Aspects = ImageAspect(Format);
            if (Aspects ==
                (vk::ImageAspectFlagBits::eDepth |
                 vk::ImageAspectFlagBits::eStencil))
            {
                return Plane == 0
                    ? vk::ImageAspectFlagBits::eDepth
                    : vk::ImageAspectFlagBits::eStencil;
            }
            return Aspects;
        }

        size_t TextureSubresourceIndex(
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

        size_t TextureSubresourceCount(
            const FArdaRHITextureDesc& Desc) noexcept
        {
            return static_cast<size_t>(Desc.mMipLevels) * Desc.mArraySize *
                GetArdaRHIFormatPlaneCount(Desc.mFormat);
        }

        vk::ImageViewType ToViewType(EArdaRHITextureDimension Dimension) noexcept
        {
            switch (Dimension)
            {
            case EArdaRHITextureDimension::Texture1D: return vk::ImageViewType::e1D;
            case EArdaRHITextureDimension::Texture1DArray: return vk::ImageViewType::e1DArray;
            case EArdaRHITextureDimension::Texture2DArray:
            case EArdaRHITextureDimension::Texture2DMSArray: return vk::ImageViewType::e2DArray;
            case EArdaRHITextureDimension::TextureCube: return vk::ImageViewType::eCube;
            case EArdaRHITextureDimension::TextureCubeArray: return vk::ImageViewType::eCubeArray;
            case EArdaRHITextureDimension::Texture3D: return vk::ImageViewType::e3D;
            default: return vk::ImageViewType::e2D;
            }
        }

        vk::ImageLayout ToImageLayout(EArdaRHIResourceState State, bool bDepth) noexcept
        {
            if (State == EArdaRHIResourceState::Present) return vk::ImageLayout::ePresentSrcKHR;
            if (HasAnyFlags(State, EArdaRHIResourceState::RenderTarget)) return vk::ImageLayout::eColorAttachmentOptimal;
            if (HasAnyFlags(State, EArdaRHIResourceState::DepthWrite)) return vk::ImageLayout::eDepthStencilAttachmentOptimal;
            if (HasAnyFlags(State, EArdaRHIResourceState::DepthRead)) return vk::ImageLayout::eDepthStencilReadOnlyOptimal;
            if (HasAnyFlags(State, EArdaRHIResourceState::CopyDest)) return vk::ImageLayout::eTransferDstOptimal;
            if (HasAnyFlags(State, EArdaRHIResourceState::CopySource)) return vk::ImageLayout::eTransferSrcOptimal;
            if (HasAnyFlags(State, EArdaRHIResourceState::ResolveDest)) return vk::ImageLayout::eTransferDstOptimal;
            if (HasAnyFlags(State, EArdaRHIResourceState::ResolveSource)) return vk::ImageLayout::eTransferSrcOptimal;
            if (HasAnyFlags(State, EArdaRHIResourceState::Discard)) return vk::ImageLayout::eUndefined;
            if (HasAnyFlags(State, EArdaRHIResourceState::ShadingRateSource)) return vk::ImageLayout::eFragmentShadingRateAttachmentOptimalKHR;
            if (HasAnyFlags(State, EArdaRHIResourceState::UnorderedAccess)) return vk::ImageLayout::eGeneral;
            if (HasAnyFlags(State, EArdaRHIResourceState::ShaderResource))
                return bDepth ? vk::ImageLayout::eDepthStencilReadOnlyOptimal : vk::ImageLayout::eShaderReadOnlyOptimal;
            return vk::ImageLayout::eGeneral;
        }

        struct FVulkanSyncState
        {
            vk::PipelineStageFlags2 mStages;
            vk::AccessFlags2 mAccess;
        };

        FVulkanSyncState ToVulkanSyncState(
            EArdaRHIResourceState State) noexcept
        {
            if (State == EArdaRHIResourceState::Unknown)
                return { vk::PipelineStageFlagBits2::eTopOfPipe, {} };
            if (State == EArdaRHIResourceState::Present)
                return { vk::PipelineStageFlagBits2::eBottomOfPipe, {} };
            if (State == EArdaRHIResourceState::Discard)
                return { vk::PipelineStageFlagBits2::eTopOfPipe, {} };
            if (State == EArdaRHIResourceState::Common)
            {
                return {
                    vk::PipelineStageFlagBits2::eAllCommands,
                    vk::AccessFlagBits2::eMemoryRead |
                        vk::AccessFlagBits2::eMemoryWrite };
            }

            FVulkanSyncState Result;
            if (HasAnyFlags(State, EArdaRHIResourceState::ConstantBuffer))
            {
                Result.mStages |= vk::PipelineStageFlagBits2::eAllGraphics |
                    vk::PipelineStageFlagBits2::eComputeShader;
                Result.mAccess |= vk::AccessFlagBits2::eUniformRead;
            }
            if (HasAnyFlags(State, EArdaRHIResourceState::VertexBuffer))
            {
                Result.mStages |= vk::PipelineStageFlagBits2::eVertexInput;
                Result.mAccess |= vk::AccessFlagBits2::eVertexAttributeRead;
            }
            if (HasAnyFlags(State, EArdaRHIResourceState::IndexBuffer))
            {
                Result.mStages |= vk::PipelineStageFlagBits2::eVertexInput;
                Result.mAccess |= vk::AccessFlagBits2::eIndexRead;
            }
            if (HasAnyFlags(State, EArdaRHIResourceState::IndirectArgument))
            {
                Result.mStages |= vk::PipelineStageFlagBits2::eDrawIndirect;
                Result.mAccess |= vk::AccessFlagBits2::eIndirectCommandRead;
            }
            if (HasAnyFlags(State, EArdaRHIResourceState::PixelShaderResource))
            {
                Result.mStages |= vk::PipelineStageFlagBits2::eFragmentShader;
                Result.mAccess |= vk::AccessFlagBits2::eShaderRead;
            }
            if (HasAnyFlags(
                    State,
                    EArdaRHIResourceState::NonPixelShaderResource))
            {
                Result.mStages |= vk::PipelineStageFlagBits2::eVertexShader |
                    vk::PipelineStageFlagBits2::eTessellationControlShader |
                    vk::PipelineStageFlagBits2::eTessellationEvaluationShader |
                    vk::PipelineStageFlagBits2::eGeometryShader |
                    vk::PipelineStageFlagBits2::eComputeShader;
                Result.mAccess |= vk::AccessFlagBits2::eShaderRead;
            }
            if (HasAnyFlags(State, EArdaRHIResourceState::UnorderedAccess))
            {
                Result.mStages |= vk::PipelineStageFlagBits2::eAllGraphics |
                    vk::PipelineStageFlagBits2::eComputeShader;
                Result.mAccess |= vk::AccessFlagBits2::eShaderRead |
                    vk::AccessFlagBits2::eShaderWrite;
            }
            if (HasAnyFlags(State, EArdaRHIResourceState::RenderTarget))
            {
                Result.mStages |=
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput;
                Result.mAccess |= vk::AccessFlagBits2::eColorAttachmentRead |
                    vk::AccessFlagBits2::eColorAttachmentWrite;
            }
            if (HasAnyFlags(State, EArdaRHIResourceState::DepthWrite))
            {
                Result.mStages |=
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                    vk::PipelineStageFlagBits2::eLateFragmentTests;
                Result.mAccess |=
                    vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            }
            if (HasAnyFlags(State, EArdaRHIResourceState::DepthRead))
            {
                Result.mStages |=
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                    vk::PipelineStageFlagBits2::eLateFragmentTests;
                Result.mAccess |=
                    vk::AccessFlagBits2::eDepthStencilAttachmentRead;
            }
            if (HasAnyFlags(State, EArdaRHIResourceState::CopyDest) ||
                HasAnyFlags(State, EArdaRHIResourceState::ResolveDest))
            {
                Result.mStages |= vk::PipelineStageFlagBits2::eCopy;
                Result.mAccess |= vk::AccessFlagBits2::eTransferWrite;
            }
            if (HasAnyFlags(State, EArdaRHIResourceState::CopySource) ||
                HasAnyFlags(State, EArdaRHIResourceState::ResolveSource))
            {
                Result.mStages |= vk::PipelineStageFlagBits2::eCopy;
                Result.mAccess |= vk::AccessFlagBits2::eTransferRead;
            }
            if (HasAnyFlags(State, EArdaRHIResourceState::CpuRead))
            {
                Result.mStages |= vk::PipelineStageFlagBits2::eHost;
                Result.mAccess |= vk::AccessFlagBits2::eHostRead;
            }
            if (HasAnyFlags(State, EArdaRHIResourceState::AccelStructRead))
            {
                Result.mStages |=
                    vk::PipelineStageFlagBits2::eRayTracingShaderKHR |
                    vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
                Result.mAccess |=
                    vk::AccessFlagBits2::eAccelerationStructureReadKHR;
            }
            if (HasAnyFlags(State, EArdaRHIResourceState::AccelStructWrite))
            {
                Result.mStages |=
                    vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
                Result.mAccess |=
                    vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
            }
            if (HasAnyFlags(State,
                    EArdaRHIResourceState::AccelStructBuildInput))
            {
                Result.mStages |=
                    vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
                Result.mAccess |=
                    vk::AccessFlagBits2::eAccelerationStructureReadKHR;
            }
            if (HasAnyFlags(State,
                    EArdaRHIResourceState::OpacityMicromapWrite))
            {
                Result.mStages |= vk::PipelineStageFlagBits2::eMicromapBuildEXT;
                Result.mAccess |= vk::AccessFlagBits2::eMicromapWriteEXT;
            }
            if (HasAnyFlags(State,
                    EArdaRHIResourceState::OpacityMicromapBuildInput))
            {
                Result.mStages |=
                    vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
                    vk::PipelineStageFlagBits2::eMicromapBuildEXT;
                Result.mAccess |= vk::AccessFlagBits2::eMicromapReadEXT;
            }
            if (HasAnyFlags(State, EArdaRHIResourceState::ShadingRateSource))
            {
                Result.mStages |=
                    vk::PipelineStageFlagBits2::eFragmentShadingRateAttachmentKHR;
                Result.mAccess |=
                    vk::AccessFlagBits2::eFragmentShadingRateAttachmentReadKHR;
            }
            if (!Result.mStages)
                Result.mStages = vk::PipelineStageFlagBits2::eAllCommands;
            return Result;
        }

        uint64_t VulkanStageMask(vk::PipelineStageFlags2 Stages) noexcept
        {
            return static_cast<uint64_t>(
                static_cast<VkPipelineStageFlags2>(Stages));
        }

        uint64_t VulkanAccessMask(vk::AccessFlags2 Access) noexcept
        {
            return static_cast<uint64_t>(static_cast<VkAccessFlags2>(Access));
        }

        vk::BuildAccelerationStructureFlagsKHR ToVulkanBuildFlags(
            EArdaRHIAccelStructBuildFlags Flags) noexcept
        {
            vk::BuildAccelerationStructureFlagsKHR Result;
            if (HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::AllowUpdate))
                Result |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;
            if (HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::AllowCompaction))
                Result |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction;
            if (HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::PreferFastTrace))
                Result |= vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
            if (HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::PreferFastBuild))
                Result |= vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild;
            if (HasAnyFlags(Flags, EArdaRHIAccelStructBuildFlags::MinimizeMemory))
                Result |= vk::BuildAccelerationStructureFlagBitsKHR::eLowMemory;
            return Result;
        }

        vk::BuildMicromapFlagsEXT ToVulkanMicromapBuildFlags(
            EArdaRHIOpacityMicromapBuildFlags Flags) noexcept
        {
            vk::BuildMicromapFlagsEXT Result;
            if (HasAnyFlags(Flags,
                    EArdaRHIOpacityMicromapBuildFlags::FastTrace))
                Result |= vk::BuildMicromapFlagBitsEXT::ePreferFastTrace;
            if (HasAnyFlags(Flags,
                    EArdaRHIOpacityMicromapBuildFlags::FastBuild))
                Result |= vk::BuildMicromapFlagBitsEXT::ePreferFastBuild;
            if (HasAnyFlags(Flags,
                    EArdaRHIOpacityMicromapBuildFlags::AllowCompaction))
                Result |= vk::BuildMicromapFlagBitsEXT::eAllowCompaction;
            return Result;
        }

        eastl::vector<vk::MicromapUsageEXT> ToVulkanMicromapUsages(
            const eastl::vector<FArdaRHIOpacityMicromapUsageCount>& Counts)
        {
            eastl::vector<vk::MicromapUsageEXT> Result;
            Result.reserve(Counts.size());
            for (const auto& Count : Counts)
            {
                vk::MicromapUsageEXT Usage;
                Usage.count = Count.mCount;
                Usage.subdivisionLevel = Count.mSubdivisionLevel;
                Usage.format = static_cast<uint32_t>(Count.mFormat);
                Result.push_back(Usage);
            }
            return Result;
        }

        vk::GeometryFlagsKHR ToVulkanGeometryFlags(
            EArdaRHIRayTracingGeometryFlags Flags) noexcept
        {
            vk::GeometryFlagsKHR Result;
            if (HasAnyFlags(Flags, EArdaRHIRayTracingGeometryFlags::Opaque))
                Result |= vk::GeometryFlagBitsKHR::eOpaque;
            if (HasAnyFlags(Flags,
                    EArdaRHIRayTracingGeometryFlags::NoDuplicateAnyHitInvocation))
                Result |= vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation;
            return Result;
        }

        constexpr uint64_t EncodeVulkanSubmission(
            EArdaRHIQueueType Queue, uint64_t Value) noexcept
        {
            return (static_cast<uint64_t>(GetArdaRHIQueueIndex(Queue)) << 60) |
                (Value & ((uint64_t{1} << 60) - 1));
        }

        constexpr uint64_t DecodeVulkanSubmissionValue(uint64_t Token) noexcept
        {
            return Token & ((uint64_t{1} << 60) - 1);
        }

        struct FArdaVulkanContext
        {
            struct FDescriptorHeapRange
            {
                uint32_t mOffset = 0;
                uint32_t mCount = 0;
            };

            struct FDescriptorHeapState
            {
                vk::Buffer mBuffer;
                vk::DeviceMemory mMemory;
                void* mMapped = nullptr;
                vk::BindHeapInfoEXT mBindInfo;
                eastl::vector<FDescriptorHeapRange> mFreeRanges;
                uint32_t mCapacity = 0;
                uint32_t mDescriptorSize = 0;
                bool mbHostCoherent = false;
            };

            ~FArdaVulkanContext() noexcept
            {
                if (mDevice)
                {
                    // A lost device is a valid shutdown condition after a
                    // watchdog reset or driver failure. Vulkan-Hpp reports it
                    // by throwing; never let that escape a destructor and
                    // turn recoverable teardown into std::terminate/abort.
                    try
                    {
                        (void)mDevice.waitIdle();
                    }
                    catch (const vk::SystemError&)
                    {
                    }
                    const auto DestroyDescriptorHeap = [this](
                        FDescriptorHeapState& Heap)
                    {
                        if (Heap.mMapped && Heap.mMemory)
                            mDevice.unmapMemory(Heap.mMemory);
                        if (Heap.mBuffer)
                            mDevice.destroyBuffer(Heap.mBuffer);
                        if (Heap.mMemory)
                            mDevice.freeMemory(Heap.mMemory);
                        Heap.mMapped = nullptr;
                        Heap.mBuffer = nullptr;
                        Heap.mMemory = nullptr;
                    };
                    DestroyDescriptorHeap(mResourceDescriptorHeap);
                    DestroyDescriptorHeap(mSamplerDescriptorHeap);
                    for (vk::Semaphore Semaphore : mQueueTimelines)
                        if (Semaphore) mDevice.destroySemaphore(Semaphore);
                    if (mDescriptorPool) mDevice.destroyDescriptorPool(mDescriptorPool);
                    mDevice.destroy();
                }
                if (mDebugMessenger && mInstance)
                    mInstance.destroyDebugUtilsMessengerEXT(mDebugMessenger);
                if (mSurface && mInstance) mInstance.destroySurfaceKHR(mSurface);
                if (mInstance) mInstance.destroy();
            }

            uint32_t FindMemoryType(uint32_t Bits, vk::MemoryPropertyFlags Flags) const
            {
                const auto Properties = mPhysicalDevice.getMemoryProperties();
                for (uint32_t Index = 0; Index < Properties.memoryTypeCount; ++Index)
                    if ((Bits & (1u << Index)) &&
                        (Properties.memoryTypes[Index].propertyFlags & Flags) == Flags)
                        return Index;
                return UINT32_MAX;
            }

            uint32_t AllocateDescriptorHeapRange(
                bool bSampler, uint32_t Count)
            {
                std::lock_guard<std::mutex> Lock(mDescriptorHeapMutex);
                auto& Heap = bSampler
                    ? mSamplerDescriptorHeap : mResourceDescriptorHeap;
                for (auto It = Heap.mFreeRanges.begin();
                     It != Heap.mFreeRanges.end(); ++It)
                {
                    if (It->mCount < Count) continue;
                    const uint32_t Offset = It->mOffset;
                    It->mOffset += Count;
                    It->mCount -= Count;
                    if (!It->mCount) Heap.mFreeRanges.erase(It);
                    return Offset;
                }
                return UINT32_MAX;
            }

            void FreeDescriptorHeapRange(
                bool bSampler, uint32_t Offset, uint32_t Count) noexcept
            {
                if (!Count) return;
                std::lock_guard<std::mutex> Lock(mDescriptorHeapMutex);
                auto& Ranges = (bSampler
                    ? mSamplerDescriptorHeap : mResourceDescriptorHeap).
                        mFreeRanges;
                Ranges.push_back({Offset, Count});
                std::sort(Ranges.begin(), Ranges.end(),
                    [](const FDescriptorHeapRange& Left,
                       const FDescriptorHeapRange& Right)
                    {
                        return Left.mOffset < Right.mOffset;
                    });
                for (size_t Index = 1; Index < Ranges.size();)
                {
                    auto& Previous = Ranges[Index - 1];
                    const auto& Current = Ranges[Index];
                    if (Previous.mOffset + Previous.mCount ==
                        Current.mOffset)
                    {
                        Previous.mCount += Current.mCount;
                        Ranges.erase(Ranges.begin() + Index);
                    }
                    else
                        ++Index;
                }
            }

            uint32_t GetQueueFamily(EArdaRHIQueueType Queue) const noexcept
            {
                return Queue == EArdaRHIQueueType::Compute
                    ? mComputeQueueFamily
                    : Queue == EArdaRHIQueueType::Copy
                        ? mCopyQueueFamily
                        : mQueueFamily;
            }

            vk::Queue GetQueue(EArdaRHIQueueType Queue) const noexcept
            {
                return Queue == EArdaRHIQueueType::Compute
                    ? mComputeQueue
                    : Queue == EArdaRHIQueueType::Copy
                        ? mCopyQueue
                        : mQueue;
            }

            vk::detail::DynamicLoader mLoader;
            vk::Instance mInstance;
            vk::PhysicalDevice mPhysicalDevice;
            vk::Device mDevice;
            vk::Queue mQueue;
            vk::Queue mComputeQueue;
            vk::Queue mCopyQueue;
            vk::SurfaceKHR mSurface;
            vk::DebugUtilsMessengerEXT mDebugMessenger;
            vk::DescriptorPool mDescriptorPool;
            eastl::array<vk::Semaphore,
                ArdaRHIQueueTypeCount> mQueueTimelines{};
            eastl::array<std::atomic<uint64_t>,
                ArdaRHIQueueTypeCount> mQueueTimelineValues{};
            uint32_t mQueueFamily = 0;
            uint32_t mComputeQueueFamily = 0;
            uint32_t mCopyQueueFamily = 0;
            uint32_t mQueueCount = 1;
            eastl::array<bool, ArdaRHIQueueTypeCount> mQueueSparseBinding{};
            bool mbAccelerationStructure = false;
            bool mbRayTracingPipeline = false;
            bool mbRayQuery = false;
            bool mbMeshShader = false;
            bool mbOpacityMicromap = false;
            bool mbDescriptorIndexing = false;
            bool mbDescriptorBuffer = false;
            bool mbDescriptorHeap = false;
            bool mbMemoryBudget = false;
            bool mbBufferDeviceAddress = false;
            vk::PhysicalDeviceRayTracingPipelinePropertiesKHR
                mRayTracingPipelineProperties;
            vk::PhysicalDeviceAccelerationStructurePropertiesKHR
                mAccelerationStructureProperties;
            vk::PhysicalDeviceMeshShaderPropertiesEXT mMeshShaderProperties;
            vk::PhysicalDeviceDescriptorHeapPropertiesEXT
                mDescriptorHeapProperties;
            vk::PhysicalDeviceDescriptorIndexingFeatures
                mDescriptorIndexingFeatures;
            vk::PhysicalDeviceRayTracingPipelineFeaturesKHR
                mRayTracingPipelineFeatures;
            vk::PhysicalDeviceMeshShaderFeaturesEXT mMeshShaderFeatures;
            std::mutex mQueueMutex;
            std::mutex mDescriptorMutex;
            std::mutex mDescriptorHeapMutex;
            FDescriptorHeapState mResourceDescriptorHeap;
            FDescriptorHeapState mSamplerDescriptorHeap;
            std::atomic<IArdaDiagnosticCallback*> mDiagnosticCallback{ nullptr };
            std::atomic<size_t> mAllocatedDescriptorSets{ 0 };
            std::atomic<uint64_t> mSubmission{ 0 };
        };

        class FVulkanTexture final : public IArdaProviderObject
        {
        public:
            ~FVulkanTexture() override
            {
                if (mContext && mContext->mDevice)
                {
                    if (mView) mContext->mDevice.destroyImageView(mView);
                    if (mbOwned && mImage) mContext->mDevice.destroyImage(mImage);
                    if (mMemory) mContext->mDevice.freeMemory(mMemory);
                }
            }
            const void* GetIdentity() const noexcept override
            {
                return reinterpret_cast<const void*>(static_cast<VkImage>(mImage));
            }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            FArdaRHITextureDesc mDesc;
            vk::Image mImage;
            vk::DeviceMemory mMemory;
            vk::ImageView mView;
            eastl::vector<vk::ImageLayout> mLayouts;
            eastl::vector<EArdaRHIResourceState> mAbstractStates;
            eastl::vector<vk::PipelineStageFlags2> mStageMasks;
            eastl::vector<vk::AccessFlags2> mAccessMasks;
            uint32_t mQueueFamily = 0;
            std::mutex mLayoutMutex;
            FArdaProviderObjectRef mHeap;
            eastl::vector<FArdaProviderObjectRef> mSparseHeaps;
            uint64_t mCommittedBytes = 0;
            bool mbOwned = true;
        };

        class FVulkanBuffer final : public IArdaProviderObject
        {
        public:
            ~FVulkanBuffer() override
            {
                if (mContext && mContext->mDevice)
                {
                    if (mbOwned && mBuffer) mContext->mDevice.destroyBuffer(mBuffer);
                    if (mMemory) mContext->mDevice.freeMemory(mMemory);
                }
            }
            const void* GetIdentity() const noexcept override
            {
                return reinterpret_cast<const void*>(static_cast<VkBuffer>(mBuffer));
            }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            FArdaRHIBufferDesc mDesc;
            vk::Buffer mBuffer;
            vk::DeviceMemory mMemory;
            mutable std::mutex mStateMutex;
            EArdaRHIResourceState mAbstractState = EArdaRHIResourceState::Unknown;
            vk::PipelineStageFlags2 mStageMask;
            vk::AccessFlags2 mAccessMask;
            uint32_t mQueueFamily = 0;
            FArdaProviderObjectRef mHeap;
            eastl::vector<FArdaProviderObjectRef> mSparseHeaps;
            uint64_t mCommittedBytes = 0;
            bool mbStateKnown = false;
            bool mbOwned = true;
        };

        class FVulkanAccelStruct final : public IArdaProviderObject
        {
        public:
            ~FVulkanAccelStruct() override
            {
                if (!mContext || !mContext->mDevice) return;
                if (mQueryPool) mContext->mDevice.destroyQueryPool(mQueryPool);
                if (mAccelStruct)
                    mContext->mDevice.destroyAccelerationStructureKHR(
                        mAccelStruct);
                if (mBuffer) mContext->mDevice.destroyBuffer(mBuffer);
                if (mMemory) mContext->mDevice.freeMemory(mMemory);
            }
            const void* GetIdentity() const noexcept override
            {
                return reinterpret_cast<const void*>(
                    static_cast<VkAccelerationStructureKHR>(mAccelStruct));
            }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            FArdaRHIAccelStructDesc mDesc;
            FArdaRHIAccelStructMemoryRequirements mRequirements;
            vk::Buffer mBuffer;
            vk::DeviceMemory mMemory;
            vk::AccelerationStructureKHR mAccelStruct;
            vk::QueryPool mQueryPool;
            mutable std::mutex mStateMutex;
            EArdaRHIResourceState mAbstractState =
                EArdaRHIResourceState::AccelStructRead;
            EArdaRHIAccelStructBuildState mBuildState =
                EArdaRHIAccelStructBuildState::Unbuilt;
            bool mbCompactedSizePending = false;
        };

        class FVulkanOpacityMicromap final : public IArdaProviderObject
        {
        public:
            ~FVulkanOpacityMicromap() override
            {
                if (!mContext || !mContext->mDevice) return;
                if (mQueryPool) mContext->mDevice.destroyQueryPool(mQueryPool);
                if (mMicromap)
                    mContext->mDevice.destroyMicromapEXT(mMicromap);
                if (mBuffer) mContext->mDevice.destroyBuffer(mBuffer);
                if (mMemory) mContext->mDevice.freeMemory(mMemory);
            }
            const void* GetIdentity() const noexcept override
            {
                return reinterpret_cast<const void*>(
                    static_cast<VkMicromapEXT>(mMicromap));
            }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            FArdaRHIOpacityMicromapDesc mDesc;
            FArdaProviderObjectRef mInputBuffer;
            FArdaProviderObjectRef mTriangleBuffer;
            eastl::vector<vk::MicromapUsageEXT> mUsageCounts;
            vk::Buffer mBuffer;
            vk::DeviceMemory mMemory;
            vk::MicromapEXT mMicromap;
            vk::QueryPool mQueryPool;
            uint64_t mStorageAddress = 0;
            uint64_t mStorageSize = 0;
            uint64_t mBuildScratchSize = 0;
            mutable std::mutex mStateMutex;
            EArdaRHIResourceState mAbstractState =
                EArdaRHIResourceState::OpacityMicromapWrite;
            EArdaRHIAccelStructBuildState mBuildState =
                EArdaRHIAccelStructBuildState::Unbuilt;
            bool mbCompactedSizePending = false;
        };

        class FVulkanHeap final : public IArdaProviderObject
        {
        public:
            ~FVulkanHeap() override
            {
                if (mContext && mContext->mDevice && mMemory)
                    mContext->mDevice.freeMemory(mMemory);
            }
            const void* GetIdentity() const noexcept override
            {
                return reinterpret_cast<const void*>(
                    static_cast<VkDeviceMemory>(mMemory));
            }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            FArdaRHIHeapDesc mDesc;
            vk::DeviceMemory mMemory;
            uint32_t mMemoryType = UINT32_MAX;
        };

        class FVulkanStagingTexture final : public IArdaProviderObject
        {
        public:
            ~FVulkanStagingTexture() override
            {
                if (mContext && mContext->mDevice)
                {
                    if (mBuffer) mContext->mDevice.destroyBuffer(mBuffer);
                    if (mMemory) mContext->mDevice.freeMemory(mMemory);
                }
            }
            const void* GetIdentity() const noexcept override
            {
                return reinterpret_cast<const void*>(
                    static_cast<VkBuffer>(mBuffer));
            }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            FArdaRHIStagingTextureDesc mDesc;
            vk::Buffer mBuffer;
            vk::DeviceMemory mMemory;
            eastl::vector<vk::DeviceSize> mOffsets;
            eastl::vector<size_t> mRowPitches;
            eastl::vector<vk::DeviceSize> mByteSizes;
            std::mutex mMapMutex;
            bool mbMapped = false;
        };

        FArdaRHIStatus ConfigureVulkanStagingCopyRegion(
            vk::BufferImageCopy& Region,
            const FVulkanStagingTexture& Texture,
            size_t SubresourceIndex,
            const FArdaRHITextureSlice& Slice)
        {
            if (SubresourceIndex >= Texture.mOffsets.size() ||
                SubresourceIndex >= Texture.mRowPitches.size())
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "Vulkan staging texture subresource is invalid.");
            }
            const FArdaRHIFormatInfo& Format =
                GetArdaRHIFormatInfo(Texture.mDesc.mTexture.mFormat);
            if (!Format.mBytesPerBlock ||
                Slice.mX % Format.mBlockWidth ||
                Slice.mY % Format.mBlockHeight)
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "Vulkan staging texture origin is not format-block aligned.");
            }
            const uint32_t MipWidth = GetArdaRHITextureMipExtent(
                Texture.mDesc.mTexture.mWidth, Slice.mMipLevel);
            const uint32_t MipHeight = GetArdaRHITextureMipExtent(
                Texture.mDesc.mTexture.mHeight, Slice.mMipLevel);
            const uint32_t BlocksPerRow =
                (MipWidth + Format.mBlockWidth - 1) /
                    Format.mBlockWidth;
            const uint32_t BlockRows =
                (MipHeight + Format.mBlockHeight - 1) /
                    Format.mBlockHeight;
            const vk::DeviceSize RowPitch = Texture.mRowPitches[SubresourceIndex];
            const vk::DeviceSize SlicePitch = RowPitch * BlockRows;
            Region.bufferOffset = Texture.mOffsets[SubresourceIndex] +
                static_cast<vk::DeviceSize>(Slice.mZ) * SlicePitch +
                static_cast<vk::DeviceSize>(
                    Slice.mY / Format.mBlockHeight) * RowPitch +
                static_cast<vk::DeviceSize>(
                    Slice.mX / Format.mBlockWidth) *
                    Format.mBytesPerBlock;
            Region.bufferRowLength = BlocksPerRow * Format.mBlockWidth;
            Region.bufferImageHeight = BlockRows * Format.mBlockHeight;
            return {};
        }

        class FVulkanSampler final : public IArdaProviderObject
        {
        public:
            ~FVulkanSampler() override { if (mSampler) mContext->mDevice.destroySampler(mSampler); }
            const void* GetIdentity() const noexcept override { return this; }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            vk::Sampler mSampler;
            vk::SamplerCreateInfo mCreateInfo;
        };

        class FVulkanShader final : public IArdaProviderObject
        {
        public:
            ~FVulkanShader() override { if (mModule) mContext->mDevice.destroyShaderModule(mModule); }
            const void* GetIdentity() const noexcept override { return this; }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            vk::ShaderModule mModule;
            EArdaRHIShaderStage mStage = EArdaRHIShaderStage::None;
            eastl::string mEntryPoint;
        };

        class FVulkanBindingLayout final : public IArdaProviderObject
        {
        public:
            ~FVulkanBindingLayout() override { if (mLayout) mContext->mDevice.destroyDescriptorSetLayout(mLayout); }
            const void* GetIdentity() const noexcept override { return this; }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            FArdaRHIBindingLayoutDesc mDesc;
            FArdaRHIBindlessLayoutDesc mBindlessDesc;
            bool mbBindless = false;
            vk::DescriptorSetLayout mLayout;
        };

        class FVulkanBindingSet final : public IArdaProviderObject
        {
        public:
            ~FVulkanBindingSet() override
            {
                if (mContext && mSet && mContext->mDescriptorPool)
                {
                    std::lock_guard<std::mutex> Lock(mContext->mDescriptorMutex);
                    (void)mContext->mDevice.freeDescriptorSets(
                        mContext->mDescriptorPool, 1, &mSet);
                    mContext->mAllocatedDescriptorSets.fetch_sub(
                        1, std::memory_order_relaxed);
                }
                for (auto View : mOwnedViews) if (View) mContext->mDevice.destroyImageView(View);
                if (mContext && mbDescriptorHeap)
                    mContext->FreeDescriptorHeapRange(
                        mbSamplerHeap, mDescriptorBaseIndex,
                        mDescriptorCount);
            }
            const void* GetIdentity() const noexcept override { return this; }
            uint32_t GetDescriptorBaseIndex() const noexcept override
            {
                return mDescriptorBaseIndex;
            }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            vk::DescriptorSet mSet;
            FArdaProviderObjectRef mLayoutObject;
            eastl::vector<vk::ImageView> mOwnedViews;
            eastl::vector<FArdaProviderObjectRef> mRetainedObjects;
            uint32_t mDescriptorBaseIndex = 0;
            uint32_t mDescriptorCount = 0;
            bool mbDescriptorHeap = false;
            bool mbSamplerHeap = false;
        };

        class FVulkanFramebuffer final : public IArdaProviderObject
        {
        public:
            const void* GetIdentity() const noexcept override { return this; }
            eastl::vector<FArdaProviderObjectRef> mColors;
            FArdaProviderObjectRef mDepth;
            vk::Extent2D mExtent;
        };

        class FVulkanPipeline final : public IArdaProviderObject
        {
        public:
            struct FDescriptorSetGroup
            {
                uint32_t mRegisterSpace = 0;
                vk::DescriptorSetLayout mLayout;
                eastl::vector<FArdaProviderObjectRef> mLogicalLayouts;
                bool mbDescriptorHeap = false;
                uint32_t mHeapPushOffset = 0;
            };

            ~FVulkanPipeline() override
            {
                if (mPipeline) mContext->mDevice.destroyPipeline(mPipeline);
                if (mLayout) mContext->mDevice.destroyPipelineLayout(mLayout);
                for (vk::DescriptorSetLayout Layout : mOwnedSetLayouts)
                    if (Layout) mContext->mDevice.destroyDescriptorSetLayout(Layout);
            }
            const void* GetIdentity() const noexcept override
            {
                return reinterpret_cast<const void*>(static_cast<VkPipeline>(mPipeline));
            }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            vk::Pipeline mPipeline;
            vk::PipelineLayout mLayout;
            vk::ShaderStageFlags mPushStages{};
            uint32_t mPushSize = 0;
            eastl::vector<FDescriptorSetGroup> mSetGroups;
            eastl::vector<vk::DescriptorSetAndBindingMappingEXT>
                mDescriptorHeapMappings;
            eastl::vector<vk::DescriptorSetLayout> mOwnedSetLayouts;
            eastl::vector<FArdaProviderObjectRef> mRetainedLayouts;
            bool mbDescriptorHeapPipeline = false;
        };

        class FVulkanRayTracingPipeline final : public IArdaProviderObject
        {
        public:
            struct FExportGroup
            {
                eastl::string mExportName;
                uint32_t mGroupIndex = 0;
            };
            ~FVulkanRayTracingPipeline() override
            {
                if (mPipeline) mContext->mDevice.destroyPipeline(mPipeline);
            }
            const void* GetIdentity() const noexcept override
            {
                return reinterpret_cast<const void*>(
                    static_cast<VkPipeline>(mPipeline));
            }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            vk::Pipeline mPipeline;
            FVulkanPipeline mBindings;
            eastl::vector<FExportGroup> mExportGroups;
        };

        class FVulkanShaderTable final : public IArdaProviderObject
        {
        public:
            struct FRecord
            {
                bool mbWritten = false;
                EArdaRHIShaderTableRecordType mType =
                    EArdaRHIShaderTableRecordType::RayGeneration;
                uint32_t mRecordIndex = 0;
                eastl::string mExportName;
                eastl::vector<uint8_t> mLocalArguments;
                uint32_t mUserData = 0;
                uint32_t mGeometrySegment = 0;
                FArdaProviderObjectRef mGeometry;
            };
            ~FVulkanShaderTable() override
            {
                if (!mContext || !mContext->mDevice) return;
                if (mBuffer) mContext->mDevice.destroyBuffer(mBuffer);
                if (mMemory) mContext->mDevice.freeMemory(mMemory);
            }
            const void* GetIdentity() const noexcept override { return this; }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            FArdaProviderObjectRef mPipelineObject;
            FVulkanRayTracingPipeline* mPipeline = nullptr;
            FArdaRHIShaderTableDesc mDesc;
            eastl::vector<FRecord> mRecords;
            vk::Buffer mBuffer;
            vk::DeviceMemory mMemory;
            vk::StridedDeviceAddressRegionKHR mRayGeneration;
            vk::StridedDeviceAddressRegionKHR mMiss;
            vk::StridedDeviceAddressRegionKHR mHit;
            vk::StridedDeviceAddressRegionKHR mCallable;
        };

        struct FVulkanCommandRecording
        {
            ~FVulkanCommandRecording()
            {
                if (mContext && mCommandPool)
                    mContext->mDevice.destroyCommandPool(mCommandPool);
            }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            vk::CommandPool mCommandPool;
            vk::CommandBuffer mCommandBuffer;
            eastl::vector<FArdaProviderObjectRef> mRetainedObjects;
        };

        class FArdaVulkanProviderDevice;

        class FArdaVulkanCommandList final : public IArdaProviderCommandList
        {
        public:
            FArdaVulkanCommandList(
                FArdaVulkanProviderDevice& Device,
                EArdaRHIQueueType Queue)
                : mDevice(Device), mQueue(Queue) {}
            ~FArdaVulkanCommandList() override;
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
            void SetAutomaticBarriers(bool Enabled) override { mbAutomaticBarriers = Enabled; }
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
            FArdaRHIStatus SetMeshletState(
                const FArdaProviderMeshletState&) override;
            FArdaRHIStatus SetRayTracingState(
                const FArdaProviderRayTracingState&) override;
            void SetPushConstants(const void*, size_t) override;
            void Draw(const FArdaRHIDrawArguments&) override;
            void DrawIndexed(const FArdaRHIDrawArguments&) override;
            FArdaRHIStatus DrawIndirect(const FArdaProviderObjectRef&, uint64_t, uint32_t, uint32_t) override;
            FArdaRHIStatus DrawIndexedIndirect(const FArdaProviderObjectRef&, uint64_t, uint32_t, uint32_t) override;
            void Dispatch(uint32_t, uint32_t, uint32_t) override;
            FArdaRHIStatus DispatchIndirect(const FArdaProviderObjectRef&, uint64_t) override;
            FArdaRHIStatus DispatchMesh(
                uint32_t, uint32_t, uint32_t) override;
            FArdaRHIStatus DispatchRays(
                uint32_t, uint32_t, uint32_t) override;
            FArdaRHIStatus DispatchRaysIndirect(
                const FArdaProviderObjectRef&, uint64_t) override;
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
            FArdaRHIStatus BuildOpacityMicromap(
                const FArdaProviderObjectRef&) override;
            FArdaRHIStatus CompactOpacityMicromap(
                const FArdaProviderObjectRef&,
                const FArdaProviderObjectRef&) override;
            TArdaRHIResult<FArdaRHINativeResourceState>
                QueryOpacityMicromapState(
                    const FArdaProviderObjectRef&) const override;
            void BeginMarker(const char*) override {}
            void EndMarker() override {}
            vk::CommandBuffer GetCommandBuffer() const noexcept
            {
                return mRecording ? mRecording->mCommandBuffer : vk::CommandBuffer{};
            }
            eastl::shared_ptr<FVulkanCommandRecording> GetRecording() const
            {
                return mRecording;
            }
            [[nodiscard]] FArdaRHIStatus ValidateTrackedStartStates() const;
            void CommitTrackedStates();

        private:
            struct FBufferTracking
            {
                EArdaRHIResourceState mAbstractState =
                    EArdaRHIResourceState::Unknown;
                vk::PipelineStageFlags2 mStageMask;
                vk::AccessFlags2 mAccessMask;
                bool mbKnown = false;
                uint32_t mQueueFamily = 0;
                EArdaRHIResourceState mExpectedStartState =
                    EArdaRHIResourceState::Unknown;
                bool mbExpectedStartState = false;
            };
            FArdaRHIStatus CreateRecording();
            void Retain(const FArdaProviderObjectRef& Object)
            {
                if (Object && mRecording)
                    mRecording->mRetainedObjects.push_back(Object);
            }
            void EndRendering();
            void GlobalBarrier();
            eastl::vector<vk::ImageLayout>& GetTrackedTextureLayouts(
                FVulkanTexture&,
                const FArdaRHITextureDesc&);
            FBufferTracking& GetBufferTracking(FVulkanBuffer& Buffer);
            FArdaRHIStatus TransitionTextureLayout(
                const FArdaProviderObjectRef&,
                const FArdaRHITextureDesc&,
                const FArdaRHITextureSubresourceRange&,
                EArdaRHIResourceState);
            FArdaRHIStatus BindSets(const FVulkanPipeline&, const eastl::vector<FArdaProviderObjectRef>&, vk::PipelineBindPoint);
            FArdaRHIStatus RecordAccelStructBuild(
                const FArdaProviderObjectRef&,
                vk::AccelerationStructureBuildGeometryInfoKHR&,
                const eastl::vector<vk::AccelerationStructureBuildRangeInfoKHR>&,
                EArdaRHIAccelStructBuildFlags);
            FArdaVulkanProviderDevice& mDevice;
            EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
            eastl::shared_ptr<FVulkanCommandRecording> mRecording;
            vk::CommandBuffer mCommandBuffer;
            FVulkanPipeline* mBoundGraphics = nullptr;
            FVulkanPipeline* mBoundCompute = nullptr;
            std::unordered_map<const void*, eastl::vector<vk::ImageLayout>>
                mTextureLayouts;
            std::unordered_map<const void*, FVulkanTexture*>
                mTrackedTextures;
            std::unordered_map<const void*, eastl::vector<EArdaRHIResourceState>>
                mTextureAbstractStates;
            std::unordered_map<const void*, eastl::vector<EArdaRHIResourceState>>
                mTextureExpectedStartStates;
            std::unordered_map<const void*, eastl::vector<vk::PipelineStageFlags2>>
                mTextureStageMasks;
            std::unordered_map<const void*, eastl::vector<vk::AccessFlags2>>
                mTextureAccessMasks;
            std::unordered_map<const void*, uint32_t> mTextureQueueFamilies;
            std::unordered_map<FVulkanBuffer*, FBufferTracking>
                mBufferStates;
            struct FAccelStructTracking
            {
                EArdaRHIResourceState mState =
                    EArdaRHIResourceState::AccelStructRead;
                EArdaRHIAccelStructBuildState mBuildState =
                    EArdaRHIAccelStructBuildState::Unbuilt;
            };
            std::unordered_map<FVulkanAccelStruct*, FAccelStructTracking>
                mAccelStructStates;
            std::unordered_map<FVulkanOpacityMicromap*, FAccelStructTracking>
                mOpacityMicromapStates;
            class FVulkanRayTracingPipeline* mBoundRayTracing = nullptr;
            class FVulkanShaderTable* mBoundShaderTable = nullptr;
            bool mbOpen = false;
            bool mbRendering = false;
            bool mbAutomaticBarriers = true;
            bool mbDescriptorHeapsBound = false;
        };

        class FArdaVulkanProviderDevice final : public IArdaRHIProviderDevice
        {
        public:
            FArdaVulkanProviderDevice(
                eastl::shared_ptr<FArdaVulkanContext> Context,
                std::filesystem::path PipelineCacheDirectory,
                IArdaDiagnosticCallback* DiagnosticCallback)
                : mContext(eastl::move(Context))
                , mPipelineCacheDirectory(eastl::move(PipelineCacheDirectory))
                , mDiagnosticCallback(DiagnosticCallback) {}
            ~FArdaVulkanProviderDevice() override
            {
                (void)WaitForIdle();
                FlushPipelineCache();
            }
            FArdaRHIStatus Initialize();
            const FArdaRHICapabilities& GetCapabilities() const noexcept override { return mCapabilities; }
            EArdaRHINativeResourceType GetTextureImportType() const noexcept override { return EArdaRHINativeResourceType::VulkanImage; }
            EArdaRHINativeResourceType GetBufferImportType() const noexcept override { return EArdaRHINativeResourceType::VulkanBuffer; }
            FArdaProviderObjectResult CreateTexture(const FArdaRHITextureDesc&) override;
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
            FArdaProviderObjectResult CreateMeshletPipeline(
                const FArdaProviderMeshletPipelineCreateInfo&) override;
            FArdaProviderObjectResult CreateRayTracingPipeline(
                const FArdaProviderRayTracingPipelineCreateInfo&) override;
            FArdaProviderObjectResult CreateShaderTable(
                const FArdaProviderObjectRef&,
                const FArdaRHIShaderTableDesc&) override;
            TArdaRHIResult<FArdaRHIAccelStructMemoryRequirements>
                GetAccelStructBuildMemoryRequirements(
                    const FArdaRHIAccelStructDesc&,
                    const eastl::vector<FArdaProviderRayTracingGeometry>&) override;
            FArdaProviderObjectResult CreateAccelStruct(
                const FArdaRHIAccelStructDesc&,
                const FArdaRHIAccelStructMemoryRequirements&) override;
            FArdaProviderObjectResult CreateOpacityMicromap(
                const FArdaRHIOpacityMicromapDesc&,
                const FArdaProviderObjectRef&,
                const FArdaProviderObjectRef&) override;
            TArdaRHIResult<uint64_t> GetOpacityMicromapCompactedSize(
                const FArdaProviderObjectRef&) override;
            TArdaRHIResult<uint64_t> GetAccelStructCompactedSize(
                const FArdaProviderObjectRef&) override;
            uint64_t GetAccelStructDeviceAddress(
                const FArdaProviderObjectRef&) const noexcept override;
            uint64_t GetOpacityMicromapDeviceAddress(
                const FArdaProviderObjectRef&) const noexcept override;
            FArdaRHIStatus SetShaderTableRecord(
                const FArdaProviderObjectRef&,
                const FArdaRHIShaderTableRecordDesc&,
                const FArdaProviderObjectRef&,
                const FArdaProviderObjectRef&) override;
            FArdaRHIStatus CommitShaderTable(
                const FArdaProviderObjectRef&) override;
            FArdaRHIStatus SetShaderTableRayGeneration(
                const FArdaProviderObjectRef&, const char*,
                const FArdaProviderObjectRef&) override;
            FArdaRHIStatus AddShaderTableEntry(
                const FArdaProviderObjectRef&, const char*,
                const FArdaProviderObjectRef&, uint32_t) override;
            TArdaRHIResult<eastl::unique_ptr<IArdaProviderCommandList>> CreateCommandList(EArdaRHIQueueType, bool) override;
            TArdaRHIResult<uint64_t> ExecuteCommandList(IArdaProviderCommandList&, EArdaRHIQueueType) override;
            FArdaRHIStatus QueueWait(
                EArdaRHIQueueType, EArdaRHIQueueType, uint64_t) override;
            FArdaRHIStatus WaitForSubmission(uint64_t) override;
            FArdaRHIStatus WaitForIdle() override;
            void RunGarbageCollection() override;
            FArdaProviderLifetimeStats GetLifetimeStats() const noexcept override;
            void FlushPipelineCache() noexcept override;

            TArdaRHIResult<vk::PipelineLayout> CreatePipelineLayout(
                const eastl::vector<FArdaProviderObjectRef>&,
                FVulkanPipeline&);
            FArdaProviderObjectResult CreateMergedBindingSet(
                vk::DescriptorSetLayout,
                const eastl::vector<FArdaProviderObjectRef>&,
                const eastl::vector<FArdaProviderObjectRef>&);
            eastl::shared_ptr<FArdaVulkanContext> GetContext() const { return mContext; }

        private:
            struct FPendingSubmission
            {
                uint64_t mValue = 0;
                vk::Fence mFence;
                eastl::shared_ptr<FVulkanCommandRecording> mRecording;
            };
            FArdaRHICapabilities mCapabilities;
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            vk::PipelineCache mPipelineCache;
            std::filesystem::path mPipelineCacheDirectory;
            IArdaDiagnosticCallback* mDiagnosticCallback = nullptr;
            std::mutex mPipelineCacheMutex;
            mutable std::mutex mSubmissionMutex;
            eastl::vector<FPendingSubmission> mPendingSubmissions;
            bool mbPipelineCacheDirty = false;
        };

        FArdaRHIStatus FArdaVulkanProviderDevice::Initialize()
        {
            try
            {
                const eastl::array<vk::DescriptorPoolSize, 6> Sizes = {{
                    { vk::DescriptorType::eSampler, 2048 },
                    { vk::DescriptorType::eSampledImage, 8192 },
                    { vk::DescriptorType::eStorageImage, 4096 },
                    { vk::DescriptorType::eUniformBuffer, 4096 },
                    { vk::DescriptorType::eStorageBuffer, 8192 },
                    { vk::DescriptorType::eAccelerationStructureKHR, 4096 }
                }};
                vk::DescriptorPoolCreateInfo PoolInfo;
                PoolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
                if (mContext->mbDescriptorIndexing)
                    PoolInfo.flags |=
                        vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;
                PoolInfo.maxSets = 8192;
                PoolInfo.poolSizeCount = static_cast<uint32_t>(Sizes.size());
                PoolInfo.pPoolSizes = Sizes.data();
                mContext->mDescriptorPool = mContext->mDevice.createDescriptorPool(PoolInfo);
                if (mContext->mbDescriptorHeap)
                {
                    const auto InitializeDescriptorHeap = [this](
                        FArdaVulkanContext::FDescriptorHeapState& Heap,
                        bool bSampler,
                        uint32_t RequestedCapacity) -> FArdaRHIStatus
                    {
                        const auto& Properties =
                            mContext->mDescriptorHeapProperties;
                        const uint64_t DescriptorSize = bSampler
                            ? Properties.samplerDescriptorSize
                            : eastl::max<uint64_t>(
                                Properties.bufferDescriptorSize,
                                Properties.imageDescriptorSize);
                        const uint64_t ReservedSize = bSampler
                            ? Properties.minSamplerHeapReservedRange
                            : Properties.minResourceHeapReservedRange;
                        const uint64_t MaximumSize = bSampler
                            ? Properties.maxSamplerHeapSize
                            : Properties.maxResourceHeapSize;
                        if (!DescriptorSize || MaximumSize <= ReservedSize)
                            return FArdaRHIStatus::Error(
                                EArdaRHIResult::BackendFailure,
                                "Vulkan reported invalid descriptor-heap sizes.");
                        const uint64_t MaximumCapacity =
                            (MaximumSize - ReservedSize) / DescriptorSize;
                        Heap.mCapacity = static_cast<uint32_t>(
                            eastl::min<uint64_t>(
                                RequestedCapacity, MaximumCapacity));
                        Heap.mDescriptorSize = static_cast<uint32_t>(
                            DescriptorSize);
                        if (!Heap.mCapacity)
                            return FArdaRHIStatus::Error(
                                EArdaRHIResult::BackendFailure,
                                "Vulkan descriptor-heap capacity is zero.");
                        const uint64_t HeapSize =
                            static_cast<uint64_t>(Heap.mCapacity) *
                                DescriptorSize + ReservedSize;
                        vk::BufferCreateInfo BufferInfo;
                        BufferInfo.size = HeapSize;
                        BufferInfo.usage =
                            vk::BufferUsageFlagBits::eDescriptorHeapEXT |
                            vk::BufferUsageFlagBits::eShaderDeviceAddress |
                            vk::BufferUsageFlagBits::eTransferDst;
                        Heap.mBuffer =
                            mContext->mDevice.createBuffer(BufferInfo);
                        const auto Requirements =
                            mContext->mDevice.getBufferMemoryRequirements(
                                Heap.mBuffer);
                        const auto MemoryProperties =
                            mContext->mPhysicalDevice.getMemoryProperties();
                        uint32_t MemoryType = mContext->FindMemoryType(
                            Requirements.memoryTypeBits,
                            vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent);
                        if (MemoryType == UINT32_MAX)
                            MemoryType = mContext->FindMemoryType(
                                Requirements.memoryTypeBits,
                                vk::MemoryPropertyFlagBits::eHostVisible);
                        if (MemoryType == UINT32_MAX)
                            return FArdaRHIStatus::Error(
                                EArdaRHIResult::BackendFailure,
                                "No host-visible Vulkan descriptor-heap memory type is available.");
                        Heap.mbHostCoherent = static_cast<bool>(
                            MemoryProperties.memoryTypes[MemoryType].
                                propertyFlags &
                            vk::MemoryPropertyFlagBits::eHostCoherent);
                        vk::MemoryAllocateFlagsInfo AddressFlags(
                            vk::MemoryAllocateFlagBits::eDeviceAddress);
                        vk::MemoryAllocateInfo Allocate(
                            Requirements.size, MemoryType);
                        Allocate.pNext = &AddressFlags;
                        Heap.mMemory =
                            mContext->mDevice.allocateMemory(Allocate);
                        mContext->mDevice.bindBufferMemory(
                            Heap.mBuffer, Heap.mMemory, 0);
                        Heap.mMapped = mContext->mDevice.mapMemory(
                            Heap.mMemory, 0, VK_WHOLE_SIZE);
                        const vk::DeviceAddress Address =
                            mContext->mDevice.getBufferAddress(
                                vk::BufferDeviceAddressInfo(Heap.mBuffer));
                        Heap.mBindInfo.heapRange.address = Address;
                        Heap.mBindInfo.heapRange.size = HeapSize;
                        Heap.mBindInfo.reservedRangeOffset =
                            static_cast<uint64_t>(Heap.mCapacity) *
                                DescriptorSize;
                        Heap.mBindInfo.reservedRangeSize = ReservedSize;
                        Heap.mFreeRanges.push_back(
                            {0, Heap.mCapacity});
                        return {};
                    };
                    if (auto Status = InitializeDescriptorHeap(
                            mContext->mResourceDescriptorHeap,
                            false, VulkanResourceDescriptorCapacity); !Status)
                        return Status;
                    if (auto Status = InitializeDescriptorHeap(
                            mContext->mSamplerDescriptorHeap,
                            true, VulkanSamplerDescriptorCapacity); !Status)
                        return Status;
                }
                vk::SemaphoreTypeCreateInfo TimelineType(
                    vk::SemaphoreType::eTimeline, 0);
                vk::SemaphoreCreateInfo TimelineInfo;
                TimelineInfo.pNext = &TimelineType;
                for (vk::Semaphore& Semaphore : mContext->mQueueTimelines)
                    Semaphore = mContext->mDevice.createSemaphore(TimelineInfo);

                if (!mPipelineCacheDirectory.empty())
                {
                    const eastl::string BackendName = "native-vulkan";
                    const auto Path = pipeline_cache::MakePath(
                        mPipelineCacheDirectory, BackendName);
                    std::vector<uint8_t> Payload;
                    std::error_code Error;
                    const bool bExists = std::filesystem::exists(Path, Error);
                    const bool bValid = bExists && pipeline_cache::ReadBlob(
                        Path, BackendName, EArdaBackendType::Vulkan, Payload);
                    vk::PipelineCacheCreateInfo CacheInfo;
                    if (bValid)
                    {
                        CacheInfo.initialDataSize = Payload.size();
                        CacheInfo.pInitialData = Payload.data();
                    }
                    try
                    {
                        mPipelineCache = mContext->mDevice.createPipelineCache(CacheInfo);
                        if (bValid)
                            pipeline_cache::Message(mDiagnosticCallback,
                                EArdaDiagnosticSeverity::Info,
                                "Vulkan persistent pipeline cache data was accepted.");
                        else if (bExists)
                            pipeline_cache::Message(mDiagnosticCallback,
                                EArdaDiagnosticSeverity::Warning,
                                "Ignoring a corrupt, truncated, or wrong-backend pipeline cache blob.");
                    }
                    catch (const vk::SystemError&)
                    {
                        if (bValid)
                        {
                            pipeline_cache::Message(mDiagnosticCallback,
                                EArdaDiagnosticSeverity::Warning,
                                "Vulkan rejected persistent pipeline cache data; using an empty cache.");
                            mPipelineCache = mContext->mDevice.createPipelineCache({});
                        }
                    }
                }
            }
            catch (const vk::SystemError& Error)
            {
                return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
            }
            mCapabilities.mQueues.mbGraphics = true;
            mCapabilities.mQueues.mbCompute = mContext->mComputeQueue != nullptr;
            mCapabilities.mQueues.mbCopy = mContext->mCopyQueue != nullptr;
            mCapabilities.mQueues.mGraphicsFamily = mContext->mQueueFamily;
            mCapabilities.mQueues.mComputeFamily =
                mContext->mComputeQueueFamily;
            mCapabilities.mQueues.mCopyFamily = mContext->mCopyQueueFamily;
            mCapabilities.mQueues.mbDedicatedComputeFamily =
                mContext->mComputeQueueFamily != mContext->mQueueFamily;
            mCapabilities.mQueues.mbDedicatedCopyFamily =
                mContext->mCopyQueueFamily != mContext->mQueueFamily &&
                mContext->mCopyQueueFamily != mContext->mComputeQueueFamily;
            mCapabilities.mQueues.mbGpuWaits = true;
            mCapabilities.mQueues.mbTimelineSynchronization = true;
            mCapabilities.mQueues.mbQueueFamilyOwnershipTransfer =
                mCapabilities.mQueues.mbDedicatedComputeFamily ||
                mCapabilities.mQueues.mbDedicatedCopyFamily;
            mCapabilities.mQueues.mbSparseBindingQueue =
                eastl::any_of(
                    mContext->mQueueSparseBinding.begin(),
                    mContext->mQueueSparseBinding.end(),
                    [](bool bSupported) { return bSupported; });
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
            const auto SparseFeatures =
                mContext->mPhysicalDevice.getFeatures();
            auto& Residency = mCapabilities.mResidency;
            Residency.mbSparseBinding =
                SparseFeatures.sparseBinding &&
                mCapabilities.mQueues.mbSparseBindingQueue;
            Residency.mbReservedBuffers = Residency.mbSparseBinding &&
                SparseFeatures.sparseResidencyBuffer;
            Residency.mbReservedTexture2D = Residency.mbSparseBinding &&
                SparseFeatures.sparseResidencyImage2D;
            Residency.mbReservedTexture3D = Residency.mbSparseBinding &&
                SparseFeatures.sparseResidencyImage3D;
            Residency.mbAliasedMappings = Residency.mbSparseBinding &&
                SparseFeatures.sparseResidencyAliased;
            Residency.mbStreamingBudget = mContext->mbMemoryBudget;
            Residency.mTileSizeInBytes = 65536;
            mCapabilities.mDescriptors.mbBindless = true;
            const auto& Indexing = mContext->mDescriptorIndexingFeatures;
            auto& Descriptors = mCapabilities.mDescriptors;
            Descriptors.mbRuntimeDescriptorArrays =
                Indexing.runtimeDescriptorArray;
            Descriptors.mbUnboundedArrays =
                Indexing.runtimeDescriptorArray;
            Descriptors.mbPartiallyBound =
                Indexing.descriptorBindingPartiallyBound;
            Descriptors.mbUpdateAfterBind =
                Indexing.descriptorBindingSampledImageUpdateAfterBind &&
                Indexing.descriptorBindingStorageImageUpdateAfterBind &&
                Indexing.descriptorBindingStorageBufferUpdateAfterBind &&
                Indexing.descriptorBindingUniformBufferUpdateAfterBind;
            Descriptors.mbUpdateUnusedWhilePending =
                Indexing.descriptorBindingUpdateUnusedWhilePending;
            Descriptors.mbVariableDescriptorCount =
                Indexing.descriptorBindingVariableDescriptorCount;
            Descriptors.mbDescriptorBuffer = mContext->mbDescriptorBuffer;
            Descriptors.mbDescriptorHeap = mContext->mbDescriptorHeap;
            Descriptors.mbDirectResourceHeapIndexing =
                mContext->mbDescriptorHeap &&
                mContext->mResourceDescriptorHeap.mBuffer;
            Descriptors.mbDirectSamplerHeapIndexing =
                mContext->mbDescriptorHeap &&
                mContext->mSamplerDescriptorHeap.mBuffer;
            // These are deliberately bounded by the native pool owned by this
            // device. "Unbounded" describes the shader runtime array; its
            // backing allocation still has a device-selected finite capacity.
            Descriptors.mMaxResourceDescriptors =
                mContext->mbDescriptorHeap
                    ? mContext->mResourceDescriptorHeap.mCapacity
                    : VulkanResourceDescriptorCapacity;
            Descriptors.mMaxSamplerDescriptors =
                mContext->mbDescriptorHeap
                    ? mContext->mSamplerDescriptorHeap.mCapacity
                    : VulkanSamplerDescriptorCapacity;
            mCapabilities.mMeshShaderTier = mContext->mbMeshShader
                ? EArdaRHIMeshShaderTier::MeshAndAmplificationShaders
                : EArdaRHIMeshShaderTier::None;
            if (mContext->mbAccelerationStructure)
            {
                auto& Ray = mCapabilities.mRayTracing;
                Ray.mbInfrastructure = true;
                Ray.mbHardwareAccelerated = true;
                Ray.mbAccelerationStructures = true;
                Ray.mbBottomLevel = true;
                Ray.mbTopLevel = true;
                Ray.mbBuildUpdate = true;
                Ray.mbCompaction = true;
                Ray.mbIndirectTopLevelBuild = true;
                Ray.mAccelerationStructureAlignment = 256;
                if (mContext->mbRayTracingPipeline)
                {
                    Ray.mbPipelineShaders = true;
                    Ray.mbIndirectDispatch =
                        mContext->mRayTracingPipelineFeatures.
                            rayTracingPipelineTraceRaysIndirect;
                    Ray.mbLocalShaderTableArguments = true;
                    Ray.mbPersistentShaderTables = true;
                    Ray.mShaderIdentifierSize =
                        mContext->mRayTracingPipelineProperties.
                            shaderGroupHandleSize;
                    Ray.mShaderRecordAlignment =
                        mContext->mRayTracingPipelineProperties.
                            shaderGroupHandleAlignment;
                    Ray.mShaderTableAlignment =
                        mContext->mRayTracingPipelineProperties.
                            shaderGroupBaseAlignment;
                    Ray.mMaxRecursionDepth =
                        mContext->mRayTracingPipelineProperties.
                            maxRayRecursionDepth;
                    Ray.mMaxRayDispatchInvocations =
                        mContext->mRayTracingPipelineProperties.
                            maxRayDispatchInvocationCount;
                }
                Ray.mbInlineRayQueries = mContext->mbRayQuery;
                Ray.mbOpacityMicromaps = mContext->mbOpacityMicromap;
            }
            mCapabilities.mbShaderLibraries = true;
            mCapabilities.mbPipelineCachePersistence = mPipelineCache != nullptr;
            mCapabilities.mMachineLearning.mbBufferDeviceAddress =
                mContext->mbBufferDeviceAddress;
            return {};
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateTexture(
            const FArdaRHITextureDesc& Desc)
        {
            try
            {
                const vk::Format Format = ToVulkan(Desc.mFormat);
                if (Format == vk::Format::eUndefined)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::Unsupported, "The Vulkan texture format is unsupported."));
                auto Texture = eastl::make_shared<FVulkanTexture>();
                Texture->mContext = mContext;
                Texture->mDesc = Desc;
                const size_t SubresourceCount =
                    TextureSubresourceCount(Desc);
                Texture->mLayouts.assign(
                    SubresourceCount, vk::ImageLayout::eUndefined);
                Texture->mAbstractStates.assign(
                    SubresourceCount, Desc.mInitialState);
                Texture->mStageMasks.assign(
                    SubresourceCount, vk::PipelineStageFlagBits2::eTopOfPipe);
                Texture->mAccessMasks.assign(
                    SubresourceCount, vk::AccessFlags2{});
                Texture->mQueueFamily = mContext->mQueueFamily;
                vk::ImageCreateInfo Info;
                Info.imageType = Desc.mDimension == EArdaRHITextureDimension::Texture3D
                    ? vk::ImageType::e3D
                    : ((Desc.mDimension == EArdaRHITextureDimension::Texture1D ||
                        Desc.mDimension == EArdaRHITextureDimension::Texture1DArray)
                        ? vk::ImageType::e1D : vk::ImageType::e2D);
                Info.format = Format;
                Info.extent = vk::Extent3D(Desc.mWidth, Desc.mHeight, Desc.mDepth);
                Info.mipLevels = Desc.mMipLevels;
                Info.arrayLayers = Desc.mArraySize;
                Info.samples = static_cast<vk::SampleCountFlagBits>(eastl::max(1u, Desc.mSampleCount));
                Info.tiling = vk::ImageTiling::eOptimal;
                Info.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
                if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::ShaderResource))
                    Info.usage |= vk::ImageUsageFlagBits::eSampled;
                if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::UnorderedAccess))
                    Info.usage |= vk::ImageUsageFlagBits::eStorage;
                if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::RenderTarget))
                    Info.usage |= vk::ImageUsageFlagBits::eColorAttachment;
                if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::DepthStencil))
                    Info.usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
                if (Desc.mDimension == EArdaRHITextureDimension::TextureCube ||
                    Desc.mDimension == EArdaRHITextureDimension::TextureCubeArray)
                    Info.flags |= vk::ImageCreateFlagBits::eCubeCompatible;
                if (Desc.mbVirtual && Desc.mbTiled)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidArgument,
                        "A Vulkan image cannot be both placed/virtual and sparse/tiled."));
                if (Desc.mbTiled)
                    Info.flags |= vk::ImageCreateFlagBits::eSparseBinding |
                        vk::ImageCreateFlagBits::eSparseResidency |
                        (mCapabilities.mResidency.mbAliasedMappings
                            ? vk::ImageCreateFlagBits::eSparseAliased
                            : vk::ImageCreateFlags{});
                if (Desc.mbVirtual)
                    Info.flags |= vk::ImageCreateFlagBits::eAlias;
                Info.sharingMode = vk::SharingMode::eExclusive;
                Info.initialLayout = vk::ImageLayout::eUndefined;
                Texture->mImage = mContext->mDevice.createImage(Info);
                if (Desc.mbVirtual)
                    return { Texture, {} };
                if (Desc.mbTiled)
                {
                    vk::ImageViewCreateInfo ViewInfo;
                    ViewInfo.image = Texture->mImage;
                    ViewInfo.viewType = ToViewType(Desc.mDimension);
                    ViewInfo.format = Format;
                    ViewInfo.subresourceRange = vk::ImageSubresourceRange(
                        ImageAspect(Desc.mFormat), 0, Desc.mMipLevels,
                        0, Desc.mArraySize);
                    Texture->mView =
                        mContext->mDevice.createImageView(ViewInfo);
                    return {Texture, {}};
                }
                const auto Requirements = mContext->mDevice.getImageMemoryRequirements(Texture->mImage);
                const uint32_t MemoryType = mContext->FindMemoryType(
                    Requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
                if (MemoryType == UINT32_MAX)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::BackendFailure, "No Vulkan device-local image memory type is available."));
                Texture->mMemory = mContext->mDevice.allocateMemory(
                    vk::MemoryAllocateInfo(Requirements.size, MemoryType));
                mContext->mDevice.bindImageMemory(Texture->mImage, Texture->mMemory, 0);
                vk::ImageViewCreateInfo ViewInfo;
                ViewInfo.image = Texture->mImage;
                ViewInfo.viewType = ToViewType(Desc.mDimension);
                ViewInfo.format = Format;
                ViewInfo.subresourceRange = vk::ImageSubresourceRange(
                    ImageAspect(Desc.mFormat), 0, Desc.mMipLevels, 0, Desc.mArraySize);
                Texture->mView = mContext->mDevice.createImageView(ViewInfo);
                return { Texture, {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateBuffer(
            const FArdaRHIBufferDesc& Desc)
        {
            try
            {
                auto Buffer = eastl::make_shared<FVulkanBuffer>();
                Buffer->mContext = mContext;
                Buffer->mDesc = Desc;
                const FVulkanSyncState InitialSync =
                    ToVulkanSyncState(Desc.mInitialState);
                Buffer->mAbstractState = Desc.mInitialState;
                Buffer->mStageMask = InitialSync.mStages;
                Buffer->mAccessMask = InitialSync.mAccess;
                Buffer->mbStateKnown = true;
                Buffer->mQueueFamily = mContext->mQueueFamily;
                vk::BufferCreateInfo Info;
                Info.size = eastl::max<uint64_t>(1, Desc.mByteSize);
                Info.usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
                if (HasAnyFlags(Desc.mUsage, EArdaRHIBufferUsage::Vertex)) Info.usage |= vk::BufferUsageFlagBits::eVertexBuffer;
                if (HasAnyFlags(Desc.mUsage, EArdaRHIBufferUsage::Index)) Info.usage |= vk::BufferUsageFlagBits::eIndexBuffer;
                if (HasAnyFlags(Desc.mUsage, EArdaRHIBufferUsage::Constant)) Info.usage |= vk::BufferUsageFlagBits::eUniformBuffer;
                if (HasAnyFlags(Desc.mUsage, EArdaRHIBufferUsage::ShaderResource) ||
                    HasAnyFlags(Desc.mUsage, EArdaRHIBufferUsage::UnorderedAccess) ||
                    HasAnyFlags(Desc.mUsage, EArdaRHIBufferUsage::Structured) ||
                    HasAnyFlags(Desc.mUsage, EArdaRHIBufferUsage::Raw))
                    Info.usage |= vk::BufferUsageFlagBits::eStorageBuffer;
                if (HasAnyFlags(Desc.mUsage, EArdaRHIBufferUsage::Indirect)) Info.usage |= vk::BufferUsageFlagBits::eIndirectBuffer;
                if (mContext->mbBufferDeviceAddress)
                    Info.usage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
                if (HasAnyFlags(Desc.mUsage,
                        EArdaRHIBufferUsage::AccelStructBuildInput))
                    Info.usage |= vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
                if (HasAnyFlags(Desc.mUsage,
                        EArdaRHIBufferUsage::AccelStructStorage))
                    Info.usage |= vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR;
                if (HasAnyFlags(Desc.mUsage,
                        EArdaRHIBufferUsage::ShaderBindingTable))
                    Info.usage |= vk::BufferUsageFlagBits::eShaderBindingTableKHR;
                if (HasAnyFlags(Desc.mUsage,
                        EArdaRHIBufferUsage::OpacityMicromapBuildInput))
                    Info.usage |=
                        vk::BufferUsageFlagBits::eMicromapBuildInputReadOnlyEXT;
                if (Desc.mbVirtual && Desc.mbTiled)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidArgument,
                        "A Vulkan buffer cannot be both placed/virtual and sparse/tiled."));
                if (Desc.mbTiled)
                    Info.flags |= vk::BufferCreateFlagBits::eSparseBinding |
                        vk::BufferCreateFlagBits::eSparseResidency |
                        (mCapabilities.mResidency.mbAliasedMappings
                            ? vk::BufferCreateFlagBits::eSparseAliased
                            : vk::BufferCreateFlags{});
                Info.sharingMode = vk::SharingMode::eExclusive;
                Buffer->mBuffer = mContext->mDevice.createBuffer(Info);
                if (Desc.mbVirtual || Desc.mbTiled)
                    return { Buffer, {} };
                const auto Requirements = mContext->mDevice.getBufferMemoryRequirements(Buffer->mBuffer);
                const vk::MemoryPropertyFlags MemoryFlags =
                    Desc.mCpuAccess == EArdaRHICpuAccess::None
                        ? vk::MemoryPropertyFlags(vk::MemoryPropertyFlagBits::eDeviceLocal)
                        : vk::MemoryPropertyFlags(
                            vk::MemoryPropertyFlagBits::eHostVisible |
                            vk::MemoryPropertyFlagBits::eHostCoherent);
                uint32_t MemoryType = mContext->FindMemoryType(
                    Requirements.memoryTypeBits, MemoryFlags);
                if (MemoryType == UINT32_MAX)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::BackendFailure,
                        Desc.mCpuAccess == EArdaRHICpuAccess::None
                            ? "No Vulkan device-local buffer memory type is available."
                            : "No coherent host-visible Vulkan buffer memory type is available."));
                vk::MemoryAllocateFlagsInfo AllocateFlags;
                if (mContext->mbBufferDeviceAddress)
                    AllocateFlags.flags =
                        vk::MemoryAllocateFlagBits::eDeviceAddress;
                vk::MemoryAllocateInfo Allocate(Requirements.size, MemoryType);
                Allocate.pNext = mContext->mbBufferDeviceAddress
                    ? &AllocateFlags : nullptr;
                Buffer->mMemory = mContext->mDevice.allocateMemory(Allocate);
                mContext->mDevice.bindBufferMemory(Buffer->mBuffer, Buffer->mMemory, 0);
                return { Buffer, {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        TArdaRHIResult<FArdaRHIAccelStructMemoryRequirements>
        FArdaVulkanProviderDevice::GetAccelStructBuildMemoryRequirements(
            const FArdaRHIAccelStructDesc& Desc,
            const eastl::vector<FArdaProviderRayTracingGeometry>& Geometries)
        {
            if (!mContext->mbAccelerationStructure)
                return Fail<FArdaRHIAccelStructMemoryRequirements>(
                    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                        "VK_KHR_acceleration_structure is unavailable."));
            try
            {
                eastl::vector<vk::AccelerationStructureGeometryKHR> Native;
                eastl::vector<uint32_t> PrimitiveCounts;
                eastl::vector<eastl::vector<vk::MicromapUsageEXT>>
                    OpacityUsageCounts;
                eastl::vector<
                    vk::AccelerationStructureTrianglesOpacityMicromapEXT>
                    OpacityInfos;
                OpacityUsageCounts.reserve(Geometries.size());
                OpacityInfos.reserve(Geometries.size());
                if (Desc.mbTopLevel)
                {
                    vk::AccelerationStructureGeometryInstancesDataKHR Instances;
                    Instances.arrayOfPointers = false;
                    Instances.data.deviceAddress = 0;
                    vk::AccelerationStructureGeometryKHR Geometry;
                    Geometry.geometryType = vk::GeometryTypeKHR::eInstances;
                    Geometry.geometry.instances = Instances;
                    Native.push_back(Geometry);
                    PrimitiveCounts.push_back(
                        static_cast<uint32_t>(Desc.mTopLevelMaxInstances));
                }
                else
                {
                    Native.reserve(Geometries.size());
                    PrimitiveCounts.reserve(Geometries.size());
                    for (const auto& Source : Geometries)
                    {
                        auto* Vertex = dynamic_cast<FVulkanBuffer*>(
                            Source.mVertexOrAABBBuffer.get());
                        if (!Vertex || !Vertex->mBuffer)
                            return Fail<FArdaRHIAccelStructMemoryRequirements>(
                                FArdaRHIStatus::Error(
                                    EArdaRHIResult::WrongDevice,
                                    "A Vulkan BLAS geometry buffer is invalid."));
                        const vk::DeviceAddress VertexAddress =
                            mContext->mDevice.getBufferAddress(
                                vk::BufferDeviceAddressInfo(Vertex->mBuffer)) +
                            Source.mDesc.mVertexOrAABBOffset;
                        vk::AccelerationStructureGeometryKHR Geometry;
                        Geometry.flags = ToVulkanGeometryFlags(
                            Source.mDesc.mFlags);
                        if (Source.mDesc.mType ==
                            EArdaRHIRayTracingGeometryType::Triangles)
                        {
                            vk::AccelerationStructureGeometryTrianglesDataKHR Triangles;
                            Triangles.vertexFormat = ToVulkan(
                                Source.mDesc.mVertexFormat);
                            Triangles.vertexData.deviceAddress = VertexAddress;
                            Triangles.vertexStride = Source.mDesc.mStride;
                            Triangles.maxVertex = Source.mDesc.mVertexOrAABBCount
                                ? Source.mDesc.mVertexOrAABBCount - 1u : 0u;
                            if (Source.mIndexBuffer)
                            {
                                auto* Index = dynamic_cast<FVulkanBuffer*>(
                                    Source.mIndexBuffer.get());
                                if (!Index || !Index->mBuffer)
                                    return Fail<FArdaRHIAccelStructMemoryRequirements>(
                                        FArdaRHIStatus::Error(
                                            EArdaRHIResult::WrongDevice,
                                            "A Vulkan BLAS index buffer is invalid."));
                                Triangles.indexData.deviceAddress =
                                    mContext->mDevice.getBufferAddress(
                                        vk::BufferDeviceAddressInfo(Index->mBuffer)) +
                                    Source.mDesc.mIndexOffset;
                                Triangles.indexType =
                                    Source.mDesc.mIndexFormat ==
                                        EArdaRHIFormat::R16UInt
                                        ? vk::IndexType::eUint16
                                        : vk::IndexType::eUint32;
                            }
                            else
                                Triangles.indexType = vk::IndexType::eNoneKHR;
                            if (Source.mOpacityMicromap)
                            {
                                auto* Micromap =
                                    dynamic_cast<FVulkanOpacityMicromap*>(
                                        Source.mOpacityMicromap.get());
                                if (!Micromap || !Micromap->mMicromap)
                                    return Fail<FArdaRHIAccelStructMemoryRequirements>(
                                        FArdaRHIStatus::Error(
                                            EArdaRHIResult::WrongDevice,
                                            "A Vulkan BLAS opacity micromap is invalid."));
                                OpacityUsageCounts.push_back(
                                    Source.mDesc.mOpacityMicromapUsageCounts.empty()
                                        ? Micromap->mUsageCounts
                                        : ToVulkanMicromapUsages(
                                            Source.mDesc.mOpacityMicromapUsageCounts));
                                vk::AccelerationStructureTrianglesOpacityMicromapEXT
                                    Opacity;
                                Opacity.indexType =
                                    Source.mDesc.mOpacityMicromapIndexFormat ==
                                        EArdaRHIFormat::R32UInt
                                        ? vk::IndexType::eUint32
                                        : Source.mDesc.mOpacityMicromapIndexFormat ==
                                            EArdaRHIFormat::R16UInt
                                            ? vk::IndexType::eUint16
                                            : vk::IndexType::eNoneKHR;
                                if (Source.mOpacityMicromapIndexBuffer)
                                {
                                    auto* Index = dynamic_cast<FVulkanBuffer*>(
                                        Source.mOpacityMicromapIndexBuffer.get());
                                    if (!Index || !Index->mBuffer)
                                        return Fail<FArdaRHIAccelStructMemoryRequirements>(
                                            FArdaRHIStatus::Error(
                                                EArdaRHIResult::WrongDevice,
                                                "A Vulkan opacity-micromap index buffer is invalid."));
                                    Opacity.indexBuffer.deviceAddress =
                                        mContext->mDevice.getBufferAddress(
                                            vk::BufferDeviceAddressInfo(
                                                Index->mBuffer)) +
                                        Source.mDesc.mOpacityMicromapIndexOffset;
                                    Opacity.indexStride =
                                        Opacity.indexType == vk::IndexType::eUint32
                                            ? 4u : 2u;
                                }
                                Opacity.usageCountsCount =
                                    static_cast<uint32_t>(
                                        OpacityUsageCounts.back().size());
                                Opacity.pUsageCounts =
                                    OpacityUsageCounts.back().data();
                                Opacity.micromap = Micromap->mMicromap;
                                OpacityInfos.push_back(Opacity);
                                Triangles.pNext = &OpacityInfos.back();
                            }
                            Geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
                            Geometry.geometry.triangles = Triangles;
                            PrimitiveCounts.push_back(Source.mIndexBuffer
                                ? Source.mDesc.mIndexCount / 3u
                                : Source.mDesc.mVertexOrAABBCount / 3u);
                        }
                        else
                        {
                            vk::AccelerationStructureGeometryAabbsDataKHR Aabbs;
                            Aabbs.data.deviceAddress = VertexAddress;
                            Aabbs.stride = Source.mDesc.mStride;
                            Geometry.geometryType = vk::GeometryTypeKHR::eAabbs;
                            Geometry.geometry.aabbs = Aabbs;
                            PrimitiveCounts.push_back(
                                Source.mDesc.mVertexOrAABBCount);
                        }
                        Native.push_back(Geometry);
                    }
                }
                vk::AccelerationStructureBuildGeometryInfoKHR Build;
                Build.type = Desc.mbTopLevel
                    ? vk::AccelerationStructureTypeKHR::eTopLevel
                    : vk::AccelerationStructureTypeKHR::eBottomLevel;
                Build.flags = ToVulkanBuildFlags(Desc.mBuildFlags);
                Build.geometryCount = static_cast<uint32_t>(Native.size());
                Build.pGeometries = Native.data();
                const auto Sizes =
                    mContext->mDevice.getAccelerationStructureBuildSizesKHR(
                        vk::AccelerationStructureBuildTypeKHR::eDevice,
                        Build, PrimitiveCounts);
                FArdaRHIAccelStructMemoryRequirements Result;
                Result.mResultSize = Desc.mResultSizeOverride
                    ? Desc.mResultSizeOverride
                    : Sizes.accelerationStructureSize;
                Result.mBuildScratchSize = Sizes.buildScratchSize;
                Result.mUpdateScratchSize = Sizes.updateScratchSize;
                Result.mResultAlignment = 256;
                Result.mScratchAlignment =
                    mContext->mAccelerationStructureProperties.
                        minAccelerationStructureScratchOffsetAlignment;
                return {Result, {}};
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaRHIAccelStructMemoryRequirements>(
                    FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
                        Error.what()));
            }
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateAccelStruct(
            const FArdaRHIAccelStructDesc& Desc,
            const FArdaRHIAccelStructMemoryRequirements& Requirements)
        {
            try
            {
                auto AccelStruct = eastl::make_shared<FVulkanAccelStruct>();
                AccelStruct->mContext = mContext;
                AccelStruct->mDesc = Desc;
                AccelStruct->mRequirements = Requirements;
                if (Desc.mbVirtual)
                    return {AccelStruct, {}};
                vk::BufferCreateInfo BufferInfo;
                BufferInfo.size = Requirements.mResultSize;
                BufferInfo.usage =
                    vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                    vk::BufferUsageFlagBits::eShaderDeviceAddress |
                    vk::BufferUsageFlagBits::eTransferSrc |
                    vk::BufferUsageFlagBits::eTransferDst;
                AccelStruct->mBuffer =
                    mContext->mDevice.createBuffer(BufferInfo);
                const auto MemoryRequirements =
                    mContext->mDevice.getBufferMemoryRequirements(
                        AccelStruct->mBuffer);
                const uint32_t MemoryType = mContext->FindMemoryType(
                    MemoryRequirements.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eDeviceLocal);
                if (MemoryType == UINT32_MAX)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::BackendFailure,
                        "No Vulkan AS storage memory type is available."));
                vk::MemoryAllocateFlagsInfo Flags(
                    vk::MemoryAllocateFlagBits::eDeviceAddress);
                vk::MemoryAllocateInfo Allocate(
                    MemoryRequirements.size, MemoryType);
                Allocate.pNext = &Flags;
                AccelStruct->mMemory =
                    mContext->mDevice.allocateMemory(Allocate);
                mContext->mDevice.bindBufferMemory(
                    AccelStruct->mBuffer, AccelStruct->mMemory, 0);
                vk::AccelerationStructureCreateInfoKHR Create;
                Create.buffer = AccelStruct->mBuffer;
                Create.size = Requirements.mResultSize;
                Create.type = Desc.mbTopLevel
                    ? vk::AccelerationStructureTypeKHR::eTopLevel
                    : vk::AccelerationStructureTypeKHR::eBottomLevel;
                AccelStruct->mAccelStruct =
                    mContext->mDevice.createAccelerationStructureKHR(Create);
                if (HasAnyFlags(Desc.mBuildFlags,
                        EArdaRHIAccelStructBuildFlags::AllowCompaction))
                {
                    vk::QueryPoolCreateInfo Query;
                    Query.queryType =
                        vk::QueryType::eAccelerationStructureCompactedSizeKHR;
                    Query.queryCount = 1;
                    AccelStruct->mQueryPool =
                        mContext->mDevice.createQueryPool(Query);
                }
                return {AccelStruct, {}};
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaProviderObjectResult
        FArdaVulkanProviderDevice::CreateOpacityMicromap(
            const FArdaRHIOpacityMicromapDesc& Desc,
            const FArdaProviderObjectRef& InputObject,
            const FArdaProviderObjectRef& TriangleObject)
        {
            if (!mContext->mbOpacityMicromap)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "VK_EXT_opacity_micromap is unavailable."));
            auto* Input = dynamic_cast<FVulkanBuffer*>(InputObject.get());
            auto* Triangles = dynamic_cast<FVulkanBuffer*>(
                TriangleObject.get());
            if (!Input || !Triangles || !Input->mBuffer ||
                !Triangles->mBuffer)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan opacity-micromap input buffers are invalid."));
            try
            {
                auto Micromap = eastl::make_shared<FVulkanOpacityMicromap>();
                Micromap->mContext = mContext;
                Micromap->mDesc = Desc;
                Micromap->mInputBuffer = InputObject;
                Micromap->mTriangleBuffer = TriangleObject;
                Micromap->mUsageCounts = ToVulkanMicromapUsages(Desc.mCounts);

                vk::MicromapBuildInfoEXT Build;
                Build.type = vk::MicromapTypeEXT::eOpacityMicromap;
                Build.flags = ToVulkanMicromapBuildFlags(Desc.mFlags);
                Build.mode = vk::BuildMicromapModeEXT::eBuild;
                Build.usageCountsCount = static_cast<uint32_t>(
                    Micromap->mUsageCounts.size());
                Build.pUsageCounts = Micromap->mUsageCounts.data();
                Build.triangleArrayStride = sizeof(vk::MicromapTriangleEXT);
                const auto Sizes = mContext->mDevice.getMicromapBuildSizesEXT(
                    vk::AccelerationStructureBuildTypeKHR::eDevice, Build);
                if (!Sizes.micromapSize || !Sizes.buildScratchSize)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::BackendFailure,
                        "Vulkan returned empty opacity-micromap build sizes."));
                Micromap->mBuildScratchSize = Sizes.buildScratchSize;
                Micromap->mStorageSize = Desc.mResultSizeOverride
                    ? Desc.mResultSizeOverride : Sizes.micromapSize;

                vk::BufferCreateInfo BufferInfo;
                BufferInfo.size = Micromap->mStorageSize;
                BufferInfo.usage =
                    vk::BufferUsageFlagBits::eMicromapStorageEXT |
                    vk::BufferUsageFlagBits::eShaderDeviceAddress |
                    vk::BufferUsageFlagBits::eTransferSrc |
                    vk::BufferUsageFlagBits::eTransferDst;
                Micromap->mBuffer =
                    mContext->mDevice.createBuffer(BufferInfo);
                const auto Requirements =
                    mContext->mDevice.getBufferMemoryRequirements(
                        Micromap->mBuffer);
                const uint32_t MemoryType = mContext->FindMemoryType(
                    Requirements.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eDeviceLocal);
                if (MemoryType == UINT32_MAX)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::BackendFailure,
                        "No Vulkan opacity-micromap storage memory type is available."));
                vk::MemoryAllocateFlagsInfo Flags(
                    vk::MemoryAllocateFlagBits::eDeviceAddress);
                vk::MemoryAllocateInfo Allocate(
                    Requirements.size, MemoryType);
                Allocate.pNext = &Flags;
                Micromap->mMemory =
                    mContext->mDevice.allocateMemory(Allocate);
                mContext->mDevice.bindBufferMemory(
                    Micromap->mBuffer, Micromap->mMemory, 0);
                Micromap->mStorageAddress =
                    mContext->mDevice.getBufferAddress(
                        vk::BufferDeviceAddressInfo(Micromap->mBuffer));
                vk::MicromapCreateInfoEXT Create;
                Create.buffer = Micromap->mBuffer;
                Create.size = Micromap->mStorageSize;
                Create.type = vk::MicromapTypeEXT::eOpacityMicromap;
                Micromap->mMicromap =
                    mContext->mDevice.createMicromapEXT(Create);
                if (!Desc.mResultSizeOverride &&
                    HasAnyFlags(Desc.mFlags,
                        EArdaRHIOpacityMicromapBuildFlags::AllowCompaction))
                {
                    try
                    {
                        vk::QueryPoolCreateInfo Query;
                        Query.queryType =
                            vk::QueryType::eMicromapCompactedSizeEXT;
                        Query.queryCount = 1;
                        Micromap->mQueryPool =
                            mContext->mDevice.createQueryPool(Query);
                    }
                    catch (const vk::SystemError&)
                    {
                        // Some desktop drivers advertise micromap compaction
                        // but reject the optional compacted-size query type.
                        // The source storage size remains a legal conservative
                        // destination for a native compact copy.
                        Micromap->mQueryPool = nullptr;
                    }
                }
                return {Micromap, {}};
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        uint64_t FArdaVulkanProviderDevice::GetAccelStructDeviceAddress(
            const FArdaProviderObjectRef& Object) const noexcept
        {
            auto* AccelStruct = dynamic_cast<FVulkanAccelStruct*>(Object.get());
            if (!AccelStruct || !AccelStruct->mAccelStruct) return 0;
            try
            {
                return mContext->mDevice.getAccelerationStructureAddressKHR(
                    vk::AccelerationStructureDeviceAddressInfoKHR(
                        AccelStruct->mAccelStruct));
            }
            catch (const vk::SystemError&)
            {
                return 0;
            }
        }

        uint64_t FArdaVulkanProviderDevice::GetOpacityMicromapDeviceAddress(
            const FArdaProviderObjectRef& Object) const noexcept
        {
            auto* Micromap = dynamic_cast<FVulkanOpacityMicromap*>(
                Object.get());
            return Micromap ? Micromap->mStorageAddress : 0;
        }

        TArdaRHIResult<uint64_t>
        FArdaVulkanProviderDevice::GetOpacityMicromapCompactedSize(
            const FArdaProviderObjectRef& Object)
        {
            auto* Micromap = dynamic_cast<FVulkanOpacityMicromap*>(
                Object.get());
            if (!Micromap)
                return Fail<uint64_t>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "The Vulkan opacity micromap is invalid."));
            if (!Micromap->mQueryPool)
                return {Micromap->mStorageSize, {}};
            if (auto Status = WaitForIdle(); !Status)
                return Fail<uint64_t>(Status);
            uint64_t Size = 0;
            const vk::Result Result = mContext->mDevice.getQueryPoolResults(
                Micromap->mQueryPool, 0, 1, sizeof(Size), &Size,
                sizeof(Size), vk::QueryResultFlagBits::e64 |
                    vk::QueryResultFlagBits::eWait);
            if (Result != vk::Result::eSuccess || !Size)
                return Fail<uint64_t>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "The Vulkan micromap compacted-size query is unavailable."));
            return {Size, {}};
        }

        TArdaRHIResult<uint64_t>
        FArdaVulkanProviderDevice::GetAccelStructCompactedSize(
            const FArdaProviderObjectRef& Object)
        {
            auto* AccelStruct = dynamic_cast<FVulkanAccelStruct*>(Object.get());
            if (!AccelStruct || !AccelStruct->mQueryPool)
                return Fail<uint64_t>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "The Vulkan AS has no compacted-size query."));
            if (auto Status = WaitForIdle(); !Status)
                return Fail<uint64_t>(Status);
            uint64_t Size = 0;
            const vk::Result Result = mContext->mDevice.getQueryPoolResults(
                AccelStruct->mQueryPool, 0, 1, sizeof(Size), &Size,
                sizeof(Size), vk::QueryResultFlagBits::e64 |
                    vk::QueryResultFlagBits::eWait);
            if (Result != vk::Result::eSuccess || !Size)
                return Fail<uint64_t>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "The Vulkan compacted-size query is unavailable."));
            return {Size, {}};
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateHeap(
            const FArdaRHIHeapDesc& Desc)
        {
            try
            {
                vk::MemoryPropertyFlags Flags;
                if (Desc.mType == EArdaRHIHeapType::DeviceLocal)
                    Flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
                else
                    Flags = vk::MemoryPropertyFlagBits::eHostVisible |
                        vk::MemoryPropertyFlagBits::eHostCoherent;
                const uint32_t MemoryType = mContext->FindMemoryType(
                    Desc.mMemoryTypeBits, Flags);
                if (MemoryType == UINT32_MAX)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::Unsupported,
                        "No Vulkan memory type satisfies the explicit heap descriptor."));
                auto Heap = eastl::make_shared<FVulkanHeap>();
                Heap->mContext = mContext;
                Heap->mDesc = Desc;
                Heap->mMemoryType = MemoryType;
                vk::MemoryAllocateFlagsInfo AllocateFlags;
                if (mContext->mbBufferDeviceAddress)
                    AllocateFlags.flags =
                        vk::MemoryAllocateFlagBits::eDeviceAddress;
                vk::MemoryAllocateInfo Allocate(Desc.mCapacity, MemoryType);
                Allocate.pNext = mContext->mbBufferDeviceAddress
                    ? &AllocateFlags : nullptr;
                Heap->mMemory = mContext->mDevice.allocateMemory(Allocate);
                return { Heap, {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        TArdaRHIResult<FArdaRHIMemoryRequirements>
        FArdaVulkanProviderDevice::GetTextureMemoryRequirements(
            const FArdaProviderObjectRef& Object,
            const FArdaRHITextureDesc&)
        {
            auto* Texture = dynamic_cast<FVulkanTexture*>(Object.get());
            if (!Texture || !Texture->mImage)
                return Fail<FArdaRHIMemoryRequirements>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan texture memory requirements have the wrong resource type."));
            const vk::MemoryRequirements Requirements =
                mContext->mDevice.getImageMemoryRequirements(Texture->mImage);
            return {{
                Requirements.size,
                Requirements.alignment,
                Requirements.memoryTypeBits}, {}};
        }

        TArdaRHIResult<FArdaRHIMemoryRequirements>
        FArdaVulkanProviderDevice::GetBufferMemoryRequirements(
            const FArdaProviderObjectRef& Object,
            const FArdaRHIBufferDesc&)
        {
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Object.get());
            if (!Buffer || !Buffer->mBuffer)
                return Fail<FArdaRHIMemoryRequirements>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan buffer memory requirements have the wrong resource type."));
            const vk::MemoryRequirements Requirements =
                mContext->mDevice.getBufferMemoryRequirements(Buffer->mBuffer);
            return {{
                Requirements.size,
                Requirements.alignment,
                Requirements.memoryTypeBits}, {}};
        }

        FArdaRHIStatus FArdaVulkanProviderDevice::BindTextureMemory(
            const FArdaProviderObjectRef& Object,
            const FArdaRHITextureDesc& Desc,
            const FArdaProviderObjectRef& HeapObject,
            uint64_t Offset)
        {
            auto* Texture = dynamic_cast<FVulkanTexture*>(Object.get());
            auto* Heap = dynamic_cast<FVulkanHeap*>(HeapObject.get());
            if (!Texture || !Heap || !Texture->mImage || !Heap->mMemory)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan texture heap binding has the wrong resource type.");
            if (Heap->mDesc.mType != EArdaRHIHeapType::DeviceLocal)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "Vulkan optimal textures require a device-local explicit heap.");
            const vk::MemoryRequirements Requirements =
                mContext->mDevice.getImageMemoryRequirements(Texture->mImage);
            if (!(Requirements.memoryTypeBits & (1u << Heap->mMemoryType)))
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "The Vulkan heap memory type is incompatible with the texture.");
            std::lock_guard<std::mutex> Lock(Texture->mLayoutMutex);
            if (Texture->mMemory || Texture->mHeap)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "Vulkan virtual texture memory is already bound.");
            try
            {
                mContext->mDevice.bindImageMemory(
                    Texture->mImage, Heap->mMemory, Offset);
                vk::ImageViewCreateInfo ViewInfo;
                ViewInfo.image = Texture->mImage;
                ViewInfo.viewType = ToViewType(Desc.mDimension);
                ViewInfo.format = ToVulkan(Desc.mFormat);
                ViewInfo.subresourceRange = vk::ImageSubresourceRange(
                    ImageAspect(Desc.mFormat),
                    0,
                    Desc.mMipLevels,
                    0,
                    Desc.mArraySize);
                Texture->mView =
                    mContext->mDevice.createImageView(ViewInfo);
                Texture->mHeap = HeapObject;
                return {};
            }
            catch (const vk::SystemError& Error)
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what());
            }
        }

        FArdaRHIStatus FArdaVulkanProviderDevice::BindBufferMemory(
            const FArdaProviderObjectRef& Object,
            const FArdaRHIBufferDesc&,
            const FArdaProviderObjectRef& HeapObject,
            uint64_t Offset)
        {
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Object.get());
            auto* Heap = dynamic_cast<FVulkanHeap*>(HeapObject.get());
            if (!Buffer || !Heap || !Buffer->mBuffer || !Heap->mMemory)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan buffer heap binding has the wrong resource type.");
            const vk::MemoryRequirements Requirements =
                mContext->mDevice.getBufferMemoryRequirements(Buffer->mBuffer);
            if (!(Requirements.memoryTypeBits & (1u << Heap->mMemoryType)))
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "The Vulkan heap memory type is incompatible with the buffer.");
            std::lock_guard<std::mutex> Lock(Buffer->mStateMutex);
            if (Buffer->mMemory || Buffer->mHeap)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "Vulkan virtual buffer memory is already bound.");
            try
            {
                mContext->mDevice.bindBufferMemory(
                    Buffer->mBuffer, Heap->mMemory, Offset);
                Buffer->mHeap = HeapObject;
                return {};
            }
            catch (const vk::SystemError& Error)
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what());
            }
        }

        TArdaRHIResult<FArdaRHITextureTiling>
        FArdaVulkanProviderDevice::GetTextureTiling(
            const FArdaProviderObjectRef& Object)
        {
            auto* Texture = dynamic_cast<FVulkanTexture*>(Object.get());
            if (!Texture || !Texture->mImage || !Texture->mDesc.mbTiled)
                return Fail<FArdaRHITextureTiling>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan texture tiling requires a sparse image."));
            try
            {
                const auto Sparse =
                    mContext->mDevice.getImageSparseMemoryRequirements(
                        Texture->mImage);
                if (Sparse.empty())
                    return Fail<FArdaRHITextureTiling>(FArdaRHIStatus::Error(
                        EArdaRHIResult::Unsupported,
                        "The Vulkan image format has no sparse residency requirements."));
                const auto& Requirement = Sparse.front();
                const vk::MemoryRequirements Memory =
                    mContext->mDevice.getImageMemoryRequirements(
                        Texture->mImage);
                FArdaRHITextureTiling Result;
                Result.mTileCount = static_cast<uint32_t>(
                    (Memory.size + Memory.alignment - 1) /
                    Memory.alignment);
                Result.mTileShape = {
                    Requirement.formatProperties.imageGranularity.width,
                    Requirement.formatProperties.imageGranularity.height,
                    Requirement.formatProperties.imageGranularity.depth};
                Result.mPackedMips.mStandardMipCount =
                    Requirement.imageMipTailFirstLod;
                Result.mPackedMips.mPackedMipCount =
                    Texture->mDesc.mMipLevels >
                        Requirement.imageMipTailFirstLod
                    ? Texture->mDesc.mMipLevels -
                        Requirement.imageMipTailFirstLod
                    : 0;
                Result.mPackedMips.mPackedMipTileCount =
                    static_cast<uint32_t>((Requirement.imageMipTailSize +
                        Memory.alignment - 1) / Memory.alignment);
                Result.mPackedMips.mStartTileIndex =
                    static_cast<uint32_t>(
                        Requirement.imageMipTailOffset /
                        Memory.alignment);
                uint32_t StartTile = 0;
                for (uint32_t ArrayLayer = 0;
                     ArrayLayer < Texture->mDesc.mArraySize; ++ArrayLayer)
                {
                    for (uint32_t Mip = 0;
                         Mip < Texture->mDesc.mMipLevels; ++Mip)
                    {
                        const uint32_t Width = GetArdaRHITextureMipExtent(
                            Texture->mDesc.mWidth, Mip);
                        const uint32_t Height = GetArdaRHITextureMipExtent(
                            Texture->mDesc.mHeight, Mip);
                        const uint32_t Depth = GetArdaRHITextureMipExtent(
                            Texture->mDesc.mDepth, Mip);
                        FArdaRHISubresourceTiling Entry;
                        Entry.mWidthInTiles = (Width +
                            Result.mTileShape.mWidthInTexels - 1) /
                            Result.mTileShape.mWidthInTexels;
                        Entry.mHeightInTiles = (Height +
                            Result.mTileShape.mHeightInTexels - 1) /
                            Result.mTileShape.mHeightInTexels;
                        Entry.mDepthInTiles = (Depth +
                            Result.mTileShape.mDepthInTexels - 1) /
                            Result.mTileShape.mDepthInTexels;
                        Entry.mStartTileIndex = StartTile;
                        if (Mip < Requirement.imageMipTailFirstLod)
                            StartTile += Entry.mWidthInTiles *
                                Entry.mHeightInTiles *
                                Entry.mDepthInTiles;
                        else
                            Entry.mStartTileIndex =
                                Result.mPackedMips.mStartTileIndex;
                        Result.mSubresources.push_back(Entry);
                    }
                }
                return {eastl::move(Result), {}};
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaRHITextureTiling>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaRHIStatus FArdaVulkanProviderDevice::UpdateTextureTileMappings(
            const FArdaProviderObjectRef& Object,
            const eastl::vector<FArdaProviderTextureTileMapping>& Mappings,
            EArdaRHIQueueType QueueType)
        {
            auto* Texture = dynamic_cast<FVulkanTexture*>(Object.get());
            const size_t SparseQueueIndex = GetArdaRHIQueueIndex(QueueType);
            if (!Texture || !Texture->mImage || !Texture->mDesc.mbTiled ||
                !mContext->mQueueSparseBinding[SparseQueueIndex])
                return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                    "The Vulkan resource or requested queue does not support sparse image binding.");
            try
            {
                const auto Sparse =
                    mContext->mDevice.getImageSparseMemoryRequirements(
                        Texture->mImage);
                if (Sparse.empty())
                    return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                        "The Vulkan image has no sparse residency requirements.");
                const auto& Requirement = Sparse.front();
                const auto Memory =
                    mContext->mDevice.getImageMemoryRequirements(
                        Texture->mImage);
                eastl::vector<vk::SparseImageMemoryBind> Binds;
                for (const auto& Mapping : Mappings)
                {
                    auto* Heap = Mapping.mHeap
                        ? dynamic_cast<FVulkanHeap*>(Mapping.mHeap.get())
                        : nullptr;
                    if (Mapping.mHeap && (!Heap || !Heap->mMemory ||
                        !(Memory.memoryTypeBits &
                            (1u << Heap->mMemoryType))))
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::WrongDevice,
                            "A Vulkan sparse-image heap is invalid or incompatible.");
                    for (size_t Index = 0;
                         Index < Mapping.mCoordinates.size(); ++Index)
                    {
                        if (Mapping.mByteOffsets[Index] % Memory.alignment)
                            return FArdaRHIStatus::Error(
                                EArdaRHIResult::InvalidArgument,
                                "Vulkan sparse-image heap offsets violate native alignment.");
                        const auto& Coordinate =
                            Mapping.mCoordinates[Index];
                        const auto& Region = Mapping.mRegions[Index];
                        const uint32_t MipWidth = GetArdaRHITextureMipExtent(
                            Texture->mDesc.mWidth, Coordinate.mMipLevel);
                        const uint32_t MipHeight = GetArdaRHITextureMipExtent(
                            Texture->mDesc.mHeight, Coordinate.mMipLevel);
                        const uint32_t MipDepth = GetArdaRHITextureMipExtent(
                            Texture->mDesc.mDepth, Coordinate.mMipLevel);
                        const vk::Extent3D Granularity =
                            Requirement.formatProperties.imageGranularity;
                        vk::SparseImageMemoryBind Bind;
                        Bind.subresource.aspectMask =
                            ImageAspect(Texture->mDesc.mFormat);
                        Bind.subresource.mipLevel = Coordinate.mMipLevel;
                        Bind.subresource.arrayLayer = Coordinate.mArrayLevel;
                        Bind.offset = vk::Offset3D(
                            static_cast<int32_t>(Coordinate.mX *
                                Granularity.width),
                            static_cast<int32_t>(Coordinate.mY *
                                Granularity.height),
                            static_cast<int32_t>(Coordinate.mZ *
                                Granularity.depth));
                        Bind.extent = vk::Extent3D(
                            eastl::min(MipWidth -
                                static_cast<uint32_t>(Bind.offset.x),
                                eastl::max(1u, Region.mWidth) *
                                    Granularity.width),
                            eastl::min(MipHeight -
                                static_cast<uint32_t>(Bind.offset.y),
                                eastl::max(1u, Region.mHeight) *
                                    Granularity.height),
                            eastl::min(MipDepth -
                                static_cast<uint32_t>(Bind.offset.z),
                                eastl::max(1u, Region.mDepth) *
                                    Granularity.depth));
                        Bind.memory = Heap ? Heap->mMemory : vk::DeviceMemory{};
                        Bind.memoryOffset = Mapping.mByteOffsets[Index];
                        Binds.push_back(Bind);
                    }
                    if (Heap)
                        Texture->mSparseHeaps.push_back(Mapping.mHeap);
                }
                vk::SparseImageMemoryBindInfo ImageInfo;
                ImageInfo.image = Texture->mImage;
                ImageInfo.bindCount = static_cast<uint32_t>(Binds.size());
                ImageInfo.pBinds = Binds.data();
                vk::BindSparseInfo Info;
                Info.imageBindCount = Binds.empty() ? 0u : 1u;
                Info.pImageBinds = Binds.empty() ? nullptr : &ImageInfo;
                vk::Fence Fence = mContext->mDevice.createFence({});
                {
                    std::lock_guard<std::mutex> Lock(mContext->mQueueMutex);
                    (void)mContext->GetQueue(QueueType).bindSparse(
                        1, &Info, Fence);
                }
                (void)mContext->mDevice.waitForFences(
                    1, &Fence, VK_TRUE, UINT64_MAX);
                mContext->mDevice.destroyFence(Fence);
                return {};
            }
            catch (const vk::SystemError& Error)
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what());
            }
        }

        FArdaRHIStatus FArdaVulkanProviderDevice::UpdateBufferTileMappings(
            const FArdaProviderObjectRef& Object,
            const eastl::vector<FArdaProviderBufferTileMapping>& Mappings,
            EArdaRHIQueueType QueueType)
        {
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Object.get());
            const size_t SparseQueueIndex = GetArdaRHIQueueIndex(QueueType);
            if (!Buffer || !Buffer->mBuffer || !Buffer->mDesc.mbTiled ||
                !mContext->mQueueSparseBinding[SparseQueueIndex])
                return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                    "The Vulkan resource or requested queue does not support sparse buffer binding.");
            try
            {
                const auto Memory =
                    mContext->mDevice.getBufferMemoryRequirements(
                        Buffer->mBuffer);
                eastl::vector<vk::SparseMemoryBind> Binds;
                Binds.reserve(Mappings.size());
                for (const auto& Mapping : Mappings)
                {
                    auto* Heap = Mapping.mHeap
                        ? dynamic_cast<FVulkanHeap*>(Mapping.mHeap.get())
                        : nullptr;
                    if (Mapping.mbCommit && (!Heap || !Heap->mMemory ||
                        !(Memory.memoryTypeBits &
                            (1u << Heap->mMemoryType))))
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::WrongDevice,
                            "A Vulkan sparse-buffer heap is invalid or incompatible.");
                    if (!Mapping.mByteSize ||
                        Mapping.mBufferOffset % Memory.alignment ||
                        Mapping.mByteSize % Memory.alignment ||
                        Mapping.mHeapOffset % Memory.alignment)
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument,
                            "Vulkan sparse-buffer mappings violate native alignment.");
                    vk::SparseMemoryBind Bind;
                    Bind.resourceOffset = Mapping.mBufferOffset;
                    Bind.size = Mapping.mByteSize;
                    Bind.memory = Mapping.mbCommit
                        ? Heap->mMemory : vk::DeviceMemory{};
                    Bind.memoryOffset = Mapping.mHeapOffset;
                    Binds.push_back(Bind);
                    if (Mapping.mbCommit)
                        Buffer->mSparseHeaps.push_back(Mapping.mHeap);
                }
                vk::SparseBufferMemoryBindInfo BufferInfo;
                BufferInfo.buffer = Buffer->mBuffer;
                BufferInfo.bindCount = static_cast<uint32_t>(Binds.size());
                BufferInfo.pBinds = Binds.data();
                vk::BindSparseInfo Info;
                Info.bufferBindCount = Binds.empty() ? 0u : 1u;
                Info.pBufferBinds = Binds.empty() ? nullptr : &BufferInfo;
                vk::Fence Fence = mContext->mDevice.createFence({});
                {
                    std::lock_guard<std::mutex> Lock(mContext->mQueueMutex);
                    (void)mContext->GetQueue(QueueType).bindSparse(
                        1, &Info, Fence);
                }
                (void)mContext->mDevice.waitForFences(
                    1, &Fence, VK_TRUE, UINT64_MAX);
                mContext->mDevice.destroyFence(Fence);
                return {};
            }
            catch (const vk::SystemError& Error)
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what());
            }
        }

        FArdaRHIStatus FArdaVulkanProviderDevice::CommitReservedResource(
            const FArdaProviderObjectRef& Object,
            bool bTexture,
            uint64_t RequestedBytes,
            EArdaRHIQueueType QueueType)
        {
            const size_t SparseQueueIndex = GetArdaRHIQueueIndex(QueueType);
            if (!mContext->mQueueSparseBinding[SparseQueueIndex])
                return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                    "The requested Vulkan queue cannot bind sparse memory.");
            try
            {
                vk::MemoryRequirements Requirements;
                vk::DeviceMemory* OwnedMemory = nullptr;
                uint64_t* CommittedBytes = nullptr;
                vk::Image Image;
                vk::Buffer BufferHandle;
                if (bTexture)
                {
                    auto* Texture = dynamic_cast<FVulkanTexture*>(Object.get());
                    if (!Texture || !Texture->mDesc.mbTiled)
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::WrongDevice,
                            "Vulkan reserved commit requires a sparse image.");
                    Image = Texture->mImage;
                    Requirements =
                        mContext->mDevice.getImageMemoryRequirements(Image);
                    OwnedMemory = &Texture->mMemory;
                    CommittedBytes = &Texture->mCommittedBytes;
                }
                else
                {
                    auto* Buffer = dynamic_cast<FVulkanBuffer*>(Object.get());
                    if (!Buffer || !Buffer->mDesc.mbTiled)
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::WrongDevice,
                            "Vulkan reserved commit requires a sparse buffer.");
                    BufferHandle = Buffer->mBuffer;
                    Requirements =
                        mContext->mDevice.getBufferMemoryRequirements(
                            BufferHandle);
                    OwnedMemory = &Buffer->mMemory;
                    CommittedBytes = &Buffer->mCommittedBytes;
                }
                const uint64_t Bytes = eastl::min<uint64_t>(
                    Requirements.size,
                    (RequestedBytes + Requirements.alignment - 1) /
                        Requirements.alignment * Requirements.alignment);
                vk::DeviceMemory NewMemory;
                if (Bytes)
                {
                    const uint32_t MemoryType = mContext->FindMemoryType(
                        Requirements.memoryTypeBits,
                        vk::MemoryPropertyFlagBits::eDeviceLocal);
                    if (MemoryType == UINT32_MAX)
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::BackendFailure,
                            "No device-local Vulkan sparse memory type exists.");
                    NewMemory = mContext->mDevice.allocateMemory(
                        vk::MemoryAllocateInfo(Bytes, MemoryType));
                }
                eastl::array<vk::SparseMemoryBind, 2> Binds{};
                uint32_t BindCount = 0;
                if (Bytes)
                {
                    Binds[BindCount].size = Bytes;
                    Binds[BindCount].memory = NewMemory;
                    ++BindCount;
                }
                if (Bytes < Requirements.size)
                {
                    Binds[BindCount].resourceOffset = Bytes;
                    Binds[BindCount].size =
                        Requirements.size - Bytes;
                    ++BindCount;
                }
                vk::SparseImageOpaqueMemoryBindInfo ImageInfo;
                vk::SparseBufferMemoryBindInfo BufferInfo;
                vk::BindSparseInfo Info;
                if (bTexture)
                {
                    ImageInfo.image = Image;
                    ImageInfo.bindCount = BindCount;
                    ImageInfo.pBinds = Binds.data();
                    Info.imageOpaqueBindCount = 1;
                    Info.pImageOpaqueBinds = &ImageInfo;
                }
                else
                {
                    BufferInfo.buffer = BufferHandle;
                    BufferInfo.bindCount = BindCount;
                    BufferInfo.pBinds = Binds.data();
                    Info.bufferBindCount = 1;
                    Info.pBufferBinds = &BufferInfo;
                }
                vk::Fence Fence = mContext->mDevice.createFence({});
                {
                    std::lock_guard<std::mutex> Lock(mContext->mQueueMutex);
                    (void)mContext->GetQueue(QueueType).bindSparse(
                        1, &Info, Fence);
                }
                (void)mContext->mDevice.waitForFences(
                    1, &Fence, VK_TRUE, UINT64_MAX);
                mContext->mDevice.destroyFence(Fence);
                if (*OwnedMemory)
                    mContext->mDevice.freeMemory(*OwnedMemory);
                *OwnedMemory = NewMemory;
                *CommittedBytes = Bytes;
                return {};
            }
            catch (const vk::SystemError& Error)
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what());
            }
        }

        TArdaRHIResult<FArdaRHIStreamingBudget>
        FArdaVulkanProviderDevice::QueryStreamingBudget(
            bool bLocalMemory) const
        {
            if (!mContext->mbMemoryBudget)
                return Fail<FArdaRHIStreamingBudget>(FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "VK_EXT_memory_budget is unavailable."));
            try
            {
                vk::PhysicalDeviceMemoryBudgetPropertiesEXT BudgetProperties;
                vk::PhysicalDeviceMemoryProperties2 Properties;
                Properties.pNext = &BudgetProperties;
                mContext->mPhysicalDevice.getMemoryProperties2(&Properties);
                FArdaRHIStreamingBudget Result;
                Result.mbLocalMemory = bLocalMemory;
                for (uint32_t Index = 0;
                     Index < Properties.memoryProperties.memoryHeapCount;
                     ++Index)
                {
                    const bool bDeviceLocal = static_cast<bool>(
                        Properties.memoryProperties.memoryHeaps[Index].flags &
                        vk::MemoryHeapFlagBits::eDeviceLocal);
                    if (bDeviceLocal != bLocalMemory) continue;
                    Result.mBudgetBytes +=
                        BudgetProperties.heapBudget[Index];
                    Result.mCurrentUsageBytes +=
                        BudgetProperties.heapUsage[Index];
                }
                Result.mAvailableForReservationBytes =
                    Result.mBudgetBytes > Result.mCurrentUsageBytes
                    ? Result.mBudgetBytes - Result.mCurrentUsageBytes
                    : 0;
                return {Result, {}};
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaRHIStreamingBudget>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateStagingTexture(
            const FArdaRHIStagingTextureDesc& Desc)
        {
            try
            {
                const FArdaRHIFormatInfo& Block =
                    GetArdaRHIFormatInfo(Desc.mTexture.mFormat);
                if (!Block.mBytesPerBlock ||
                    Desc.mTexture.mSampleCount != 1)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::Unsupported,
                        "Vulkan staging textures require a supported single-sample format."));
                auto Texture = eastl::make_shared<FVulkanStagingTexture>();
                Texture->mContext = mContext;
                Texture->mDesc = Desc;
                const uint32_t PlaneCount =
                    GetArdaRHIFormatPlaneCount(Desc.mTexture.mFormat);
                const size_t SubresourceCount =
                    static_cast<size_t>(Desc.mTexture.mMipLevels) *
                    Desc.mTexture.mArraySize * PlaneCount;
                Texture->mOffsets.resize(SubresourceCount);
                Texture->mRowPitches.resize(SubresourceCount);
                Texture->mByteSizes.resize(SubresourceCount);
                vk::DeviceSize TotalBytes = 0;
                for (uint32_t Plane = 0; Plane < PlaneCount; ++Plane)
                {
                    for (uint32_t ArraySlice = 0;
                         ArraySlice < Desc.mTexture.mArraySize;
                         ++ArraySlice)
                    {
                        for (uint32_t MipLevel = 0;
                             MipLevel < Desc.mTexture.mMipLevels;
                             ++MipLevel)
                        {
                            const size_t Index =
                                static_cast<size_t>(Plane) *
                                    Desc.mTexture.mMipLevels *
                                    Desc.mTexture.mArraySize +
                                static_cast<size_t>(ArraySlice) *
                                    Desc.mTexture.mMipLevels +
                                MipLevel;
                            const uint32_t Width = GetArdaRHITextureMipExtent(
                                Desc.mTexture.mWidth, MipLevel);
                            const uint32_t Height = GetArdaRHITextureMipExtent(
                                Desc.mTexture.mHeight, MipLevel);
                            const uint32_t Depth = GetArdaRHITextureMipExtent(
                                Desc.mTexture.mDepth, MipLevel);
                            const uint32_t BlocksX =
                                (Width + Block.mBlockWidth - 1) /
                                    Block.mBlockWidth;
                            const uint32_t BlocksY =
                                (Height + Block.mBlockHeight - 1) /
                                    Block.mBlockHeight;
                            const size_t RowPitch =
                                static_cast<size_t>(BlocksX) *
                                    Block.mBytesPerBlock;
                            const vk::DeviceSize ByteSize =
                                static_cast<vk::DeviceSize>(RowPitch) *
                                BlocksY * Depth;
                            TotalBytes = AlignDeviceSize(
                                TotalBytes,
                                eastl::max<vk::DeviceSize>(
                                    4, Block.mBytesPerBlock));
                            Texture->mOffsets[Index] = TotalBytes;
                            Texture->mRowPitches[Index] = RowPitch;
                            Texture->mByteSizes[Index] = ByteSize;
                            TotalBytes += ByteSize;
                        }
                    }
                }
                vk::BufferCreateInfo Info;
                Info.size = TotalBytes;
                Info.usage = vk::BufferUsageFlagBits::eTransferSrc |
                    vk::BufferUsageFlagBits::eTransferDst;
                Info.sharingMode = vk::SharingMode::eExclusive;
                Texture->mBuffer = mContext->mDevice.createBuffer(Info);
                const auto Requirements =
                    mContext->mDevice.getBufferMemoryRequirements(
                        Texture->mBuffer);
                const uint32_t MemoryType = mContext->FindMemoryType(
                    Requirements.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eHostVisible |
                        vk::MemoryPropertyFlagBits::eHostCoherent);
                if (MemoryType == UINT32_MAX)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::BackendFailure,
                        "No coherent host-visible Vulkan staging memory is available."));
                Texture->mMemory = mContext->mDevice.allocateMemory(
                    vk::MemoryAllocateInfo(Requirements.size, MemoryType));
                mContext->mDevice.bindBufferMemory(
                    Texture->mBuffer, Texture->mMemory, 0);
                return {Texture, {}};
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        TArdaRHIResult<FArdaRHIStagingTextureMapping>
        FArdaVulkanProviderDevice::MapStagingTexture(
            const FArdaProviderObjectRef& Object,
            const FArdaRHITextureSlice& Slice,
            EArdaRHICpuAccess Access)
        {
            auto* Texture = dynamic_cast<FVulkanStagingTexture*>(Object.get());
            if (!Texture || !Texture->mMemory)
                return Fail<FArdaRHIStagingTextureMapping>(
                    FArdaRHIStatus::Error(
                        EArdaRHIResult::WrongDevice,
                        "Vulkan staging mapping has the wrong resource type."));
            const FArdaRHITextureDesc& Desc = Texture->mDesc.mTexture;
            const uint32_t PlaneCount =
                GetArdaRHIFormatPlaneCount(Desc.mFormat);
            if (Access != Texture->mDesc.mCpuAccess ||
                Slice.mMipLevel >= Desc.mMipLevels ||
                Slice.mArraySlice >= Desc.mArraySize ||
                Slice.mPlane >= PlaneCount ||
                Slice.mX || Slice.mY || Slice.mZ)
            {
                return Fail<FArdaRHIStagingTextureMapping>(
                    FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidArgument,
                        "Vulkan staging mapping slice or access is invalid."));
            }
            std::lock_guard<std::mutex> Lock(Texture->mMapMutex);
            if (Texture->mbMapped)
                return Fail<FArdaRHIStagingTextureMapping>(
                    FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidState,
                        "Vulkan staging texture is already mapped."));
            const size_t Index =
                static_cast<size_t>(Slice.mPlane) * Desc.mMipLevels *
                    Desc.mArraySize +
                static_cast<size_t>(Slice.mArraySlice) * Desc.mMipLevels +
                Slice.mMipLevel;
            try
            {
                void* Data = mContext->mDevice.mapMemory(
                    Texture->mMemory,
                    Texture->mOffsets[Index],
                    Texture->mByteSizes[Index]);
                Texture->mbMapped = true;
                return {{Data, Texture->mRowPitches[Index]}, {}};
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaRHIStagingTextureMapping>(
                    FArdaRHIStatus::Error(
                        EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaRHIStatus FArdaVulkanProviderDevice::UnmapStagingTexture(
            const FArdaProviderObjectRef& Object)
        {
            auto* Texture = dynamic_cast<FVulkanStagingTexture*>(Object.get());
            if (!Texture || !Texture->mMemory)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan staging unmap has the wrong resource type.");
            std::lock_guard<std::mutex> Lock(Texture->mMapMutex);
            if (!Texture->mbMapped)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidState,
                    "Vulkan staging texture is not mapped.");
            mContext->mDevice.unmapMemory(Texture->mMemory);
            Texture->mbMapped = false;
            return {};
        }

        TArdaRHIResult<void*> FArdaVulkanProviderDevice::MapBuffer(
            const FArdaProviderObjectRef& Object,
            uint64_t Offset,
            size_t Size)
        {
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Object.get());
            if (!Buffer || !Buffer->mMemory)
                return Fail<void*>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan buffer mapping has the wrong resource type."));
            if (Buffer->mDesc.mCpuAccess == EArdaRHICpuAccess::None ||
                Offset > Buffer->mDesc.mByteSize ||
                Size > Buffer->mDesc.mByteSize - Offset)
                return Fail<void*>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "Vulkan buffer mapping range is invalid or not host visible."));
            try
            {
                return { mContext->mDevice.mapMemory(
                    Buffer->mMemory, Offset, Size), {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<void*>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        void FArdaVulkanProviderDevice::UnmapBuffer(
            const FArdaProviderObjectRef& Object) noexcept
        {
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Object.get());
            if (!Buffer || !Buffer->mMemory) return;
            try { mContext->mDevice.unmapMemory(Buffer->mMemory); }
            catch (const vk::SystemError&) {}
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::ImportTexture(
            const FArdaRHINativeTextureImportDesc& Desc)
        {
            if (Desc.mNativeType != EArdaRHINativeResourceType::VulkanImage || !Desc.mNativeObject)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument, "The Vulkan texture import requires a VkImage."));
            try
            {
                auto Texture = eastl::make_shared<FVulkanTexture>();
                Texture->mContext = mContext;
                Texture->mDesc = Desc.mTexture;
                Texture->mImage = vk::Image(reinterpret_cast<VkImage>(Desc.mNativeObject));
                Texture->mbOwned = Desc.mOwnership == EArdaRHINativeOwnership::Transferred;
                const size_t SubresourceCount =
                    TextureSubresourceCount(Desc.mTexture);
                Texture->mLayouts.assign(
                    SubresourceCount,
                    ToImageLayout(
                        Desc.mInitialState,
                        ImageAspect(Desc.mTexture.mFormat) !=
                            vk::ImageAspectFlagBits::eColor));
                const FVulkanSyncState InitialSync =
                    ToVulkanSyncState(Desc.mInitialState);
                Texture->mAbstractStates.assign(
                    SubresourceCount, Desc.mInitialState);
                Texture->mStageMasks.assign(
                    SubresourceCount, InitialSync.mStages);
                Texture->mAccessMasks.assign(
                    SubresourceCount, InitialSync.mAccess);
                Texture->mQueueFamily = mContext->mQueueFamily;
                vk::ImageViewCreateInfo ViewInfo;
                ViewInfo.image = Texture->mImage;
                ViewInfo.viewType = ToViewType(Desc.mTexture.mDimension);
                ViewInfo.format = ToVulkan(Desc.mTexture.mFormat);
                ViewInfo.subresourceRange = vk::ImageSubresourceRange(
                    ImageAspect(Desc.mTexture.mFormat), 0, Desc.mTexture.mMipLevels,
                    0, Desc.mTexture.mArraySize);
                Texture->mView = mContext->mDevice.createImageView(ViewInfo);
                return { Texture, {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::ImportBuffer(
            const FArdaRHINativeBufferImportDesc& Desc)
        {
            if (Desc.mNativeType != EArdaRHINativeResourceType::VulkanBuffer || !Desc.mNativeObject)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument, "The Vulkan buffer import requires a VkBuffer."));
            auto Buffer = eastl::make_shared<FVulkanBuffer>();
            Buffer->mContext = mContext;
            Buffer->mDesc = Desc.mBuffer;
            const FVulkanSyncState InitialSync =
                ToVulkanSyncState(Desc.mInitialState);
            Buffer->mAbstractState = Desc.mInitialState;
            Buffer->mStageMask = InitialSync.mStages;
            Buffer->mAccessMask = InitialSync.mAccess;
            Buffer->mbStateKnown = true;
            Buffer->mQueueFamily = mContext->mQueueFamily;
            Buffer->mBuffer = vk::Buffer(reinterpret_cast<VkBuffer>(Desc.mNativeObject));
            Buffer->mbOwned = Desc.mOwnership == EArdaRHINativeOwnership::Transferred;
            return { Buffer, {} };
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateSampler(
            const FArdaRHISamplerDesc& Desc)
        {
            const auto Address = [](EArdaRHISamplerAddressMode Mode)
            {
                switch (Mode)
                {
                case EArdaRHISamplerAddressMode::Wrap: return vk::SamplerAddressMode::eRepeat;
                case EArdaRHISamplerAddressMode::Border: return vk::SamplerAddressMode::eClampToBorder;
                case EArdaRHISamplerAddressMode::Mirror: return vk::SamplerAddressMode::eMirroredRepeat;
                case EArdaRHISamplerAddressMode::MirrorOnce: return vk::SamplerAddressMode::eMirrorClampToEdge;
                default: return vk::SamplerAddressMode::eClampToEdge;
                }
            };
            try
            {
                auto Sampler = eastl::make_shared<FVulkanSampler>();
                Sampler->mContext = mContext;
                vk::SamplerCreateInfo Info;
                Info.magFilter = Desc.mbMagFilter ? vk::Filter::eLinear : vk::Filter::eNearest;
                Info.minFilter = Desc.mbMinFilter ? vk::Filter::eLinear : vk::Filter::eNearest;
                Info.mipmapMode = Desc.mbMipFilter ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest;
                Info.addressModeU = Address(Desc.mAddressU);
                Info.addressModeV = Address(Desc.mAddressV);
                Info.addressModeW = Address(Desc.mAddressW);
                Info.mipLodBias = Desc.mMipBias;
                Info.anisotropyEnable = Desc.mMaxAnisotropy > 1.f;
                Info.maxAnisotropy = eastl::max(1.f, Desc.mMaxAnisotropy);
                Info.compareEnable = Desc.mReduction == EArdaRHISamplerReduction::Comparison;
                Info.compareOp = vk::CompareOp::eLessOrEqual;
                Info.minLod = 0.f;
                Info.maxLod = VK_LOD_CLAMP_NONE;
                Info.borderColor = vk::BorderColor::eFloatOpaqueWhite;
                Sampler->mCreateInfo = Info;
                Sampler->mSampler = mContext->mDevice.createSampler(Info);
                return { Sampler, {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateShader(
            const FArdaRHIShaderDesc& Desc)
        {
            if (!Desc.mBytecode || Desc.mBytecodeSize == 0 || (Desc.mBytecodeSize & 3u) != 0)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument, "Vulkan shaders require aligned SPIR-V bytecode."));
            try
            {
                auto Shader = eastl::make_shared<FVulkanShader>();
                Shader->mContext = mContext;
                Shader->mStage = Desc.mStage;
                Shader->mEntryPoint = Desc.mEntryPoint.empty() ? "main" : Desc.mEntryPoint;
                vk::ShaderModuleCreateInfo Info;
                Info.codeSize = Desc.mBytecodeSize;
                Info.pCode = static_cast<const uint32_t*>(Desc.mBytecode);
                Shader->mModule = mContext->mDevice.createShaderModule(Info);
                return { Shader, {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateBindingLayout(
            const FArdaRHIBindingLayoutDesc& Desc)
        {
            try
            {
                auto Layout = eastl::make_shared<FVulkanBindingLayout>();
                Layout->mContext = mContext;
                Layout->mDesc = Desc;
                eastl::vector<vk::DescriptorSetLayoutBinding> Bindings;
                for (const auto& Item : Desc.mItems)
                {
                    if (Item.mType == EArdaRHIBindingType::PushConstants) continue;
                    vk::DescriptorSetLayoutBinding Binding;
                    Binding.binding = BindingOffset(Item.mType) + Item.mSlot;
                    Binding.descriptorType = ToDescriptorType(Item.mType);
                    Binding.descriptorCount = eastl::max(1u, Item.mArraySize);
                    Binding.stageFlags = ToStages(Desc.mVisibility);
                    Bindings.push_back(Binding);
                }
                vk::DescriptorSetLayoutCreateInfo Info;
                Info.bindingCount = static_cast<uint32_t>(Bindings.size());
                Info.pBindings = Bindings.data();
                Layout->mLayout = mContext->mDevice.createDescriptorSetLayout(Info);
                return { Layout, {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateBindlessLayout(
            const FArdaRHIBindlessLayoutDesc& BindlessDesc,
            const FArdaRHIBindingLayoutDesc& Desc)
        {
            try
            {
                auto Layout = eastl::make_shared<FVulkanBindingLayout>();
                Layout->mContext = mContext;
                Layout->mDesc = Desc;
                Layout->mBindlessDesc = BindlessDesc;
                Layout->mbBindless = true;

                if (BindlessDesc.mbDirectHeapIndexing)
                {
                    if (!mContext->mbDescriptorHeap)
                        return Fail<FArdaProviderObjectRef>(
                            FArdaRHIStatus::Error(
                                EArdaRHIResult::Unsupported,
                                "VK_EXT_descriptor_heap is unavailable."));
                    return {Layout, {}};
                }

                eastl::vector<vk::DescriptorSetLayoutBinding> Bindings;
                Bindings.reserve(Desc.mItems.size());
                for (const auto& Item : Desc.mItems)
                {
                    if (Item.mType == EArdaRHIBindingType::PushConstants)
                        continue;
                    vk::DescriptorSetLayoutBinding Binding;
                    Binding.binding = BindingOffset(Item.mType) + Item.mSlot;
                    Binding.descriptorType = ToDescriptorType(Item.mType);
                    Binding.descriptorCount = eastl::max(1u, Item.mArraySize);
                    Binding.stageFlags = ToStages(Desc.mVisibility);
                    Bindings.push_back(Binding);
                }
                std::sort(
                    Bindings.begin(), Bindings.end(),
                    [](const auto& Left, const auto& Right)
                    {
                        return Left.binding < Right.binding;
                    });

                eastl::vector<vk::DescriptorBindingFlags> BindingFlags(
                    Bindings.size(), vk::DescriptorBindingFlags{});
                for (auto& Flags : BindingFlags)
                {
                    if (mCapabilities.mDescriptors.mbPartiallyBound)
                        Flags |= vk::DescriptorBindingFlagBits::ePartiallyBound;
                    if (BindlessDesc.mbUpdateAfterBind)
                        Flags |= vk::DescriptorBindingFlagBits::eUpdateAfterBind;
                    if (BindlessDesc.mbUpdateAfterBind &&
                        mCapabilities.mDescriptors.mbUpdateUnusedWhilePending)
                        Flags |=
                            vk::DescriptorBindingFlagBits::eUpdateUnusedWhilePending;
                }
                if (BindlessDesc.mbVariableDescriptorCount &&
                    !BindingFlags.empty())
                    BindingFlags.back() |=
                        vk::DescriptorBindingFlagBits::eVariableDescriptorCount;

                vk::DescriptorSetLayoutBindingFlagsCreateInfo FlagsInfo;
                FlagsInfo.bindingCount =
                    static_cast<uint32_t>(BindingFlags.size());
                FlagsInfo.pBindingFlags = BindingFlags.data();
                vk::DescriptorSetLayoutCreateInfo Info;
                Info.pNext = &FlagsInfo;
                if (BindlessDesc.mbUpdateAfterBind)
                    Info.flags |= vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
                Info.bindingCount = static_cast<uint32_t>(Bindings.size());
                Info.pBindings = Bindings.data();
                Layout->mLayout =
                    mContext->mDevice.createDescriptorSetLayout(Info);
                return {Layout, {}};
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateBindingSet(
            const FArdaRHIBindingSetDesc& Desc,
            const FArdaProviderObjectRef& LayoutObject,
            const eastl::vector<FArdaProviderBinding>& Bindings)
        {
            auto* Layout = dynamic_cast<FVulkanBindingLayout*>(LayoutObject.get());
            if (!Layout)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice, "Vulkan binding layout has the wrong implementation."));
            try
            {
                auto Set = eastl::make_shared<FVulkanBindingSet>();
                Set->mContext = mContext;
                Set->mLayoutObject = LayoutObject;
                Set->mRetainedObjects.push_back(LayoutObject);
                for (const auto& Binding : Bindings)
                    Set->mRetainedObjects.push_back(Binding.mObject);

                if (Layout->mbBindless &&
                    Layout->mBindlessDesc.mbDirectHeapIndexing)
                {
                    if (!mContext->mbDescriptorHeap)
                        return Fail<FArdaProviderObjectRef>(
                            FArdaRHIStatus::Error(
                                EArdaRHIResult::Unsupported,
                                "VK_EXT_descriptor_heap is unavailable."));
                    const bool bSampler =
                        Layout->mBindlessDesc.mLayoutType ==
                            EArdaRHIBindlessLayoutType::MutableSampler;
                    const bool bHomogeneous = eastl::all_of(
                        Layout->mDesc.mItems.begin(),
                        Layout->mDesc.mItems.end(),
                        [bSampler](const FArdaRHIBindingLayoutItem& Item)
                        {
                            return (Item.mType ==
                                EArdaRHIBindingType::Sampler) == bSampler;
                        });
                    if (!bHomogeneous)
                        return Fail<FArdaProviderObjectRef>(
                            FArdaRHIStatus::Error(
                                EArdaRHIResult::InvalidArgument,
                                "A direct Vulkan descriptor table must be entirely sampler or entirely resource descriptors."));
                    const uint32_t DescriptorCount =
                        Desc.mVariableDescriptorCount
                            ? Desc.mVariableDescriptorCount
                            : Layout->mBindlessDesc.mMaxCapacity;
                    const uint32_t BindingCount = static_cast<uint32_t>(
                        eastl::count_if(
                            Layout->mDesc.mItems.begin(),
                            Layout->mDesc.mItems.end(),
                            [](const FArdaRHIBindingLayoutItem& Item)
                            {
                                return Item.mType !=
                                    EArdaRHIBindingType::PushConstants;
                            }));
                    const uint32_t AllocationCount =
                        DescriptorCount * BindingCount;
                    const uint32_t BaseIndex =
                        mContext->AllocateDescriptorHeapRange(
                            bSampler, AllocationCount);
                    if (BaseIndex == UINT32_MAX)
                        return Fail<FArdaProviderObjectRef>(
                            FArdaRHIStatus::Error(
                                EArdaRHIResult::BackendFailure,
                                "The native Vulkan descriptor heap is full."));
                    Set->mbDescriptorHeap = true;
                    Set->mbSamplerHeap = bSampler;
                    Set->mDescriptorBaseIndex = BaseIndex;
                    Set->mDescriptorCount = AllocationCount;
                    auto& Heap = bSampler
                        ? mContext->mSamplerDescriptorHeap
                        : mContext->mResourceDescriptorHeap;
                    for (const auto& Binding : Bindings)
                    {
                        if (Binding.mItem.mArrayElement >= DescriptorCount)
                            return Fail<FArdaProviderObjectRef>(
                                FArdaRHIStatus::Error(
                                    EArdaRHIResult::InvalidArgument,
                                    "A Vulkan descriptor-heap write exceeds its allocation."));
                        uint32_t BindingIndex = 0;
                        bool bFoundBinding = false;
                        for (const auto& Declared : Layout->mDesc.mItems)
                        {
                            if (Declared.mType ==
                                EArdaRHIBindingType::PushConstants)
                                continue;
                            if (Declared.mType == Binding.mItem.mType &&
                                Declared.mSlot == Binding.mItem.mSlot)
                            {
                                bFoundBinding = true;
                                break;
                            }
                            ++BindingIndex;
                        }
                        if (!bFoundBinding)
                            return Fail<FArdaProviderObjectRef>(
                                FArdaRHIStatus::Error(
                                    EArdaRHIResult::InvalidArgument,
                                    "A Vulkan descriptor-heap write does not match its layout."));
                        const uint32_t DescriptorIndex = BaseIndex +
                            BindingIndex * DescriptorCount +
                            Binding.mItem.mArrayElement;
                        vk::HostAddressRangeEXT Destination;
                        Destination.address = static_cast<uint8_t*>(
                            Heap.mMapped) +
                            static_cast<size_t>(DescriptorIndex) *
                                Heap.mDescriptorSize;
                        Destination.size = Heap.mDescriptorSize;
                        vk::Result Result = vk::Result::eSuccess;
                        if (bSampler)
                        {
                            auto* Sampler = dynamic_cast<FVulkanSampler*>(
                                Binding.mObject.get());
                            if (!Sampler)
                                return Fail<FArdaProviderObjectRef>(
                                    FArdaRHIStatus::Error(
                                        EArdaRHIResult::InvalidArgument,
                                        "A Vulkan sampler heap write requires a sampler."));
                            Result = mContext->mDevice.
                                writeSamplerDescriptorsEXT(
                                    1, &Sampler->mCreateInfo,
                                    &Destination);
                        }
                        else
                        {
                            vk::ResourceDescriptorInfoEXT Resource;
                            Resource.type = ToDescriptorType(
                                Binding.mItem.mType);
                            vk::DeviceAddressRangeEXT AddressRange;
                            vk::ImageViewCreateInfo View;
                            vk::ImageDescriptorInfoEXT Image;
                            if (auto* Buffer = dynamic_cast<FVulkanBuffer*>(
                                    Binding.mObject.get()))
                            {
                                const auto Range = Binding.mItem.mView.
                                    mBufferRange.Resolve(Buffer->mDesc);
                                AddressRange.address =
                                    mContext->mDevice.getBufferAddress(
                                        vk::BufferDeviceAddressInfo(
                                            Buffer->mBuffer)) +
                                    Range.mByteOffset;
                                AddressRange.size = Range.mByteSize;
                                Resource.data.pAddressRange = &AddressRange;
                            }
                            else if (auto* Texture =
                                dynamic_cast<FVulkanTexture*>(
                                    Binding.mObject.get()))
                            {
                                const auto Range = Binding.mItem.mView.
                                    mTextureRange.Resolve(Texture->mDesc);
                                View.image = Texture->mImage;
                                View.viewType = ToViewType(
                                    Binding.mItem.mView.mDimension ==
                                            EArdaRHITextureDimension::Unknown
                                        ? Texture->mDesc.mDimension
                                        : Binding.mItem.mView.mDimension);
                                View.format = ToVulkan(
                                    Binding.mItem.mView.mFormat ==
                                            EArdaRHIFormat::Unknown
                                        ? Texture->mDesc.mFormat
                                        : Binding.mItem.mView.mFormat);
                                View.subresourceRange =
                                    vk::ImageSubresourceRange(
                                        ImageAspect(Texture->mDesc.mFormat),
                                        Range.mBaseMipLevel,
                                        Range.mMipLevelCount,
                                        Range.mBaseArraySlice,
                                        Range.mArraySliceCount);
                                Image.pView = &View;
                                Image.layout =
                                    Binding.mItem.mType ==
                                            EArdaRHIBindingType::TextureUAV ||
                                        Binding.mItem.mType ==
                                            EArdaRHIBindingType::
                                                SamplerFeedbackTextureUAV
                                        ? vk::ImageLayout::eGeneral
                                        : (ImageAspect(Texture->mDesc.mFormat) ==
                                                vk::ImageAspectFlagBits::eColor
                                            ? vk::ImageLayout::
                                                eShaderReadOnlyOptimal
                                            : vk::ImageLayout::
                                                eDepthStencilReadOnlyOptimal);
                                Resource.data.pImage = &Image;
                            }
                            else if (auto* AccelStruct =
                                dynamic_cast<FVulkanAccelStruct*>(
                                    Binding.mObject.get()))
                            {
                                AddressRange.address =
                                    mContext->mDevice.
                                        getAccelerationStructureAddressKHR(
                                            vk::
                                                AccelerationStructureDeviceAddressInfoKHR(
                                                    AccelStruct->mAccelStruct));
                                AddressRange.size =
                                    AccelStruct->mRequirements.mResultSize;
                                Resource.data.pAddressRange = &AddressRange;
                            }
                            else
                                return Fail<FArdaProviderObjectRef>(
                                    FArdaRHIStatus::Error(
                                        EArdaRHIResult::InvalidArgument,
                                        "A Vulkan resource heap write has the wrong resource type."));
                            Result = mContext->mDevice.
                                writeResourceDescriptorsEXT(
                                    1, &Resource, &Destination);
                        }
                        if (Result != vk::Result::eSuccess)
                            return Fail<FArdaProviderObjectRef>(
                                VulkanFailure(
                                    "Writing a Vulkan descriptor heap failed.",
                                    Result));
                    }
                    if (!Heap.mbHostCoherent && !Bindings.empty())
                        mContext->mDevice.flushMappedMemoryRanges(
                            vk::MappedMemoryRange(
                                Heap.mMemory, 0, VK_WHOLE_SIZE));
                    return {Set, {}};
                }

                vk::DescriptorSetAllocateInfo Allocate;
                Allocate.descriptorPool = mContext->mDescriptorPool;
                Allocate.descriptorSetCount = 1;
                Allocate.pSetLayouts = &Layout->mLayout;
                vk::DescriptorSetVariableDescriptorCountAllocateInfo
                    VariableCount;
                uint32_t VariableDescriptorCount = 0;
                if (Layout->mbBindless &&
                    Layout->mBindlessDesc.mbVariableDescriptorCount)
                {
                    VariableDescriptorCount = Desc.mVariableDescriptorCount;
                    VariableCount.descriptorSetCount = 1;
                    VariableCount.pDescriptorCounts =
                        &VariableDescriptorCount;
                    Allocate.pNext = &VariableCount;
                }
                {
                    std::lock_guard<std::mutex> Lock(mContext->mDescriptorMutex);
                    Set->mSet = mContext->mDevice.allocateDescriptorSets(Allocate).front();
                    mContext->mAllocatedDescriptorSets.fetch_add(
                        1, std::memory_order_relaxed);
                }
                eastl::vector<vk::WriteDescriptorSet> Writes;
                eastl::vector<vk::DescriptorImageInfo> Images;
                eastl::vector<vk::DescriptorBufferInfo> Buffers;
                eastl::vector<vk::AccelerationStructureKHR> AccelStructs;
                eastl::vector<vk::WriteDescriptorSetAccelerationStructureKHR>
                    AccelStructWrites;
                Writes.reserve(Bindings.size());
                Images.reserve(Bindings.size());
                Buffers.reserve(Bindings.size());
                AccelStructs.reserve(Bindings.size());
                AccelStructWrites.reserve(Bindings.size());
                for (const auto& Binding : Bindings)
                {
                    vk::WriteDescriptorSet Write;
                    Write.dstSet = Set->mSet;
                    Write.dstBinding = BindingOffset(Binding.mItem.mType) + Binding.mItem.mSlot;
                    Write.dstArrayElement = Binding.mItem.mArrayElement;
                    Write.descriptorCount = 1;
                    Write.descriptorType = ToDescriptorType(Binding.mItem.mType);
                    if (auto* Sampler = dynamic_cast<FVulkanSampler*>(Binding.mObject.get()))
                    {
                        Images.push_back(vk::DescriptorImageInfo(Sampler->mSampler));
                        Write.pImageInfo = &Images.back();
                    }
                    else if (auto* Texture = dynamic_cast<FVulkanTexture*>(Binding.mObject.get()))
                    {
                        const auto Range = Binding.mItem.mView.mTextureRange.Resolve(
                            Texture->mDesc);
                        vk::ImageViewCreateInfo ViewInfo;
                        ViewInfo.image = Texture->mImage;
                        ViewInfo.viewType = ToViewType(
                            Binding.mItem.mView.mDimension ==
                                    EArdaRHITextureDimension::Unknown
                                ? Texture->mDesc.mDimension
                                : Binding.mItem.mView.mDimension);
                        ViewInfo.format = ToVulkan(
                            Binding.mItem.mView.mFormat == EArdaRHIFormat::Unknown
                                ? Texture->mDesc.mFormat
                                : Binding.mItem.mView.mFormat);
                        ViewInfo.subresourceRange = vk::ImageSubresourceRange(
                            ImageAspect(Texture->mDesc.mFormat),
                            Range.mBaseMipLevel,
                            Range.mMipLevelCount,
                            Range.mBaseArraySlice,
                            Range.mArraySliceCount);
                        const vk::ImageView View =
                            mContext->mDevice.createImageView(ViewInfo);
                        Set->mOwnedViews.push_back(View);
                        vk::ImageLayout ImageLayout =
                            Binding.mItem.mType == EArdaRHIBindingType::TextureUAV ||
                            Binding.mItem.mType == EArdaRHIBindingType::SamplerFeedbackTextureUAV
                                ? vk::ImageLayout::eGeneral
                                : (ImageAspect(Texture->mDesc.mFormat) == vk::ImageAspectFlagBits::eColor
                                    ? vk::ImageLayout::eShaderReadOnlyOptimal
                                    : vk::ImageLayout::eDepthStencilReadOnlyOptimal);
                        Images.push_back(vk::DescriptorImageInfo({}, View, ImageLayout));
                        Write.pImageInfo = &Images.back();
                    }
                    else if (auto* Buffer = dynamic_cast<FVulkanBuffer*>(Binding.mObject.get()))
                    {
                        const auto Range = Binding.mItem.mView.mBufferRange.Resolve(Buffer->mDesc);
                        Buffers.push_back(vk::DescriptorBufferInfo(
                            Buffer->mBuffer, Range.mByteOffset, Range.mByteSize));
                        Write.pBufferInfo = &Buffers.back();
                    }
                    else if (auto* AccelStruct =
                        dynamic_cast<FVulkanAccelStruct*>(Binding.mObject.get()))
                    {
                        AccelStructs.push_back(AccelStruct->mAccelStruct);
                        vk::WriteDescriptorSetAccelerationStructureKHR ASWrite;
                        ASWrite.accelerationStructureCount = 1;
                        ASWrite.pAccelerationStructures = &AccelStructs.back();
                        AccelStructWrites.push_back(ASWrite);
                        Write.pNext = &AccelStructWrites.back();
                    }
                    else
                    {
                        return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument, "A Vulkan binding has the wrong resource type."));
                    }
                    Writes.push_back(Write);
                }
                // Repoint after vector growth so every write references stable storage.
                size_t ImageIndex = 0, BufferIndex = 0, AccelStructIndex = 0;
                for (size_t Index = 0; Index < Bindings.size(); ++Index)
                {
                    if (dynamic_cast<FVulkanBuffer*>(Bindings[Index].mObject.get()))
                        Writes[Index].pBufferInfo = &Buffers[BufferIndex++];
                    else if (dynamic_cast<FVulkanAccelStruct*>(
                                 Bindings[Index].mObject.get()))
                    {
                        AccelStructWrites[AccelStructIndex].
                            pAccelerationStructures =
                            &AccelStructs[AccelStructIndex];
                        Writes[Index].pNext =
                            &AccelStructWrites[AccelStructIndex++];
                    }
                    else
                        Writes[Index].pImageInfo = &Images[ImageIndex++];
                }
                mContext->mDevice.updateDescriptorSets(
                    static_cast<uint32_t>(Writes.size()), Writes.data(), 0, nullptr);
                return { Set, {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateMergedBindingSet(
            vk::DescriptorSetLayout TargetLayout,
            const eastl::vector<FArdaProviderObjectRef>& LogicalLayouts,
            const eastl::vector<FArdaProviderObjectRef>& BindingSets)
        {
            try
            {
                auto Target = eastl::make_shared<FVulkanBindingSet>();
                Target->mContext = mContext;
                vk::DescriptorSetAllocateInfo Allocate;
                Allocate.descriptorPool = mContext->mDescriptorPool;
                Allocate.descriptorSetCount = 1;
                Allocate.pSetLayouts = &TargetLayout;
                {
                    std::lock_guard<std::mutex> Lock(mContext->mDescriptorMutex);
                    Target->mSet =
                        mContext->mDevice.allocateDescriptorSets(Allocate).front();
                    mContext->mAllocatedDescriptorSets.fetch_add(
                        1, std::memory_order_relaxed);
                }

                eastl::vector<vk::CopyDescriptorSet> Copies;
                eastl::vector<uint32_t> CopiedBindings;
                for (const auto& LogicalObject : LogicalLayouts)
                {
                    auto* Logical = dynamic_cast<FVulkanBindingLayout*>(
                        LogicalObject.get());
                    if (!Logical)
                        return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::WrongDevice,
                            "A merged Vulkan layout has the wrong implementation."));
                    FVulkanBindingSet* Source = nullptr;
                    FArdaProviderObjectRef SourceObject;
                    for (const auto& Candidate : BindingSets)
                    {
                        auto* CandidateSet = dynamic_cast<FVulkanBindingSet*>(
                            Candidate.get());
                        if (CandidateSet &&
                            CandidateSet->mLayoutObject.get() == LogicalObject.get())
                        {
                            Source = CandidateSet;
                            SourceObject = Candidate;
                            break;
                        }
                    }
                    for (const auto& Item : Logical->mDesc.mItems)
                    {
                        if (Item.mType == EArdaRHIBindingType::PushConstants)
                            continue;
                        if (!Source)
                            return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                                EArdaRHIResult::InvalidArgument,
                                "The active Vulkan pipeline is missing a required binding set."));
                        const uint32_t Binding =
                            BindingOffset(Item.mType) + Item.mSlot;
                        if (eastl::find(
                                CopiedBindings.begin(),
                                CopiedBindings.end(),
                                Binding) != CopiedBindings.end())
                            continue;
                        CopiedBindings.push_back(Binding);
                        vk::CopyDescriptorSet Copy;
                        Copy.srcSet = Source->mSet;
                        Copy.srcBinding = Binding;
                        Copy.dstSet = Target->mSet;
                        Copy.dstBinding = Binding;
                        Copy.descriptorCount = eastl::max(1u, Item.mArraySize);
                        Copies.push_back(Copy);
                    }
                    if (SourceObject)
                        Target->mRetainedObjects.push_back(
                            eastl::move(SourceObject));
                }
                mContext->mDevice.updateDescriptorSets(
                    0, nullptr, static_cast<uint32_t>(Copies.size()), Copies.data());
                return { Target, {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateFramebuffer(
            const FArdaProviderFramebufferCreateInfo& Info)
        {
            auto Result = eastl::make_shared<FVulkanFramebuffer>();
            for (const auto& Target : Info.mColors)
            {
                auto* Texture = dynamic_cast<FVulkanTexture*>(Target.mTexture.get());
                if (!Texture) return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice, "Vulkan framebuffer texture has the wrong implementation."));
                Result->mColors.push_back(Target.mTexture);
                Result->mExtent = vk::Extent2D(Texture->mDesc.mWidth, Texture->mDesc.mHeight);
            }
            if (Info.mDepth.mTexture)
            {
                auto* Texture = dynamic_cast<FVulkanTexture*>(Info.mDepth.mTexture.get());
                if (!Texture) return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice, "Vulkan depth texture has the wrong implementation."));
                Result->mDepth = Info.mDepth.mTexture;
                Result->mExtent = vk::Extent2D(Texture->mDesc.mWidth, Texture->mDesc.mHeight);
            }
            return { Result, {} };
        }

        TArdaRHIResult<vk::PipelineLayout> FArdaVulkanProviderDevice::CreatePipelineLayout(
            const eastl::vector<FArdaProviderObjectRef>& LayoutObjects,
            FVulkanPipeline& Pipeline)
        {
            Pipeline.mPushStages = {};
            Pipeline.mPushSize = 0;
            uint32_t MaximumSpace = 0;
            uint32_t DescriptorHeapPushOffset = 0;
            bool bHasClassicLayouts = false;
            for (const auto& Object : LayoutObjects)
            {
                auto* Layout = dynamic_cast<FVulkanBindingLayout*>(Object.get());
                if (!Layout) return Fail<vk::PipelineLayout>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice, "Vulkan pipeline layout has the wrong implementation."));
                Pipeline.mRetainedLayouts.push_back(Object);
                const bool bDescriptorHeap = Layout->mbBindless &&
                    Layout->mBindlessDesc.mbDirectHeapIndexing;
                if (bDescriptorHeap)
                {
                    Pipeline.mbDescriptorHeapPipeline = true;
                    FVulkanPipeline::FDescriptorSetGroup Group;
                    Group.mRegisterSpace = Layout->mDesc.mRegisterSpace;
                    Group.mLogicalLayouts.push_back(Object);
                    Group.mbDescriptorHeap = true;
                    Group.mHeapPushOffset = DescriptorHeapPushOffset;
                    Pipeline.mSetGroups.push_back(eastl::move(Group));
                    uint32_t HeapBindingIndex = 0;
                    for (const auto& Item : Layout->mDesc.mItems)
                    {
                        if (Item.mType ==
                            EArdaRHIBindingType::PushConstants)
                            continue;
                        vk::DescriptorSetAndBindingMappingEXT Mapping;
                        Mapping.descriptorSet =
                            Layout->mDesc.mRegisterSpace;
                        Mapping.firstBinding =
                            BindingOffset(Item.mType) + Item.mSlot;
                        Mapping.bindingCount = 1;
                        Mapping.resourceMask =
                            ToSpirvResourceMask(Item.mType);
                        Mapping.source = vk::DescriptorMappingSourceEXT::
                            eHeapWithPushIndex;
                        const uint32_t ResourceStride =
                            mContext->mResourceDescriptorHeap.
                                mDescriptorSize;
                        const uint32_t SamplerStride =
                            mContext->mSamplerDescriptorHeap.
                                mDescriptorSize;
                        const uint32_t BindingCapacity =
                            Layout->mBindlessDesc.mMaxCapacity;
                        Mapping.sourceData.pushIndex.heapOffset =
                            Item.mType == EArdaRHIBindingType::Sampler
                                ? 0u
                                : HeapBindingIndex * BindingCapacity *
                                    ResourceStride;
                        Mapping.sourceData.pushIndex.pushOffset =
                            DescriptorHeapPushOffset;
                        Mapping.sourceData.pushIndex.heapIndexStride =
                            ResourceStride;
                        Mapping.sourceData.pushIndex.heapArrayStride =
                            ResourceStride;
                        Mapping.sourceData.pushIndex.useCombinedImageSamplerIndex =
                            false;
                        Mapping.sourceData.pushIndex.samplerHeapOffset =
                            Item.mType == EArdaRHIBindingType::Sampler
                                ? HeapBindingIndex * BindingCapacity *
                                    SamplerStride
                                : 0u;
                        Mapping.sourceData.pushIndex.samplerPushOffset =
                            DescriptorHeapPushOffset;
                        Mapping.sourceData.pushIndex.samplerHeapIndexStride =
                            SamplerStride;
                        Mapping.sourceData.pushIndex.samplerHeapArrayStride =
                            SamplerStride;
                        Pipeline.mDescriptorHeapMappings.push_back(Mapping);
                        ++HeapBindingIndex;
                    }
                    DescriptorHeapPushOffset += sizeof(uint32_t);
                    if (DescriptorHeapPushOffset >
                        mContext->mDescriptorHeapProperties.
                            maxPushDataSize)
                        return Fail<vk::PipelineLayout>(
                            FArdaRHIStatus::Error(
                                EArdaRHIResult::InvalidArgument,
                                "Vulkan descriptor-table base indices exceed maxPushDataSize."));
                }
                else
                {
                    bHasClassicLayouts = true;
                    MaximumSpace = eastl::max(
                        MaximumSpace, Layout->mDesc.mRegisterSpace);
                }
                for (const auto& Item : Layout->mDesc.mItems)
                    if (Item.mType == EArdaRHIBindingType::PushConstants)
                    {
                        Pipeline.mPushSize = eastl::max(
                            Pipeline.mPushSize, Item.mArraySize);
                        Pipeline.mPushStages |= ToStages(Layout->mDesc.mVisibility);
                    }
            }
            const uint32_t MaximumBoundSets =
                mContext->mPhysicalDevice.getProperties().limits.maxBoundDescriptorSets;
            if (bHasClassicLayouts && MaximumSpace >= MaximumBoundSets)
                return Fail<vk::PipelineLayout>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "A Vulkan binding layout register space exceeds maxBoundDescriptorSets."));

            eastl::vector<eastl::vector<FArdaProviderObjectRef>> Groups(
                bHasClassicLayouts ? MaximumSpace + 1u : 0u);
            for (const auto& Object : LayoutObjects)
            {
                auto* Layout = static_cast<FVulkanBindingLayout*>(Object.get());
                if (Layout->mbBindless &&
                    Layout->mBindlessDesc.mbDirectHeapIndexing)
                    continue;
                Groups[Layout->mDesc.mRegisterSpace].push_back(Object);
            }

            eastl::vector<vk::DescriptorSetLayout> SetLayouts;
            SetLayouts.reserve(Groups.size());
            for (uint32_t Space = 0; Space < Groups.size(); ++Space)
            {
                eastl::vector<vk::DescriptorSetLayoutBinding> MergedBindings;
                for (const auto& Object : Groups[Space])
                {
                    auto* Layout = static_cast<FVulkanBindingLayout*>(Object.get());
                    for (const auto& Item : Layout->mDesc.mItems)
                    {
                        if (Item.mType == EArdaRHIBindingType::PushConstants)
                            continue;
                        const uint32_t BindingNumber =
                            BindingOffset(Item.mType) + Item.mSlot;
                        auto Existing = eastl::find_if(
                            MergedBindings.begin(), MergedBindings.end(),
                            [BindingNumber](const vk::DescriptorSetLayoutBinding& Value)
                            {
                                return Value.binding == BindingNumber;
                            });
                        if (Existing == MergedBindings.end())
                        {
                            vk::DescriptorSetLayoutBinding Binding;
                            Binding.binding = BindingNumber;
                            Binding.descriptorType = ToDescriptorType(Item.mType);
                            Binding.descriptorCount = eastl::max(1u, Item.mArraySize);
                            Binding.stageFlags = ToStages(Layout->mDesc.mVisibility);
                            MergedBindings.push_back(Binding);
                        }
                        else if (Existing->descriptorType != ToDescriptorType(Item.mType) ||
                            Existing->descriptorCount != eastl::max(1u, Item.mArraySize))
                        {
                            return Fail<vk::PipelineLayout>(FArdaRHIStatus::Error(
                                EArdaRHIResult::InvalidArgument,
                                "Vulkan layouts contain conflicting descriptors in the same register space and slot."));
                        }
                        else
                        {
                            Existing->stageFlags |= ToStages(Layout->mDesc.mVisibility);
                        }
                    }
                }

                vk::DescriptorSetLayout SetLayout;
                if (Groups[Space].size() == 1)
                {
                    SetLayout = static_cast<FVulkanBindingLayout*>(
                        Groups[Space][0].get())->mLayout;
                }
                else
                {
                    vk::DescriptorSetLayoutCreateInfo SetInfo;
                    SetInfo.bindingCount =
                        static_cast<uint32_t>(MergedBindings.size());
                    SetInfo.pBindings = MergedBindings.data();
                    try
                    {
                        SetLayout = mContext->mDevice.createDescriptorSetLayout(SetInfo);
                        Pipeline.mOwnedSetLayouts.push_back(SetLayout);
                    }
                    catch (const vk::SystemError& Error)
                    {
                        return Fail<vk::PipelineLayout>(FArdaRHIStatus::Error(
                            EArdaRHIResult::BackendFailure, Error.what()));
                    }
                }
                SetLayouts.push_back(SetLayout);
                if (!Groups[Space].empty() && !MergedBindings.empty())
                {
                    FVulkanPipeline::FDescriptorSetGroup Group;
                    Group.mRegisterSpace = Space;
                    Group.mLayout = SetLayout;
                    Group.mLogicalLayouts = Groups[Space];
                    Pipeline.mSetGroups.push_back(eastl::move(Group));
                }
            }
            vk::PushConstantRange Push;
            Push.stageFlags = Pipeline.mPushStages;
            Push.offset = 0;
            Push.size = Pipeline.mPushSize;
            vk::PipelineLayoutCreateInfo Info;
            Info.setLayoutCount = static_cast<uint32_t>(SetLayouts.size());
            Info.pSetLayouts = SetLayouts.data();
            Info.pushConstantRangeCount = Pipeline.mPushSize ? 1u : 0u;
            Info.pPushConstantRanges = Pipeline.mPushSize ? &Push : nullptr;
            try
            {
                return { mContext->mDevice.createPipelineLayout(Info), {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<vk::PipelineLayout>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        namespace
        {
            void ApplyDescriptorHeapShaderMappings(
                FVulkanPipeline& Pipeline,
                eastl::vector<vk::PipelineShaderStageCreateInfo>& Stages,
                vk::ShaderDescriptorSetAndBindingMappingInfoEXT& Info)
            {
                if (!Pipeline.mbDescriptorHeapPipeline) return;
                Info.mappingCount = static_cast<uint32_t>(
                    Pipeline.mDescriptorHeapMappings.size());
                Info.pMappings = Pipeline.mDescriptorHeapMappings.data();
                for (auto& Stage : Stages) Stage.pNext = &Info;
            }

            void ApplyDescriptorHeapPipelineFlag(
                const FVulkanPipeline& Pipeline,
                const void*& PNext,
                vk::PipelineCreateFlags2CreateInfo& Flags)
            {
                if (!Pipeline.mbDescriptorHeapPipeline) return;
                Flags.flags =
                    vk::PipelineCreateFlagBits2::eDescriptorHeapEXT;
                Flags.pNext = PNext;
                PNext = &Flags;
            }
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateGraphicsPipeline(
            const FArdaProviderGraphicsPipelineCreateInfo& Info)
        {
            try
            {
                auto Pipeline = eastl::make_shared<FVulkanPipeline>();
                Pipeline->mContext = mContext;
                auto Layout = CreatePipelineLayout(Info.mBindingLayouts, *Pipeline);
                if (!Layout) return Fail<FArdaProviderObjectRef>(eastl::move(Layout.mStatus));
                Pipeline->mLayout = Layout.mValue;

                eastl::vector<vk::PipelineShaderStageCreateInfo> Stages;
                const auto AddStage = [&Stages](const FArdaProviderObjectRef& Object,
                    vk::ShaderStageFlagBits Stage)
                {
                    if (auto* Shader = dynamic_cast<FVulkanShader*>(Object.get()))
                        Stages.push_back(vk::PipelineShaderStageCreateInfo(
                            {}, Stage, Shader->mModule, Shader->mEntryPoint.c_str()));
                };
                AddStage(Info.mVertexShader, vk::ShaderStageFlagBits::eVertex);
                AddStage(Info.mHullShader, vk::ShaderStageFlagBits::eTessellationControl);
                AddStage(Info.mDomainShader, vk::ShaderStageFlagBits::eTessellationEvaluation);
                AddStage(Info.mGeometryShader, vk::ShaderStageFlagBits::eGeometry);
                AddStage(Info.mPixelShader, vk::ShaderStageFlagBits::eFragment);
                if (Stages.empty() || !Info.mVertexShader)
                    return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidArgument, "A Vulkan graphics pipeline requires a vertex shader."));

                eastl::vector<vk::VertexInputBindingDescription> VertexBindings;
                eastl::vector<vk::VertexInputAttributeDescription> VertexAttributes;
                if (Info.mInputLayout)
                {
                    uint32_t Location = 0;
                    for (const auto& Attribute : Info.mInputLayout->mAttributes)
                    {
                        bool bHasBinding = false;
                        for (const auto& Existing : VertexBindings)
                            if (Existing.binding == Attribute.mBufferIndex) { bHasBinding = true; break; }
                        if (!bHasBinding)
                            VertexBindings.push_back(vk::VertexInputBindingDescription(
                                Attribute.mBufferIndex, Attribute.mElementStride,
                                Attribute.mbInstanced ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex));
                        for (uint32_t Element = 0; Element < eastl::max(1u, Attribute.mArraySize); ++Element)
                            VertexAttributes.push_back(vk::VertexInputAttributeDescription(
                                Location++, Attribute.mBufferIndex, ToVulkan(Attribute.mFormat),
                                Attribute.mOffset + Element *
                                    GetArdaRHIFormatElementSize(Attribute.mFormat)));
                    }
                }
                vk::PipelineVertexInputStateCreateInfo VertexInput;
                VertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(VertexBindings.size());
                VertexInput.pVertexBindingDescriptions = VertexBindings.data();
                VertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(VertexAttributes.size());
                VertexInput.pVertexAttributeDescriptions = VertexAttributes.data();
                vk::PipelineInputAssemblyStateCreateInfo InputAssembly;
                InputAssembly.topology = ToTopology(Info.mDesc.mTopology);

                vk::PipelineTessellationStateCreateInfo Tessellation;
                Tessellation.patchControlPoints = eastl::max(1u, Info.mDesc.mPatchControlPoints);
                vk::PipelineViewportStateCreateInfo Viewport;
                Viewport.viewportCount = 1;
                Viewport.scissorCount = 1;
                vk::PipelineRasterizationStateCreateInfo Raster;
                Raster.depthClampEnable = !Info.mDesc.mRasterState.mbDepthClip;
                Raster.rasterizerDiscardEnable = false;
                Raster.polygonMode = Info.mDesc.mRasterState.mFillMode == EArdaRHIFillMode::Wireframe
                    ? vk::PolygonMode::eLine : vk::PolygonMode::eFill;
                Raster.cullMode = Info.mDesc.mRasterState.mCullMode == EArdaRHICullMode::Back
                    ? vk::CullModeFlagBits::eBack
                    : (Info.mDesc.mRasterState.mCullMode == EArdaRHICullMode::Front
                        ? vk::CullModeFlagBits::eFront : vk::CullModeFlagBits::eNone);
                Raster.frontFace = Info.mDesc.mRasterState.mbFrontCounterClockwise
                    ? vk::FrontFace::eCounterClockwise : vk::FrontFace::eClockwise;
                Raster.lineWidth = 1.f;
                vk::PipelineMultisampleStateCreateInfo Multisample;
                Multisample.rasterizationSamples = static_cast<vk::SampleCountFlagBits>(
                    eastl::max(1u, Info.mDesc.mSampleCount));
                Multisample.alphaToCoverageEnable = Info.mDesc.mBlendState.mbAlphaToCoverage;
                vk::PipelineDepthStencilStateCreateInfo Depth;
                Depth.depthTestEnable = Info.mDesc.mDepthStencilState.mbDepthTest;
                Depth.depthWriteEnable = Info.mDesc.mDepthStencilState.mbDepthWrite;
                Depth.depthCompareOp = ToCompare(Info.mDesc.mDepthStencilState.mDepthFunc);

                eastl::vector<vk::PipelineColorBlendAttachmentState> BlendAttachments;
                for (size_t Index = 0; Index < Info.mDesc.mColorFormats.size(); ++Index)
                {
                    const auto& Source = Info.mDesc.mBlendState.mTargets[Index];
                    vk::PipelineColorBlendAttachmentState Target;
                    Target.blendEnable = Source.mbEnable;
                    Target.srcColorBlendFactor = ToBlend(Source.mSourceColor);
                    Target.dstColorBlendFactor = ToBlend(Source.mDestinationColor);
                    Target.colorBlendOp = vk::BlendOp::eAdd;
                    Target.srcAlphaBlendFactor = ToBlend(Source.mSourceAlpha);
                    Target.dstAlphaBlendFactor = ToBlend(Source.mDestinationAlpha);
                    Target.alphaBlendOp = vk::BlendOp::eAdd;
                    Target.colorWriteMask = vk::ColorComponentFlagBits::eR |
                        vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB |
                        vk::ColorComponentFlagBits::eA;
                    BlendAttachments.push_back(Target);
                }
                vk::PipelineColorBlendStateCreateInfo Blend;
                Blend.attachmentCount = static_cast<uint32_t>(BlendAttachments.size());
                Blend.pAttachments = BlendAttachments.data();
                const eastl::array<vk::DynamicState, 2> DynamicStates = {
                    vk::DynamicState::eViewport, vk::DynamicState::eScissor };
                vk::PipelineDynamicStateCreateInfo Dynamic;
                Dynamic.dynamicStateCount = static_cast<uint32_t>(DynamicStates.size());
                Dynamic.pDynamicStates = DynamicStates.data();

                eastl::vector<vk::Format> ColorFormats;
                for (const auto Format : Info.mDesc.mColorFormats) ColorFormats.push_back(ToVulkan(Format));
                vk::PipelineRenderingCreateInfo Rendering;
                Rendering.colorAttachmentCount = static_cast<uint32_t>(ColorFormats.size());
                Rendering.pColorAttachmentFormats = ColorFormats.data();
                const auto DepthFormat = ToVulkan(Info.mDesc.mDepthFormat);
                const auto Aspect = ImageAspect(Info.mDesc.mDepthFormat);
                Rendering.depthAttachmentFormat = (Aspect & vk::ImageAspectFlagBits::eDepth)
                    ? DepthFormat : vk::Format::eUndefined;
                Rendering.stencilAttachmentFormat = (Aspect & vk::ImageAspectFlagBits::eStencil)
                    ? DepthFormat : vk::Format::eUndefined;

                vk::ShaderDescriptorSetAndBindingMappingInfoEXT MappingInfo;
                ApplyDescriptorHeapShaderMappings(
                    *Pipeline, Stages, MappingInfo);
                vk::GraphicsPipelineCreateInfo Native;
                Native.pNext = &Rendering;
                vk::PipelineCreateFlags2CreateInfo HeapFlags;
                ApplyDescriptorHeapPipelineFlag(
                    *Pipeline, Native.pNext, HeapFlags);
                Native.stageCount = static_cast<uint32_t>(Stages.size());
                Native.pStages = Stages.data();
                Native.pVertexInputState = &VertexInput;
                Native.pInputAssemblyState = &InputAssembly;
                Native.pTessellationState = Info.mDesc.mTopology == EArdaRHIPrimitiveTopology::PatchList
                    ? &Tessellation : nullptr;
                Native.pViewportState = &Viewport;
                Native.pRasterizationState = &Raster;
                Native.pMultisampleState = &Multisample;
                Native.pDepthStencilState = &Depth;
                Native.pColorBlendState = &Blend;
                Native.pDynamicState = &Dynamic;
                Native.layout = Pipeline->mLayout;
                // VkPipelineCache access is externally synchronized. Pipeline
                // precaching may create different PSOs concurrently.
                std::unique_lock<std::mutex> CacheLock(
                    mPipelineCacheMutex, std::defer_lock);
                if (mPipelineCache)
                    CacheLock.lock();
                Pipeline->mPipeline = mContext->mDevice.createGraphicsPipeline(
                    mPipelineCache, Native).value;
                if (mPipelineCache)
                    mbPipelineCacheDirty = true;
                return { Pipeline, {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateMeshletPipeline(
            const FArdaProviderMeshletPipelineCreateInfo& Info)
        {
            if (!mContext->mbMeshShader)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "VK_EXT_mesh_shader is unavailable."));
            auto* MeshShader = dynamic_cast<FVulkanShader*>(
                Info.mMeshShader.get());
            if (!MeshShader)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "A Vulkan mesh pipeline requires a mesh shader."));
            try
            {
                auto Pipeline = eastl::make_shared<FVulkanPipeline>();
                Pipeline->mContext = mContext;
                auto Layout = CreatePipelineLayout(
                    Info.mBindingLayouts, *Pipeline);
                if (!Layout)
                    return Fail<FArdaProviderObjectRef>(
                        eastl::move(Layout.mStatus));
                Pipeline->mLayout = Layout.mValue;
                eastl::vector<vk::PipelineShaderStageCreateInfo> Stages;
                if (auto* Task = dynamic_cast<FVulkanShader*>(
                        Info.mAmplificationShader.get()))
                    Stages.emplace_back(vk::PipelineShaderStageCreateFlags{},
                        vk::ShaderStageFlagBits::eTaskEXT, Task->mModule,
                        Task->mEntryPoint.c_str());
                Stages.emplace_back(vk::PipelineShaderStageCreateFlags{},
                    vk::ShaderStageFlagBits::eMeshEXT, MeshShader->mModule,
                    MeshShader->mEntryPoint.c_str());
                if (auto* Pixel = dynamic_cast<FVulkanShader*>(
                        Info.mPixelShader.get()))
                    Stages.emplace_back(vk::PipelineShaderStageCreateFlags{},
                        vk::ShaderStageFlagBits::eFragment, Pixel->mModule,
                        Pixel->mEntryPoint.c_str());

                vk::PipelineViewportStateCreateInfo Viewport;
                Viewport.viewportCount = 1;
                Viewport.scissorCount = 1;
                vk::PipelineRasterizationStateCreateInfo Raster;
                Raster.depthClampEnable = !Info.mDesc.mRasterState.mbDepthClip;
                Raster.polygonMode =
                    Info.mDesc.mRasterState.mFillMode ==
                        EArdaRHIFillMode::Wireframe
                        ? vk::PolygonMode::eLine : vk::PolygonMode::eFill;
                Raster.cullMode =
                    Info.mDesc.mRasterState.mCullMode == EArdaRHICullMode::Back
                        ? vk::CullModeFlagBits::eBack
                        : Info.mDesc.mRasterState.mCullMode == EArdaRHICullMode::Front
                            ? vk::CullModeFlagBits::eFront
                            : vk::CullModeFlagBits::eNone;
                Raster.frontFace =
                    Info.mDesc.mRasterState.mbFrontCounterClockwise
                        ? vk::FrontFace::eCounterClockwise
                        : vk::FrontFace::eClockwise;
                Raster.lineWidth = 1.f;
                vk::PipelineMultisampleStateCreateInfo Multisample;
                Multisample.rasterizationSamples =
                    static_cast<vk::SampleCountFlagBits>(
                        eastl::max(1u, Info.mDesc.mSampleCount));
                Multisample.alphaToCoverageEnable =
                    Info.mDesc.mBlendState.mbAlphaToCoverage;
                vk::PipelineDepthStencilStateCreateInfo Depth;
                Depth.depthTestEnable =
                    Info.mDesc.mDepthStencilState.mbDepthTest;
                Depth.depthWriteEnable =
                    Info.mDesc.mDepthStencilState.mbDepthWrite;
                Depth.depthCompareOp = ToCompare(
                    Info.mDesc.mDepthStencilState.mDepthFunc);
                eastl::vector<vk::PipelineColorBlendAttachmentState> Attachments;
                for (size_t Index = 0;
                     Index < Info.mDesc.mColorFormats.size(); ++Index)
                {
                    const auto& Source = Info.mDesc.mBlendState.mTargets[Index];
                    vk::PipelineColorBlendAttachmentState Target;
                    Target.blendEnable = Source.mbEnable;
                    Target.srcColorBlendFactor = ToBlend(Source.mSourceColor);
                    Target.dstColorBlendFactor = ToBlend(Source.mDestinationColor);
                    Target.colorBlendOp = vk::BlendOp::eAdd;
                    Target.srcAlphaBlendFactor = ToBlend(Source.mSourceAlpha);
                    Target.dstAlphaBlendFactor = ToBlend(Source.mDestinationAlpha);
                    Target.alphaBlendOp = vk::BlendOp::eAdd;
                    Target.colorWriteMask = vk::ColorComponentFlagBits::eR |
                        vk::ColorComponentFlagBits::eG |
                        vk::ColorComponentFlagBits::eB |
                        vk::ColorComponentFlagBits::eA;
                    Attachments.push_back(Target);
                }
                vk::PipelineColorBlendStateCreateInfo Blend;
                Blend.attachmentCount = static_cast<uint32_t>(Attachments.size());
                Blend.pAttachments = Attachments.data();
                const eastl::array<vk::DynamicState, 2> DynamicStates = {
                    vk::DynamicState::eViewport, vk::DynamicState::eScissor};
                vk::PipelineDynamicStateCreateInfo Dynamic;
                Dynamic.dynamicStateCount =
                    static_cast<uint32_t>(DynamicStates.size());
                Dynamic.pDynamicStates = DynamicStates.data();
                eastl::vector<vk::Format> ColorFormats;
                for (auto Format : Info.mDesc.mColorFormats)
                    ColorFormats.push_back(ToVulkan(Format));
                vk::PipelineRenderingCreateInfo Rendering;
                Rendering.colorAttachmentCount =
                    static_cast<uint32_t>(ColorFormats.size());
                Rendering.pColorAttachmentFormats = ColorFormats.data();
                const vk::Format DepthFormat = ToVulkan(Info.mDesc.mDepthFormat);
                const auto Aspect = ImageAspect(Info.mDesc.mDepthFormat);
                Rendering.depthAttachmentFormat =
                    (Aspect & vk::ImageAspectFlagBits::eDepth)
                        ? DepthFormat : vk::Format::eUndefined;
                Rendering.stencilAttachmentFormat =
                    (Aspect & vk::ImageAspectFlagBits::eStencil)
                        ? DepthFormat : vk::Format::eUndefined;
                vk::ShaderDescriptorSetAndBindingMappingInfoEXT MappingInfo;
                ApplyDescriptorHeapShaderMappings(
                    *Pipeline, Stages, MappingInfo);
                vk::GraphicsPipelineCreateInfo Native;
                Native.pNext = &Rendering;
                vk::PipelineCreateFlags2CreateInfo HeapFlags;
                ApplyDescriptorHeapPipelineFlag(
                    *Pipeline, Native.pNext, HeapFlags);
                Native.stageCount = static_cast<uint32_t>(Stages.size());
                Native.pStages = Stages.data();
                Native.pViewportState = &Viewport;
                Native.pRasterizationState = &Raster;
                Native.pMultisampleState = &Multisample;
                Native.pDepthStencilState = &Depth;
                Native.pColorBlendState = &Blend;
                Native.pDynamicState = &Dynamic;
                Native.layout = Pipeline->mLayout;
                std::unique_lock<std::mutex> CacheLock(
                    mPipelineCacheMutex, std::defer_lock);
                if (mPipelineCache) CacheLock.lock();
                Pipeline->mPipeline =
                    mContext->mDevice.createGraphicsPipeline(
                        mPipelineCache, Native).value;
                if (mPipelineCache) mbPipelineCacheDirty = true;
                return {Pipeline, {}};
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaProviderObjectResult
        FArdaVulkanProviderDevice::CreateRayTracingPipeline(
            const FArdaProviderRayTracingPipelineCreateInfo& Info)
        {
            if (!mContext->mbRayTracingPipeline)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::Unsupported,
                    "VK_KHR_ray_tracing_pipeline is unavailable."));
            try
            {
                auto Pipeline = eastl::make_shared<FVulkanRayTracingPipeline>();
                Pipeline->mContext = mContext;
                Pipeline->mBindings.mContext = mContext;
                auto Layout = CreatePipelineLayout(
                    Info.mGlobalBindingLayouts, Pipeline->mBindings);
                if (!Layout)
                    return Fail<FArdaProviderObjectRef>(
                        eastl::move(Layout.mStatus));
                Pipeline->mBindings.mLayout = Layout.mValue;
                eastl::vector<vk::PipelineShaderStageCreateInfo> Stages;
                eastl::vector<vk::RayTracingShaderGroupCreateInfoKHR> Groups;
                const auto AddStage = [&Stages](
                    const FArdaProviderRayTracingShader& Source)
                    -> TArdaRHIResult<uint32_t>
                {
                    auto* Shader = dynamic_cast<FVulkanShader*>(
                        Source.mShader.get());
                    if (!Shader)
                        return Fail<uint32_t>(FArdaRHIStatus::Error(
                            EArdaRHIResult::WrongDevice,
                            "A Vulkan ray-tracing shader is invalid."));
                    const uint32_t Index = static_cast<uint32_t>(Stages.size());
                    Stages.emplace_back(vk::PipelineShaderStageCreateFlags{},
                        ToRayTracingStage(Shader->mStage), Shader->mModule,
                        Shader->mEntryPoint.c_str());
                    return {Index, {}};
                };
                for (const auto& Shader : Info.mShaders)
                {
                    auto Stage = AddStage(Shader);
                    if (!Stage)
                        return Fail<FArdaProviderObjectRef>(Stage.mStatus);
                    vk::RayTracingShaderGroupCreateInfoKHR Group;
                    Group.type =
                        vk::RayTracingShaderGroupTypeKHR::eGeneral;
                    Group.generalShader = Stage.mValue;
                    Group.closestHitShader = VK_SHADER_UNUSED_KHR;
                    Group.anyHitShader = VK_SHADER_UNUSED_KHR;
                    Group.intersectionShader = VK_SHADER_UNUSED_KHR;
                    const uint32_t GroupIndex =
                        static_cast<uint32_t>(Groups.size());
                    Groups.push_back(Group);
                    Pipeline->mExportGroups.push_back({
                        Shader.mExportName, GroupIndex});
                }
                for (const auto& Hit : Info.mHitGroups)
                {
                    vk::RayTracingShaderGroupCreateInfoKHR Group;
                    Group.type = Hit.mbProceduralPrimitive
                        ? vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup
                        : vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup;
                    Group.generalShader = VK_SHADER_UNUSED_KHR;
                    Group.closestHitShader = VK_SHADER_UNUSED_KHR;
                    Group.anyHitShader = VK_SHADER_UNUSED_KHR;
                    Group.intersectionShader = VK_SHADER_UNUSED_KHR;
                    if (Hit.mClosestHit.mShader)
                    {
                        auto Stage = AddStage(Hit.mClosestHit);
                        if (!Stage) return Fail<FArdaProviderObjectRef>(Stage.mStatus);
                        Group.closestHitShader = Stage.mValue;
                    }
                    if (Hit.mAnyHit.mShader)
                    {
                        auto Stage = AddStage(Hit.mAnyHit);
                        if (!Stage) return Fail<FArdaProviderObjectRef>(Stage.mStatus);
                        Group.anyHitShader = Stage.mValue;
                    }
                    if (Hit.mIntersection.mShader)
                    {
                        auto Stage = AddStage(Hit.mIntersection);
                        if (!Stage) return Fail<FArdaProviderObjectRef>(Stage.mStatus);
                        Group.intersectionShader = Stage.mValue;
                    }
                    const uint32_t GroupIndex =
                        static_cast<uint32_t>(Groups.size());
                    Groups.push_back(Group);
                    Pipeline->mExportGroups.push_back({
                        Hit.mExportName, GroupIndex});
                }
                vk::ShaderDescriptorSetAndBindingMappingInfoEXT MappingInfo;
                ApplyDescriptorHeapShaderMappings(
                    Pipeline->mBindings, Stages, MappingInfo);
                vk::RayTracingPipelineCreateInfoKHR Create;
                vk::PipelineCreateFlags2CreateInfo HeapFlags;
                ApplyDescriptorHeapPipelineFlag(
                    Pipeline->mBindings, Create.pNext, HeapFlags);
                Create.stageCount = static_cast<uint32_t>(Stages.size());
                Create.pStages = Stages.data();
                Create.groupCount = static_cast<uint32_t>(Groups.size());
                Create.pGroups = Groups.data();
                Create.maxPipelineRayRecursionDepth =
                    Info.mDesc.mMaxRecursionDepth;
                Create.layout = Pipeline->mBindings.mLayout;
                Pipeline->mPipeline =
                    mContext->mDevice.createRayTracingPipelineKHR(
                        {}, mPipelineCache, Create).value;
                if (mPipelineCache) mbPipelineCacheDirty = true;
                return {Pipeline, {}};
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateShaderTable(
            const FArdaProviderObjectRef& PipelineObject,
            const FArdaRHIShaderTableDesc& Desc)
        {
            auto* Pipeline = dynamic_cast<FVulkanRayTracingPipeline*>(
                PipelineObject.get());
            if (!Pipeline)
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "The Vulkan shader table has the wrong pipeline."));
            auto Table = eastl::make_shared<FVulkanShaderTable>();
            Table->mContext = mContext;
            Table->mPipelineObject = PipelineObject;
            Table->mPipeline = Pipeline;
            Table->mDesc = Desc;
            Table->mRecords.resize(Desc.mMaxEntries);
            return {Table, {}};
        }

        FArdaRHIStatus FArdaVulkanProviderDevice::SetShaderTableRecord(
            const FArdaProviderObjectRef& TableObject,
            const FArdaRHIShaderTableRecordDesc& Record,
            const FArdaProviderObjectRef& LocalBindings,
            const FArdaProviderObjectRef& Geometry)
        {
            auto* Table = dynamic_cast<FVulkanShaderTable*>(TableObject.get());
            if (!Table || Record.mRecordIndex >= Table->mRecords.size())
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "The Vulkan shader-table record is invalid.");
            if (LocalBindings)
                return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                    "Vulkan local descriptor sets require descriptor-heap shader records; use raw local arguments.");
            if (Geometry &&
                !dynamic_cast<FVulkanAccelStruct*>(Geometry.get()))
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "The Vulkan shader-table geometry is invalid.");
            auto& Native = Table->mRecords[Record.mRecordIndex];
            Native.mbWritten = true;
            Native.mType = Record.mType;
            Native.mRecordIndex = Record.mRecordIndex;
            Native.mExportName = Record.mExportName;
            Native.mLocalArguments = Record.mLocalArguments;
            Native.mUserData = Record.mUserData;
            Native.mGeometrySegment = Record.mGeometrySegment;
            Native.mGeometry = Geometry;
            return {};
        }

        FArdaRHIStatus FArdaVulkanProviderDevice::CommitShaderTable(
            const FArdaProviderObjectRef& TableObject)
        {
            auto* Table = dynamic_cast<FVulkanShaderTable*>(TableObject.get());
            if (!Table || !Table->mPipeline)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "The Vulkan shader table is invalid.");
            try
            {
                eastl::vector<const FVulkanShaderTable::FRecord*> Ray;
                eastl::vector<const FVulkanShaderTable::FRecord*> Miss;
                eastl::vector<const FVulkanShaderTable::FRecord*> Hit;
                eastl::vector<const FVulkanShaderTable::FRecord*> Callable;
                for (const auto& Record : Table->mRecords)
                {
                    if (!Record.mbWritten) continue;
                    switch (Record.mType)
                    {
                    case EArdaRHIShaderTableRecordType::RayGeneration:
                        Ray.push_back(&Record); break;
                    case EArdaRHIShaderTableRecordType::Miss:
                        Miss.push_back(&Record); break;
                    case EArdaRHIShaderTableRecordType::HitGroup:
                        Hit.push_back(&Record); break;
                    case EArdaRHIShaderTableRecordType::Callable:
                        Callable.push_back(&Record); break;
                    }
                }
                if (Ray.size() != 1)
                    return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
                        "A Vulkan shader table requires exactly one ray-generation record.");
                const auto& Properties =
                    mContext->mRayTracingPipelineProperties;
                const uint64_t HandleSize = Properties.shaderGroupHandleSize;
                const uint64_t HandleAlignment =
                    Properties.shaderGroupHandleAlignment;
                const uint64_t BaseAlignment =
                    Properties.shaderGroupBaseAlignment;
                uint64_t MaxPayload = 0;
                for (const auto& Record : Table->mRecords)
                    if (Record.mbWritten)
                        MaxPayload = eastl::max<uint64_t>(MaxPayload,
                            Record.mLocalArguments.size() +
                            (Record.mGeometry ? sizeof(uint64_t) : 0u) +
                            ((Record.mUserData || Record.mGeometrySegment)
                                ? sizeof(uint32_t) * 2u : 0u));
                const uint64_t Stride = AlignDeviceSize(
                    HandleSize + MaxPayload, HandleAlignment);
                uint64_t Cursor = 0;
                const uint64_t RayOffset = AlignDeviceSize(Cursor, BaseAlignment);
                Cursor = RayOffset + Stride;
                const uint64_t MissOffset = Miss.empty() ? 0
                    : AlignDeviceSize(Cursor, BaseAlignment);
                if (!Miss.empty()) Cursor = MissOffset + Miss.size() * Stride;
                const uint64_t HitOffset = Hit.empty() ? 0
                    : AlignDeviceSize(Cursor, BaseAlignment);
                if (!Hit.empty()) Cursor = HitOffset + Hit.size() * Stride;
                const uint64_t CallableOffset = Callable.empty() ? 0
                    : AlignDeviceSize(Cursor, BaseAlignment);
                if (!Callable.empty())
                    Cursor = CallableOffset + Callable.size() * Stride;

                if (Table->mBuffer)
                {
                    mContext->mDevice.destroyBuffer(Table->mBuffer);
                    mContext->mDevice.freeMemory(Table->mMemory);
                    Table->mBuffer = nullptr;
                    Table->mMemory = nullptr;
                }
                vk::BufferCreateInfo BufferInfo;
                BufferInfo.size = Cursor;
                BufferInfo.usage =
                    vk::BufferUsageFlagBits::eShaderBindingTableKHR |
                    vk::BufferUsageFlagBits::eShaderDeviceAddress;
                Table->mBuffer = mContext->mDevice.createBuffer(BufferInfo);
                const auto Requirements =
                    mContext->mDevice.getBufferMemoryRequirements(Table->mBuffer);
                const uint32_t MemoryType = mContext->FindMemoryType(
                    Requirements.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eHostVisible |
                    vk::MemoryPropertyFlagBits::eHostCoherent);
                if (MemoryType == UINT32_MAX)
                    return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
                        "No host-visible Vulkan shader-table memory is available.");
                vk::MemoryAllocateFlagsInfo Flags(
                    vk::MemoryAllocateFlagBits::eDeviceAddress);
                vk::MemoryAllocateInfo Allocate(Requirements.size, MemoryType);
                Allocate.pNext = &Flags;
                Table->mMemory = mContext->mDevice.allocateMemory(Allocate);
                mContext->mDevice.bindBufferMemory(
                    Table->mBuffer, Table->mMemory, 0);
                uint8_t* Mapped = static_cast<uint8_t*>(
                    mContext->mDevice.mapMemory(Table->mMemory, 0, Cursor));
                std::memset(Mapped, 0, static_cast<size_t>(Cursor));
                const uint32_t GroupCount = static_cast<uint32_t>(
                    Table->mPipeline->mExportGroups.size());
                const auto Handles =
                    mContext->mDevice.getRayTracingShaderGroupHandlesKHR<uint8_t>(
                        Table->mPipeline->mPipeline, 0, GroupCount,
                        GroupCount * HandleSize);
                const auto Write = [&](
                    const FVulkanShaderTable::FRecord& Record,
                    uint64_t Offset) -> FArdaRHIStatus
                {
                    const auto Group = eastl::find_if(
                        Table->mPipeline->mExportGroups.begin(),
                        Table->mPipeline->mExportGroups.end(),
                        [&Record](const auto& Candidate)
                        {
                            return Candidate.mExportName == Record.mExportName;
                        });
                    if (Group == Table->mPipeline->mExportGroups.end())
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument,
                            "A Vulkan shader-table export is absent from the pipeline.");
                    std::memcpy(Mapped + Offset,
                        Handles.data() + Group->mGroupIndex * HandleSize,
                        HandleSize);
                    uint8_t* Payload = Mapped + Offset + HandleSize;
                    if (!Record.mLocalArguments.empty())
                    {
                        std::memcpy(Payload, Record.mLocalArguments.data(),
                            Record.mLocalArguments.size());
                        Payload += Record.mLocalArguments.size();
                    }
                    if (auto* Geometry = dynamic_cast<FVulkanAccelStruct*>(
                            Record.mGeometry.get()))
                    {
                        const uint64_t Address =
                            GetAccelStructDeviceAddress(Record.mGeometry);
                        std::memcpy(Payload, &Address, sizeof(Address));
                        Payload += sizeof(Address);
                    }
                    if (Record.mUserData || Record.mGeometrySegment)
                    {
                        std::memcpy(Payload, &Record.mUserData,
                            sizeof(Record.mUserData));
                        Payload += sizeof(Record.mUserData);
                        std::memcpy(Payload, &Record.mGeometrySegment,
                            sizeof(Record.mGeometrySegment));
                    }
                    return {};
                };
                FArdaRHIStatus Status = Write(*Ray.front(), RayOffset);
                const auto WriteSection = [&Write, Stride](
                    const auto& Records, uint64_t Offset)
                {
                    FArdaRHIStatus Status;
                    for (size_t Index = 0; Status && Index < Records.size(); ++Index)
                        Status = Write(*Records[Index], Offset + Index * Stride);
                    return Status;
                };
                if (Status) Status = WriteSection(Miss, MissOffset);
                if (Status) Status = WriteSection(Hit, HitOffset);
                if (Status) Status = WriteSection(Callable, CallableOffset);
                mContext->mDevice.unmapMemory(Table->mMemory);
                if (!Status) return Status;
                const uint64_t Address = mContext->mDevice.getBufferAddress(
                    vk::BufferDeviceAddressInfo(Table->mBuffer));
                Table->mRayGeneration = vk::StridedDeviceAddressRegionKHR(
                    Address + RayOffset, Stride, Stride);
                Table->mMiss = Miss.empty()
                    ? vk::StridedDeviceAddressRegionKHR{}
                    : vk::StridedDeviceAddressRegionKHR{
                        Address + MissOffset, Stride, Miss.size() * Stride};
                Table->mHit = Hit.empty()
                    ? vk::StridedDeviceAddressRegionKHR{}
                    : vk::StridedDeviceAddressRegionKHR{
                        Address + HitOffset, Stride, Hit.size() * Stride};
                Table->mCallable = Callable.empty()
                    ? vk::StridedDeviceAddressRegionKHR{}
                    : vk::StridedDeviceAddressRegionKHR{
                        Address + CallableOffset, Stride,
                        Callable.size() * Stride};
                return {};
            }
            catch (const vk::SystemError& Error)
            {
                return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
                    Error.what());
            }
        }

        FArdaRHIStatus
        FArdaVulkanProviderDevice::SetShaderTableRayGeneration(
            const FArdaProviderObjectRef& TableObject,
            const char* ExportName,
            const FArdaProviderObjectRef& LocalBindings)
        {
            if (LocalBindings)
                return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                    "Vulkan legacy local descriptor-set records are unsupported.");
            FArdaRHIShaderTableRecordDesc Record;
            Record.mType = EArdaRHIShaderTableRecordType::RayGeneration;
            Record.mRecordIndex = 0;
            Record.mExportName = ExportName;
            if (auto Status = SetShaderTableRecord(
                    TableObject, Record, {}, {}); !Status)
                return Status;
            return CommitShaderTable(TableObject);
        }

        FArdaRHIStatus FArdaVulkanProviderDevice::AddShaderTableEntry(
            const FArdaProviderObjectRef& TableObject,
            const char* ExportName,
            const FArdaProviderObjectRef& LocalBindings,
            uint32_t Category)
        {
            auto* Table = dynamic_cast<FVulkanShaderTable*>(TableObject.get());
            if (!Table || LocalBindings)
                return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                    "Vulkan shader-table entry bindings are unsupported.");
            uint32_t Index = 0;
            while (Index < Table->mRecords.size() &&
                Table->mRecords[Index].mbWritten) ++Index;
            if (Index >= Table->mRecords.size())
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                    "The Vulkan shader table is at capacity.");
            FArdaRHIShaderTableRecordDesc Record;
            Record.mRecordIndex = Index;
            Record.mExportName = ExportName;
            Record.mType = Category == 0
                ? EArdaRHIShaderTableRecordType::Miss
                : Category == 1
                    ? EArdaRHIShaderTableRecordType::HitGroup
                    : EArdaRHIShaderTableRecordType::Callable;
            if (auto Status = SetShaderTableRecord(
                    TableObject, Record, {}, {}); !Status)
                return Status;
            return CommitShaderTable(TableObject);
        }

        FArdaProviderObjectResult FArdaVulkanProviderDevice::CreateComputePipeline(
            const FArdaProviderComputePipelineCreateInfo& Info)
        {
            auto* Shader = dynamic_cast<FVulkanShader*>(Info.mComputeShader.get());
            if (!Shader) return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                EArdaRHIResult::InvalidArgument, "A Vulkan compute pipeline requires a compute shader."));
            try
            {
                auto Pipeline = eastl::make_shared<FVulkanPipeline>();
                Pipeline->mContext = mContext;
                auto Layout = CreatePipelineLayout(Info.mBindingLayouts, *Pipeline);
                if (!Layout) return Fail<FArdaProviderObjectRef>(eastl::move(Layout.mStatus));
                Pipeline->mLayout = Layout.mValue;
                vk::ComputePipelineCreateInfo Native;
                Native.stage = vk::PipelineShaderStageCreateInfo({},
                    vk::ShaderStageFlagBits::eCompute, Shader->mModule,
                    Shader->mEntryPoint.c_str());
                vk::ShaderDescriptorSetAndBindingMappingInfoEXT MappingInfo;
                if (Pipeline->mbDescriptorHeapPipeline)
                {
                    MappingInfo.mappingCount = static_cast<uint32_t>(
                        Pipeline->mDescriptorHeapMappings.size());
                    MappingInfo.pMappings =
                        Pipeline->mDescriptorHeapMappings.data();
                    Native.stage.pNext = &MappingInfo;
                }
                vk::PipelineCreateFlags2CreateInfo HeapFlags;
                ApplyDescriptorHeapPipelineFlag(
                    *Pipeline, Native.pNext, HeapFlags);
                Native.layout = Pipeline->mLayout;
                std::unique_lock<std::mutex> CacheLock(
                    mPipelineCacheMutex, std::defer_lock);
                if (mPipelineCache)
                    CacheLock.lock();
                Pipeline->mPipeline = mContext->mDevice.createComputePipeline(
                    mPipelineCache, Native).value;
                if (mPipelineCache)
                    mbPipelineCacheDirty = true;
                return { Pipeline, {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaProviderObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaVulkanCommandList::~FArdaVulkanCommandList()
        {
        }

        void FArdaVulkanProviderDevice::FlushPipelineCache() noexcept
        {
            std::lock_guard<std::mutex> Lock(mPipelineCacheMutex);
            if (!mPipelineCache || !mContext || !mContext->mDevice)
                return;
            if (mbPipelineCacheDirty)
            {
                try
                {
                    std::vector<uint8_t> Payload =
                        mContext->mDevice.getPipelineCacheData(mPipelineCache);
                    if (!pipeline_cache::WriteBlob(
                            pipeline_cache::MakePath(
                                mPipelineCacheDirectory, "native-vulkan"),
                            "native-vulkan", EArdaBackendType::Vulkan, Payload))
                    {
                        pipeline_cache::Message(mDiagnosticCallback,
                            EArdaDiagnosticSeverity::Warning,
                            "Failed to atomically save the Vulkan pipeline cache.");
                    }
                }
                catch (const vk::SystemError&)
                {
                    pipeline_cache::Message(mDiagnosticCallback,
                        EArdaDiagnosticSeverity::Warning,
                        "Failed to query Vulkan pipeline cache data.");
                }
            }
            mContext->mDevice.destroyPipelineCache(mPipelineCache);
            mPipelineCache = nullptr;
            mbPipelineCacheDirty = false;
            mPipelineCacheDirectory.clear();
            mDiagnosticCallback = nullptr;
            mCapabilities.mbPipelineCachePersistence = false;
        }

        FArdaRHIStatus FArdaVulkanCommandList::Initialize()
        {
            return CreateRecording();
        }

        FArdaRHIStatus FArdaVulkanCommandList::CreateRecording()
        {
            try
            {
                const auto Context = mDevice.GetContext();
                auto Recording = eastl::make_shared<FVulkanCommandRecording>();
                Recording->mContext = Context;
                const uint32_t QueueFamily =
                    mQueue == EArdaRHIQueueType::Compute
                        ? Context->mComputeQueueFamily
                        : mQueue == EArdaRHIQueueType::Copy
                            ? Context->mCopyQueueFamily
                            : Context->mQueueFamily;
                vk::CommandPoolCreateInfo PoolInfo(
                    vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                    QueueFamily);
                Recording->mCommandPool = Context->mDevice.createCommandPool(PoolInfo);
                vk::CommandBufferAllocateInfo Allocate(
                    Recording->mCommandPool, vk::CommandBufferLevel::ePrimary, 1);
                Recording->mCommandBuffer =
                    Context->mDevice.allocateCommandBuffers(Allocate).front();
                mCommandBuffer = Recording->mCommandBuffer;
                mRecording = eastl::move(Recording);
                return {};
            }
            catch (const vk::SystemError& Error)
            {
                return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
            }
        }

        FArdaRHIStatus FArdaVulkanCommandList::Open()
        {
            if (mbOpen) return FArdaRHIStatus::Error(
                EArdaRHIResult::InvalidArgument, "The Vulkan command list is already open.");
            try
            {
                mCommandBuffer.begin(vk::CommandBufferBeginInfo(
                    vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
                mbOpen = true;
                return {};
            }
            catch (const vk::SystemError& Error)
            {
                return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
            }
        }

        FArdaRHIStatus FArdaVulkanCommandList::Close()
        {
            if (!mbOpen) return FArdaRHIStatus::Error(
                EArdaRHIResult::InvalidArgument, "The Vulkan command list is not open.");
            try
            {
                EndRendering();
                mCommandBuffer.end();
                mbOpen = false;
                return {};
            }
            catch (const vk::SystemError& Error)
            {
                return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
            }
        }

        FArdaRHIStatus FArdaVulkanCommandList::Reset()
        {
            if (mbOpen) return FArdaRHIStatus::Error(
                EArdaRHIResult::InvalidState,
                "An open Vulkan command list must be closed before reset.");
            try
            {
                if (!mRecording || mRecording.use_count() > 1)
                {
                    if (auto Status = CreateRecording(); !Status)
                        return Status;
                }
                else
                {
                    mDevice.GetContext()->mDevice.resetCommandPool(
                        mRecording->mCommandPool);
                    mRecording->mRetainedObjects.clear();
                }
                mbOpen = false;
                mBoundGraphics = nullptr;
                mBoundCompute = nullptr;
                mBoundRayTracing = nullptr;
                mBoundShaderTable = nullptr;
                mTextureLayouts.clear();
                mTrackedTextures.clear();
                mTextureAbstractStates.clear();
                mTextureExpectedStartStates.clear();
                mTextureStageMasks.clear();
                mTextureAccessMasks.clear();
                mBufferStates.clear();
                mAccelStructStates.clear();
                mOpacityMicromapStates.clear();
                mbAutomaticBarriers = true;
                mbDescriptorHeapsBound = false;
                return Open();
            }
            catch (const vk::SystemError& Error)
            {
                return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
            }
        }

        void FArdaVulkanCommandList::EndRendering()
        {
            if (mbRendering)
            {
                mCommandBuffer.endRendering();
                mbRendering = false;
            }
        }

        FArdaRHIStatus FArdaVulkanCommandList::WriteBuffer(
            const FArdaProviderObjectRef& Object, const FArdaRHIBufferDesc& Desc,
            const void* Data, size_t Size, uint64_t Offset)
        {
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Object.get());
            if (!Buffer || !Data || Offset > Desc.mByteSize || Size > Desc.mByteSize - Offset)
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                    "The Vulkan buffer write is invalid.");
            Retain(Object);
            try
            {
                if (Desc.mCpuAccess == EArdaRHICpuAccess::Write)
                {
                    void* Destination = mDevice.GetContext()->mDevice.mapMemory(
                        Buffer->mMemory, Offset, Size);
                    std::memcpy(Destination, Data, Size);
                    mDevice.GetContext()->mDevice.unmapMemory(Buffer->mMemory);
                    return {};
                }

                FArdaRHIBufferDesc UploadDesc;
                UploadDesc.mByteSize = Size;
                UploadDesc.mCpuAccess = EArdaRHICpuAccess::Write;
                UploadDesc.mInitialState = EArdaRHIResourceState::CopySource;
                UploadDesc.mbKeepInitialState = true;
                UploadDesc.mDebugName = "Vulkan transient upload buffer";
                auto UploadResult = mDevice.CreateBuffer(UploadDesc);
                if (!UploadResult)
                    return eastl::move(UploadResult.mStatus);
                auto* Upload = dynamic_cast<FVulkanBuffer*>(
                    UploadResult.mValue.get());
                void* Source = mDevice.GetContext()->mDevice.mapMemory(
                    Upload->mMemory, 0, Size);
                std::memcpy(Source, Data, Size);
                mDevice.GetContext()->mDevice.unmapMemory(Upload->mMemory);
                const EArdaRHIResourceState PreviousState =
                    GetBufferTracking(*Buffer).mAbstractState;
                if (mbAutomaticBarriers)
                {
                    if (auto Status = SetBufferState(
                            Object, Desc, EArdaRHIResourceState::CopyDest); !Status)
                        return Status;
                }
                mCommandBuffer.copyBuffer(
                    Upload->mBuffer,
                    Buffer->mBuffer,
                    vk::BufferCopy(0, Offset, Size));
                if (mbAutomaticBarriers)
                {
                    if (auto Status = SetBufferState(
                            Object, Desc, PreviousState); !Status)
                        return Status;
                }
                Retain(UploadResult.mValue);
                return {};
            }
            catch (const vk::SystemError& Error)
            {
                return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
            }
        }

        FArdaRHIStatus FArdaVulkanCommandList::CopyBuffer(
            const FArdaProviderObjectRef& Destination, uint64_t DestinationOffset,
            const FArdaProviderObjectRef& Source, uint64_t SourceOffset, uint64_t Size)
        {
            auto* Dst = dynamic_cast<FVulkanBuffer*>(Destination.get());
            auto* Src = dynamic_cast<FVulkanBuffer*>(Source.get());
            if (!Dst || !Src) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "Vulkan buffer copy has the wrong resource type.");
            Retain(Destination);
            Retain(Source);
            EndRendering();
            mCommandBuffer.copyBuffer(Src->mBuffer, Dst->mBuffer,
                vk::BufferCopy(SourceOffset, DestinationOffset, Size));
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::CopyTexture(
            const FArdaProviderObjectRef& Destination,
            const FArdaRHITextureDesc& DestinationDesc,
            const FArdaRHITextureSlice& DestinationSlice,
            const FArdaProviderObjectRef& Source,
            const FArdaRHITextureDesc& SourceDesc,
            const FArdaRHITextureSlice& SourceSlice)
        {
            auto* Dst = dynamic_cast<FVulkanTexture*>(Destination.get());
            auto* Src = dynamic_cast<FVulkanTexture*>(Source.get());
            if (!Dst || !Src || !Dst->mImage || !Src->mImage)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan texture copy has the wrong resource type.");
            FArdaRHITextureCopyExtent CopyExtent;
            if (auto Status = ResolveArdaRHITextureCopyExtent(
                    DestinationDesc, DestinationSlice,
                    SourceDesc, SourceSlice, CopyExtent);
                !Status)
                return Status;
            const uint32_t Width = CopyExtent.mWidth;
            const uint32_t Height = CopyExtent.mHeight;
            const uint32_t Depth = CopyExtent.mDepth;
            const size_t DstIndex = TextureSubresourceIndex(
                DestinationDesc,
                DestinationSlice.mMipLevel,
                DestinationSlice.mArraySlice,
                DestinationSlice.mPlane);
            const size_t SrcIndex = TextureSubresourceIndex(
                SourceDesc,
                SourceSlice.mMipLevel,
                SourceSlice.mArraySlice,
                SourceSlice.mPlane);
            if (Dst == Src && DstIndex == SrcIndex &&
                DestinationSlice.mPlane == SourceSlice.mPlane)
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "Vulkan cannot copy a texture subresource onto itself.");
            }
            (void)GetTrackedTextureLayouts(*Dst, DestinationDesc);
            (void)GetTrackedTextureLayouts(*Src, SourceDesc);
            const EArdaRHIResourceState PreviousDst =
                mTextureAbstractStates.at(Dst->GetIdentity())[DstIndex];
            const EArdaRHIResourceState PreviousSrc =
                mTextureAbstractStates.at(Src->GetIdentity())[SrcIndex];
            FArdaRHITextureSubresourceRange DstRange{
                DestinationSlice.mMipLevel, 1,
                DestinationSlice.mArraySlice, 1,
                DestinationSlice.mPlane, 1};
            FArdaRHITextureSubresourceRange SrcRange{
                SourceSlice.mMipLevel, 1,
                SourceSlice.mArraySlice, 1,
                SourceSlice.mPlane, 1};
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTextureLayout(
                        Destination, DestinationDesc, DstRange,
                        EArdaRHIResourceState::CopyDest); !Status)
                    return Status;
                if (auto Status = TransitionTextureLayout(
                        Source, SourceDesc, SrcRange,
                        EArdaRHIResourceState::CopySource); !Status)
                    return Status;
            }
            EndRendering();
            vk::ImageCopy Region;
            Region.srcSubresource = vk::ImageSubresourceLayers(
                ImageAspect(SourceDesc.mFormat, SourceSlice.mPlane),
                SourceSlice.mMipLevel,
                SourceSlice.mArraySlice,
                1);
            Region.srcOffset = vk::Offset3D(
                SourceSlice.mX, SourceSlice.mY, SourceSlice.mZ);
            Region.dstSubresource = vk::ImageSubresourceLayers(
                ImageAspect(DestinationDesc.mFormat, DestinationSlice.mPlane),
                DestinationSlice.mMipLevel,
                DestinationSlice.mArraySlice,
                1);
            Region.dstOffset = vk::Offset3D(
                DestinationSlice.mX,
                DestinationSlice.mY,
                DestinationSlice.mZ);
            Region.extent = vk::Extent3D(Width, Height, Depth);
            mCommandBuffer.copyImage(
                Src->mImage,
                vk::ImageLayout::eTransferSrcOptimal,
                Dst->mImage,
                vk::ImageLayout::eTransferDstOptimal,
                Region);
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTextureLayout(
                        Destination, DestinationDesc, DstRange, PreviousDst); !Status)
                    return Status;
                if (auto Status = TransitionTextureLayout(
                        Source, SourceDesc, SrcRange, PreviousSrc); !Status)
                    return Status;
            }
            Retain(Destination);
            Retain(Source);
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::ResolveTexture(
            const FArdaProviderObjectRef& Destination,
            const FArdaRHITextureDesc& DestinationDesc,
            const FArdaRHITextureSlice& DestinationSlice,
            const FArdaProviderObjectRef& Source,
            const FArdaRHITextureDesc& SourceDesc,
            const FArdaRHITextureSlice& SourceSlice)
        {
            auto* Dst = dynamic_cast<FVulkanTexture*>(Destination.get());
            auto* Src = dynamic_cast<FVulkanTexture*>(Source.get());
            if (!Dst || !Src || !Dst->mImage || !Src->mImage)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan texture resolve has the wrong resource type.");
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
            const size_t DstIndex = TextureSubresourceIndex(
                DestinationDesc,
                DestinationSlice.mMipLevel,
                DestinationSlice.mArraySlice,
                0);
            const size_t SrcIndex = TextureSubresourceIndex(
                SourceDesc,
                SourceSlice.mMipLevel,
                SourceSlice.mArraySlice,
                0);
            (void)GetTrackedTextureLayouts(*Dst, DestinationDesc);
            (void)GetTrackedTextureLayouts(*Src, SourceDesc);
            const EArdaRHIResourceState PreviousDst =
                mTextureAbstractStates.at(Dst->GetIdentity())[DstIndex];
            const EArdaRHIResourceState PreviousSrc =
                mTextureAbstractStates.at(Src->GetIdentity())[SrcIndex];
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTextureLayout(
                        Destination, DestinationDesc, DstRange,
                        EArdaRHIResourceState::ResolveDest); !Status)
                    return Status;
                if (auto Status = TransitionTextureLayout(
                        Source, SourceDesc, SrcRange,
                        EArdaRHIResourceState::ResolveSource); !Status)
                    return Status;
            }
            EndRendering();
            vk::ImageResolve Region;
            Region.srcSubresource = vk::ImageSubresourceLayers(
                vk::ImageAspectFlagBits::eColor,
                SourceSlice.mMipLevel,
                SourceSlice.mArraySlice,
                1);
            Region.srcOffset = vk::Offset3D(0, 0, 0);
            Region.dstSubresource = vk::ImageSubresourceLayers(
                vk::ImageAspectFlagBits::eColor,
                DestinationSlice.mMipLevel,
                DestinationSlice.mArraySlice,
                1);
            Region.dstOffset = vk::Offset3D(0, 0, 0);
            Region.extent = vk::Extent3D(
                Extent.mWidth, Extent.mHeight, Extent.mDepth);
            mCommandBuffer.resolveImage(
                Src->mImage,
                vk::ImageLayout::eTransferSrcOptimal,
                Dst->mImage,
                vk::ImageLayout::eTransferDstOptimal,
                Region);
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTextureLayout(
                        Destination, DestinationDesc, DstRange, PreviousDst); !Status)
                    return Status;
                if (auto Status = TransitionTextureLayout(
                        Source, SourceDesc, SrcRange, PreviousSrc); !Status)
                    return Status;
            }
            Retain(Destination);
            Retain(Source);
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::CopyTextureToStaging(
            const FArdaProviderObjectRef& Destination,
            const FArdaRHIStagingTextureDesc& DestinationDesc,
            const FArdaRHITextureSlice& DestinationSlice,
            const FArdaProviderObjectRef& Source,
            const FArdaRHITextureDesc& SourceDesc,
            const FArdaRHITextureSlice& SourceSlice)
        {
            auto* Dst = dynamic_cast<FVulkanStagingTexture*>(Destination.get());
            auto* Src = dynamic_cast<FVulkanTexture*>(Source.get());
            if (!Dst || !Src || !Dst->mBuffer || !Src->mImage)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan texture readback has the wrong resource type.");
            FArdaRHITextureCopyExtent CopyExtent;
            if (auto Status = ResolveArdaRHITextureCopyExtent(
                    DestinationDesc.mTexture, DestinationSlice,
                    SourceDesc, SourceSlice, CopyExtent); !Status)
                return Status;
            const uint32_t Width = CopyExtent.mWidth;
            const uint32_t Height = CopyExtent.mHeight;
            const uint32_t Depth = CopyExtent.mDepth;
            const size_t StagingIndex =
                static_cast<size_t>(DestinationSlice.mPlane) *
                    DestinationDesc.mTexture.mMipLevels *
                    DestinationDesc.mTexture.mArraySize +
                static_cast<size_t>(DestinationSlice.mArraySlice) *
                    DestinationDesc.mTexture.mMipLevels +
                DestinationSlice.mMipLevel;
            const size_t SourceIndex = TextureSubresourceIndex(
                SourceDesc,
                SourceSlice.mMipLevel,
                SourceSlice.mArraySlice,
                SourceSlice.mPlane);
            (void)GetTrackedTextureLayouts(*Src, SourceDesc);
            const EArdaRHIResourceState Previous =
                mTextureAbstractStates.at(Src->GetIdentity())[SourceIndex];
            FArdaRHITextureSubresourceRange Range{
                SourceSlice.mMipLevel, 1,
                SourceSlice.mArraySlice, 1,
                SourceSlice.mPlane, 1};
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTextureLayout(
                        Source, SourceDesc, Range,
                        EArdaRHIResourceState::CopySource); !Status)
                    return Status;
            }
            vk::BufferImageCopy Region;
            if (auto Status = ConfigureVulkanStagingCopyRegion(
                    Region, *Dst, StagingIndex, DestinationSlice); !Status)
                return Status;
            Region.imageSubresource = vk::ImageSubresourceLayers(
                ImageAspect(SourceDesc.mFormat, SourceSlice.mPlane),
                SourceSlice.mMipLevel,
                SourceSlice.mArraySlice,
                1);
            Region.imageOffset = vk::Offset3D(
                SourceSlice.mX, SourceSlice.mY, SourceSlice.mZ);
            Region.imageExtent = vk::Extent3D(Width, Height, Depth);
            EndRendering();
            mCommandBuffer.copyImageToBuffer(
                Src->mImage,
                vk::ImageLayout::eTransferSrcOptimal,
                Dst->mBuffer,
                Region);
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTextureLayout(
                        Source, SourceDesc, Range, Previous); !Status)
                    return Status;
            }
            Retain(Destination);
            Retain(Source);
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::CopyTextureFromStaging(
            const FArdaProviderObjectRef& Destination,
            const FArdaRHITextureDesc& DestinationDesc,
            const FArdaRHITextureSlice& DestinationSlice,
            const FArdaProviderObjectRef& Source,
            const FArdaRHIStagingTextureDesc& SourceDesc,
            const FArdaRHITextureSlice& SourceSlice)
        {
            auto* Dst = dynamic_cast<FVulkanTexture*>(Destination.get());
            auto* Src = dynamic_cast<FVulkanStagingTexture*>(Source.get());
            if (!Dst || !Src || !Dst->mImage || !Src->mBuffer)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan texture upload has the wrong resource type.");
            FArdaRHITextureCopyExtent CopyExtent;
            if (auto Status = ResolveArdaRHITextureCopyExtent(
                    DestinationDesc, DestinationSlice,
                    SourceDesc.mTexture, SourceSlice, CopyExtent); !Status)
                return Status;
            const uint32_t Width = CopyExtent.mWidth;
            const uint32_t Height = CopyExtent.mHeight;
            const uint32_t Depth = CopyExtent.mDepth;
            const size_t StagingIndex =
                static_cast<size_t>(SourceSlice.mPlane) *
                    SourceDesc.mTexture.mMipLevels *
                    SourceDesc.mTexture.mArraySize +
                static_cast<size_t>(SourceSlice.mArraySlice) *
                    SourceDesc.mTexture.mMipLevels +
                SourceSlice.mMipLevel;
            const size_t DestinationIndex = TextureSubresourceIndex(
                DestinationDesc,
                DestinationSlice.mMipLevel,
                DestinationSlice.mArraySlice,
                DestinationSlice.mPlane);
            (void)GetTrackedTextureLayouts(*Dst, DestinationDesc);
            const EArdaRHIResourceState Previous =
                mTextureAbstractStates.at(Dst->GetIdentity())[DestinationIndex];
            FArdaRHITextureSubresourceRange Range{
                DestinationSlice.mMipLevel, 1,
                DestinationSlice.mArraySlice, 1,
                DestinationSlice.mPlane, 1};
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTextureLayout(
                        Destination, DestinationDesc, Range,
                        EArdaRHIResourceState::CopyDest); !Status)
                    return Status;
            }
            vk::BufferImageCopy Region;
            if (auto Status = ConfigureVulkanStagingCopyRegion(
                    Region, *Src, StagingIndex, SourceSlice); !Status)
                return Status;
            Region.imageSubresource = vk::ImageSubresourceLayers(
                ImageAspect(DestinationDesc.mFormat, DestinationSlice.mPlane),
                DestinationSlice.mMipLevel,
                DestinationSlice.mArraySlice,
                1);
            Region.imageOffset = vk::Offset3D(
                DestinationSlice.mX,
                DestinationSlice.mY,
                DestinationSlice.mZ);
            Region.imageExtent = vk::Extent3D(Width, Height, Depth);
            EndRendering();
            mCommandBuffer.copyBufferToImage(
                Src->mBuffer,
                Dst->mImage,
                vk::ImageLayout::eTransferDstOptimal,
                Region);
            if (mbAutomaticBarriers)
            {
                if (auto Status = TransitionTextureLayout(
                        Destination, DestinationDesc, Range, Previous); !Status)
                    return Status;
            }
            Retain(Destination);
            Retain(Source);
            return {};
        }

        eastl::vector<vk::ImageLayout>&
        FArdaVulkanCommandList::GetTrackedTextureLayouts(
            FVulkanTexture& Texture,
            const FArdaRHITextureDesc& Desc)
        {
            mTrackedTextures[Texture.GetIdentity()] = &Texture;
            auto Existing = mTextureLayouts.find(Texture.GetIdentity());
            if (Existing != mTextureLayouts.end())
                return Existing->second;
            eastl::vector<vk::ImageLayout> Layouts;
            eastl::vector<EArdaRHIResourceState> AbstractStates;
            eastl::vector<vk::PipelineStageFlags2> StageMasks;
            eastl::vector<vk::AccessFlags2> AccessMasks;
            uint32_t QueueFamily = 0;
            {
                std::lock_guard<std::mutex> Lock(Texture.mLayoutMutex);
                Layouts = Texture.mLayouts;
                AbstractStates = Texture.mAbstractStates;
                StageMasks = Texture.mStageMasks;
                AccessMasks = Texture.mAccessMasks;
                QueueFamily = Texture.mQueueFamily;
            }
            const size_t SubresourceCount = TextureSubresourceCount(Desc);
            if (Layouts.size() != SubresourceCount)
                Layouts.assign(SubresourceCount, vk::ImageLayout::eUndefined);
            if (AbstractStates.size() != SubresourceCount)
                AbstractStates.assign(SubresourceCount, Desc.mInitialState);
            if (StageMasks.size() != SubresourceCount)
                StageMasks.assign(
                    SubresourceCount,
                    vk::PipelineStageFlagBits2::eTopOfPipe);
            if (AccessMasks.size() != SubresourceCount)
                AccessMasks.assign(SubresourceCount, {});
            mTextureAbstractStates.emplace(
                Texture.GetIdentity(), eastl::move(AbstractStates));
            mTextureExpectedStartStates.emplace(
                Texture.GetIdentity(),
                eastl::vector<EArdaRHIResourceState>(
                    SubresourceCount,
                    EArdaRHIResourceState::Unknown));
            mTextureStageMasks.emplace(
                Texture.GetIdentity(), eastl::move(StageMasks));
            mTextureAccessMasks.emplace(
                Texture.GetIdentity(), eastl::move(AccessMasks));
            mTextureQueueFamilies.emplace(Texture.GetIdentity(), QueueFamily);
            return mTextureLayouts.emplace(
                Texture.GetIdentity(), eastl::move(Layouts)).first->second;
        }

        FArdaVulkanCommandList::FBufferTracking&
            FArdaVulkanCommandList::GetBufferTracking(FVulkanBuffer& Buffer)
        {
            auto Existing = mBufferStates.find(&Buffer);
            if (Existing != mBufferStates.end())
                return Existing->second;
            FBufferTracking Tracking;
            {
                std::lock_guard<std::mutex> Lock(Buffer.mStateMutex);
                Tracking.mAbstractState = Buffer.mAbstractState;
                Tracking.mStageMask = Buffer.mStageMask;
                Tracking.mAccessMask = Buffer.mAccessMask;
                Tracking.mbKnown = Buffer.mbStateKnown;
                Tracking.mQueueFamily = Buffer.mQueueFamily;
            }
            return mBufferStates.emplace(&Buffer, Tracking).first->second;
        }

        FArdaRHIStatus
            FArdaVulkanCommandList::ValidateTrackedStartStates() const
        {
            for (const auto& Entry : mTrackedTextures)
            {
                FVulkanTexture* Texture = Entry.second;
                const auto& Expected =
                    mTextureExpectedStartStates.at(Entry.first);
                std::lock_guard<std::mutex> Lock(Texture->mLayoutMutex);
                for (size_t Index = 0; Index < Expected.size(); ++Index)
                {
                    if (Expected[Index] != EArdaRHIResourceState::Unknown &&
                        Expected[Index] != Texture->mAbstractStates[Index])
                    {
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidState,
                            "Vulkan texture start state differs at submission.");
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
                        "Vulkan buffer start state differs at submission.");
                }
            }
            return {};
        }

        void FArdaVulkanCommandList::CommitTrackedStates()
        {
            for (const auto& Entry : mTrackedTextures)
            {
                FVulkanTexture* Texture = Entry.second;
                std::lock_guard<std::mutex> Lock(Texture->mLayoutMutex);
                Texture->mLayouts = mTextureLayouts.at(Entry.first);
                Texture->mAbstractStates =
                    mTextureAbstractStates.at(Entry.first);
                Texture->mStageMasks = mTextureStageMasks.at(Entry.first);
                Texture->mAccessMasks = mTextureAccessMasks.at(Entry.first);
                Texture->mQueueFamily =
                    mTextureQueueFamilies.at(Entry.first);
            }
            for (const auto& Entry : mBufferStates)
            {
                std::lock_guard<std::mutex> Lock(Entry.first->mStateMutex);
                Entry.first->mAbstractState = Entry.second.mAbstractState;
                Entry.first->mStageMask = Entry.second.mStageMask;
                Entry.first->mAccessMask = Entry.second.mAccessMask;
                Entry.first->mbStateKnown = Entry.second.mbKnown;
                Entry.first->mQueueFamily = Entry.second.mQueueFamily;
            }
            for (const auto& Entry : mAccelStructStates)
            {
                std::lock_guard<std::mutex> Lock(Entry.first->mStateMutex);
                Entry.first->mAbstractState = Entry.second.mState;
                Entry.first->mBuildState = Entry.second.mBuildState;
            }
            for (const auto& Entry : mOpacityMicromapStates)
            {
                std::lock_guard<std::mutex> Lock(Entry.first->mStateMutex);
                Entry.first->mAbstractState = Entry.second.mState;
                Entry.first->mBuildState = Entry.second.mBuildState;
            }
        }

        FArdaRHIStatus FArdaVulkanCommandList::TransitionTextureLayout(
            const FArdaProviderObjectRef& Object, const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& InputRange,
            EArdaRHIResourceState State)
        {
            auto* Texture = dynamic_cast<FVulkanTexture*>(Object.get());
            if (!Texture) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "Vulkan texture transition has the wrong resource type.");
            Retain(Object);
            EndRendering();
            const auto Range = InputRange.Resolve(Desc);
            auto& Layouts = GetTrackedTextureLayouts(*Texture, Desc);
            auto& AbstractStates =
                mTextureAbstractStates.at(Texture->GetIdentity());
            auto& StageMasks =
                mTextureStageMasks.at(Texture->GetIdentity());
            auto& AccessMasks =
                mTextureAccessMasks.at(Texture->GetIdentity());
            const vk::ImageLayout NewLayout = ToImageLayout(
                State,
                ImageAspect(Desc.mFormat) !=
                    vk::ImageAspectFlagBits::eColor);
            const FVulkanSyncState NewSync = ToVulkanSyncState(State);
            eastl::vector<vk::ImageMemoryBarrier2> Barriers;
            Barriers.reserve(
                static_cast<size_t>(Range.mMipLevelCount) *
                Range.mArraySliceCount * Range.mPlaneCount);
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
                        const size_t Index = TextureSubresourceIndex(
                            Desc, MipLevel, ArraySlice, Plane);
                        const vk::ImageLayout OldLayout = Layouts[Index];
                        if (OldLayout != NewLayout ||
                            StageMasks[Index] != NewSync.mStages ||
                            AccessMasks[Index] != NewSync.mAccess)
                        {
                            vk::ImageMemoryBarrier2 Barrier;
                            Barrier.srcStageMask = StageMasks[Index];
                            Barrier.srcAccessMask = AccessMasks[Index];
                            Barrier.dstStageMask = NewSync.mStages;
                            Barrier.dstAccessMask = NewSync.mAccess;
                            Barrier.oldLayout = OldLayout;
                            Barrier.newLayout = NewLayout;
                            Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                            Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                            Barrier.image = Texture->mImage;
                            Barrier.subresourceRange = vk::ImageSubresourceRange(
                                ImageAspect(Desc.mFormat, Plane),
                                MipLevel, 1, ArraySlice, 1);
                            Barriers.push_back(Barrier);
                        }
                        Layouts[Index] = NewLayout;
                        AbstractStates[Index] = State;
                        StageMasks[Index] = NewSync.mStages;
                        AccessMasks[Index] = NewSync.mAccess;
                    }
                }
            }
            if (!Barriers.empty())
            {
                vk::DependencyInfo Dependency;
                Dependency.imageMemoryBarrierCount =
                    static_cast<uint32_t>(Barriers.size());
                Dependency.pImageMemoryBarriers = Barriers.data();
                mCommandBuffer.pipelineBarrier2(Dependency);
            }
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::SetTextureState(
            const FArdaProviderObjectRef& Object, const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& InputRange,
            EArdaRHIResourceState State)
        {
            return TransitionTextureLayout(Object, Desc, InputRange, State);
        }

        FArdaRHIStatus FArdaVulkanCommandList::SetBufferState(
            const FArdaProviderObjectRef& Object, const FArdaRHIBufferDesc& Desc,
            EArdaRHIResourceState State)
        {
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Object.get());
            if (!Buffer) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "Vulkan buffer transition has the wrong resource type.");
            Retain(Object);
            EndRendering();
            const FVulkanSyncState NewSync = ToVulkanSyncState(State);
            FBufferTracking& Tracking = GetBufferTracking(*Buffer);
            vk::BufferMemoryBarrier2 Barrier;
            Barrier.srcStageMask = Tracking.mStageMask;
            Barrier.srcAccessMask = Tracking.mAccessMask;
            Barrier.dstStageMask = NewSync.mStages;
            Barrier.dstAccessMask = NewSync.mAccess;
            Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            Barrier.buffer = Buffer->mBuffer;
            Barrier.offset = 0;
            Barrier.size = Desc.mByteSize;
            vk::DependencyInfo Dependency;
            Dependency.bufferMemoryBarrierCount = 1;
            Dependency.pBufferMemoryBarriers = &Barrier;
            mCommandBuffer.pipelineBarrier2(Dependency);
            Tracking.mAbstractState = State;
            Tracking.mStageMask = NewSync.mStages;
            Tracking.mAccessMask = NewSync.mAccess;
            Tracking.mbKnown = true;
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::TransitionTexture(
            const FArdaProviderObjectRef& Object,
            const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureTransitionDesc& Transition)
        {
            auto* Texture = dynamic_cast<FVulkanTexture*>(Object.get());
            if (!Texture)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan explicit texture transition has the wrong resource type.");
            const bool bBegin = HasAnyFlags(
                Transition.mFlags, EArdaRHITransitionFlags::BeginOnly);
            const bool bEnd = HasAnyFlags(
                Transition.mFlags, EArdaRHITransitionFlags::EndOnly);
            if (bBegin && bEnd)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "A Vulkan transition cannot be both begin-only and end-only.");
            if (Transition.mbQueueOwnershipTransfer)
            {
                const auto Context = mDevice.GetContext();
                const uint32_t SourceFamily = Context->GetQueueFamily(
                    Transition.mSourceQueue);
                const uint32_t DestinationFamily = Context->GetQueueFamily(
                    Transition.mDestinationQueue);
                if (SourceFamily != DestinationFamily)
                {
                    if (!bBegin && !bEnd)
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument,
                            "A Vulkan cross-family ownership transfer requires paired begin-only and end-only barriers.");
                    const EArdaRHIQueueType RequiredQueue = bBegin
                        ? Transition.mSourceQueue
                        : Transition.mDestinationQueue;
                    if (mQueue != RequiredQueue)
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument,
                            "The Vulkan ownership barrier was recorded on the wrong queue.");

                    Retain(Object);
                    EndRendering();
                    const auto Range = Transition.mSubresources.Resolve(Desc);
                    auto& Layouts = GetTrackedTextureLayouts(*Texture, Desc);
                    auto& Stages = mTextureStageMasks.at(Texture->GetIdentity());
                    auto& Access = mTextureAccessMasks.at(Texture->GetIdentity());
                    eastl::vector<vk::ImageMemoryBarrier2> Barriers;
                    Barriers.reserve(
                        static_cast<size_t>(Range.mMipLevelCount) *
                        Range.mArraySliceCount * Range.mPlaneCount);
                    for (uint32_t Plane = Range.mBasePlane;
                         Plane < Range.mBasePlane + Range.mPlaneCount; ++Plane)
                    {
                        for (uint32_t ArraySlice = Range.mBaseArraySlice;
                             ArraySlice < Range.mBaseArraySlice + Range.mArraySliceCount;
                             ++ArraySlice)
                        {
                            for (uint32_t MipLevel = Range.mBaseMipLevel;
                                 MipLevel < Range.mBaseMipLevel + Range.mMipLevelCount;
                                 ++MipLevel)
                            {
                                const size_t Index = TextureSubresourceIndex(
                                    Desc, MipLevel, ArraySlice, Plane);
                                vk::ImageMemoryBarrier2 Barrier;
                                Barrier.srcStageMask = bBegin
                                    ? Stages[Index]
                                    : vk::PipelineStageFlags2{};
                                Barrier.srcAccessMask = bBegin
                                    ? Access[Index]
                                    : vk::AccessFlags2{};
                                Barrier.dstStageMask = bEnd
                                    ? Stages[Index]
                                    : vk::PipelineStageFlags2{};
                                Barrier.dstAccessMask = bEnd
                                    ? Access[Index]
                                    : vk::AccessFlags2{};
                                Barrier.oldLayout = Layouts[Index];
                                Barrier.newLayout = Layouts[Index];
                                Barrier.srcQueueFamilyIndex = SourceFamily;
                                Barrier.dstQueueFamilyIndex = DestinationFamily;
                                Barrier.image = Texture->mImage;
                                Barrier.subresourceRange = vk::ImageSubresourceRange(
                                    ImageAspect(Desc.mFormat, Plane),
                                    MipLevel, 1, ArraySlice, 1);
                                Barriers.push_back(Barrier);
                            }
                        }
                    }
                    vk::DependencyInfo Dependency;
                    Dependency.imageMemoryBarrierCount =
                        static_cast<uint32_t>(Barriers.size());
                    Dependency.pImageMemoryBarriers = Barriers.data();
                    mCommandBuffer.pipelineBarrier2(Dependency);
                    mTextureQueueFamilies[Texture->GetIdentity()] =
                        DestinationFamily;
                    if (bBegin)
                        return {};
                    return TransitionTextureLayout(
                        Object, Desc, Transition.mSubresources,
                        Transition.mStateAfter);
                }
            }
            if (bBegin)
            {
                GlobalBarrier();
                Retain(Object);
                return {};
            }
            if (HasAnyFlags(
                    Transition.mFlags,
                    EArdaRHITransitionFlags::Discard))
            {
                const auto Range = Transition.mSubresources.Resolve(Desc);
                auto& Layouts = GetTrackedTextureLayouts(*Texture, Desc);
                auto& Stages = mTextureStageMasks.at(Texture->GetIdentity());
                auto& Access = mTextureAccessMasks.at(Texture->GetIdentity());
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
                            const size_t Index = TextureSubresourceIndex(
                                Desc, MipLevel, ArraySlice, Plane);
                            Layouts[Index] = vk::ImageLayout::eUndefined;
                            Stages[Index] = vk::PipelineStageFlagBits2::eTopOfPipe;
                            Access[Index] = {};
                        }
                    }
                }
            }
            return TransitionTextureLayout(
                Object,
                Desc,
                Transition.mSubresources,
                Transition.mStateAfter);
        }

        FArdaRHIStatus FArdaVulkanCommandList::TransitionBuffer(
            const FArdaProviderObjectRef& Object,
            const FArdaRHIBufferDesc& Desc,
            const FArdaRHIBufferTransitionDesc& Transition)
        {
            const bool bBegin = HasAnyFlags(
                Transition.mFlags, EArdaRHITransitionFlags::BeginOnly);
            const bool bEnd = HasAnyFlags(
                Transition.mFlags, EArdaRHITransitionFlags::EndOnly);
            if (bBegin && bEnd)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "A Vulkan transition cannot be both begin-only and end-only.");
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Object.get());
            if (!Buffer)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan explicit buffer transition has the wrong resource type.");
            if (Transition.mbQueueOwnershipTransfer)
            {
                const auto Context = mDevice.GetContext();
                const uint32_t SourceFamily = Context->GetQueueFamily(
                    Transition.mSourceQueue);
                const uint32_t DestinationFamily = Context->GetQueueFamily(
                    Transition.mDestinationQueue);
                if (SourceFamily != DestinationFamily)
                {
                    if (!bBegin && !bEnd)
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument,
                            "A Vulkan cross-family ownership transfer requires paired begin-only and end-only barriers.");
                    const EArdaRHIQueueType RequiredQueue = bBegin
                        ? Transition.mSourceQueue
                        : Transition.mDestinationQueue;
                    if (mQueue != RequiredQueue)
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument,
                            "The Vulkan ownership barrier was recorded on the wrong queue.");
                    Retain(Object);
                    EndRendering();
                    FBufferTracking& Tracking = GetBufferTracking(*Buffer);
                    vk::BufferMemoryBarrier2 Barrier;
                    Barrier.srcStageMask = bBegin
                        ? Tracking.mStageMask
                        : vk::PipelineStageFlags2{};
                    Barrier.srcAccessMask = bBegin
                        ? Tracking.mAccessMask
                        : vk::AccessFlags2{};
                    Barrier.dstStageMask = bEnd
                        ? Tracking.mStageMask
                        : vk::PipelineStageFlags2{};
                    Barrier.dstAccessMask = bEnd
                        ? Tracking.mAccessMask
                        : vk::AccessFlags2{};
                    Barrier.srcQueueFamilyIndex = SourceFamily;
                    Barrier.dstQueueFamilyIndex = DestinationFamily;
                    Barrier.buffer = Buffer->mBuffer;
                    Barrier.offset = 0;
                    Barrier.size = Desc.mByteSize;
                    vk::DependencyInfo Dependency;
                    Dependency.bufferMemoryBarrierCount = 1;
                    Dependency.pBufferMemoryBarriers = &Barrier;
                    mCommandBuffer.pipelineBarrier2(Dependency);
                    Tracking.mQueueFamily = DestinationFamily;
                    if (bBegin)
                        return {};
                    return SetBufferState(Object, Desc, Transition.mStateAfter);
                }
            }
            if (bBegin)
            {
                GlobalBarrier();
                Retain(Object);
                return {};
            }
            return SetBufferState(Object, Desc, Transition.mStateAfter);
        }

        FArdaRHIStatus FArdaVulkanCommandList::BeginTrackingTextureState(
            const FArdaProviderObjectRef& Object, const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& InputRange,
            EArdaRHIResourceState State)
        {
            auto* Texture = dynamic_cast<FVulkanTexture*>(Object.get());
            if (!Texture) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "Vulkan texture tracking has the wrong resource type.");
            const auto Range = InputRange.Resolve(Desc);
            auto& Layouts = GetTrackedTextureLayouts(*Texture, Desc);
            auto& AbstractStates =
                mTextureAbstractStates.at(Texture->GetIdentity());
            auto& ExpectedStartStates =
                mTextureExpectedStartStates.at(Texture->GetIdentity());
            auto& StageMasks =
                mTextureStageMasks.at(Texture->GetIdentity());
            auto& AccessMasks =
                mTextureAccessMasks.at(Texture->GetIdentity());
            const vk::ImageLayout Layout = ToImageLayout(
                State,
                ImageAspect(Desc.mFormat) !=
                    vk::ImageAspectFlagBits::eColor);
            const FVulkanSyncState Sync = ToVulkanSyncState(State);
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
                        const size_t Index = TextureSubresourceIndex(
                            Desc, MipLevel, ArraySlice, Plane);
                        ExpectedStartStates[Index] = State;
                        AbstractStates[Index] = State;
                        if (Layouts[Index] != vk::ImageLayout::eUndefined)
                        {
                            Layouts[Index] = Layout;
                            StageMasks[Index] = Sync.mStages;
                            AccessMasks[Index] = Sync.mAccess;
                        }
                    }
                }
            }
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::BeginTrackingBufferState(
            const FArdaProviderObjectRef& Object,
            const FArdaRHIBufferDesc&,
            EArdaRHIResourceState State)
        {
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Object.get());
            if (!Buffer)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan buffer tracking has the wrong resource type.");
            FBufferTracking& Tracking = GetBufferTracking(*Buffer);
            const FVulkanSyncState Sync = ToVulkanSyncState(State);
            Tracking.mExpectedStartState = State;
            Tracking.mbExpectedStartState = true;
            Tracking.mAbstractState = State;
            Tracking.mStageMask = Sync.mStages;
            Tracking.mAccessMask = Sync.mAccess;
            Tracking.mbKnown = true;
            return {};
        }

        TArdaRHIResult<FArdaRHINativeResourceState>
            FArdaVulkanCommandList::QueryTextureState(
                const FArdaProviderObjectRef& Object,
                const FArdaRHITextureDesc& Desc,
                const FArdaRHITextureSubresourceRange& InputRange) const
        {
            auto* Texture = dynamic_cast<FVulkanTexture*>(Object.get());
            if (!Texture)
                return Fail<FArdaRHINativeResourceState>(
                    FArdaRHIStatus::Error(
                        EArdaRHIResult::WrongDevice,
                        "Vulkan texture query has the wrong resource type."));
            const auto Range = InputRange.Resolve(Desc);
            std::unique_lock<std::mutex> ResourceLock;
            const eastl::vector<vk::ImageLayout>* Layouts = nullptr;
            const eastl::vector<EArdaRHIResourceState>* AbstractStates = nullptr;
            const eastl::vector<vk::PipelineStageFlags2>* StageMasks = nullptr;
            const eastl::vector<vk::AccessFlags2>* AccessMasks = nullptr;
            uint32_t QueueFamily = 0;
            const auto Local = mTextureLayouts.find(Texture->GetIdentity());
            if (Local != mTextureLayouts.end())
            {
                Layouts = &Local->second;
                AbstractStates = &mTextureAbstractStates.at(
                    Texture->GetIdentity());
                StageMasks = &mTextureStageMasks.at(Texture->GetIdentity());
                AccessMasks = &mTextureAccessMasks.at(Texture->GetIdentity());
                QueueFamily = mTextureQueueFamilies.at(Texture->GetIdentity());
            }
            else
            {
                ResourceLock = std::unique_lock<std::mutex>(
                    Texture->mLayoutMutex);
                Layouts = &Texture->mLayouts;
                AbstractStates = &Texture->mAbstractStates;
                StageMasks = &Texture->mStageMasks;
                AccessMasks = &Texture->mAccessMasks;
                QueueFamily = Texture->mQueueFamily;
            }
            const size_t First = TextureSubresourceIndex(
                Desc,
                Range.mBaseMipLevel,
                Range.mBaseArraySlice,
                Range.mBasePlane);
            FArdaRHINativeResourceState Snapshot;
            Snapshot.mState = (*AbstractStates)[First];
            Snapshot.mNativeType = EArdaRHINativeResourceType::VulkanImage;
            Snapshot.mPrimaryState = static_cast<uint64_t>(
                static_cast<VkImageLayout>((*Layouts)[First]));
            Snapshot.mPipelineStageMask =
                VulkanStageMask((*StageMasks)[First]);
            Snapshot.mAccessMask =
                VulkanAccessMask((*AccessMasks)[First]);
            Snapshot.mQueueFamily = QueueFamily;
            Snapshot.mbKnown = true;
            const FVulkanSyncState ExpectedSync =
                ToVulkanSyncState(Snapshot.mState);
            const vk::ImageLayout ExpectedLayout = ToImageLayout(
                Snapshot.mState,
                ImageAspect(Desc.mFormat) !=
                    vk::ImageAspectFlagBits::eColor);
            const bool bUninitialized =
                (*Layouts)[First] == vk::ImageLayout::eUndefined &&
                (*StageMasks)[First] ==
                    vk::PipelineStageFlagBits2::eTopOfPipe &&
                !(*AccessMasks)[First];
            Snapshot.mbNativeCompatible = bUninitialized ||
                ((*Layouts)[First] == ExpectedLayout &&
                 (*StageMasks)[First] == ExpectedSync.mStages &&
                 (*AccessMasks)[First] == ExpectedSync.mAccess);
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
                        const size_t Index = TextureSubresourceIndex(
                            Desc, MipLevel, ArraySlice, Plane);
                        if ((*AbstractStates)[Index] != Snapshot.mState ||
                            (*Layouts)[Index] != (*Layouts)[First] ||
                            (*StageMasks)[Index] != (*StageMasks)[First] ||
                            (*AccessMasks)[Index] != (*AccessMasks)[First])
                        {
                            return Fail<FArdaRHINativeResourceState>(
                                FArdaRHIStatus::Error(
                                    EArdaRHIResult::InvalidState,
                                    "Vulkan texture range contains mixed backend states."));
                        }
                    }
                }
            }
            return { Snapshot, {} };
        }

        TArdaRHIResult<FArdaRHINativeResourceState>
            FArdaVulkanCommandList::QueryBufferState(
                const FArdaProviderObjectRef& Object,
                const FArdaRHIBufferDesc&) const
        {
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Object.get());
            if (!Buffer)
                return Fail<FArdaRHINativeResourceState>(
                    FArdaRHIStatus::Error(
                        EArdaRHIResult::WrongDevice,
                        "Vulkan buffer query has the wrong resource type."));
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
                Tracking.mStageMask = Buffer->mStageMask;
                Tracking.mAccessMask = Buffer->mAccessMask;
                Tracking.mbKnown = Buffer->mbStateKnown;
                Tracking.mQueueFamily = Buffer->mQueueFamily;
            }
            FArdaRHINativeResourceState Snapshot;
            Snapshot.mState = Tracking.mAbstractState;
            Snapshot.mNativeType = EArdaRHINativeResourceType::VulkanBuffer;
            Snapshot.mPipelineStageMask = VulkanStageMask(Tracking.mStageMask);
            Snapshot.mAccessMask = VulkanAccessMask(Tracking.mAccessMask);
            Snapshot.mQueueFamily = Tracking.mQueueFamily;
            Snapshot.mbKnown = Tracking.mbKnown;
            const FVulkanSyncState Expected =
                ToVulkanSyncState(Tracking.mAbstractState);
            Snapshot.mbNativeCompatible = Tracking.mbKnown &&
                Tracking.mStageMask == Expected.mStages &&
                Tracking.mAccessMask == Expected.mAccess;
            return { Snapshot, {} };
        }

        FArdaRHIStatus FArdaVulkanCommandList::SetAccelStructState(
            const FArdaProviderObjectRef& Object,
            EArdaRHIResourceState State)
        {
            auto* AccelStruct = dynamic_cast<FVulkanAccelStruct*>(Object.get());
            if (!AccelStruct || !AccelStruct->mAccelStruct)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "The Vulkan acceleration structure is invalid.");
            EndRendering();
            vk::MemoryBarrier2 Barrier;
            Barrier.srcStageMask =
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
                vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
            Barrier.srcAccessMask =
                vk::AccessFlagBits2::eAccelerationStructureWriteKHR |
                vk::AccessFlagBits2::eAccelerationStructureReadKHR;
            Barrier.dstStageMask = Barrier.srcStageMask;
            Barrier.dstAccessMask =
                HasAnyFlags(State, EArdaRHIResourceState::AccelStructWrite)
                    ? vk::AccessFlagBits2::eAccelerationStructureWriteKHR
                    : vk::AccessFlagBits2::eAccelerationStructureReadKHR;
            vk::DependencyInfo Dependency;
            Dependency.memoryBarrierCount = 1;
            Dependency.pMemoryBarriers = &Barrier;
            mCommandBuffer.pipelineBarrier2(Dependency);
            FAccelStructTracking Tracking;
            const auto Existing = mAccelStructStates.find(AccelStruct);
            if (Existing != mAccelStructStates.end())
                Tracking = Existing->second;
            else
            {
                std::lock_guard<std::mutex> Lock(AccelStruct->mStateMutex);
                Tracking.mState = AccelStruct->mAbstractState;
                Tracking.mBuildState = AccelStruct->mBuildState;
            }
            Tracking.mState = State;
            mAccelStructStates[AccelStruct] = Tracking;
            Retain(Object);
            return {};
        }

        TArdaRHIResult<FArdaRHINativeResourceState>
        FArdaVulkanCommandList::QueryAccelStructState(
            const FArdaProviderObjectRef& Object) const
        {
            auto* AccelStruct = dynamic_cast<FVulkanAccelStruct*>(Object.get());
            if (!AccelStruct || !AccelStruct->mAccelStruct)
                return Fail<FArdaRHINativeResourceState>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "The Vulkan acceleration-structure query is invalid."));
            FAccelStructTracking Tracking;
            const auto Existing = mAccelStructStates.find(AccelStruct);
            if (Existing != mAccelStructStates.end())
                Tracking = Existing->second;
            else
            {
                std::lock_guard<std::mutex> Lock(AccelStruct->mStateMutex);
                Tracking.mState = AccelStruct->mAbstractState;
                Tracking.mBuildState = AccelStruct->mBuildState;
            }
            FArdaRHINativeResourceState Snapshot;
            Snapshot.mState = Tracking.mState;
            Snapshot.mNativeType =
                EArdaRHINativeResourceType::VulkanAccelerationStructure;
            Snapshot.mPipelineStageMask = VulkanStageMask(
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
                vk::PipelineStageFlagBits2::eRayTracingShaderKHR);
            Snapshot.mAccessMask = VulkanAccessMask(
                HasAnyFlags(Tracking.mState,
                    EArdaRHIResourceState::AccelStructWrite)
                    ? vk::AccessFlagBits2::eAccelerationStructureWriteKHR
                    : vk::AccessFlagBits2::eAccelerationStructureReadKHR);
            Snapshot.mbKnown = true;
            Snapshot.mbNativeCompatible =
                HasAnyFlags(Tracking.mState,
                    EArdaRHIResourceState::AccelStructRead) ||
                HasAnyFlags(Tracking.mState,
                    EArdaRHIResourceState::AccelStructWrite);
            return {Snapshot, {}};
        }

        TArdaRHIResult<FArdaRHINativeResourceState>
        FArdaVulkanCommandList::QueryOpacityMicromapState(
            const FArdaProviderObjectRef& Object) const
        {
            auto* Micromap = dynamic_cast<FVulkanOpacityMicromap*>(
                Object.get());
            if (!Micromap || !Micromap->mMicromap)
                return Fail<FArdaRHINativeResourceState>(
                    FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                        "The Vulkan opacity-micromap query is invalid."));
            FAccelStructTracking Tracking;
            const auto Existing = mOpacityMicromapStates.find(Micromap);
            if (Existing != mOpacityMicromapStates.end())
                Tracking = Existing->second;
            else
            {
                std::lock_guard<std::mutex> Lock(Micromap->mStateMutex);
                Tracking.mState = Micromap->mAbstractState;
                Tracking.mBuildState = Micromap->mBuildState;
            }
            const FVulkanSyncState Sync = ToVulkanSyncState(Tracking.mState);
            FArdaRHINativeResourceState Snapshot;
            Snapshot.mState = Tracking.mState;
            Snapshot.mNativeType =
                EArdaRHINativeResourceType::VulkanOpacityMicromap;
            Snapshot.mPipelineStageMask = VulkanStageMask(Sync.mStages);
            Snapshot.mAccessMask = VulkanAccessMask(Sync.mAccess);
            Snapshot.mbKnown = true;
            Snapshot.mbNativeCompatible =
                HasAnyFlags(Tracking.mState,
                    EArdaRHIResourceState::OpacityMicromapWrite) ||
                HasAnyFlags(Tracking.mState,
                    EArdaRHIResourceState::OpacityMicromapBuildInput);
            return {Snapshot, {}};
        }

        void FArdaVulkanCommandList::GlobalBarrier()
        {
            EndRendering();
            vk::MemoryBarrier2 Barrier;
            Barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            Barrier.srcAccessMask = vk::AccessFlagBits2::eShaderWrite;
            Barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            Barrier.dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
            vk::DependencyInfo Dependency;
            Dependency.memoryBarrierCount = 1;
            Dependency.pMemoryBarriers = &Barrier;
            mCommandBuffer.pipelineBarrier2(Dependency);
        }

        FArdaRHIStatus FArdaVulkanCommandList::SetUAVBarriersForTexture(
            const FArdaProviderObjectRef&, bool Enabled)
        {
            if (Enabled) GlobalBarrier();
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::SetUAVBarriersForBuffer(
            const FArdaProviderObjectRef&, bool Enabled)
        {
            if (Enabled) GlobalBarrier();
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::AliasingBarrier(
            const FArdaProviderObjectRef& ResourceBefore,
            const FArdaProviderObjectRef& ResourceAfter)
        {
            if (!ResourceBefore && !ResourceAfter)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "A Vulkan aliasing barrier requires at least one resource.");
            GlobalBarrier();
            Retain(ResourceBefore);
            Retain(ResourceAfter);
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::ClearTexture(
            const FArdaProviderObjectRef& Object, const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& InputRange, const FArdaRHIColor& Color)
        {
            auto* Texture = dynamic_cast<FVulkanTexture*>(Object.get());
            if (!Texture) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "Vulkan texture clear has the wrong resource type.");
            const auto Range = InputRange.Resolve(Desc);
            (void)GetTrackedTextureLayouts(*Texture, Desc);
            auto& AbstractStates =
                mTextureAbstractStates.at(Texture->GetIdentity());
            eastl::vector<EArdaRHIResourceState> PreviousStates;
            PreviousStates.reserve(
                static_cast<size_t>(Range.mMipLevelCount) *
                Range.mArraySliceCount * Range.mPlaneCount);
            for (uint32_t Plane = Range.mBasePlane;
                 Plane < Range.mBasePlane + Range.mPlaneCount;
                 ++Plane)
                for (uint32_t ArraySlice = Range.mBaseArraySlice;
                     ArraySlice < Range.mBaseArraySlice + Range.mArraySliceCount;
                     ++ArraySlice)
                    for (uint32_t MipLevel = Range.mBaseMipLevel;
                         MipLevel < Range.mBaseMipLevel + Range.mMipLevelCount;
                         ++MipLevel)
                        PreviousStates.push_back(AbstractStates[
                            TextureSubresourceIndex(
                                Desc, MipLevel, ArraySlice, Plane)]);
            if (auto Status = TransitionTextureLayout(
                    Object,
                    Desc,
                    InputRange,
                    EArdaRHIResourceState::CopyDest);
                !Status)
                return Status;
            const vk::ImageSubresourceRange NativeRange(
                vk::ImageAspectFlagBits::eColor, Range.mBaseMipLevel, Range.mMipLevelCount,
                Range.mBaseArraySlice, Range.mArraySliceCount);
            const vk::ClearColorValue Value(std::array<float, 4>{
                Color.mR, Color.mG, Color.mB, Color.mA });
            mCommandBuffer.clearColorImage(Texture->mImage,
                vk::ImageLayout::eTransferDstOptimal, Value, NativeRange);
            size_t PreviousIndex = 0;
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
                         ++MipLevel, ++PreviousIndex)
                    {
                        const EArdaRHIResourceState Previous =
                            PreviousStates[PreviousIndex];
                        if (Previous == EArdaRHIResourceState::CopyDest)
                            continue;
                        if (auto Status = TransitionTextureLayout(
                                Object,
                                Desc,
                                { MipLevel, 1, ArraySlice, 1, Plane, 1 },
                                Previous);
                            !Status)
                            return Status;
                    }
                }
            }
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::ClearDepthStencilTexture(
            const FArdaProviderObjectRef& Object, const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& InputRange, bool bClearDepth,
            float Depth, bool bClearStencil, uint8_t Stencil)
        {
            auto* Texture = dynamic_cast<FVulkanTexture*>(Object.get());
            if (!Texture) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "Vulkan depth clear has the wrong resource type.");
            const auto Range = InputRange.Resolve(Desc);
            (void)GetTrackedTextureLayouts(*Texture, Desc);
            auto& AbstractStates =
                mTextureAbstractStates.at(Texture->GetIdentity());
            eastl::vector<EArdaRHIResourceState> PreviousStates;
            PreviousStates.reserve(
                static_cast<size_t>(Range.mMipLevelCount) *
                Range.mArraySliceCount * Range.mPlaneCount);
            for (uint32_t Plane = Range.mBasePlane;
                 Plane < Range.mBasePlane + Range.mPlaneCount;
                 ++Plane)
                for (uint32_t ArraySlice = Range.mBaseArraySlice;
                     ArraySlice < Range.mBaseArraySlice + Range.mArraySliceCount;
                     ++ArraySlice)
                    for (uint32_t MipLevel = Range.mBaseMipLevel;
                         MipLevel < Range.mBaseMipLevel + Range.mMipLevelCount;
                         ++MipLevel)
                        PreviousStates.push_back(AbstractStates[
                            TextureSubresourceIndex(
                                Desc, MipLevel, ArraySlice, Plane)]);
            if (auto Status = TransitionTextureLayout(
                    Object,
                    Desc,
                    InputRange,
                    EArdaRHIResourceState::CopyDest);
                !Status)
                return Status;
            vk::ImageAspectFlags Aspect{};
            if (bClearDepth) Aspect |= vk::ImageAspectFlagBits::eDepth;
            if (bClearStencil) Aspect |= vk::ImageAspectFlagBits::eStencil;
            const vk::ImageSubresourceRange NativeRange(
                Aspect, Range.mBaseMipLevel, Range.mMipLevelCount,
                Range.mBaseArraySlice, Range.mArraySliceCount);
            mCommandBuffer.clearDepthStencilImage(Texture->mImage,
                vk::ImageLayout::eTransferDstOptimal,
                vk::ClearDepthStencilValue(Depth, Stencil), NativeRange);
            size_t PreviousIndex = 0;
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
                         ++MipLevel, ++PreviousIndex)
                    {
                        const EArdaRHIResourceState Previous =
                            PreviousStates[PreviousIndex];
                        if (Previous == EArdaRHIResourceState::CopyDest)
                            continue;
                        if (auto Status = TransitionTextureLayout(
                                Object,
                                Desc,
                                { MipLevel, 1, ArraySlice, 1, Plane, 1 },
                                Previous);
                            !Status)
                            return Status;
                    }
                }
            }
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::BindSets(
            const FVulkanPipeline& Pipeline,
            const eastl::vector<FArdaProviderObjectRef>& Objects,
            vk::PipelineBindPoint BindPoint)
        {
            if (Pipeline.mbDescriptorHeapPipeline)
            {
                if (!mbDescriptorHeapsBound)
                {
                    const auto& Context = *mDevice.GetContext();
                    mCommandBuffer.bindSamplerHeapEXT(
                        Context.mSamplerDescriptorHeap.mBindInfo);
                    mCommandBuffer.bindResourceHeapEXT(
                        Context.mResourceDescriptorHeap.mBindInfo);
                    mbDescriptorHeapsBound = true;
                }
            }
            else
                mbDescriptorHeapsBound = false;

            for (const auto& Object : Objects)
            {
                auto* Set = dynamic_cast<FVulkanBindingSet*>(Object.get());
                if (!Set) return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice, "Vulkan binding set has the wrong implementation.");
                const bool bExpected = eastl::any_of(
                    Pipeline.mSetGroups.begin(), Pipeline.mSetGroups.end(),
                    [Set](const FVulkanPipeline::FDescriptorSetGroup& Group)
                    {
                        return eastl::any_of(
                            Group.mLogicalLayouts.begin(), Group.mLogicalLayouts.end(),
                            [Set](const FArdaProviderObjectRef& Layout)
                            {
                                return Layout.get() == Set->mLayoutObject.get();
                            });
                    });
                if (!bExpected)
                    return FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidArgument,
                        "A Vulkan binding set layout is not part of the active pipeline.");
            }

            for (const FVulkanPipeline::FDescriptorSetGroup& Group : Pipeline.mSetGroups)
            {
                FArdaProviderObjectRef BoundObject;
                if (Group.mLogicalLayouts.size() == 1)
                {
                    for (const auto& Object : Objects)
                    {
                        auto* Set = static_cast<FVulkanBindingSet*>(Object.get());
                        if (Set->mLayoutObject.get() == Group.mLogicalLayouts[0].get())
                        {
                            BoundObject = Object;
                            break;
                        }
                    }
                }
                else
                {
                    auto Merged = mDevice.CreateMergedBindingSet(
                        Group.mLayout, Group.mLogicalLayouts, Objects);
                    if (!Merged)
                        return eastl::move(Merged.mStatus);
                    BoundObject = eastl::move(Merged.mValue);
                }
                auto* BoundSet = dynamic_cast<FVulkanBindingSet*>(BoundObject.get());
                if (!BoundSet)
                    return FArdaRHIStatus::Error(
                        EArdaRHIResult::InvalidArgument,
                        "The active Vulkan pipeline is missing a required binding set.");
                if (Group.mbDescriptorHeap)
                {
                    if (!BoundSet->mbDescriptorHeap)
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument,
                            "A direct Vulkan layout requires a descriptor-heap table.");
                    const uint32_t BaseIndex =
                        BoundSet->mDescriptorBaseIndex;
                    vk::PushDataInfoEXT Push;
                    Push.offset = Group.mHeapPushOffset;
                    Push.data.address = &BaseIndex;
                    Push.data.size = sizeof(BaseIndex);
                    mCommandBuffer.pushDataEXT(Push);
                    Retain(BoundObject);
                    continue;
                }
                const vk::DescriptorSet DescriptorSet = BoundSet->mSet;
                mCommandBuffer.bindDescriptorSets(
                    BindPoint,
                    Pipeline.mLayout,
                    Group.mRegisterSpace,
                    1,
                    &DescriptorSet,
                    0,
                    nullptr);
                Retain(BoundObject);
            }
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::SetGraphicsState(
            const FArdaProviderGraphicsState& State)
        {
            auto* Pipeline = dynamic_cast<FVulkanPipeline*>(State.mPipeline.get());
            auto* Framebuffer = dynamic_cast<FVulkanFramebuffer*>(State.mFramebuffer.get());
            if (!Pipeline || !Framebuffer) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "Vulkan graphics state has the wrong implementation.");
            Retain(State.mPipeline);
            Retain(State.mFramebuffer);
            for (const auto& Binding : State.mBindings)
                Retain(Binding);
            for (const auto& Binding : State.mVertexBuffers)
                Retain(Binding.mBuffer);
            Retain(State.mIndexBuffer);
            EndRendering();
            if (mbAutomaticBarriers)
            {
                for (const auto& Object : Framebuffer->mColors)
                {
                    auto* Texture = static_cast<FVulkanTexture*>(Object.get());
                    if (auto Status = SetTextureState(Object, Texture->mDesc, {},
                        EArdaRHIResourceState::RenderTarget); !Status) return Status;
                }
                if (Framebuffer->mDepth)
                {
                    auto* Texture = static_cast<FVulkanTexture*>(Framebuffer->mDepth.get());
                    if (auto Status = SetTextureState(Framebuffer->mDepth,
                        Texture->mDesc, {}, EArdaRHIResourceState::DepthWrite); !Status) return Status;
                }
            }
            eastl::vector<vk::RenderingAttachmentInfo> Colors;
            for (const auto& Object : Framebuffer->mColors)
            {
                auto* Texture = static_cast<FVulkanTexture*>(Object.get());
                vk::RenderingAttachmentInfo Attachment;
                Attachment.imageView = Texture->mView;
                Attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
                Attachment.loadOp = vk::AttachmentLoadOp::eLoad;
                Attachment.storeOp = vk::AttachmentStoreOp::eStore;
                Colors.push_back(Attachment);
            }
            vk::RenderingAttachmentInfo Depth;
            vk::RenderingInfo Rendering;
            Rendering.renderArea = vk::Rect2D({ 0, 0 }, Framebuffer->mExtent);
            Rendering.layerCount = 1;
            Rendering.colorAttachmentCount = static_cast<uint32_t>(Colors.size());
            Rendering.pColorAttachments = Colors.data();
            if (Framebuffer->mDepth)
            {
                auto* Texture = static_cast<FVulkanTexture*>(Framebuffer->mDepth.get());
                Depth.imageView = Texture->mView;
                Depth.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                Depth.loadOp = vk::AttachmentLoadOp::eLoad;
                Depth.storeOp = vk::AttachmentStoreOp::eStore;
                Rendering.pDepthAttachment = &Depth;
                if (ImageAspect(Texture->mDesc.mFormat) & vk::ImageAspectFlagBits::eStencil)
                    Rendering.pStencilAttachment = &Depth;
            }
            mCommandBuffer.beginRendering(Rendering);
            mbRendering = true;
            mCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, Pipeline->mPipeline);
            if (auto Status = BindSets(*Pipeline, State.mBindings,
                vk::PipelineBindPoint::eGraphics); !Status) return Status;
            eastl::vector<vk::Buffer> Buffers;
            eastl::vector<vk::DeviceSize> Offsets;
            for (const auto& Binding : State.mVertexBuffers)
            {
                auto* Buffer = dynamic_cast<FVulkanBuffer*>(Binding.mBuffer.get());
                if (!Buffer) return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice, "Vulkan vertex buffer has the wrong implementation.");
                while (Buffers.size() < Binding.mSlot)
                {
                    Buffers.push_back({});
                    Offsets.push_back(0);
                }
                Buffers.push_back(Buffer->mBuffer);
                Offsets.push_back(Binding.mOffset);
            }
            if (!Buffers.empty()) mCommandBuffer.bindVertexBuffers(
                0, static_cast<uint32_t>(Buffers.size()), Buffers.data(), Offsets.data());
            if (State.mIndexBuffer)
            {
                auto* Buffer = dynamic_cast<FVulkanBuffer*>(State.mIndexBuffer.get());
                if (!Buffer) return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice, "Vulkan index buffer has the wrong implementation.");
                mCommandBuffer.bindIndexBuffer(Buffer->mBuffer, State.mIndexOffset,
                    State.mIndexFormat == EArdaRHIFormat::R16UInt
                        ? vk::IndexType::eUint16 : vk::IndexType::eUint32);
            }
            eastl::vector<vk::Viewport> Viewports;
            for (const auto& Source : State.mViewports)
                Viewports.push_back(vk::Viewport(Source.mMinX, Source.mMaxY,
                    Source.mMaxX - Source.mMinX, -(Source.mMaxY - Source.mMinY),
                    Source.mMinZ, Source.mMaxZ));
            if (Viewports.empty()) Viewports.push_back(vk::Viewport(0.f,
                static_cast<float>(Framebuffer->mExtent.height),
                static_cast<float>(Framebuffer->mExtent.width),
                -static_cast<float>(Framebuffer->mExtent.height), 0.f, 1.f));
            mCommandBuffer.setViewport(0, static_cast<uint32_t>(Viewports.size()), Viewports.data());
            eastl::vector<vk::Rect2D> Scissors;
            for (const auto& Source : State.mScissors)
                Scissors.push_back(vk::Rect2D(
                    { Source.mMinX, Source.mMinY },
                    { static_cast<uint32_t>(eastl::max(0, Source.mMaxX - Source.mMinX)),
                      static_cast<uint32_t>(eastl::max(0, Source.mMaxY - Source.mMinY)) }));
            if (Scissors.empty()) Scissors.push_back(vk::Rect2D({ 0, 0 }, Framebuffer->mExtent));
            mCommandBuffer.setScissor(0, static_cast<uint32_t>(Scissors.size()), Scissors.data());
            mBoundGraphics = Pipeline;
            mBoundCompute = nullptr;
            mBoundRayTracing = nullptr;
            mBoundShaderTable = nullptr;
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::SetComputeState(
            const FArdaProviderComputeState& State)
        {
            auto* Pipeline = dynamic_cast<FVulkanPipeline*>(State.mPipeline.get());
            if (!Pipeline) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "Vulkan compute state has the wrong implementation.");
            Retain(State.mPipeline);
            for (const auto& Binding : State.mBindings)
                Retain(Binding);
            EndRendering();
            mCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, Pipeline->mPipeline);
            if (auto Status = BindSets(*Pipeline, State.mBindings,
                vk::PipelineBindPoint::eCompute); !Status) return Status;
            mBoundCompute = Pipeline;
            mBoundGraphics = nullptr;
            mBoundRayTracing = nullptr;
            mBoundShaderTable = nullptr;
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::SetMeshletState(
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

        FArdaRHIStatus FArdaVulkanCommandList::SetRayTracingState(
            const FArdaProviderRayTracingState& State)
        {
            auto* Table = dynamic_cast<FVulkanShaderTable*>(
                State.mShaderTable.get());
            if (!Table || !Table->mPipeline || !Table->mBuffer ||
                !Table->mRayGeneration.deviceAddress)
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
                    "The Vulkan ray-tracing shader table is incomplete.");
            EndRendering();
            mCommandBuffer.bindPipeline(
                vk::PipelineBindPoint::eRayTracingKHR,
                Table->mPipeline->mPipeline);
            if (auto Status = BindSets(
                    Table->mPipeline->mBindings, State.mBindings,
                    vk::PipelineBindPoint::eRayTracingKHR); !Status)
                return Status;
            Retain(State.mShaderTable);
            for (const auto& Binding : State.mBindings) Retain(Binding);
            mBoundRayTracing = Table->mPipeline;
            mBoundShaderTable = Table;
            mBoundGraphics = nullptr;
            mBoundCompute = nullptr;
            return {};
        }

        void FArdaVulkanCommandList::SetPushConstants(const void* Data, size_t Size)
        {
            const FVulkanPipeline* Pipeline = mBoundGraphics
                ? mBoundGraphics
                : mBoundCompute
                    ? mBoundCompute
                    : mBoundRayTracing
                        ? &mBoundRayTracing->mBindings : nullptr;
            if (Pipeline && Data && Size && Pipeline->mPushSize)
                mCommandBuffer.pushConstants(Pipeline->mLayout, Pipeline->mPushStages,
                    0, static_cast<uint32_t>(eastl::min<size_t>(Size, Pipeline->mPushSize)), Data);
        }

        void FArdaVulkanCommandList::Draw(const FArdaRHIDrawArguments& Arguments)
        {
            mCommandBuffer.draw(Arguments.mVertexCount, Arguments.mInstanceCount,
                Arguments.mStartVertex, Arguments.mStartInstance);
        }

        void FArdaVulkanCommandList::DrawIndexed(const FArdaRHIDrawArguments& Arguments)
        {
            mCommandBuffer.drawIndexed(Arguments.mVertexCount, Arguments.mInstanceCount,
                Arguments.mStartIndex, static_cast<int32_t>(Arguments.mStartVertex),
                Arguments.mStartInstance);
        }

        FArdaRHIStatus FArdaVulkanCommandList::DrawIndirect(
            const FArdaProviderObjectRef& Arguments,
            uint64_t Offset,
            uint32_t DrawCount,
            uint32_t Stride)
        {
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Arguments.get());
            if (!Buffer || !Buffer->mBuffer)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan indirect draw has the wrong buffer type.");
            mCommandBuffer.drawIndirect(
                Buffer->mBuffer, Offset, DrawCount, Stride);
            Retain(Arguments);
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::DrawIndexedIndirect(
            const FArdaProviderObjectRef& Arguments,
            uint64_t Offset,
            uint32_t DrawCount,
            uint32_t Stride)
        {
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Arguments.get());
            if (!Buffer || !Buffer->mBuffer)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan indexed indirect draw has the wrong buffer type.");
            mCommandBuffer.drawIndexedIndirect(
                Buffer->mBuffer, Offset, DrawCount, Stride);
            Retain(Arguments);
            return {};
        }

        void FArdaVulkanCommandList::Dispatch(uint32_t X, uint32_t Y, uint32_t Z)
        {
            mCommandBuffer.dispatch(X, Y, Z);
        }

        FArdaRHIStatus FArdaVulkanCommandList::DispatchIndirect(
            const FArdaProviderObjectRef& Arguments,
            uint64_t Offset)
        {
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Arguments.get());
            if (!Buffer || !Buffer->mBuffer)
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice,
                    "Vulkan indirect dispatch has the wrong buffer type.");
            mCommandBuffer.dispatchIndirect(Buffer->mBuffer, Offset);
            Retain(Arguments);
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::RecordAccelStructBuild(
            const FArdaProviderObjectRef& Object,
            vk::AccelerationStructureBuildGeometryInfoKHR& Build,
            const eastl::vector<vk::AccelerationStructureBuildRangeInfoKHR>& Ranges,
            EArdaRHIAccelStructBuildFlags Flags)
        {
            auto* AccelStruct = dynamic_cast<FVulkanAccelStruct*>(Object.get());
            if (!AccelStruct || !AccelStruct->mAccelStruct)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "The Vulkan acceleration-structure build is invalid.");
            const bool bUpdate = HasAnyFlags(Flags,
                EArdaRHIAccelStructBuildFlags::PerformUpdate);
            FArdaRHIBufferDesc ScratchDesc;
            ScratchDesc.mByteSize = bUpdate
                ? AccelStruct->mRequirements.mUpdateScratchSize
                : AccelStruct->mRequirements.mBuildScratchSize;
            ScratchDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess |
                EArdaRHIBufferUsage::AccelStructBuildInput;
            ScratchDesc.mInitialState =
                EArdaRHIResourceState::UnorderedAccess;
            ScratchDesc.mDebugName = "Vulkan AS scratch";
            auto ScratchObject = mDevice.CreateBuffer(ScratchDesc);
            if (!ScratchObject) return ScratchObject.mStatus;
            auto* Scratch = dynamic_cast<FVulkanBuffer*>(
                ScratchObject.mValue.get());
            Build.flags = ToVulkanBuildFlags(
                AccelStruct->mDesc.mBuildFlags | Flags);
            Build.mode = bUpdate
                ? vk::BuildAccelerationStructureModeKHR::eUpdate
                : vk::BuildAccelerationStructureModeKHR::eBuild;
            Build.srcAccelerationStructure = bUpdate
                ? AccelStruct->mAccelStruct
                : vk::AccelerationStructureKHR{};
            Build.dstAccelerationStructure = AccelStruct->mAccelStruct;
            Build.scratchData.deviceAddress =
                mDevice.GetContext()->mDevice.getBufferAddress(
                    vk::BufferDeviceAddressInfo(Scratch->mBuffer));
            const vk::AccelerationStructureBuildRangeInfoKHR* RangePointer =
                Ranges.data();
            EndRendering();
            mCommandBuffer.buildAccelerationStructuresKHR(
                1, &Build, &RangePointer);
            vk::MemoryBarrier2 Barrier;
            Barrier.srcStageMask =
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
            Barrier.srcAccessMask =
                vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
            Barrier.dstStageMask =
                vk::PipelineStageFlagBits2::eRayTracingShaderKHR |
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
            Barrier.dstAccessMask =
                vk::AccessFlagBits2::eAccelerationStructureReadKHR;
            vk::DependencyInfo Dependency;
            Dependency.memoryBarrierCount = 1;
            Dependency.pMemoryBarriers = &Barrier;
            mCommandBuffer.pipelineBarrier2(Dependency);
            if (AccelStruct->mQueryPool)
            {
                mCommandBuffer.resetQueryPool(
                    AccelStruct->mQueryPool, 0, 1);
                mCommandBuffer.writeAccelerationStructuresPropertiesKHR(
                    1, &AccelStruct->mAccelStruct,
                    vk::QueryType::eAccelerationStructureCompactedSizeKHR,
                    AccelStruct->mQueryPool, 0);
                AccelStruct->mbCompactedSizePending = true;
            }
            Retain(Object);
            Retain(ScratchObject.mValue);
            mAccelStructStates[AccelStruct] = {
                EArdaRHIResourceState::AccelStructRead,
                bUpdate ? EArdaRHIAccelStructBuildState::Updated
                        : EArdaRHIAccelStructBuildState::Built};
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::BuildOpacityMicromap(
            const FArdaProviderObjectRef& Object)
        {
            auto* Micromap = dynamic_cast<FVulkanOpacityMicromap*>(
                Object.get());
            if (!Micromap || !Micromap->mMicromap)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "The Vulkan opacity-micromap build is invalid.");
            auto* Input = dynamic_cast<FVulkanBuffer*>(
                Micromap->mInputBuffer.get());
            auto* Triangles = dynamic_cast<FVulkanBuffer*>(
                Micromap->mTriangleBuffer.get());
            if (!Input || !Triangles || !Input->mBuffer ||
                !Triangles->mBuffer)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "The Vulkan opacity-micromap input buffers are invalid.");

            FArdaRHIBufferDesc ScratchDesc;
            ScratchDesc.mByteSize = Micromap->mBuildScratchSize;
            ScratchDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
            ScratchDesc.mInitialState =
                EArdaRHIResourceState::UnorderedAccess;
            ScratchDesc.mDebugName = "Vulkan opacity micromap scratch";
            auto ScratchObject = mDevice.CreateBuffer(ScratchDesc);
            if (!ScratchObject) return ScratchObject.mStatus;
            auto* Scratch = dynamic_cast<FVulkanBuffer*>(
                ScratchObject.mValue.get());

            const auto BufferAddress = [this](FVulkanBuffer& Buffer)
            {
                return mDevice.GetContext()->mDevice.getBufferAddress(
                    vk::BufferDeviceAddressInfo(Buffer.mBuffer));
            };
            vk::MicromapBuildInfoEXT Build;
            Build.type = vk::MicromapTypeEXT::eOpacityMicromap;
            Build.flags = ToVulkanMicromapBuildFlags(Micromap->mDesc.mFlags);
            Build.mode = vk::BuildMicromapModeEXT::eBuild;
            Build.dstMicromap = Micromap->mMicromap;
            Build.usageCountsCount = static_cast<uint32_t>(
                Micromap->mUsageCounts.size());
            Build.pUsageCounts = Micromap->mUsageCounts.data();
            Build.data.deviceAddress = BufferAddress(*Input) +
                Micromap->mDesc.mInputBufferOffset;
            Build.scratchData.deviceAddress = BufferAddress(*Scratch);
            Build.triangleArray.deviceAddress = BufferAddress(*Triangles) +
                Micromap->mDesc.mPerMicromapDescBufferOffset;
            Build.triangleArrayStride = sizeof(vk::MicromapTriangleEXT);
            EndRendering();
            mCommandBuffer.buildMicromapsEXT(1, &Build);

            vk::MemoryBarrier2 Barrier;
            Barrier.srcStageMask = vk::PipelineStageFlagBits2::eMicromapBuildEXT;
            Barrier.srcAccessMask = vk::AccessFlagBits2::eMicromapWriteEXT;
            Barrier.dstStageMask =
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
                vk::PipelineStageFlagBits2::eMicromapBuildEXT;
            Barrier.dstAccessMask = vk::AccessFlagBits2::eMicromapReadEXT;
            vk::DependencyInfo Dependency;
            Dependency.memoryBarrierCount = 1;
            Dependency.pMemoryBarriers = &Barrier;
            mCommandBuffer.pipelineBarrier2(Dependency);
            if (Micromap->mQueryPool)
            {
                mCommandBuffer.resetQueryPool(Micromap->mQueryPool, 0, 1);
                mCommandBuffer.writeMicromapsPropertiesEXT(
                    1, &Micromap->mMicromap,
                    vk::QueryType::eMicromapCompactedSizeEXT,
                    Micromap->mQueryPool, 0);
                Micromap->mbCompactedSizePending = true;
            }
            Retain(Object);
            Retain(ScratchObject.mValue);
            Retain(Micromap->mInputBuffer);
            Retain(Micromap->mTriangleBuffer);
            mOpacityMicromapStates[Micromap] = {
                EArdaRHIResourceState::OpacityMicromapBuildInput,
                EArdaRHIAccelStructBuildState::Built};
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::CompactOpacityMicromap(
            const FArdaProviderObjectRef& DestinationObject,
            const FArdaProviderObjectRef& SourceObject)
        {
            auto* Destination = dynamic_cast<FVulkanOpacityMicromap*>(
                DestinationObject.get());
            auto* Source = dynamic_cast<FVulkanOpacityMicromap*>(
                SourceObject.get());
            if (!Destination || !Source || !Destination->mMicromap ||
                !Source->mMicromap)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "The Vulkan opacity-micromap compaction resources are invalid.");
            EndRendering();
            vk::CopyMicromapInfoEXT Copy;
            Copy.src = Source->mMicromap;
            Copy.dst = Destination->mMicromap;
            Copy.mode = vk::CopyMicromapModeEXT::eCompact;
            mCommandBuffer.copyMicromapEXT(Copy);
            vk::MemoryBarrier2 Barrier;
            Barrier.srcStageMask =
                vk::PipelineStageFlagBits2::eMicromapBuildEXT;
            Barrier.srcAccessMask = vk::AccessFlagBits2::eMicromapWriteEXT;
            Barrier.dstStageMask =
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
                vk::PipelineStageFlagBits2::eMicromapBuildEXT;
            Barrier.dstAccessMask = vk::AccessFlagBits2::eMicromapReadEXT;
            vk::DependencyInfo Dependency;
            Dependency.memoryBarrierCount = 1;
            Dependency.pMemoryBarriers = &Barrier;
            mCommandBuffer.pipelineBarrier2(Dependency);
            Retain(DestinationObject);
            Retain(SourceObject);
            mOpacityMicromapStates[Destination] = {
                EArdaRHIResourceState::OpacityMicromapBuildInput,
                EArdaRHIAccelStructBuildState::Compacted};
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::BuildBottomLevelAccelStruct(
            const FArdaProviderObjectRef& Object,
            const eastl::vector<FArdaProviderRayTracingGeometry>& Geometries,
            EArdaRHIAccelStructBuildFlags Flags)
        {
            eastl::vector<vk::AccelerationStructureGeometryKHR> Native;
            eastl::vector<vk::AccelerationStructureBuildRangeInfoKHR> Ranges;
            eastl::vector<eastl::vector<vk::MicromapUsageEXT>>
                OpacityUsageCounts;
            eastl::vector<
                vk::AccelerationStructureTrianglesOpacityMicromapEXT>
                OpacityInfos;
            Native.reserve(Geometries.size());
            Ranges.reserve(Geometries.size());
            OpacityUsageCounts.reserve(Geometries.size());
            OpacityInfos.reserve(Geometries.size());
            for (const auto& Source : Geometries)
            {
                auto* Vertex = dynamic_cast<FVulkanBuffer*>(
                    Source.mVertexOrAABBBuffer.get());
                if (!Vertex || !Vertex->mBuffer)
                    return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                        "A Vulkan BLAS geometry buffer is invalid.");
                const uint64_t VertexAddress =
                    mDevice.GetContext()->mDevice.getBufferAddress(
                        vk::BufferDeviceAddressInfo(Vertex->mBuffer)) +
                    Source.mDesc.mVertexOrAABBOffset;
                vk::AccelerationStructureGeometryKHR Geometry;
                Geometry.flags = ToVulkanGeometryFlags(Source.mDesc.mFlags);
                vk::AccelerationStructureBuildRangeInfoKHR Range;
                if (Source.mDesc.mType ==
                    EArdaRHIRayTracingGeometryType::Triangles)
                {
                    vk::AccelerationStructureGeometryTrianglesDataKHR Triangles;
                    Triangles.vertexFormat = ToVulkan(
                        Source.mDesc.mVertexFormat);
                    Triangles.vertexData.deviceAddress = VertexAddress;
                    Triangles.vertexStride = Source.mDesc.mStride;
                    Triangles.maxVertex = Source.mDesc.mVertexOrAABBCount
                        ? Source.mDesc.mVertexOrAABBCount - 1u : 0u;
                    if (Source.mIndexBuffer)
                    {
                        auto* Index = dynamic_cast<FVulkanBuffer*>(
                            Source.mIndexBuffer.get());
                        if (!Index || !Index->mBuffer)
                            return FArdaRHIStatus::Error(
                                EArdaRHIResult::WrongDevice,
                                "A Vulkan BLAS index buffer is invalid.");
                        Triangles.indexData.deviceAddress =
                            mDevice.GetContext()->mDevice.getBufferAddress(
                                vk::BufferDeviceAddressInfo(Index->mBuffer)) +
                            Source.mDesc.mIndexOffset;
                        Triangles.indexType =
                            Source.mDesc.mIndexFormat == EArdaRHIFormat::R16UInt
                                ? vk::IndexType::eUint16
                                : vk::IndexType::eUint32;
                        Range.primitiveCount = Source.mDesc.mIndexCount / 3u;
                        Retain(Source.mIndexBuffer);
                    }
                    else
                    {
                        Triangles.indexType = vk::IndexType::eNoneKHR;
                        Range.primitiveCount =
                            Source.mDesc.mVertexOrAABBCount / 3u;
                    }
                    if (Source.mOpacityMicromap)
                    {
                        auto* Micromap =
                            dynamic_cast<FVulkanOpacityMicromap*>(
                                Source.mOpacityMicromap.get());
                        if (!Micromap || !Micromap->mMicromap)
                            return FArdaRHIStatus::Error(
                                EArdaRHIResult::WrongDevice,
                                "A Vulkan BLAS opacity micromap is invalid.");
                        bool bMicromapBuilt = false;
                        const auto Tracked =
                            mOpacityMicromapStates.find(Micromap);
                        if (Tracked != mOpacityMicromapStates.end())
                            bMicromapBuilt = Tracked->second.mBuildState !=
                                EArdaRHIAccelStructBuildState::Unbuilt;
                        else
                        {
                            std::lock_guard<std::mutex> Lock(
                                Micromap->mStateMutex);
                            bMicromapBuilt = Micromap->mBuildState !=
                                EArdaRHIAccelStructBuildState::Unbuilt;
                        }
                        if (!bMicromapBuilt)
                            return FArdaRHIStatus::Error(
                                EArdaRHIResult::InvalidState,
                                "A Vulkan BLAS opacity micromap must be built before it is consumed.");
                        OpacityUsageCounts.push_back(
                            Source.mDesc.mOpacityMicromapUsageCounts.empty()
                                ? Micromap->mUsageCounts
                                : ToVulkanMicromapUsages(
                                    Source.mDesc.mOpacityMicromapUsageCounts));
                        vk::AccelerationStructureTrianglesOpacityMicromapEXT
                            Opacity;
                        Opacity.indexType =
                            Source.mDesc.mOpacityMicromapIndexFormat ==
                                EArdaRHIFormat::R32UInt
                                ? vk::IndexType::eUint32
                                : Source.mDesc.mOpacityMicromapIndexFormat ==
                                    EArdaRHIFormat::R16UInt
                                    ? vk::IndexType::eUint16
                                    : vk::IndexType::eNoneKHR;
                        if (Source.mOpacityMicromapIndexBuffer)
                        {
                            auto* Index = dynamic_cast<FVulkanBuffer*>(
                                Source.mOpacityMicromapIndexBuffer.get());
                            if (!Index || !Index->mBuffer)
                                return FArdaRHIStatus::Error(
                                    EArdaRHIResult::WrongDevice,
                                    "A Vulkan opacity-micromap index buffer is invalid.");
                            Opacity.indexBuffer.deviceAddress =
                                mDevice.GetContext()->mDevice.getBufferAddress(
                                    vk::BufferDeviceAddressInfo(
                                        Index->mBuffer)) +
                                Source.mDesc.mOpacityMicromapIndexOffset;
                            Opacity.indexStride =
                                Opacity.indexType == vk::IndexType::eUint32
                                    ? 4u : 2u;
                            Retain(Source.mOpacityMicromapIndexBuffer);
                        }
                        Opacity.usageCountsCount = static_cast<uint32_t>(
                            OpacityUsageCounts.back().size());
                        Opacity.pUsageCounts =
                            OpacityUsageCounts.back().data();
                        Opacity.micromap = Micromap->mMicromap;
                        OpacityInfos.push_back(Opacity);
                        Triangles.pNext = &OpacityInfos.back();
                        Retain(Source.mOpacityMicromap);
                    }
                    Geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
                    Geometry.geometry.triangles = Triangles;
                }
                else
                {
                    vk::AccelerationStructureGeometryAabbsDataKHR Aabbs;
                    Aabbs.data.deviceAddress = VertexAddress;
                    Aabbs.stride = Source.mDesc.mStride;
                    Geometry.geometryType = vk::GeometryTypeKHR::eAabbs;
                    Geometry.geometry.aabbs = Aabbs;
                    Range.primitiveCount =
                        Source.mDesc.mVertexOrAABBCount;
                }
                Retain(Source.mVertexOrAABBBuffer);
                Native.push_back(Geometry);
                Ranges.push_back(Range);
            }
            vk::AccelerationStructureBuildGeometryInfoKHR Build;
            Build.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
            Build.geometryCount = static_cast<uint32_t>(Native.size());
            Build.pGeometries = Native.data();
            return RecordAccelStructBuild(Object, Build, Ranges, Flags);
        }

        FArdaRHIStatus FArdaVulkanCommandList::BuildTopLevelAccelStruct(
            const FArdaProviderObjectRef& Object,
            const eastl::vector<FArdaProviderRayTracingInstance>& Instances,
            EArdaRHIAccelStructBuildFlags Flags)
        {
            FArdaRHIBufferDesc BufferDesc;
            BufferDesc.mByteSize = eastl::max<uint64_t>(1,
                Instances.size() * sizeof(vk::AccelerationStructureInstanceKHR));
            BufferDesc.mUsage = EArdaRHIBufferUsage::AccelStructBuildInput;
            BufferDesc.mCpuAccess = EArdaRHICpuAccess::Write;
            BufferDesc.mInitialState = EArdaRHIResourceState::Common;
            BufferDesc.mDebugName = "Vulkan TLAS instances";
            auto InstanceObject = mDevice.CreateBuffer(BufferDesc);
            if (!InstanceObject) return InstanceObject.mStatus;
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(
                InstanceObject.mValue.get());
            void* Mapped = nullptr;
            if (!Instances.empty())
            {
                auto Mapping = mDevice.MapBuffer(
                    InstanceObject.mValue, 0,
                    static_cast<size_t>(BufferDesc.mByteSize));
                if (!Mapping) return Mapping.mStatus;
                Mapped = Mapping.mValue;
                auto* Output =
                    static_cast<vk::AccelerationStructureInstanceKHR*>(Mapped);
                for (size_t Index = 0; Index < Instances.size(); ++Index)
                {
                    const auto& Instance = Instances[Index];
                    auto* BottomLevel = dynamic_cast<FVulkanAccelStruct*>(
                        Instance.mBottomLevelAccelStruct.get());
                    if (!BottomLevel || !BottomLevel->mAccelStruct)
                    {
                        mDevice.UnmapBuffer(InstanceObject.mValue);
                        return FArdaRHIStatus::Error(
                            EArdaRHIResult::WrongDevice,
                            "A Vulkan TLAS instance BLAS is invalid.");
                    }
                    vk::TransformMatrixKHR Transform;
                    std::memcpy(&Transform.matrix[0][0], Instance.mTransform,
                        sizeof(Instance.mTransform));
                    Output[Index] = vk::AccelerationStructureInstanceKHR(
                        Transform,
                        Instance.mInstanceID & 0xffffffu,
                        Instance.mInstanceMask & 0xffu,
                        Instance.mInstanceContributionToHitGroupIndex & 0xffffffu,
                        vk::GeometryInstanceFlagsKHR(Instance.mFlags),
                        mDevice.GetAccelStructDeviceAddress(
                            Instance.mBottomLevelAccelStruct));
                    Retain(Instance.mBottomLevelAccelStruct);
                }
                mDevice.UnmapBuffer(InstanceObject.mValue);
            }
            const uint64_t Address =
                mDevice.GetContext()->mDevice.getBufferAddress(
                    vk::BufferDeviceAddressInfo(Buffer->mBuffer));
            vk::AccelerationStructureGeometryInstancesDataKHR InstanceData;
            InstanceData.arrayOfPointers = false;
            InstanceData.data.deviceAddress = Address;
            vk::AccelerationStructureGeometryKHR Geometry;
            Geometry.geometryType = vk::GeometryTypeKHR::eInstances;
            Geometry.geometry.instances = InstanceData;
            vk::AccelerationStructureBuildRangeInfoKHR Range;
            Range.primitiveCount = static_cast<uint32_t>(Instances.size());
            vk::AccelerationStructureBuildGeometryInfoKHR Build;
            Build.type = vk::AccelerationStructureTypeKHR::eTopLevel;
            Build.geometryCount = 1;
            Build.pGeometries = &Geometry;
            const eastl::vector<vk::AccelerationStructureBuildRangeInfoKHR>
                Ranges{Range};
            const FArdaRHIStatus Status = RecordAccelStructBuild(
                Object, Build, Ranges, Flags);
            if (Status) Retain(InstanceObject.mValue);
            return Status;
        }

        FArdaRHIStatus
        FArdaVulkanCommandList::BuildTopLevelAccelStructFromBuffer(
            const FArdaProviderObjectRef& Object,
            const FArdaProviderObjectRef& InstanceObject,
            uint64_t Offset,
            size_t InstanceCount,
            EArdaRHIAccelStructBuildFlags Flags)
        {
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(InstanceObject.get());
            if (!Buffer || !Buffer->mBuffer)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "The Vulkan TLAS instance buffer is invalid.");
            const uint64_t Address =
                mDevice.GetContext()->mDevice.getBufferAddress(
                    vk::BufferDeviceAddressInfo(Buffer->mBuffer)) + Offset;
            vk::AccelerationStructureGeometryInstancesDataKHR InstanceData;
            InstanceData.arrayOfPointers = false;
            InstanceData.data.deviceAddress = Address;
            vk::AccelerationStructureGeometryKHR Geometry;
            Geometry.geometryType = vk::GeometryTypeKHR::eInstances;
            Geometry.geometry.instances = InstanceData;
            vk::AccelerationStructureBuildRangeInfoKHR Range;
            Range.primitiveCount = static_cast<uint32_t>(InstanceCount);
            vk::AccelerationStructureBuildGeometryInfoKHR Build;
            Build.type = vk::AccelerationStructureTypeKHR::eTopLevel;
            Build.geometryCount = 1;
            Build.pGeometries = &Geometry;
            const eastl::vector<vk::AccelerationStructureBuildRangeInfoKHR>
                Ranges{Range};
            Retain(InstanceObject);
            return RecordAccelStructBuild(Object, Build, Ranges, Flags);
        }

        FArdaRHIStatus FArdaVulkanCommandList::CompactAccelStruct(
            const FArdaProviderObjectRef& DestinationObject,
            const FArdaProviderObjectRef& SourceObject)
        {
            auto* Destination = dynamic_cast<FVulkanAccelStruct*>(
                DestinationObject.get());
            auto* Source = dynamic_cast<FVulkanAccelStruct*>(SourceObject.get());
            if (!Destination || !Source || !Destination->mAccelStruct ||
                !Source->mAccelStruct)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "The Vulkan AS compaction resources are invalid.");
            EndRendering();
            vk::CopyAccelerationStructureInfoKHR Copy;
            Copy.src = Source->mAccelStruct;
            Copy.dst = Destination->mAccelStruct;
            Copy.mode = vk::CopyAccelerationStructureModeKHR::eCompact;
            mCommandBuffer.copyAccelerationStructureKHR(Copy);
            vk::MemoryBarrier2 Barrier;
            Barrier.srcStageMask =
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
            Barrier.srcAccessMask =
                vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
            Barrier.dstStageMask =
                vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
            Barrier.dstAccessMask =
                vk::AccessFlagBits2::eAccelerationStructureReadKHR;
            vk::DependencyInfo Dependency;
            Dependency.memoryBarrierCount = 1;
            Dependency.pMemoryBarriers = &Barrier;
            mCommandBuffer.pipelineBarrier2(Dependency);
            Retain(DestinationObject);
            Retain(SourceObject);
            mAccelStructStates[Destination] = {
                EArdaRHIResourceState::AccelStructRead,
                EArdaRHIAccelStructBuildState::Compacted};
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::DispatchMesh(
            uint32_t X, uint32_t Y, uint32_t Z)
        {
            if (!mBoundGraphics)
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
                    "A Vulkan mesh pipeline must be bound before dispatch.");
            mCommandBuffer.drawMeshTasksEXT(X, Y, Z);
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::DispatchRays(
            uint32_t Width, uint32_t Height, uint32_t Depth)
        {
            if (!mBoundShaderTable)
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
                    "A Vulkan shader table must be bound before ray dispatch.");
            mCommandBuffer.traceRaysKHR(
                &mBoundShaderTable->mRayGeneration,
                &mBoundShaderTable->mMiss,
                &mBoundShaderTable->mHit,
                &mBoundShaderTable->mCallable,
                Width, Height, Depth);
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::DispatchRaysIndirect(
            const FArdaProviderObjectRef& Arguments,
            uint64_t Offset)
        {
            if (!mBoundShaderTable)
                return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
                    "A Vulkan shader table must be bound before indirect ray dispatch.");
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Arguments.get());
            if (!Buffer || !Buffer->mBuffer)
                return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
                    "The Vulkan indirect ray buffer is invalid.");
            const uint64_t Address = mDevice.GetContext()->mDevice.getBufferAddress(
                vk::BufferDeviceAddressInfo(Buffer->mBuffer)) + Offset;
            mCommandBuffer.traceRaysIndirectKHR(
                &mBoundShaderTable->mRayGeneration,
                &mBoundShaderTable->mMiss,
                &mBoundShaderTable->mHit,
                &mBoundShaderTable->mCallable,
                Address);
            Retain(Arguments);
            return {};
        }

        TArdaRHIResult<eastl::unique_ptr<IArdaProviderCommandList>>
        FArdaVulkanProviderDevice::CreateCommandList(EArdaRHIQueueType Queue, bool)
        {
            if (!mCapabilities.IsQueueSupported(Queue))
                return Fail<eastl::unique_ptr<IArdaProviderCommandList>>(
                    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                        "The requested Vulkan command queue is unavailable."));
            auto Commands = eastl::make_unique<FArdaVulkanCommandList>(
                *this, Queue);
            if (auto Status = Commands->Initialize(); !Status)
                return Fail<eastl::unique_ptr<IArdaProviderCommandList>>(eastl::move(Status));
            return { eastl::unique_ptr<IArdaProviderCommandList>(Commands.release()), {} };
        }

        TArdaRHIResult<uint64_t> FArdaVulkanProviderDevice::ExecuteCommandList(
            IArdaProviderCommandList& CommandList, EArdaRHIQueueType QueueType)
        {
            auto* Commands = dynamic_cast<FArdaVulkanCommandList*>(&CommandList);
            if (!Commands) return Fail<uint64_t>(FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "Vulkan command list has the wrong implementation."));
            if (const FArdaRHIStatus Status =
                    Commands->ValidateTrackedStartStates();
                !Status)
            {
                return Fail<uint64_t>(Status);
            }
            vk::Fence Fence;
            try
            {
                Fence = mContext->mDevice.createFence({});
                const vk::CommandBuffer Buffer = Commands->GetCommandBuffer();
                const size_t TimelineIndex = GetArdaRHIQueueIndex(QueueType);
                const uint64_t TimelineValue =
                    mContext->mQueueTimelineValues[TimelineIndex].fetch_add(
                        1, std::memory_order_relaxed) + 1;
                vk::CommandBufferSubmitInfo CommandInfo(Buffer);
                vk::SemaphoreSubmitInfo SignalInfo(
                    mContext->mQueueTimelines[TimelineIndex],
                    TimelineValue,
                    vk::PipelineStageFlagBits2::eAllCommands);
                vk::SubmitInfo2 Submit;
                Submit.commandBufferInfoCount = 1;
                Submit.pCommandBufferInfos = &CommandInfo;
                Submit.signalSemaphoreInfoCount = 1;
                Submit.pSignalSemaphoreInfos = &SignalInfo;
                {
                    std::lock_guard<std::mutex> Lock(mContext->mQueueMutex);
                    vk::Queue Queue = mContext->mQueue;
                    if (QueueType == EArdaRHIQueueType::Compute &&
                        mContext->mComputeQueue)
                        Queue = mContext->mComputeQueue;
                    else if (QueueType == EArdaRHIQueueType::Copy &&
                        mContext->mCopyQueue)
                        Queue = mContext->mCopyQueue;
                    Queue.submit2(Submit, Fence);
                }
                Commands->CommitTrackedStates();
                {
                    std::lock_guard<std::mutex> Lock(mSubmissionMutex);
                    const uint64_t Value = EncodeVulkanSubmission(
                        QueueType, TimelineValue);
                    mPendingSubmissions.push_back(
                        { Value, Fence, Commands->GetRecording() });
                    return { Value, {} };
                }
            }
            catch (const vk::SystemError& Error)
            {
                if (Fence)
                    mContext->mDevice.destroyFence(Fence);
                return Fail<uint64_t>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaRHIStatus FArdaVulkanProviderDevice::QueueWait(
            EArdaRHIQueueType WaitQueue,
            EArdaRHIQueueType ExecutionQueue,
            uint64_t Submission)
        {
            if (!mCapabilities.IsQueueSupported(WaitQueue) ||
                !mCapabilities.IsQueueSupported(ExecutionQueue))
            {
                return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
                    "A Vulkan GPU queue wait referenced an unavailable queue.");
            }
            try
            {
                const size_t ExecutionIndex =
                    GetArdaRHIQueueIndex(ExecutionQueue);
                vk::SemaphoreSubmitInfo WaitInfo(
                    mContext->mQueueTimelines[ExecutionIndex],
                    DecodeVulkanSubmissionValue(Submission),
                    vk::PipelineStageFlagBits2::eAllCommands);
                vk::SubmitInfo2 Submit;
                Submit.waitSemaphoreInfoCount = 1;
                Submit.pWaitSemaphoreInfos = &WaitInfo;
                vk::Queue Queue = mContext->mQueue;
                if (WaitQueue == EArdaRHIQueueType::Compute)
                    Queue = mContext->mComputeQueue;
                else if (WaitQueue == EArdaRHIQueueType::Copy)
                    Queue = mContext->mCopyQueue;
                std::lock_guard<std::mutex> Lock(mContext->mQueueMutex);
                Queue.submit2(Submit, {});
                return {};
            }
            catch (const vk::SystemError& Error)
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what());
            }
        }

        FArdaRHIStatus FArdaVulkanProviderDevice::WaitForSubmission(uint64_t Value)
        {
            try
            {
                std::lock_guard<std::mutex> Lock(mSubmissionMutex);
                const auto It = eastl::find_if(
                    mPendingSubmissions.begin(), mPendingSubmissions.end(),
                    [Value](const FPendingSubmission& Submission)
                    {
                        return Submission.mValue == Value;
                    });
                if (It == mPendingSubmissions.end()) return {};
                const vk::Result Result = mContext->mDevice.waitForFences(
                    It->mFence, true, UINT64_MAX);
                if (Result != vk::Result::eSuccess)
                    return FArdaRHIStatus::Error(
                        EArdaRHIResult::BackendFailure,
                        "Failed while waiting for a Vulkan submission fence.");
                mContext->mDevice.destroyFence(It->mFence);
                mPendingSubmissions.erase(It);
                return {};
            }
            catch (const vk::SystemError& Error)
            {
                return FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what());
            }
        }

        void FArdaVulkanProviderDevice::RunGarbageCollection()
        {
            std::lock_guard<std::mutex> Lock(mSubmissionMutex);
            for (size_t Index = 0; Index < mPendingSubmissions.size();)
            {
                vk::Result Status = vk::Result::eNotReady;
                try
                {
                    Status = mContext->mDevice.getFenceStatus(
                        mPendingSubmissions[Index].mFence);
                }
                catch (const vk::SystemError&)
                {
                    ++Index;
                    continue;
                }
                if (Status != vk::Result::eSuccess)
                {
                    ++Index;
                    continue;
                }
                mContext->mDevice.destroyFence(
                    mPendingSubmissions[Index].mFence);
                mPendingSubmissions.erase(mPendingSubmissions.begin() + Index);
            }
        }

        FArdaProviderLifetimeStats
        FArdaVulkanProviderDevice::GetLifetimeStats() const noexcept
        {
            FArdaProviderLifetimeStats Stats;
            Stats.mDescriptorSets = mContext->mAllocatedDescriptorSets.load(
                std::memory_order_relaxed);
            std::lock_guard<std::mutex> Lock(mSubmissionMutex);
            Stats.mPendingSubmissions = mPendingSubmissions.size();
            return Stats;
        }

        FArdaRHIStatus FArdaVulkanProviderDevice::WaitForIdle()
        {
            try
            {
                {
                    std::lock_guard<std::mutex> Lock(mContext->mQueueMutex);
                    mContext->mQueue.waitIdle();
                    if (mContext->mComputeQueue &&
                        mContext->mComputeQueue != mContext->mQueue)
                        mContext->mComputeQueue.waitIdle();
                    if (mContext->mCopyQueue &&
                        mContext->mCopyQueue != mContext->mQueue &&
                        mContext->mCopyQueue != mContext->mComputeQueue)
                        mContext->mCopyQueue.waitIdle();
                }
                std::lock_guard<std::mutex> Lock(mSubmissionMutex);
                for (const FPendingSubmission& Submission : mPendingSubmissions)
                    if (Submission.mFence)
                        mContext->mDevice.destroyFence(Submission.mFence);
                mPendingSubmissions.clear();
                return {};
            }
            catch (const vk::SystemError& Error)
            {
                return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
            }
        }

        class FArdaVulkanSwapChain final : public IArdaSwapChain
        {
        public:
            FArdaVulkanSwapChain(eastl::shared_ptr<FArdaVulkanContext> Context,
                eastl::shared_ptr<FArdaVulkanProviderDevice> ProviderDevice,
                FArdaRHIDeviceRef ArdaDevice, uint32_t Width, uint32_t Height)
                : mContext(eastl::move(Context)), mProviderDevice(eastl::move(ProviderDevice)),
                  mArdaDevice(eastl::move(ArdaDevice)), mWidth(Width), mHeight(Height) {}
            ~FArdaVulkanSwapChain() override
            {
                WaitForIdle();
                ReleaseResources();
                if (mAcquireFence) mContext->mDevice.destroyFence(mAcquireFence);
                if (mSwapchain) mContext->mDevice.destroySwapchainKHR(mSwapchain);
            }

            bool Initialize()
            {
                try
                {
                    mAcquireFence = mContext->mDevice.createFence({});
                    return CreateResources();
                }
                catch (const vk::SystemError& Error)
                {
                    mError = Error.what();
                    return false;
                }
            }

            bool Resize(uint32_t Width, uint32_t Height) override
            {
                if (Width == 0 || Height == 0 || (Width == mWidth && Height == mHeight))
                    return true;
                WaitForIdle();
                ReleaseResources();
                if (mSwapchain)
                {
                    mContext->mDevice.destroySwapchainKHR(mSwapchain);
                    mSwapchain = nullptr;
                }
                mWidth = Width;
                mHeight = Height;
                const bool bCreated = CreateResources();
                if (bCreated && mCustomPresent)
                    mCustomPresent->OnBackBufferResize(mWidth, mHeight);
                return bCreated;
            }

            bool AcquireFrame(FArdaRHIFramebufferRef& OutFramebuffer) override
            {
                OutFramebuffer = nullptr;
                try
                {
                    const auto ResetResult = mContext->mDevice.resetFences(1, &mAcquireFence);
                    if (ResetResult != vk::Result::eSuccess)
                    {
                        mError = VulkanFailure("Failed to reset the Vulkan acquisition fence.",
                            ResetResult).mMessage;
                        return false;
                    }
                    const auto Acquired = mContext->mDevice.acquireNextImageKHR(
                        mSwapchain, UINT64_MAX, {}, mAcquireFence);
                    if (Acquired.result != vk::Result::eSuccess &&
                        Acquired.result != vk::Result::eSuboptimalKHR)
                    {
                        mError = VulkanFailure("Failed to acquire a Vulkan swap-chain image.",
                            Acquired.result).mMessage;
                        return false;
                    }
                    const auto WaitResult = mContext->mDevice.waitForFences(
                        1, &mAcquireFence, true, UINT64_MAX);
                    if (WaitResult != vk::Result::eSuccess)
                    {
                        mError = VulkanFailure("Failed to wait for Vulkan image acquisition.",
                            WaitResult).mMessage;
                        return false;
                    }
                    mImageIndex = Acquired.value;
                    OutFramebuffer = mFramebuffers[mImageIndex];
                    mError.clear();
                    return static_cast<bool>(OutFramebuffer);
                }
                catch (const vk::OutOfDateKHRError&)
                {
                    mError = "The Vulkan swap chain is out of date.";
                    return false;
                }
                catch (const vk::SystemError& Error)
                {
                    mError = Error.what();
                    return false;
                }
            }

            void PrepareSubmit() override {}

            bool Present() override
            {
                try
                {
                    if (mImageIndex >= mRenderFinished.size())
                    {
                        mError = "The Vulkan swap-chain image has no presentation semaphore.";
                        return false;
                    }
                    if (mCustomPresent)
                    {
                        if (!mCustomPresent->Present(
                                FArdaNativeObject(reinterpret_cast<uintptr_t>(
                                    mTextures[mImageIndex]
                                        ? mTextures[mImageIndex]->
                                            GetPhysicalIdentity()
                                        : nullptr)),
                                mWidth, mHeight))
                        {
                            mError =
                                "The Vulkan custom-present callback failed.";
                            return false;
                        }
                        if (!mCustomPresent->NeedsNativePresent())
                        {
                            mCustomPresent->PostPresent();
                            mError.clear();
                            return true;
                        }
                    }
                    const vk::Semaphore RenderFinished =
                        mRenderFinished[mImageIndex];
                    vk::SubmitInfo Signal;
                    Signal.signalSemaphoreCount = 1;
                    Signal.pSignalSemaphores = &RenderFinished;
                    vk::PresentInfoKHR Info;
                    Info.waitSemaphoreCount = 1;
                    Info.pWaitSemaphores = &RenderFinished;
                    Info.swapchainCount = 1;
                    Info.pSwapchains = &mSwapchain;
                    Info.pImageIndices = &mImageIndex;
                    std::lock_guard<std::mutex> Lock(mContext->mQueueMutex);
                    mContext->mQueue.submit(Signal, {});
                    const vk::Result Result = mContext->mQueue.presentKHR(Info);
                    if (Result != vk::Result::eSuccess && Result != vk::Result::eSuboptimalKHR)
                    {
                        mError = VulkanFailure("Failed to present the Vulkan swap chain.", Result).mMessage;
                        return false;
                    }
                    if (mCustomPresent) mCustomPresent->PostPresent();
                    mError.clear();
                    return true;
                }
                catch (const vk::OutOfDateKHRError&)
                {
                    mError = "The Vulkan swap chain is out of date.";
                    return false;
                }
                catch (const vk::SystemError& Error)
                {
                    mError = Error.what();
                    return false;
                }
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
            EArdaRHIFormat GetFormat() const noexcept override { return mFormat; }
            uint32_t GetWidth() const noexcept override { return mWidth; }
            uint32_t GetHeight() const noexcept override { return mHeight; }
            const eastl::string& GetError() const noexcept override { return mError; }

        private:
            bool CreateResources()
            {
                try
                {
                    const auto Capabilities = mContext->mPhysicalDevice.getSurfaceCapabilitiesKHR(
                        mContext->mSurface);
                    const auto Formats = mContext->mPhysicalDevice.getSurfaceFormatsKHR(
                        mContext->mSurface);
                    if (Formats.empty())
                    {
                        mError = "The Vulkan surface exposes no presentation formats.";
                        return false;
                    }
                    vk::SurfaceFormatKHR SurfaceFormat = Formats.front();
                    for (const auto& Candidate : Formats)
                        if (Candidate.format == vk::Format::eB8G8R8A8Unorm &&
                            Candidate.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
                        {
                            SurfaceFormat = Candidate;
                            break;
                        }
                    mFormat = SurfaceFormat.format == vk::Format::eB8G8R8A8Unorm
                        ? EArdaRHIFormat::BGRA8UNorm : EArdaRHIFormat::RGBA8UNorm;
                    vk::Extent2D Extent(mWidth, mHeight);
                    if (Capabilities.currentExtent.width != UINT32_MAX)
                        Extent = Capabilities.currentExtent;
                    else
                    {
                        Extent.width = eastl::max(Capabilities.minImageExtent.width,
                            eastl::min(Capabilities.maxImageExtent.width, Extent.width));
                        Extent.height = eastl::max(Capabilities.minImageExtent.height,
                            eastl::min(Capabilities.maxImageExtent.height, Extent.height));
                    }
                    mWidth = Extent.width;
                    mHeight = Extent.height;
                    uint32_t ImageCount = eastl::max(2u, Capabilities.minImageCount);
                    if (Capabilities.maxImageCount) ImageCount = eastl::min(ImageCount,
                        Capabilities.maxImageCount);
                    vk::SwapchainCreateInfoKHR Info;
                    Info.surface = mContext->mSurface;
                    Info.minImageCount = ImageCount;
                    Info.imageFormat = SurfaceFormat.format;
                    Info.imageColorSpace = SurfaceFormat.colorSpace;
                    Info.imageExtent = Extent;
                    Info.imageArrayLayers = 1;
                    Info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment |
                        vk::ImageUsageFlagBits::eTransferDst;
                    Info.imageSharingMode = vk::SharingMode::eExclusive;
                    Info.preTransform = Capabilities.currentTransform;
                    Info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
                    Info.presentMode = vk::PresentModeKHR::eFifo;
                    Info.clipped = true;
                    mSwapchain = mContext->mDevice.createSwapchainKHR(Info);
                    const auto Images = mContext->mDevice.getSwapchainImagesKHR(mSwapchain);
                    mFramebuffers.resize(Images.size());
                    mTextures.resize(Images.size());
                    mRenderFinished.resize(Images.size());
                    for (size_t Index = 0; Index < Images.size(); ++Index)
                    {
                        mRenderFinished[Index] =
                            mContext->mDevice.createSemaphore({});
                        FArdaRHINativeTextureImportDesc Import;
#if VK_USE_64_BIT_PTR_DEFINES
                        Import.mNativeObject = reinterpret_cast<uintptr_t>(
                            static_cast<VkImage>(Images[Index]));
#else
                        Import.mNativeObject = static_cast<uintptr_t>(
                            static_cast<VkImage>(Images[Index]));
#endif
                        Import.mNativeType = EArdaRHINativeResourceType::VulkanImage;
                        Import.mOwnership = EArdaRHINativeOwnership::Borrowed;
                        Import.mInitialState = EArdaRHIResourceState::Present;
                        Import.mTexture.mWidth = mWidth;
                        Import.mTexture.mHeight = mHeight;
                        Import.mTexture.mFormat = mFormat;
                        Import.mTexture.mUsage = EArdaRHITextureUsage::RenderTarget;
                        Import.mTexture.mInitialState = EArdaRHIResourceState::Present;
                        Import.mTexture.mbKeepInitialState = true;
                        Import.mTexture.mDebugName = "Vulkan swap-chain image";
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
                catch (const vk::SystemError& Error)
                {
                    mError = Error.what();
                    ReleaseResources();
                    return false;
                }
            }

            void ReleaseResources()
            {
                for (auto& Framebuffer : mFramebuffers) Framebuffer = nullptr;
                mFramebuffers.clear();
                mTextures.clear();
                for (vk::Semaphore Semaphore : mRenderFinished)
                    if (Semaphore) mContext->mDevice.destroySemaphore(Semaphore);
                mRenderFinished.clear();
                if (mArdaDevice) mArdaDevice->TrimDescriptorCaches();
            }

            eastl::shared_ptr<FArdaVulkanContext> mContext;
            eastl::shared_ptr<FArdaVulkanProviderDevice> mProviderDevice;
            FArdaRHIDeviceRef mArdaDevice;
            vk::SwapchainKHR mSwapchain;
            vk::Fence mAcquireFence;
            eastl::vector<FArdaRHIFramebufferRef> mFramebuffers;
            eastl::vector<FArdaRHITextureRef> mTextures;
            eastl::vector<vk::Semaphore> mRenderFinished;
            EArdaRHIFormat mFormat = EArdaRHIFormat::BGRA8UNorm;
            uint32_t mWidth = 0;
            uint32_t mHeight = 0;
            uint32_t mImageIndex = 0;
            eastl::shared_ptr<IArdaCustomPresent> mCustomPresent;
            eastl::string mError;
        };

        class FArdaVulkanBackendRuntime final : public IArdaBackendRuntime
        {
        public:
            ~FArdaVulkanBackendRuntime() override
            {
                if (mProviderDevice) (void)mProviderDevice->WaitForIdle();
                mProviderDevice.reset();
                if (mContext)
                    mContext->mDiagnosticCallback.store(
                        nullptr, std::memory_order_release);
                mContext.reset();
            }

            static FArdaBackendDeviceCreateResult Create(
                const FArdaBackendConfiguration& Configuration,
                IArdaWindowSurface* WindowSurface,
                const IArdaExternalDeviceProvider* ExternalProvider)
            {
                FArdaBackendDeviceCreateResult Result;
                auto Runtime = eastl::make_unique<FArdaVulkanBackendRuntime>();
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
                if (!mContext || !mContext->mSurface)
                {
                    Result.mError =
                        "Vulkan presentation was not initialized with a window surface.";
                    return Result;
                }
                if (!Device)
                {
                    Result.mError =
                        "Vulkan presentation requires the core RHI device.";
                    return Result;
                }
                auto SwapChain = eastl::make_unique<FArdaVulkanSwapChain>(
                    mContext, mProviderDevice, eastl::move(Device), Width, Height);
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
                const IArdaExternalDeviceProvider*)
            {
                if (Configuration.mDeviceSource == EArdaDeviceSource::ExternalProvider)
                {
                    mError = "The native Vulkan module does not adopt external devices yet.";
                    return EArdaInitializeResult::Failure;
                }
                mContext = eastl::make_shared<FArdaVulkanContext>();
                mContext->mDiagnosticCallback.store(
                    Configuration.mMessageCallback,
                    std::memory_order_release);
                try
                {
                    const auto GetInstanceProcAddr = mContext->mLoader.getProcAddress<
                        PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
                    if (!GetInstanceProcAddr)
                    {
                        mError = "The Vulkan loader is unavailable.";
                        return EArdaInitializeResult::Unavailable;
                    }
                    VULKAN_HPP_DEFAULT_DISPATCHER.init(GetInstanceProcAddr);
                    uint32_t LoaderVersion = VK_API_VERSION_1_0;
                    if (VULKAN_HPP_DEFAULT_DISPATCHER.vkEnumerateInstanceVersion)
                        LoaderVersion = vk::enumerateInstanceVersion();
                    if (LoaderVersion < VK_API_VERSION_1_3)
                    {
                        mError = "Vulkan 1.3 or newer is required by the native Vulkan 1.4 backend.";
                        return EArdaInitializeResult::Unavailable;
                    }
                    eastl::vector<const char*> Extensions;
                    if (WindowSurface) Extensions = WindowSurface->GetVulkanInstanceExtensions();
                    eastl::vector<const char*> Layers;
                    bool bValidationLayerEnabled = false;
                    bool bDebugUtilsEnabled = false;
                    if (Configuration.mbEnableValidation)
                    {
                        for (const auto& Layer : vk::enumerateInstanceLayerProperties())
                            if (std::strcmp(Layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
                            {
                                Layers.push_back("VK_LAYER_KHRONOS_validation");
                                bValidationLayerEnabled = true;
                                break;
                            }
                        if (!bValidationLayerEnabled && Configuration.mMessageCallback)
                        {
                            Configuration.mMessageCallback->Message(
                                EArdaDiagnosticSeverity::Warning,
                                "VK_LAYER_KHRONOS_validation is unavailable; continuing without Vulkan validation.");
                        }
                        if (bValidationLayerEnabled)
                        {
                            for (const auto& Extension :
                                 vk::enumerateInstanceExtensionProperties())
                            {
                                if (std::strcmp(
                                        Extension.extensionName,
                                        VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
                                {
                                    bDebugUtilsEnabled = true;
                                    break;
                                }
                            }
                            if (bDebugUtilsEnabled)
                            {
                                const bool bAlreadyEnabled = eastl::any_of(
                                    Extensions.begin(),
                                    Extensions.end(),
                                    [](const char* Extension)
                                    {
                                        return Extension &&
                                            std::strcmp(
                                                Extension,
                                                VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
                                    });
                                if (!bAlreadyEnabled)
                                    Extensions.push_back(
                                        VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                            }
                            else if (Configuration.mMessageCallback)
                            {
                                Configuration.mMessageCallback->Message(
                                    EArdaDiagnosticSeverity::Warning,
                                    "VK_EXT_debug_utils is unavailable; Vulkan validation messages cannot be forwarded.");
                            }
                        }
                    }
                    vk::DebugUtilsMessengerCreateInfoEXT DebugInfo;
                    DebugInfo.messageSeverity =
                        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
                    DebugInfo.messageType =
                        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
                    DebugInfo.pfnUserCallback = VulkanDebugCallback;
                    DebugInfo.pUserData = &mContext->mDiagnosticCallback;
                    vk::ApplicationInfo Application("Ardashir", VK_MAKE_VERSION(0, 1, 0),
                        "Ardashir native Vulkan", VK_MAKE_VERSION(0, 1, 0),
                        eastl::min(LoaderVersion, VK_API_VERSION_1_4));
                    vk::InstanceCreateInfo InstanceInfo;
                    InstanceInfo.pNext = bDebugUtilsEnabled ? &DebugInfo : nullptr;
                    InstanceInfo.pApplicationInfo = &Application;
                    InstanceInfo.enabledExtensionCount = static_cast<uint32_t>(Extensions.size());
                    InstanceInfo.ppEnabledExtensionNames = Extensions.data();
                    InstanceInfo.enabledLayerCount = static_cast<uint32_t>(Layers.size());
                    InstanceInfo.ppEnabledLayerNames = Layers.data();
                    mContext->mInstance = vk::createInstance(InstanceInfo);
                    VULKAN_HPP_DEFAULT_DISPATCHER.init(mContext->mInstance);
                    if (bDebugUtilsEnabled)
                        mContext->mDebugMessenger =
                            mContext->mInstance.createDebugUtilsMessengerEXT(
                                DebugInfo);
                    if (WindowSurface)
                    {
                        eastl::string SurfaceError;
                        const FArdaNativeObject Surface = WindowSurface->CreateVulkanSurface(
                            FArdaNativeObject(static_cast<VkInstance>(mContext->mInstance)),
                            SurfaceError);
                        mContext->mSurface = vk::SurfaceKHR(Surface.As<VkSurfaceKHR>());
                        if (!mContext->mSurface)
                        {
                            mError = SurfaceError.empty()
                                ? "Failed to create the Vulkan presentation surface." : SurfaceError;
                            return EArdaInitializeResult::Failure;
                        }
                    }

                    int SelectedScore = -1;
                    for (const auto& Physical : mContext->mInstance.enumeratePhysicalDevices())
                    {
                        const auto QueueFamilies = Physical.getQueueFamilyProperties();
                        for (uint32_t Index = 0; Index < QueueFamilies.size(); ++Index)
                        {
                            if (!(QueueFamilies[Index].queueFlags & vk::QueueFlagBits::eGraphics))
                                continue;
                            if (mContext->mSurface && !Physical.getSurfaceSupportKHR(
                                Index, mContext->mSurface)) continue;
                            vk::PhysicalDeviceVulkan13Features Vulkan13;
                            vk::PhysicalDeviceFeatures2 Features;
                            Features.pNext = &Vulkan13;
                            Physical.getFeatures2(&Features);
                            if (!Vulkan13.dynamicRendering || !Vulkan13.synchronization2)
                                continue;
                            const auto Properties = Physical.getProperties();
                            const int Score =
                                Properties.deviceType ==
                                    vk::PhysicalDeviceType::eDiscreteGpu
                                    ? 100 : 10;
                            if (Score > SelectedScore)
                            {
                                SelectedScore = Score;
                                mContext->mPhysicalDevice = Physical;
                                mContext->mQueueFamily = Index;
                            }
                            break;
                        }
                    }
                    if (!mContext->mPhysicalDevice)
                    {
                        mError = "No Vulkan adapter supports graphics, dynamic rendering, and synchronization2.";
                        return EArdaInitializeResult::Unavailable;
                    }
                    const auto SelectedQueueFamilies =
                        mContext->mPhysicalDevice.getQueueFamilyProperties();
                    mContext->mComputeQueueFamily = mContext->mQueueFamily;
                    mContext->mCopyQueueFamily = mContext->mQueueFamily;
                    for (uint32_t Index = 0;
                         Index < SelectedQueueFamilies.size(); ++Index)
                    {
                        const auto Flags = SelectedQueueFamilies[Index].queueFlags;
                        if ((Flags & vk::QueueFlagBits::eCompute) &&
                            !(Flags & vk::QueueFlagBits::eGraphics))
                        {
                            mContext->mComputeQueueFamily = Index;
                            break;
                        }
                    }
                    for (uint32_t Index = 0;
                         Index < SelectedQueueFamilies.size(); ++Index)
                    {
                        const auto Flags = SelectedQueueFamilies[Index].queueFlags;
                        if ((Flags & vk::QueueFlagBits::eTransfer) &&
                            !(Flags & vk::QueueFlagBits::eGraphics) &&
                            !(Flags & vk::QueueFlagBits::eCompute))
                        {
                            mContext->mCopyQueueFamily = Index;
                            break;
                        }
                    }
                    uint32_t GraphicsQueueIndex = 0;
                    uint32_t ComputeQueueIndex = 0;
                    uint32_t CopyQueueIndex = 0;
                    uint32_t GraphicsQueueCount = 1;
                    if (mContext->mComputeQueueFamily == mContext->mQueueFamily &&
                        SelectedQueueFamilies[mContext->mQueueFamily].queueCount > 1)
                    {
                        ComputeQueueIndex = 1;
                        GraphicsQueueCount = 2;
                    }
                    if (mContext->mCopyQueueFamily == mContext->mQueueFamily &&
                        SelectedQueueFamilies[mContext->mQueueFamily].queueCount > 2)
                    {
                        CopyQueueIndex = 2;
                        GraphicsQueueCount = 3;
                    }
                    else if (mContext->mCopyQueueFamily ==
                             mContext->mComputeQueueFamily &&
                             mContext->mCopyQueueFamily != mContext->mQueueFamily &&
                             SelectedQueueFamilies[mContext->mCopyQueueFamily].queueCount > 1)
                    {
                        CopyQueueIndex = 1;
                    }
                    mContext->mQueueCount = GraphicsQueueCount;
                    mContext->mQueueSparseBinding[GetArdaRHIQueueIndex(
                        EArdaRHIQueueType::Graphics)] =
                        static_cast<bool>(SelectedQueueFamilies[
                            mContext->mQueueFamily].queueFlags &
                            vk::QueueFlagBits::eSparseBinding);
                    mContext->mQueueSparseBinding[GetArdaRHIQueueIndex(
                        EArdaRHIQueueType::Compute)] =
                        static_cast<bool>(SelectedQueueFamilies[
                            mContext->mComputeQueueFamily].queueFlags &
                            vk::QueueFlagBits::eSparseBinding);
                    mContext->mQueueSparseBinding[GetArdaRHIQueueIndex(
                        EArdaRHIQueueType::Copy)] =
                        static_cast<bool>(SelectedQueueFamilies[
                            mContext->mCopyQueueFamily].queueFlags &
                            vk::QueueFlagBits::eSparseBinding);
                    eastl::array<float, ArdaRHIQueueTypeCount> Priorities{};
                    Priorities.fill(1.f);
                    eastl::vector<vk::DeviceQueueCreateInfo> QueueInfos;
                    QueueInfos.emplace_back(vk::DeviceQueueCreateFlags{},
                        mContext->mQueueFamily, GraphicsQueueCount,
                        Priorities.data());
                    if (mContext->mComputeQueueFamily != mContext->mQueueFamily)
                    {
                        const uint32_t Count =
                            mContext->mCopyQueueFamily ==
                                mContext->mComputeQueueFamily &&
                            CopyQueueIndex == 1 ? 2u : 1u;
                        QueueInfos.emplace_back(vk::DeviceQueueCreateFlags{},
                            mContext->mComputeQueueFamily, Count,
                            Priorities.data());
                    }
                    if (mContext->mCopyQueueFamily != mContext->mQueueFamily &&
                        mContext->mCopyQueueFamily !=
                            mContext->mComputeQueueFamily)
                    {
                        QueueInfos.emplace_back(vk::DeviceQueueCreateFlags{},
                            mContext->mCopyQueueFamily, 1,
                            Priorities.data());
                    }
                    const auto AvailableExtensions =
                        mContext->mPhysicalDevice.enumerateDeviceExtensionProperties();
                    const auto HasExtension = [&AvailableExtensions](
                        const char* Name)
                    {
                        return eastl::any_of(
                            AvailableExtensions.begin(), AvailableExtensions.end(),
                            [Name](const vk::ExtensionProperties& Extension)
                            {
                                return std::strcmp(
                                    Extension.extensionName, Name) == 0;
                            });
                    };

                    vk::PhysicalDeviceVulkan13Features Supported13;
                    vk::PhysicalDeviceTimelineSemaphoreFeatures SupportedTimeline;
                    vk::PhysicalDeviceDescriptorIndexingFeatures SupportedIndexing;
                    vk::PhysicalDeviceBufferDeviceAddressFeatures SupportedAddress;
                    vk::PhysicalDeviceAccelerationStructureFeaturesKHR SupportedAS;
                    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR SupportedRT;
                    vk::PhysicalDeviceRayQueryFeaturesKHR SupportedRayQuery;
                    vk::PhysicalDeviceMeshShaderFeaturesEXT SupportedMesh;
                    vk::PhysicalDeviceOpacityMicromapFeaturesEXT SupportedMicromap;
                    vk::PhysicalDeviceDescriptorBufferFeaturesEXT SupportedDescriptorBuffer;
                    vk::PhysicalDeviceDescriptorHeapFeaturesEXT SupportedDescriptorHeap;
                    vk::PhysicalDeviceFeatures2 SupportedFeatures;
                    SupportedFeatures.pNext = &Supported13;
                    Supported13.pNext = &SupportedTimeline;
                    SupportedTimeline.pNext = &SupportedIndexing;
                    SupportedIndexing.pNext = &SupportedAddress;
                    SupportedAddress.pNext = &SupportedAS;
                    SupportedAS.pNext = &SupportedRT;
                    SupportedRT.pNext = &SupportedRayQuery;
                    SupportedRayQuery.pNext = &SupportedMesh;
                    SupportedMesh.pNext = &SupportedMicromap;
                    SupportedMicromap.pNext = &SupportedDescriptorBuffer;
                    SupportedDescriptorBuffer.pNext = &SupportedDescriptorHeap;
                    mContext->mPhysicalDevice.getFeatures2(&SupportedFeatures);

                    const bool bDeferredHost = HasExtension(
                        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
                    mContext->mbAccelerationStructure =
                        bDeferredHost &&
                        HasExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
                        SupportedAS.accelerationStructure &&
                        SupportedAddress.bufferDeviceAddress;
                    mContext->mbBufferDeviceAddress =
                        SupportedAddress.bufferDeviceAddress;
                    mContext->mbRayTracingPipeline =
                        mContext->mbAccelerationStructure &&
                        HasExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
                        SupportedRT.rayTracingPipeline;
                    mContext->mbRayQuery =
                        mContext->mbAccelerationStructure &&
                        HasExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
                        SupportedRayQuery.rayQuery;
                    mContext->mbMeshShader =
                        HasExtension(VK_EXT_MESH_SHADER_EXTENSION_NAME) &&
                        SupportedMesh.meshShader;
                    mContext->mbOpacityMicromap =
                        mContext->mbAccelerationStructure &&
                        HasExtension(VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME) &&
                        SupportedMicromap.micromap;
                    mContext->mbDescriptorIndexing =
                        SupportedIndexing.runtimeDescriptorArray &&
                        SupportedIndexing.descriptorBindingPartiallyBound &&
                        SupportedIndexing.descriptorBindingVariableDescriptorCount;
                    mContext->mbDescriptorBuffer =
                        HasExtension(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME) &&
                        SupportedDescriptorBuffer.descriptorBuffer;
                    mContext->mbDescriptorHeap =
                        HasExtension(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME) &&
                        SupportedDescriptorHeap.descriptorHeap;
                    mContext->mbMemoryBudget =
                        HasExtension(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
                    mContext->mDescriptorIndexingFeatures = SupportedIndexing;
                    mContext->mDescriptorIndexingFeatures.pNext = nullptr;
                    mContext->mRayTracingPipelineFeatures = SupportedRT;
                    mContext->mRayTracingPipelineFeatures.pNext = nullptr;
                    mContext->mMeshShaderFeatures = SupportedMesh;
                    mContext->mMeshShaderFeatures.pNext = nullptr;

                    vk::PhysicalDeviceVulkan13Features Vulkan13;
                    Vulkan13.synchronization2 = true;
                    Vulkan13.dynamicRendering = true;
                    vk::PhysicalDeviceTimelineSemaphoreFeatures TimelineFeatures;
                    TimelineFeatures.timelineSemaphore =
                        SupportedTimeline.timelineSemaphore;
                    vk::PhysicalDeviceDescriptorIndexingFeatures Indexing;
                    Indexing.shaderSampledImageArrayNonUniformIndexing =
                        SupportedIndexing.shaderSampledImageArrayNonUniformIndexing;
                    Indexing.shaderStorageImageArrayNonUniformIndexing =
                        SupportedIndexing.shaderStorageImageArrayNonUniformIndexing;
                    Indexing.shaderStorageBufferArrayNonUniformIndexing =
                        SupportedIndexing.shaderStorageBufferArrayNonUniformIndexing;
                    Indexing.runtimeDescriptorArray =
                        SupportedIndexing.runtimeDescriptorArray;
                    Indexing.descriptorBindingPartiallyBound =
                        SupportedIndexing.descriptorBindingPartiallyBound;
                    Indexing.descriptorBindingVariableDescriptorCount =
                        SupportedIndexing.descriptorBindingVariableDescriptorCount;
                    Indexing.descriptorBindingSampledImageUpdateAfterBind =
                        SupportedIndexing.descriptorBindingSampledImageUpdateAfterBind;
                    Indexing.descriptorBindingStorageImageUpdateAfterBind =
                        SupportedIndexing.descriptorBindingStorageImageUpdateAfterBind;
                    Indexing.descriptorBindingStorageBufferUpdateAfterBind =
                        SupportedIndexing.descriptorBindingStorageBufferUpdateAfterBind;
                    Indexing.descriptorBindingUniformBufferUpdateAfterBind =
                        SupportedIndexing.descriptorBindingUniformBufferUpdateAfterBind;
                    Indexing.descriptorBindingUpdateUnusedWhilePending =
                        SupportedIndexing.descriptorBindingUpdateUnusedWhilePending;
                    vk::PhysicalDeviceBufferDeviceAddressFeatures Address;
                    Address.bufferDeviceAddress =
                        SupportedAddress.bufferDeviceAddress;
                    vk::PhysicalDeviceAccelerationStructureFeaturesKHR AS;
                    AS.accelerationStructure =
                        mContext->mbAccelerationStructure;
                    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR RT;
                    RT.rayTracingPipeline = mContext->mbRayTracingPipeline;
                    RT.rayTracingPipelineTraceRaysIndirect =
                        mContext->mbRayTracingPipeline &&
                        SupportedRT.rayTracingPipelineTraceRaysIndirect;
                    vk::PhysicalDeviceRayQueryFeaturesKHR RayQuery;
                    RayQuery.rayQuery = mContext->mbRayQuery;
                    vk::PhysicalDeviceMeshShaderFeaturesEXT Mesh;
                    Mesh.taskShader = mContext->mbMeshShader &&
                        SupportedMesh.taskShader;
                    Mesh.meshShader = mContext->mbMeshShader;
                    vk::PhysicalDeviceOpacityMicromapFeaturesEXT Micromap;
                    Micromap.micromap = mContext->mbOpacityMicromap;
                    vk::PhysicalDeviceDescriptorBufferFeaturesEXT DescriptorBuffer;
                    DescriptorBuffer.descriptorBuffer =
                        mContext->mbDescriptorBuffer;
                    vk::PhysicalDeviceDescriptorHeapFeaturesEXT DescriptorHeap;
                    DescriptorHeap.descriptorHeap = mContext->mbDescriptorHeap;
                    Vulkan13.pNext = &TimelineFeatures;
                    TimelineFeatures.pNext = &Indexing;
                    Indexing.pNext = &Address;
                    Address.pNext = &AS;
                    AS.pNext = &RT;
                    RT.pNext = &RayQuery;
                    RayQuery.pNext = &Mesh;
                    Mesh.pNext = &Micromap;
                    Micromap.pNext = &DescriptorBuffer;
                    DescriptorBuffer.pNext = &DescriptorHeap;
                    const auto Supported = mContext->mPhysicalDevice.getFeatures();
                    vk::PhysicalDeviceFeatures Enabled;
                    Enabled.fillModeNonSolid = Supported.fillModeNonSolid;
                    Enabled.samplerAnisotropy = Supported.samplerAnisotropy;
                    Enabled.geometryShader = Supported.geometryShader;
                    Enabled.tessellationShader = Supported.tessellationShader;
                    Enabled.sparseBinding = Supported.sparseBinding;
                    Enabled.sparseResidencyBuffer =
                        Supported.sparseResidencyBuffer;
                    Enabled.sparseResidencyImage2D =
                        Supported.sparseResidencyImage2D;
                    Enabled.sparseResidencyImage3D =
                        Supported.sparseResidencyImage3D;
                    Enabled.sparseResidencyAliased =
                        Supported.sparseResidencyAliased;
                    eastl::vector<const char*> DeviceExtensions;
                    const auto AddExtension = [&DeviceExtensions](const char* Name)
                    {
                        if (eastl::find(DeviceExtensions.begin(),
                                DeviceExtensions.end(), Name) ==
                            DeviceExtensions.end())
                            DeviceExtensions.push_back(Name);
                    };
                    if (mContext->mSurface)
                        AddExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
                    if (mContext->mbAccelerationStructure)
                    {
                        AddExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
                        AddExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
                    }
                    if (mContext->mbRayTracingPipeline)
                        AddExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
                    if (mContext->mbRayQuery)
                        AddExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME);
                    if (mContext->mbMeshShader)
                        AddExtension(VK_EXT_MESH_SHADER_EXTENSION_NAME);
                    if (mContext->mbOpacityMicromap)
                        AddExtension(VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME);
                    if (mContext->mbDescriptorBuffer)
                        AddExtension(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
                    if (mContext->mbDescriptorHeap)
                        AddExtension(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME);
                    if (mContext->mbMemoryBudget)
                        AddExtension(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
                    if (HasExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME))
                        AddExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
                    if (HasExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
                        AddExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
                    vk::DeviceCreateInfo DeviceInfo;
                    DeviceInfo.pNext = &Vulkan13;
                    DeviceInfo.queueCreateInfoCount =
                        static_cast<uint32_t>(QueueInfos.size());
                    DeviceInfo.pQueueCreateInfos = QueueInfos.data();
                    DeviceInfo.enabledExtensionCount =
                        static_cast<uint32_t>(DeviceExtensions.size());
                    DeviceInfo.ppEnabledExtensionNames = DeviceExtensions.data();
                    DeviceInfo.pEnabledFeatures = &Enabled;
                    mContext->mDevice = mContext->mPhysicalDevice.createDevice(DeviceInfo);
                    VULKAN_HPP_DEFAULT_DISPATCHER.init(mContext->mDevice);
                    vk::PhysicalDeviceProperties2 Properties;
                    Properties.pNext = &mContext->mRayTracingPipelineProperties;
                    mContext->mRayTracingPipelineProperties.pNext =
                        &mContext->mAccelerationStructureProperties;
                    mContext->mAccelerationStructureProperties.pNext =
                        &mContext->mMeshShaderProperties;
                    mContext->mMeshShaderProperties.pNext =
                        &mContext->mDescriptorHeapProperties;
                    mContext->mPhysicalDevice.getProperties2(&Properties);
                    mContext->mQueue = mContext->mDevice.getQueue(
                        mContext->mQueueFamily, GraphicsQueueIndex);
                    mContext->mComputeQueue = mContext->mDevice.getQueue(
                        mContext->mComputeQueueFamily, ComputeQueueIndex);
                    mContext->mCopyQueue = mContext->mDevice.getQueue(
                        mContext->mCopyQueueFamily, CopyQueueIndex);
                    mProviderDevice = eastl::make_shared<FArdaVulkanProviderDevice>(
                        mContext, Configuration.mPipelineCacheDirectory,
                        Configuration.mMessageCallback);
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
                catch (const vk::SystemError& Error)
                {
                    mError = Error.what();
                    return EArdaInitializeResult::Unavailable;
                }
            }

            eastl::string mError;
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            eastl::shared_ptr<FArdaVulkanProviderDevice> mProviderDevice;
        };

        class FArdaVulkanBackendModule final : public IArdaBackendModule
        {
        public:
            FArdaVulkanBackendModule()
            {
                mDescriptor.mName = "native-vulkan";
                mDescriptor.mDisplayName = "Native Vulkan 1.4 (headers 1.4.357)";
                mDescriptor.mBackendType = EArdaBackendType::Vulkan;
                mDescriptor.mShaderBinaryFormat = EArdaShaderBinaryFormat::Spirv;
                mDescriptor.mShaderArtifactExtension = ".spv";
                mDescriptor.mShaderCompilerIdentity = "dxc-vulkan1.3-native-v2";
                mDescriptor.mbSupportsOwnedDevice = true;
                mDescriptor.mbSupportsExternalDevice = false;
                mDescriptor.mPriority = 200;
            }
            const FArdaBackendModuleDescriptor& GetDescriptor() const noexcept override
            {
                return mDescriptor;
            }
            FArdaBackendDeviceCreateResult CreateDevice(
                const FArdaBackendConfiguration& Configuration,
                IArdaWindowSurface* WindowSurface,
                const IArdaExternalDeviceProvider* ExternalProvider) override
            {
                return FArdaVulkanBackendRuntime::Create(
                    Configuration, WindowSurface, ExternalProvider);
            }
            FArdaRHIStatus ConfigureShaderCompileInvocation(
                FArdaBackendShaderCompileInvocation& Invocation) const override
            {
                Invocation.mArguments.push_back("-spirv");
                Invocation.mArguments.push_back("-fspv-target-env=vulkan1.3");
                using Stage = EArdaRHIShaderStage;
                if (IsArdaRHIRayTracingShaderStage(Invocation.mStage))
                    Invocation.mArguments.push_back("-fspv-extension=SPV_KHR_ray_tracing");
                if (Invocation.mStage == Stage::Amplification ||
                    Invocation.mStage == Stage::Mesh)
                    Invocation.mArguments.push_back(
                        "-fspv-extension=SPV_EXT_mesh_shader");
                const auto AddShift = [&Invocation](
                    const char* Flag,
                    const char* Value,
                    uint32_t Space)
                {
                    Invocation.mArguments.push_back(Flag);
                    Invocation.mArguments.push_back(Value);
                    const std::string SpaceText = std::to_string(Space);
                    Invocation.mArguments.push_back(SpaceText.c_str());
                };
                // DXC applies each shift to one register space only. Emit the
                // namespace-preserving mapping for every descriptor-set index
                // the native backend accepts so t/s/b/u never alias outside
                // space zero.
                for (uint32_t Space = 0; Space < 32; ++Space)
                {
                    AddShift("-fvk-t-shift", "0", Space);
                    AddShift("-fvk-s-shift", "128", Space);
                    AddShift("-fvk-b-shift", "256", Space);
                    AddShift("-fvk-u-shift", "384", Space);
                }
                return {};
            }
        private:
            FArdaBackendModuleDescriptor mDescriptor;
        };

        FArdaVulkanBackendModule& GetVulkanModule()
        {
            static FArdaVulkanBackendModule Module;
            return Module;
        }
    }

    bool RegisterArdaVulkanBackend()
    {
        return RegisterBackendModule(GetVulkanModule());
    }
}
