#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include "Common/ArdaNativeRHI.h"
#include "Common/ArdaNativePipelineCache.h"

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
        using namespace rhi::native;

        constexpr uint32_t ArdaVulkanHeaderVersion = VK_HEADER_VERSION;

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

        uint32_t FormatSize(EArdaRHIFormat Format) noexcept
        {
            switch (Format)
            {
            case EArdaRHIFormat::R8UInt: case EArdaRHIFormat::R8SInt:
            case EArdaRHIFormat::R8UNorm: case EArdaRHIFormat::R8SNorm: return 1;
            case EArdaRHIFormat::RG8UInt: case EArdaRHIFormat::RG8SInt:
            case EArdaRHIFormat::RG8UNorm: case EArdaRHIFormat::RG8SNorm:
            case EArdaRHIFormat::R16UInt: case EArdaRHIFormat::R16SInt:
            case EArdaRHIFormat::R16UNorm: case EArdaRHIFormat::R16SNorm:
            case EArdaRHIFormat::R16Float: case EArdaRHIFormat::D16: return 2;
            case EArdaRHIFormat::RGBA8UInt: case EArdaRHIFormat::RGBA8SInt:
            case EArdaRHIFormat::RGBA8UNorm: case EArdaRHIFormat::RGBA8SNorm:
            case EArdaRHIFormat::BGRA8UNorm: case EArdaRHIFormat::SRGBA8UNorm:
            case EArdaRHIFormat::SBGRA8UNorm: case EArdaRHIFormat::R10G10B10A2UNorm:
            case EArdaRHIFormat::R11G11B10Float: case EArdaRHIFormat::RG16UInt:
            case EArdaRHIFormat::RG16SInt: case EArdaRHIFormat::RG16UNorm:
            case EArdaRHIFormat::RG16SNorm: case EArdaRHIFormat::RG16Float:
            case EArdaRHIFormat::R32UInt: case EArdaRHIFormat::R32SInt:
            case EArdaRHIFormat::R32Float: case EArdaRHIFormat::D24S8:
            case EArdaRHIFormat::D32: return 4;
            case EArdaRHIFormat::RGBA16UInt: case EArdaRHIFormat::RGBA16SInt:
            case EArdaRHIFormat::RGBA16Float: case EArdaRHIFormat::RGBA16UNorm:
            case EArdaRHIFormat::RGBA16SNorm: case EArdaRHIFormat::RG32UInt:
            case EArdaRHIFormat::RG32SInt: case EArdaRHIFormat::RG32Float:
            case EArdaRHIFormat::D32S8: return 8;
            case EArdaRHIFormat::RGB32UInt: case EArdaRHIFormat::RGB32SInt:
            case EArdaRHIFormat::RGB32Float: return 12;
            case EArdaRHIFormat::RGBA32UInt: case EArdaRHIFormat::RGBA32SInt:
            case EArdaRHIFormat::RGBA32Float: return 16;
            default: return 0;
            }
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
            return Result ? Result : vk::ShaderStageFlagBits::eAll;
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
            if (HasAnyFlags(State, EArdaRHIResourceState::UnorderedAccess)) return vk::ImageLayout::eGeneral;
            if (HasAnyFlags(State, EArdaRHIResourceState::ShaderResource))
                return bDepth ? vk::ImageLayout::eDepthStencilReadOnlyOptimal : vk::ImageLayout::eShaderReadOnlyOptimal;
            return vk::ImageLayout::eGeneral;
        }

        struct FArdaVulkanContext
        {
            ~FArdaVulkanContext()
            {
                if (mDevice)
                {
                    (void)mDevice.waitIdle();
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

            vk::detail::DynamicLoader mLoader;
            vk::Instance mInstance;
            vk::PhysicalDevice mPhysicalDevice;
            vk::Device mDevice;
            vk::Queue mQueue;
            vk::SurfaceKHR mSurface;
            vk::DebugUtilsMessengerEXT mDebugMessenger;
            vk::DescriptorPool mDescriptorPool;
            uint32_t mQueueFamily = 0;
            std::mutex mQueueMutex;
            std::mutex mDescriptorMutex;
            std::atomic<IArdaDiagnosticCallback*> mDiagnosticCallback{ nullptr };
            std::atomic<size_t> mAllocatedDescriptorSets{ 0 };
            std::atomic<uint64_t> mSubmission{ 0 };
        };

        class FVulkanTexture final : public IArdaNativeObject
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
            std::mutex mLayoutMutex;
            bool mbOwned = true;
        };

        class FVulkanBuffer final : public IArdaNativeObject
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
            bool mbOwned = true;
        };

        class FVulkanSampler final : public IArdaNativeObject
        {
        public:
            ~FVulkanSampler() override { if (mSampler) mContext->mDevice.destroySampler(mSampler); }
            const void* GetIdentity() const noexcept override { return this; }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            vk::Sampler mSampler;
        };

        class FVulkanShader final : public IArdaNativeObject
        {
        public:
            ~FVulkanShader() override { if (mModule) mContext->mDevice.destroyShaderModule(mModule); }
            const void* GetIdentity() const noexcept override { return this; }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            vk::ShaderModule mModule;
            EArdaRHIShaderStage mStage = EArdaRHIShaderStage::None;
            eastl::string mEntryPoint;
        };

        class FVulkanBindingLayout final : public IArdaNativeObject
        {
        public:
            ~FVulkanBindingLayout() override { if (mLayout) mContext->mDevice.destroyDescriptorSetLayout(mLayout); }
            const void* GetIdentity() const noexcept override { return this; }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            FArdaRHIBindingLayoutDesc mDesc;
            vk::DescriptorSetLayout mLayout;
        };

        class FVulkanBindingSet final : public IArdaNativeObject
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
            }
            const void* GetIdentity() const noexcept override { return this; }
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            vk::DescriptorSet mSet;
            FArdaNativeObjectRef mLayoutObject;
            eastl::vector<vk::ImageView> mOwnedViews;
            eastl::vector<FArdaNativeObjectRef> mRetainedObjects;
        };

        class FVulkanFramebuffer final : public IArdaNativeObject
        {
        public:
            const void* GetIdentity() const noexcept override { return this; }
            eastl::vector<FArdaNativeObjectRef> mColors;
            FArdaNativeObjectRef mDepth;
            vk::Extent2D mExtent;
        };

        class FVulkanPipeline final : public IArdaNativeObject
        {
        public:
            struct FDescriptorSetGroup
            {
                uint32_t mRegisterSpace = 0;
                vk::DescriptorSetLayout mLayout;
                eastl::vector<FArdaNativeObjectRef> mLogicalLayouts;
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
            eastl::vector<vk::DescriptorSetLayout> mOwnedSetLayouts;
            eastl::vector<FArdaNativeObjectRef> mRetainedLayouts;
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
            eastl::vector<FArdaNativeObjectRef> mRetainedObjects;
        };

        class FArdaVulkanApiDevice;

        class FArdaVulkanCommandList final : public IArdaNativeCommandList
        {
        public:
            explicit FArdaVulkanCommandList(FArdaVulkanApiDevice& Device) : mDevice(Device) {}
            ~FArdaVulkanCommandList() override;
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
            void SetAutomaticBarriers(bool Enabled) override { mbAutomaticBarriers = Enabled; }
            FArdaRHIStatus BeginTrackingTextureState(const FArdaNativeObjectRef&, const FArdaRHITextureDesc&, const FArdaRHITextureSubresourceRange&, EArdaRHIResourceState) override;
            FArdaRHIStatus BeginTrackingBufferState(const FArdaNativeObjectRef&, const FArdaRHIBufferDesc&, EArdaRHIResourceState) override { return {}; }
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
            vk::CommandBuffer GetCommandBuffer() const noexcept
            {
                return mRecording ? mRecording->mCommandBuffer : vk::CommandBuffer{};
            }
            eastl::shared_ptr<FVulkanCommandRecording> GetRecording() const
            {
                return mRecording;
            }

        private:
            FArdaRHIStatus CreateRecording();
            void Retain(const FArdaNativeObjectRef& Object)
            {
                if (Object && mRecording)
                    mRecording->mRetainedObjects.push_back(Object);
            }
            void EndRendering();
            void GlobalBarrier();
            eastl::vector<vk::ImageLayout>& GetTrackedTextureLayouts(
                FVulkanTexture&,
                const FArdaRHITextureDesc&);
            FArdaRHIStatus TransitionTextureLayout(
                const FArdaNativeObjectRef&,
                const FArdaRHITextureDesc&,
                const FArdaRHITextureSubresourceRange&,
                vk::ImageLayout);
            FArdaRHIStatus BindSets(const FVulkanPipeline&, const eastl::vector<FArdaNativeObjectRef>&, vk::PipelineBindPoint);
            FArdaVulkanApiDevice& mDevice;
            eastl::shared_ptr<FVulkanCommandRecording> mRecording;
            vk::CommandBuffer mCommandBuffer;
            FVulkanPipeline* mBoundGraphics = nullptr;
            FVulkanPipeline* mBoundCompute = nullptr;
            std::unordered_map<const void*, eastl::vector<vk::ImageLayout>>
                mTextureLayouts;
            bool mbOpen = false;
            bool mbRendering = false;
            bool mbAutomaticBarriers = true;
        };

        class FArdaVulkanApiDevice final : public IArdaNativeApiDevice
        {
        public:
            FArdaVulkanApiDevice(
                eastl::shared_ptr<FArdaVulkanContext> Context,
                std::filesystem::path PipelineCacheDirectory,
                IArdaDiagnosticCallback* DiagnosticCallback)
                : mContext(eastl::move(Context))
                , mPipelineCacheDirectory(eastl::move(PipelineCacheDirectory))
                , mDiagnosticCallback(DiagnosticCallback) {}
            ~FArdaVulkanApiDevice() override
            {
                (void)WaitForIdle();
                FlushPipelineCache();
            }
            FArdaRHIStatus Initialize();
            const FArdaRHICapabilities& GetCapabilities() const noexcept override { return mCapabilities; }
            EArdaRHINativeResourceType GetTextureImportType() const noexcept override { return EArdaRHINativeResourceType::VulkanImage; }
            EArdaRHINativeResourceType GetBufferImportType() const noexcept override { return EArdaRHINativeResourceType::VulkanBuffer; }
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
            void RunGarbageCollection() override;
            FArdaNativeLifetimeStats GetLifetimeStats() const noexcept override;
            void FlushPipelineCache() noexcept override;

            TArdaRHIResult<vk::PipelineLayout> CreatePipelineLayout(
                const eastl::vector<FArdaNativeObjectRef>&,
                FVulkanPipeline&);
            FArdaNativeObjectResult CreateMergedBindingSet(
                vk::DescriptorSetLayout,
                const eastl::vector<FArdaNativeObjectRef>&,
                const eastl::vector<FArdaNativeObjectRef>&);
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

        FArdaRHIStatus FArdaVulkanApiDevice::Initialize()
        {
            try
            {
                const eastl::array<vk::DescriptorPoolSize, 5> Sizes = {{
                    { vk::DescriptorType::eSampler, 2048 },
                    { vk::DescriptorType::eSampledImage, 8192 },
                    { vk::DescriptorType::eStorageImage, 4096 },
                    { vk::DescriptorType::eUniformBuffer, 4096 },
                    { vk::DescriptorType::eStorageBuffer, 8192 }
                }};
                vk::DescriptorPoolCreateInfo PoolInfo;
                PoolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
                PoolInfo.maxSets = 8192;
                PoolInfo.poolSizeCount = static_cast<uint32_t>(Sizes.size());
                PoolInfo.pPoolSizes = Sizes.data();
                mContext->mDescriptorPool = mContext->mDevice.createDescriptorPool(PoolInfo);

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
            mCapabilities.mbGraphicsQueue = true;
            mCapabilities.mbComputeQueue = false;
            mCapabilities.mbCopyQueue = false;
            mCapabilities.mbStagingTextures = true;
            mCapabilities.mbQueries = true;
            mCapabilities.mbHeaps = false;
            mCapabilities.mbBindless = false;
            mCapabilities.mbShaderLibraries = true;
            mCapabilities.mbPipelineCachePersistence = mPipelineCache != nullptr;
            return {};
        }

        FArdaNativeObjectResult FArdaVulkanApiDevice::CreateTexture(
            const FArdaRHITextureDesc& Desc)
        {
            try
            {
                const vk::Format Format = ToVulkan(Desc.mFormat);
                if (Format == vk::Format::eUndefined)
                    return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::Unsupported, "The Vulkan texture format is unsupported."));
                auto Texture = eastl::make_shared<FVulkanTexture>();
                Texture->mContext = mContext;
                Texture->mDesc = Desc;
                Texture->mLayouts.assign(
                    static_cast<size_t>(Desc.mMipLevels) * Desc.mArraySize,
                    vk::ImageLayout::eUndefined);
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
                Info.sharingMode = vk::SharingMode::eExclusive;
                Info.initialLayout = vk::ImageLayout::eUndefined;
                Texture->mImage = mContext->mDevice.createImage(Info);
                const auto Requirements = mContext->mDevice.getImageMemoryRequirements(Texture->mImage);
                const uint32_t MemoryType = mContext->FindMemoryType(
                    Requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
                if (MemoryType == UINT32_MAX)
                    return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
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
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaNativeObjectResult FArdaVulkanApiDevice::CreateBuffer(
            const FArdaRHIBufferDesc& Desc)
        {
            try
            {
                auto Buffer = eastl::make_shared<FVulkanBuffer>();
                Buffer->mContext = mContext;
                Buffer->mDesc = Desc;
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
                Info.sharingMode = vk::SharingMode::eExclusive;
                Buffer->mBuffer = mContext->mDevice.createBuffer(Info);
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
                    return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                        EArdaRHIResult::BackendFailure,
                        Desc.mCpuAccess == EArdaRHICpuAccess::None
                            ? "No Vulkan device-local buffer memory type is available."
                            : "No coherent host-visible Vulkan buffer memory type is available."));
                Buffer->mMemory = mContext->mDevice.allocateMemory(
                    vk::MemoryAllocateInfo(Requirements.size, MemoryType));
                mContext->mDevice.bindBufferMemory(Buffer->mBuffer, Buffer->mMemory, 0);
                return { Buffer, {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        TArdaRHIResult<void*> FArdaVulkanApiDevice::MapBuffer(
            const FArdaNativeObjectRef& Object,
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

        void FArdaVulkanApiDevice::UnmapBuffer(
            const FArdaNativeObjectRef& Object) noexcept
        {
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Object.get());
            if (!Buffer || !Buffer->mMemory) return;
            try { mContext->mDevice.unmapMemory(Buffer->mMemory); }
            catch (const vk::SystemError&) {}
        }

        FArdaNativeObjectResult FArdaVulkanApiDevice::ImportTexture(
            const FArdaRHINativeTextureImportDesc& Desc)
        {
            if (Desc.mNativeType != EArdaRHINativeResourceType::VulkanImage || !Desc.mNativeObject)
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument, "The Vulkan texture import requires a VkImage."));
            try
            {
                auto Texture = eastl::make_shared<FVulkanTexture>();
                Texture->mContext = mContext;
                Texture->mDesc = Desc.mTexture;
                Texture->mImage = vk::Image(reinterpret_cast<VkImage>(Desc.mNativeObject));
                Texture->mbOwned = Desc.mOwnership == EArdaRHINativeOwnership::Transferred;
                Texture->mLayouts.assign(
                    static_cast<size_t>(Desc.mTexture.mMipLevels) *
                        Desc.mTexture.mArraySize,
                    ToImageLayout(
                        Desc.mInitialState,
                        ImageAspect(Desc.mTexture.mFormat) !=
                            vk::ImageAspectFlagBits::eColor));
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
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaNativeObjectResult FArdaVulkanApiDevice::ImportBuffer(
            const FArdaRHINativeBufferImportDesc& Desc)
        {
            if (Desc.mNativeType != EArdaRHINativeResourceType::VulkanBuffer || !Desc.mNativeObject)
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument, "The Vulkan buffer import requires a VkBuffer."));
            auto Buffer = eastl::make_shared<FVulkanBuffer>();
            Buffer->mContext = mContext;
            Buffer->mDesc = Desc.mBuffer;
            Buffer->mBuffer = vk::Buffer(reinterpret_cast<VkBuffer>(Desc.mNativeObject));
            Buffer->mbOwned = Desc.mOwnership == EArdaRHINativeOwnership::Transferred;
            return { Buffer, {} };
        }

        FArdaNativeObjectResult FArdaVulkanApiDevice::CreateSampler(
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
                Sampler->mSampler = mContext->mDevice.createSampler(Info);
                return { Sampler, {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaNativeObjectResult FArdaVulkanApiDevice::CreateShader(
            const FArdaRHIShaderDesc& Desc)
        {
            if (!Desc.mBytecode || Desc.mBytecodeSize == 0 || (Desc.mBytecodeSize & 3u) != 0)
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
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
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaNativeObjectResult FArdaVulkanApiDevice::CreateBindingLayout(
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
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaNativeObjectResult FArdaVulkanApiDevice::CreateBindingSet(
            const FArdaRHIBindingSetDesc&,
            const FArdaNativeObjectRef& LayoutObject,
            const eastl::vector<FArdaNativeBinding>& Bindings)
        {
            auto* Layout = dynamic_cast<FVulkanBindingLayout*>(LayoutObject.get());
            if (!Layout)
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice, "Vulkan binding layout has the wrong implementation."));
            try
            {
                auto Set = eastl::make_shared<FVulkanBindingSet>();
                Set->mContext = mContext;
                Set->mLayoutObject = LayoutObject;
                vk::DescriptorSetAllocateInfo Allocate;
                Allocate.descriptorPool = mContext->mDescriptorPool;
                Allocate.descriptorSetCount = 1;
                Allocate.pSetLayouts = &Layout->mLayout;
                {
                    std::lock_guard<std::mutex> Lock(mContext->mDescriptorMutex);
                    Set->mSet = mContext->mDevice.allocateDescriptorSets(Allocate).front();
                    mContext->mAllocatedDescriptorSets.fetch_add(
                        1, std::memory_order_relaxed);
                }
                Set->mRetainedObjects.push_back(LayoutObject);
                for (const auto& Binding : Bindings) Set->mRetainedObjects.push_back(Binding.mObject);

                eastl::vector<vk::WriteDescriptorSet> Writes;
                eastl::vector<vk::DescriptorImageInfo> Images;
                eastl::vector<vk::DescriptorBufferInfo> Buffers;
                Writes.reserve(Bindings.size());
                Images.reserve(Bindings.size());
                Buffers.reserve(Bindings.size());
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
                    else
                    {
                        return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::InvalidArgument, "A Vulkan binding has the wrong resource type."));
                    }
                    Writes.push_back(Write);
                }
                // Repoint after vector growth so every write references stable storage.
                size_t ImageIndex = 0, BufferIndex = 0;
                for (size_t Index = 0; Index < Bindings.size(); ++Index)
                {
                    if (dynamic_cast<FVulkanBuffer*>(Bindings[Index].mObject.get()))
                        Writes[Index].pBufferInfo = &Buffers[BufferIndex++];
                    else
                        Writes[Index].pImageInfo = &Images[ImageIndex++];
                }
                mContext->mDevice.updateDescriptorSets(
                    static_cast<uint32_t>(Writes.size()), Writes.data(), 0, nullptr);
                return { Set, {} };
            }
            catch (const vk::SystemError& Error)
            {
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaNativeObjectResult FArdaVulkanApiDevice::CreateMergedBindingSet(
            vk::DescriptorSetLayout TargetLayout,
            const eastl::vector<FArdaNativeObjectRef>& LogicalLayouts,
            const eastl::vector<FArdaNativeObjectRef>& BindingSets)
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
                        return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                            EArdaRHIResult::WrongDevice,
                            "A merged Vulkan layout has the wrong implementation."));
                    FVulkanBindingSet* Source = nullptr;
                    FArdaNativeObjectRef SourceObject;
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
                            return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
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
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaNativeObjectResult FArdaVulkanApiDevice::CreateFramebuffer(
            const FArdaNativeFramebufferCreateInfo& Info)
        {
            auto Result = eastl::make_shared<FVulkanFramebuffer>();
            for (const auto& Target : Info.mColors)
            {
                auto* Texture = dynamic_cast<FVulkanTexture*>(Target.mTexture.get());
                if (!Texture) return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice, "Vulkan framebuffer texture has the wrong implementation."));
                Result->mColors.push_back(Target.mTexture);
                Result->mExtent = vk::Extent2D(Texture->mDesc.mWidth, Texture->mDesc.mHeight);
            }
            if (Info.mDepth.mTexture)
            {
                auto* Texture = dynamic_cast<FVulkanTexture*>(Info.mDepth.mTexture.get());
                if (!Texture) return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice, "Vulkan depth texture has the wrong implementation."));
                Result->mDepth = Info.mDepth.mTexture;
                Result->mExtent = vk::Extent2D(Texture->mDesc.mWidth, Texture->mDesc.mHeight);
            }
            return { Result, {} };
        }

        TArdaRHIResult<vk::PipelineLayout> FArdaVulkanApiDevice::CreatePipelineLayout(
            const eastl::vector<FArdaNativeObjectRef>& LayoutObjects,
            FVulkanPipeline& Pipeline)
        {
            Pipeline.mPushStages = {};
            Pipeline.mPushSize = 0;
            uint32_t MaximumSpace = 0;
            for (const auto& Object : LayoutObjects)
            {
                auto* Layout = dynamic_cast<FVulkanBindingLayout*>(Object.get());
                if (!Layout) return Fail<vk::PipelineLayout>(FArdaRHIStatus::Error(
                    EArdaRHIResult::WrongDevice, "Vulkan pipeline layout has the wrong implementation."));
                MaximumSpace = eastl::max(MaximumSpace, Layout->mDesc.mRegisterSpace);
                Pipeline.mRetainedLayouts.push_back(Object);
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
            if (!LayoutObjects.empty() && MaximumSpace >= MaximumBoundSets)
                return Fail<vk::PipelineLayout>(FArdaRHIStatus::Error(
                    EArdaRHIResult::InvalidArgument,
                    "A Vulkan binding layout register space exceeds maxBoundDescriptorSets."));

            eastl::vector<eastl::vector<FArdaNativeObjectRef>> Groups(
                LayoutObjects.empty() ? 0u : MaximumSpace + 1u);
            for (const auto& Object : LayoutObjects)
            {
                auto* Layout = static_cast<FVulkanBindingLayout*>(Object.get());
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

        FArdaNativeObjectResult FArdaVulkanApiDevice::CreateGraphicsPipeline(
            const FArdaNativeGraphicsPipelineCreateInfo& Info)
        {
            try
            {
                auto Pipeline = eastl::make_shared<FVulkanPipeline>();
                Pipeline->mContext = mContext;
                auto Layout = CreatePipelineLayout(Info.mBindingLayouts, *Pipeline);
                if (!Layout) return Fail<FArdaNativeObjectRef>(eastl::move(Layout.mStatus));
                Pipeline->mLayout = Layout.mValue;

                eastl::vector<vk::PipelineShaderStageCreateInfo> Stages;
                const auto AddStage = [&Stages](const FArdaNativeObjectRef& Object,
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
                    return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
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
                                Attribute.mOffset + Element * FormatSize(Attribute.mFormat)));
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

                vk::GraphicsPipelineCreateInfo Native;
                Native.pNext = &Rendering;
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
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaNativeObjectResult FArdaVulkanApiDevice::CreateComputePipeline(
            const FArdaNativeComputePipelineCreateInfo& Info)
        {
            auto* Shader = dynamic_cast<FVulkanShader*>(Info.mComputeShader.get());
            if (!Shader) return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                EArdaRHIResult::InvalidArgument, "A Vulkan compute pipeline requires a compute shader."));
            try
            {
                auto Pipeline = eastl::make_shared<FVulkanPipeline>();
                Pipeline->mContext = mContext;
                auto Layout = CreatePipelineLayout(Info.mBindingLayouts, *Pipeline);
                if (!Layout) return Fail<FArdaNativeObjectRef>(eastl::move(Layout.mStatus));
                Pipeline->mLayout = Layout.mValue;
                vk::ComputePipelineCreateInfo Native;
                Native.stage = vk::PipelineShaderStageCreateInfo({},
                    vk::ShaderStageFlagBits::eCompute, Shader->mModule,
                    Shader->mEntryPoint.c_str());
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
                return Fail<FArdaNativeObjectRef>(FArdaRHIStatus::Error(
                    EArdaRHIResult::BackendFailure, Error.what()));
            }
        }

        FArdaVulkanCommandList::~FArdaVulkanCommandList()
        {
        }

        void FArdaVulkanApiDevice::FlushPipelineCache() noexcept
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
                vk::CommandPoolCreateInfo PoolInfo(
                    vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                    Context->mQueueFamily);
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
                mTextureLayouts.clear();
                mbAutomaticBarriers = true;
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
            const FArdaNativeObjectRef& Object, const FArdaRHIBufferDesc& Desc,
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
                            Object, Desc, Desc.mInitialState); !Status)
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
            const FArdaNativeObjectRef& Destination, uint64_t DestinationOffset,
            const FArdaNativeObjectRef& Source, uint64_t SourceOffset, uint64_t Size)
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

        eastl::vector<vk::ImageLayout>&
        FArdaVulkanCommandList::GetTrackedTextureLayouts(
            FVulkanTexture& Texture,
            const FArdaRHITextureDesc& Desc)
        {
            auto Existing = mTextureLayouts.find(Texture.GetIdentity());
            if (Existing != mTextureLayouts.end())
                return Existing->second;
            eastl::vector<vk::ImageLayout> Layouts;
            {
                std::lock_guard<std::mutex> Lock(Texture.mLayoutMutex);
                Layouts = Texture.mLayouts;
            }
            const size_t SubresourceCount =
                static_cast<size_t>(Desc.mMipLevels) * Desc.mArraySize;
            if (Layouts.size() != SubresourceCount)
                Layouts.assign(SubresourceCount, vk::ImageLayout::eUndefined);
            return mTextureLayouts.emplace(
                Texture.GetIdentity(), eastl::move(Layouts)).first->second;
        }

        FArdaRHIStatus FArdaVulkanCommandList::TransitionTextureLayout(
            const FArdaNativeObjectRef& Object, const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& InputRange,
            vk::ImageLayout NewLayout)
        {
            auto* Texture = dynamic_cast<FVulkanTexture*>(Object.get());
            if (!Texture) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "Vulkan texture transition has the wrong resource type.");
            Retain(Object);
            EndRendering();
            const auto Range = InputRange.Resolve(Desc);
            auto& Layouts = GetTrackedTextureLayouts(*Texture, Desc);
            eastl::vector<vk::ImageMemoryBarrier2> Barriers;
            Barriers.reserve(
                static_cast<size_t>(Range.mMipLevelCount) *
                Range.mArraySliceCount);
            for (uint32_t ArraySlice = Range.mBaseArraySlice;
                 ArraySlice < Range.mBaseArraySlice + Range.mArraySliceCount;
                 ++ArraySlice)
            {
                for (uint32_t MipLevel = Range.mBaseMipLevel;
                     MipLevel < Range.mBaseMipLevel + Range.mMipLevelCount;
                     ++MipLevel)
                {
                    const size_t Index =
                        static_cast<size_t>(ArraySlice) * Desc.mMipLevels +
                        MipLevel;
                    const vk::ImageLayout OldLayout = Layouts[Index];
                    if (OldLayout == NewLayout)
                        continue;
                    vk::ImageMemoryBarrier2 Barrier;
                    Barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
                    Barrier.srcAccessMask = vk::AccessFlagBits2::eMemoryRead |
                        vk::AccessFlagBits2::eMemoryWrite;
                    Barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
                    Barrier.dstAccessMask = vk::AccessFlagBits2::eMemoryRead |
                        vk::AccessFlagBits2::eMemoryWrite;
                    Barrier.oldLayout = OldLayout;
                    Barrier.newLayout = NewLayout;
                    Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    Barrier.image = Texture->mImage;
                    Barrier.subresourceRange = vk::ImageSubresourceRange(
                        ImageAspect(Desc.mFormat), MipLevel, 1, ArraySlice, 1);
                    Barriers.push_back(Barrier);
                    Layouts[Index] = NewLayout;
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
            {
                std::lock_guard<std::mutex> Lock(Texture->mLayoutMutex);
                if (Texture->mLayouts.size() == Layouts.size())
                {
                    for (uint32_t ArraySlice = Range.mBaseArraySlice;
                         ArraySlice < Range.mBaseArraySlice + Range.mArraySliceCount;
                         ++ArraySlice)
                    {
                        for (uint32_t MipLevel = Range.mBaseMipLevel;
                             MipLevel < Range.mBaseMipLevel + Range.mMipLevelCount;
                             ++MipLevel)
                        {
                            Texture->mLayouts[
                                static_cast<size_t>(ArraySlice) *
                                    Desc.mMipLevels + MipLevel] = NewLayout;
                        }
                    }
                }
            }
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::SetTextureState(
            const FArdaNativeObjectRef& Object, const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& InputRange,
            EArdaRHIResourceState State)
        {
            return TransitionTextureLayout(
                Object,
                Desc,
                InputRange,
                ToImageLayout(
                    State,
                    ImageAspect(Desc.mFormat) !=
                        vk::ImageAspectFlagBits::eColor));
        }

        FArdaRHIStatus FArdaVulkanCommandList::SetBufferState(
            const FArdaNativeObjectRef& Object, const FArdaRHIBufferDesc& Desc,
            EArdaRHIResourceState)
        {
            auto* Buffer = dynamic_cast<FVulkanBuffer*>(Object.get());
            if (!Buffer) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "Vulkan buffer transition has the wrong resource type.");
            Retain(Object);
            EndRendering();
            vk::BufferMemoryBarrier2 Barrier;
            Barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            Barrier.srcAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
            Barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
            Barrier.dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
            Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            Barrier.buffer = Buffer->mBuffer;
            Barrier.offset = 0;
            Barrier.size = Desc.mByteSize;
            vk::DependencyInfo Dependency;
            Dependency.bufferMemoryBarrierCount = 1;
            Dependency.pBufferMemoryBarriers = &Barrier;
            mCommandBuffer.pipelineBarrier2(Dependency);
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::BeginTrackingTextureState(
            const FArdaNativeObjectRef& Object, const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& InputRange,
            EArdaRHIResourceState State)
        {
            auto* Texture = dynamic_cast<FVulkanTexture*>(Object.get());
            if (!Texture) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "Vulkan texture tracking has the wrong resource type.");
            const auto Range = InputRange.Resolve(Desc);
            auto& Layouts = GetTrackedTextureLayouts(*Texture, Desc);
            const vk::ImageLayout Layout = ToImageLayout(
                State,
                ImageAspect(Desc.mFormat) != vk::ImageAspectFlagBits::eColor);
            for (uint32_t ArraySlice = Range.mBaseArraySlice;
                 ArraySlice < Range.mBaseArraySlice + Range.mArraySliceCount;
                 ++ArraySlice)
            {
                for (uint32_t MipLevel = Range.mBaseMipLevel;
                     MipLevel < Range.mBaseMipLevel + Range.mMipLevelCount;
                     ++MipLevel)
                {
                    const size_t Index =
                        static_cast<size_t>(ArraySlice) * Desc.mMipLevels +
                        MipLevel;
                    // Optimal-tiled Vulkan images are physically created in
                    // UNDEFINED even when the cross-API descriptor names a
                    // logical initial state. Keep that native fact until the
                    // first real layout transition is recorded.
                    if (Layouts[Index] != vk::ImageLayout::eUndefined)
                        Layouts[Index] = Layout;
                }
            }
            return {};
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
            const FArdaNativeObjectRef&, bool Enabled)
        {
            if (Enabled) GlobalBarrier();
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::SetUAVBarriersForBuffer(
            const FArdaNativeObjectRef&, bool Enabled)
        {
            if (Enabled) GlobalBarrier();
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::ClearTexture(
            const FArdaNativeObjectRef& Object, const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& InputRange, const FArdaRHIColor& Color)
        {
            auto* Texture = dynamic_cast<FVulkanTexture*>(Object.get());
            if (!Texture) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "Vulkan texture clear has the wrong resource type.");
            const auto Range = InputRange.Resolve(Desc);
            auto& Layouts = GetTrackedTextureLayouts(*Texture, Desc);
            eastl::vector<vk::ImageLayout> PreviousLayouts;
            PreviousLayouts.reserve(
                static_cast<size_t>(Range.mMipLevelCount) *
                Range.mArraySliceCount);
            for (uint32_t ArraySlice = Range.mBaseArraySlice;
                 ArraySlice < Range.mBaseArraySlice + Range.mArraySliceCount;
                 ++ArraySlice)
                for (uint32_t MipLevel = Range.mBaseMipLevel;
                     MipLevel < Range.mBaseMipLevel + Range.mMipLevelCount;
                     ++MipLevel)
                    PreviousLayouts.push_back(Layouts[
                        static_cast<size_t>(ArraySlice) * Desc.mMipLevels +
                        MipLevel]);
            if (auto Status = TransitionTextureLayout(
                    Object,
                    Desc,
                    InputRange,
                    vk::ImageLayout::eTransferDstOptimal);
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
            for (uint32_t ArraySlice = Range.mBaseArraySlice;
                 ArraySlice < Range.mBaseArraySlice + Range.mArraySliceCount;
                 ++ArraySlice)
            {
                for (uint32_t MipLevel = Range.mBaseMipLevel;
                     MipLevel < Range.mBaseMipLevel + Range.mMipLevelCount;
                     ++MipLevel, ++PreviousIndex)
                {
                    const vk::ImageLayout Previous =
                        PreviousLayouts[PreviousIndex];
                    if (Previous == vk::ImageLayout::eUndefined ||
                        Previous == vk::ImageLayout::eTransferDstOptimal)
                        continue;
                    if (auto Status = TransitionTextureLayout(
                            Object,
                            Desc,
                            { MipLevel, 1, ArraySlice, 1 },
                            Previous);
                        !Status)
                        return Status;
                }
            }
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::ClearDepthStencilTexture(
            const FArdaNativeObjectRef& Object, const FArdaRHITextureDesc& Desc,
            const FArdaRHITextureSubresourceRange& InputRange, bool bClearDepth,
            float Depth, bool bClearStencil, uint8_t Stencil)
        {
            auto* Texture = dynamic_cast<FVulkanTexture*>(Object.get());
            if (!Texture) return FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "Vulkan depth clear has the wrong resource type.");
            const auto Range = InputRange.Resolve(Desc);
            auto& Layouts = GetTrackedTextureLayouts(*Texture, Desc);
            eastl::vector<vk::ImageLayout> PreviousLayouts;
            PreviousLayouts.reserve(
                static_cast<size_t>(Range.mMipLevelCount) *
                Range.mArraySliceCount);
            for (uint32_t ArraySlice = Range.mBaseArraySlice;
                 ArraySlice < Range.mBaseArraySlice + Range.mArraySliceCount;
                 ++ArraySlice)
                for (uint32_t MipLevel = Range.mBaseMipLevel;
                     MipLevel < Range.mBaseMipLevel + Range.mMipLevelCount;
                     ++MipLevel)
                    PreviousLayouts.push_back(Layouts[
                        static_cast<size_t>(ArraySlice) * Desc.mMipLevels +
                        MipLevel]);
            if (auto Status = TransitionTextureLayout(
                    Object,
                    Desc,
                    InputRange,
                    vk::ImageLayout::eTransferDstOptimal);
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
            for (uint32_t ArraySlice = Range.mBaseArraySlice;
                 ArraySlice < Range.mBaseArraySlice + Range.mArraySliceCount;
                 ++ArraySlice)
            {
                for (uint32_t MipLevel = Range.mBaseMipLevel;
                     MipLevel < Range.mBaseMipLevel + Range.mMipLevelCount;
                     ++MipLevel, ++PreviousIndex)
                {
                    const vk::ImageLayout Previous =
                        PreviousLayouts[PreviousIndex];
                    if (Previous == vk::ImageLayout::eUndefined ||
                        Previous == vk::ImageLayout::eTransferDstOptimal)
                        continue;
                    if (auto Status = TransitionTextureLayout(
                            Object,
                            Desc,
                            { MipLevel, 1, ArraySlice, 1 },
                            Previous);
                        !Status)
                        return Status;
                }
            }
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::BindSets(
            const FVulkanPipeline& Pipeline,
            const eastl::vector<FArdaNativeObjectRef>& Objects,
            vk::PipelineBindPoint BindPoint)
        {
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
                            [Set](const FArdaNativeObjectRef& Layout)
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
                FArdaNativeObjectRef BoundObject;
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
            const FArdaNativeGraphicsState& State)
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
            return {};
        }

        FArdaRHIStatus FArdaVulkanCommandList::SetComputeState(
            const FArdaNativeComputeState& State)
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
            return {};
        }

        void FArdaVulkanCommandList::SetPushConstants(const void* Data, size_t Size)
        {
            const FVulkanPipeline* Pipeline = mBoundGraphics ? mBoundGraphics : mBoundCompute;
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

        void FArdaVulkanCommandList::Dispatch(uint32_t X, uint32_t Y, uint32_t Z)
        {
            mCommandBuffer.dispatch(X, Y, Z);
        }

        TArdaRHIResult<eastl::unique_ptr<IArdaNativeCommandList>>
        FArdaVulkanApiDevice::CreateCommandList(EArdaRHIQueueType, bool)
        {
            auto Commands = eastl::make_unique<FArdaVulkanCommandList>(*this);
            if (auto Status = Commands->Initialize(); !Status)
                return Fail<eastl::unique_ptr<IArdaNativeCommandList>>(eastl::move(Status));
            return { eastl::unique_ptr<IArdaNativeCommandList>(Commands.release()), {} };
        }

        TArdaRHIResult<uint64_t> FArdaVulkanApiDevice::ExecuteCommandList(
            IArdaNativeCommandList& CommandList, EArdaRHIQueueType)
        {
            auto* Commands = dynamic_cast<FArdaVulkanCommandList*>(&CommandList);
            if (!Commands) return Fail<uint64_t>(FArdaRHIStatus::Error(
                EArdaRHIResult::WrongDevice, "Vulkan command list has the wrong implementation."));
            vk::Fence Fence;
            try
            {
                Fence = mContext->mDevice.createFence({});
                vk::SubmitInfo Submit;
                const vk::CommandBuffer Buffer = Commands->GetCommandBuffer();
                Submit.commandBufferCount = 1;
                Submit.pCommandBuffers = &Buffer;
                {
                    std::lock_guard<std::mutex> Lock(mContext->mQueueMutex);
                    mContext->mQueue.submit(Submit, Fence);
                }
                {
                    std::lock_guard<std::mutex> Lock(mSubmissionMutex);
                    const uint64_t Value =
                        mContext->mSubmission.fetch_add(
                            1, std::memory_order_relaxed) + 1;
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

        FArdaRHIStatus FArdaVulkanApiDevice::WaitForSubmission(uint64_t Value)
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

        void FArdaVulkanApiDevice::RunGarbageCollection()
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

        FArdaNativeLifetimeStats
        FArdaVulkanApiDevice::GetLifetimeStats() const noexcept
        {
            FArdaNativeLifetimeStats Stats;
            Stats.mDescriptorSets = mContext->mAllocatedDescriptorSets.load(
                std::memory_order_relaxed);
            std::lock_guard<std::mutex> Lock(mSubmissionMutex);
            Stats.mPendingSubmissions = mPendingSubmissions.size();
            return Stats;
        }

        FArdaRHIStatus FArdaVulkanApiDevice::WaitForIdle()
        {
            try
            {
                {
                    std::lock_guard<std::mutex> Lock(mContext->mQueueMutex);
                    mContext->mQueue.waitIdle();
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
                eastl::shared_ptr<FArdaVulkanApiDevice> ApiDevice,
                FArdaRHIDeviceRef ArdaDevice, uint32_t Width, uint32_t Height)
                : mContext(eastl::move(Context)), mApiDevice(eastl::move(ApiDevice)),
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
                return CreateResources();
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

            void WaitForIdle() noexcept override
            {
                if (mApiDevice) (void)mApiDevice->WaitForIdle();
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
                for (vk::Semaphore Semaphore : mRenderFinished)
                    if (Semaphore) mContext->mDevice.destroySemaphore(Semaphore);
                mRenderFinished.clear();
                if (mArdaDevice) mArdaDevice->TrimDescriptorCaches();
            }

            eastl::shared_ptr<FArdaVulkanContext> mContext;
            eastl::shared_ptr<FArdaVulkanApiDevice> mApiDevice;
            FArdaRHIDeviceRef mArdaDevice;
            vk::SwapchainKHR mSwapchain;
            vk::Fence mAcquireFence;
            eastl::vector<FArdaRHIFramebufferRef> mFramebuffers;
            eastl::vector<vk::Semaphore> mRenderFinished;
            EArdaRHIFormat mFormat = EArdaRHIFormat::BGRA8UNorm;
            uint32_t mWidth = 0;
            uint32_t mHeight = 0;
            uint32_t mImageIndex = 0;
            eastl::string mError;
        };

        class FArdaVulkanBackendDevice final : public IArdaBackendDevice
        {
        public:
            ~FArdaVulkanBackendDevice() override
            {
                WaitForIdle();
                mArdaDevice = nullptr;
                mApiDevice.reset();
                if (mContext)
                    mContext->mDiagnosticCallback.store(
                        nullptr, std::memory_order_release);
                mContext.reset();
            }

            EArdaInitializeResult Initialize(
                const FArdaBackendConfiguration& Configuration,
                IArdaWindowSurface* WindowSurface,
                const IArdaExternalDeviceProvider*) override
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
                            mContext->mPhysicalDevice = Physical;
                            mContext->mQueueFamily = Index;
                            break;
                        }
                        if (mContext->mPhysicalDevice) break;
                    }
                    if (!mContext->mPhysicalDevice)
                    {
                        mError = "No Vulkan adapter supports graphics, dynamic rendering, and synchronization2.";
                        return EArdaInitializeResult::Unavailable;
                    }
                    const float Priority = 1.f;
                    vk::DeviceQueueCreateInfo QueueInfo({}, mContext->mQueueFamily, 1, &Priority);
                    vk::PhysicalDeviceVulkan13Features Vulkan13;
                    Vulkan13.synchronization2 = true;
                    Vulkan13.dynamicRendering = true;
                    const auto Supported = mContext->mPhysicalDevice.getFeatures();
                    vk::PhysicalDeviceFeatures Enabled;
                    Enabled.fillModeNonSolid = Supported.fillModeNonSolid;
                    Enabled.samplerAnisotropy = Supported.samplerAnisotropy;
                    Enabled.geometryShader = Supported.geometryShader;
                    Enabled.tessellationShader = Supported.tessellationShader;
                    const char* SwapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
                    vk::DeviceCreateInfo DeviceInfo;
                    DeviceInfo.pNext = &Vulkan13;
                    DeviceInfo.queueCreateInfoCount = 1;
                    DeviceInfo.pQueueCreateInfos = &QueueInfo;
                    DeviceInfo.enabledExtensionCount = mContext->mSurface ? 1u : 0u;
                    DeviceInfo.ppEnabledExtensionNames = mContext->mSurface ? &SwapchainExtension : nullptr;
                    DeviceInfo.pEnabledFeatures = &Enabled;
                    mContext->mDevice = mContext->mPhysicalDevice.createDevice(DeviceInfo);
                    VULKAN_HPP_DEFAULT_DISPATCHER.init(mContext->mDevice);
                    mContext->mQueue = mContext->mDevice.getQueue(mContext->mQueueFamily, 0);
                    mApiDevice = eastl::make_shared<FArdaVulkanApiDevice>(
                        mContext, Configuration.mPipelineCacheDirectory,
                        Configuration.mMessageCallback);
                    if (auto Status = mApiDevice->Initialize(); !Status)
                    {
                        mError = Status.mMessage;
                        return EArdaInitializeResult::Failure;
                    }
                    mArdaDevice = CreateArdaNativeRHIDevice(mApiDevice);
                    if (!mArdaDevice)
                    {
                        mError = "Failed to create the native Vulkan RHI facade.";
                        return EArdaInitializeResult::Failure;
                    }
                    mQueues.mbGraphics = true;
                    mQueues.mbCompute = false;
                    mQueues.mbCopy = false;
                    mError.clear();
                    return EArdaInitializeResult::Success;
                }
                catch (const vk::SystemError& Error)
                {
                    mError = Error.what();
                    return EArdaInitializeResult::Unavailable;
                }
            }

            eastl::unique_ptr<IArdaSwapChain> CreateSwapChain(
                uint32_t Width, uint32_t Height) override
            {
                if (!mContext || !mContext->mSurface)
                {
                    mError = "Vulkan presentation was not initialized with a window surface.";
                    return {};
                }
                auto Result = eastl::make_unique<FArdaVulkanSwapChain>(
                    mContext, mApiDevice, mArdaDevice, Width, Height);
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
            eastl::string mError;
            eastl::shared_ptr<FArdaVulkanContext> mContext;
            eastl::shared_ptr<FArdaVulkanApiDevice> mApiDevice;
            FArdaRHIDeviceRef mArdaDevice;
            FArdaQueueCapabilities mQueues;
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
            eastl::unique_ptr<IArdaBackendDevice> CreateDevice(
                EArdaDeviceSource Source) override
            {
                return Source == EArdaDeviceSource::ArdaCreated
                    ? eastl::make_unique<FArdaVulkanBackendDevice>() : nullptr;
            }
            FArdaRHIStatus ConfigureShaderCompileInvocation(
                FArdaBackendShaderCompileInvocation& Invocation) const override
            {
                Invocation.mArguments.push_back("-spirv");
                Invocation.mArguments.push_back("-fspv-target-env=vulkan1.3");
                using Stage = EArdaRHIShaderStage;
                if (Invocation.mStage == Stage::RayGeneration || Invocation.mStage == Stage::AnyHit ||
                    Invocation.mStage == Stage::ClosestHit || Invocation.mStage == Stage::Miss ||
                    Invocation.mStage == Stage::Intersection || Invocation.mStage == Stage::Callable)
                    Invocation.mArguments.push_back("-fspv-extension=SPV_KHR_ray_tracing");
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
