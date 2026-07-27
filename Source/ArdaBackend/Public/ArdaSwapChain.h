#pragma once

#include "ArdaDevice.h"

#include <cstdint>
#include <EASTL/unique_ptr.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace arda::backend
{
    /**
     * Bridges a platform window system to the backend without exposing its types.
     * Native Vulkan handles are encoded in nvrhi::Object.
     */
    class IArdaWindowSurface
    {
    public:
        /** Destroys the window-surface adapter. */
        virtual ~IArdaWindowSurface() = default;

        /** Returns the native HWND encoded as an object pointer on Windows. */
        [[nodiscard]] virtual nvrhi::Object GetD3D12WindowHandle() const noexcept = 0;

        /**
         * Returns Vulkan instance extensions required by the window system.
         * The returned name pointers must remain valid during initialization.
         */
        [[nodiscard]] virtual eastl::vector<const char*> GetVulkanInstanceExtensions() const = 0;

        /**
         * Creates a Vulkan presentation surface owned by the backend.
         * @param VulkanInstance VkInstance encoded as an NVRHI object.
         * @param OutError Receives a diagnostic message when creation fails.
         * @return The created VkSurfaceKHR encoded as an NVRHI object.
         */
        [[nodiscard]] virtual nvrhi::Object CreateVulkanSurface(
            nvrhi::Object VulkanInstance,
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
        [[nodiscard]] virtual bool AcquireFrame(nvrhi::FramebufferHandle& OutFramebuffer) = 0;
        /** Transitions the acquired frame for queue submission. */
        virtual void PrepareSubmit() = 0;
        /** Presents the submitted frame and reports whether presentation succeeded. */
        [[nodiscard]] virtual bool Present() = 0;
        /** Blocks until pending swap-chain operations have completed. */
        virtual void WaitForIdle() noexcept = 0;

        /** Returns the pixel format used by presentation images. */
        [[nodiscard]] virtual nvrhi::Format GetFormat() const noexcept = 0;
        /** Returns the current presentation width in pixels. */
        [[nodiscard]] virtual uint32_t GetWidth() const noexcept = 0;
        /** Returns the current presentation height in pixels. */
        [[nodiscard]] virtual uint32_t GetHeight() const noexcept = 0;
        /** Returns the most recent swap-chain error message. */
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
