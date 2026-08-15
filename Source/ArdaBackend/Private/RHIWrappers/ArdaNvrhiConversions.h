#pragma once

#include "RHIWrappers/ArdaRHI.h"

#include <nvrhi/nvrhi.h>

namespace arda::rhi::private_impl
{
    [[nodiscard]] nvrhi::Format ToNvrhi(EArdaRHIFormat Value) noexcept;
    [[nodiscard]] nvrhi::TextureDimension ToNvrhi(EArdaRHITextureDimension Value) noexcept;
    [[nodiscard]] nvrhi::ResourceStates ToNvrhi(EArdaRHIResourceState Value) noexcept;
    [[nodiscard]] nvrhi::ShaderType ToNvrhi(EArdaRHIShaderStage Value) noexcept;
    [[nodiscard]] nvrhi::CommandQueue ToNvrhi(EArdaRHIQueueType Value) noexcept;
    [[nodiscard]] nvrhi::TextureSubresourceSet ToNvrhi(const FArdaRHITextureSubresourceRange& Value) noexcept;
    [[nodiscard]] nvrhi::BufferRange ToNvrhi(const FArdaRHIBufferRange& Value) noexcept;
    [[nodiscard]] nvrhi::TextureDesc ToNvrhi(const FArdaRHITextureDesc& Value);
    [[nodiscard]] nvrhi::BufferDesc ToNvrhi(const FArdaRHIBufferDesc& Value);
    [[nodiscard]] nvrhi::SamplerDesc ToNvrhi(const FArdaRHISamplerDesc& Value);
}
