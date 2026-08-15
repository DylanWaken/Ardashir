/** @file ArdaNvrhiConversions.h
 *  Declares conversions from Arda RHI values and descriptors to NVRHI equivalents.
 */
#pragma once

#include "RHIWrappers/ArdaRHI.h"

#include <nvrhi/nvrhi.h>

namespace arda::rhi::private_impl
{
    /** Converts an Arda resource format to NVRHI.
     *  @param Value Arda resource format.
     *  @return Corresponding NVRHI format.
     */
    [[nodiscard]] nvrhi::Format ToNvrhi(EArdaRHIFormat Value) noexcept;

    /** Converts an Arda texture dimension to NVRHI.
     *  @param Value Arda texture dimension.
     *  @return Corresponding NVRHI texture dimension.
     */
    [[nodiscard]] nvrhi::TextureDimension ToNvrhi(EArdaRHITextureDimension Value) noexcept;

    /** Converts an Arda resource state to NVRHI.
     *  @param Value Arda resource state flags.
     *  @return Corresponding NVRHI resource states.
     */
    [[nodiscard]] nvrhi::ResourceStates ToNvrhi(EArdaRHIResourceState Value) noexcept;

    /** Converts an Arda shader stage to NVRHI.
     *  @param Value Arda shader stage.
     *  @return Corresponding NVRHI shader type.
     */
    [[nodiscard]] nvrhi::ShaderType ToNvrhi(EArdaRHIShaderStage Value) noexcept;

    /** Converts an Arda queue type to NVRHI.
     *  @param Value Arda queue type.
     *  @return Corresponding NVRHI command queue.
     */
    [[nodiscard]] nvrhi::CommandQueue ToNvrhi(EArdaRHIQueueType Value) noexcept;

    /** Converts an Arda texture subresource range to NVRHI.
     *  @param Value Arda texture subresource range.
     *  @return Corresponding NVRHI texture subresource set.
     */
    [[nodiscard]] nvrhi::TextureSubresourceSet ToNvrhi(const FArdaRHITextureSubresourceRange& Value) noexcept;

    /** Converts an Arda buffer range to NVRHI.
     *  @param Value Arda buffer range.
     *  @return Corresponding NVRHI buffer range.
     */
    [[nodiscard]] nvrhi::BufferRange ToNvrhi(const FArdaRHIBufferRange& Value) noexcept;

    /** Converts an Arda texture descriptor to NVRHI.
     *  @param Value Arda texture descriptor.
     *  @return Corresponding NVRHI texture descriptor.
     */
    [[nodiscard]] nvrhi::TextureDesc ToNvrhi(const FArdaRHITextureDesc& Value);

    /** Converts an Arda buffer descriptor to NVRHI.
     *  @param Value Arda buffer descriptor.
     *  @return Corresponding NVRHI buffer descriptor.
     */
    [[nodiscard]] nvrhi::BufferDesc ToNvrhi(const FArdaRHIBufferDesc& Value);

    /** Converts an Arda sampler descriptor to NVRHI.
     *  @param Value Arda sampler descriptor.
     *  @return Corresponding NVRHI sampler descriptor.
     */
    [[nodiscard]] nvrhi::SamplerDesc ToNvrhi(const FArdaRHISamplerDesc& Value);
}
