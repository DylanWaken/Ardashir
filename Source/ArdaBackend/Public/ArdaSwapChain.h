/** @file ArdaSwapChain.h
 *  @brief Declares window-surface integration and presentation swap chains.
 */
#pragma once

#include "ArdaBackend.h"

#include <cstdint>
#include <EASTL/unique_ptr.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace arda::backend
{
    /** Application callback that can replace or augment native presentation. */
    class IArdaCustomPresent
    {
    public:
        virtual ~IArdaCustomPresent() = default;
        virtual void OnBackBufferResize(uint32_t Width, uint32_t Height) = 0;
        [[nodiscard]] virtual bool NeedsNativePresent() const noexcept = 0;
        /** Returns true when custom presentation succeeded. */
        [[nodiscard]] virtual bool Present(
            FArdaNativeObject BackBuffer,
            uint32_t Width,
            uint32_t Height) = 0;
        virtual void PostPresent() = 0;
    };

    /**
     * Bridges a platform window system to the backend without exposing its types.
     * Native platform handles are encoded in FArdaNativeObject.
     */
    class IArdaWindowSurface
    {
    public:
        /** Destroys the window-surface adapter. */
        virtual ~IArdaWindowSurface() = default;

        /** @return The native HWND encoded as an opaque object on Windows. */
        [[nodiscard]] virtual FArdaNativeObject GetD3D12WindowHandle() const noexcept = 0;

        /**
         * Returns Vulkan instance extensions required by the window system.
         * The returned name pointers must remain valid during initialization.
         * @return Required null-terminated Vulkan extension names.
         */
        [[nodiscard]] virtual eastl::vector<const char*> GetVulkanInstanceExtensions() const = 0;

        /**
         * Creates a Vulkan presentation surface owned by the backend.
         * @param VulkanInstance VkInstance encoded as an opaque native object.
         * @param OutError Receives a diagnostic message when creation fails.
         * @return The created VkSurfaceKHR encoded as an opaque native object.
         */
        [[nodiscard]] virtual FArdaNativeObject CreateVulkanSurface(
            FArdaNativeObject VulkanInstance,
            eastl::string& OutError) = 0;
    };

    /** Manages presentation images and submission for a native swap chain. */
    class IArdaSwapChain
    {
    public:
        /** Destroys the swap chain and its presentation resources. */
        virtual ~IArdaSwapChain() = default;

        /**
         * Resizes presentation resources.
         * @param Width New presentation width in pixels.
         * @param Height New presentation height in pixels.
         * @return True when the resources were resized successfully.
         */
        [[nodiscard]] virtual bool Resize(uint32_t Width, uint32_t Height) = 0;
        /**
         * Acquires the framebuffer for the next frame.
         * @param OutFramebuffer Receives the acquired framebuffer.
         * @return True when a frame was acquired successfully.
         */
        [[nodiscard]] virtual bool AcquireFrame(rhi::FArdaRHIFramebufferRef& OutFramebuffer) = 0;
        /** Transitions the acquired frame for queue submission. */
        virtual void PrepareSubmit() = 0;
        /** @return True when the submitted frame was presented successfully. */
        [[nodiscard]] virtual bool Present() = 0;
        /** Installs or clears a custom-present callback. */
        virtual void SetCustomPresent(eastl::shared_ptr<IArdaCustomPresent>) {}
        /** Returns the currently installed custom-present callback. */
        [[nodiscard]] virtual eastl::shared_ptr<IArdaCustomPresent>
            GetCustomPresent() const { return {}; }
        /** Blocks until pending swap-chain operations have completed. */
        virtual void WaitForIdle() noexcept = 0;

        /** @return The pixel format used by presentation images. */
        [[nodiscard]] virtual rhi::EArdaRHIFormat GetFormat() const noexcept = 0;
        /** @return The current presentation width in pixels. */
        [[nodiscard]] virtual uint32_t GetWidth() const noexcept = 0;
        /** @return The current presentation height in pixels. */
        [[nodiscard]] virtual uint32_t GetHeight() const noexcept = 0;
        /** @return The most recent swap-chain error message. */
        [[nodiscard]] virtual const eastl::string& GetError() const noexcept = 0;
    };

    /**
     * Initializes the backend and a swap chain for a window surface.
     * @param WindowSurface Platform adapter used to create the native surface.
     * @param Width Initial presentation width in pixels.
     * @param Height Initial presentation height in pixels.
     * @param OutSwapChain Receives a swap chain to destroy before shutdown.
     * @return The backend initialization outcome.
     */
    [[nodiscard]] EArdaInitializeResult InitializeBackendForPresentation(
        IArdaWindowSurface& WindowSurface,
        uint32_t Width,
        uint32_t Height,
        eastl::unique_ptr<IArdaSwapChain>& OutSwapChain);
}
