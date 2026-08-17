/** @file ArdaNvrhiBackend.h
 *  @brief Declares explicit registration entry points for the sample NVRHI modules.
 */
#pragma once

namespace arda::backend
{
    /** Registers the independently linkable NVRHI Vulkan backend module. */
    [[nodiscard]] bool RegisterArdaNvrhiVulkanBackend();

#if defined(_WIN32)
    /** Registers the independently linkable NVRHI D3D12 backend module. */
    [[nodiscard]] bool RegisterArdaNvrhiD3D12Backend();
#endif
}
