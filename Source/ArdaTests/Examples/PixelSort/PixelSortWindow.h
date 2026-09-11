#pragma once
#include "ArdaSwapChain.h"
#include <Windows.h>

namespace arda
{
    // Minimal platform adapter for ArdaBackend presentation. An application with
    // its own window library can implement the same IArdaWindowSurface interface
    // instead; the renderer and CUDA operand need no knowledge of HWND/VkSurfaceKHR.
    // Keep the window alive until the swap chain and backend have been destroyed.
    class FPixelSortWindow final : public IArdaWindowSurface
    {
    public:
        ~FPixelSortWindow();
        void Create(uint32_t Width, uint32_t Height, bool Hidden);
        bool Pump();
        void Resize(uint32_t Width, uint32_t Height);
        // Coalesce native size notifications for the main/render loop. GPU resource
        // recreation belongs there, not inside a reentrant Win32 window procedure.
        bool ConsumeResize();
        void SetTitle(const char* Title);
        uint32_t mWidth = 0, mHeight = 0, mChannel = 0, mThreshold = 48;
        bool mbPaused = false, mbOriginal = false;
        FArdaNativeObject GetD3D12WindowHandle() const noexcept override;
        // Extension name strings remain valid through backend initialization.
        eastl::vector<const char*> GetVulkanInstanceExtensions() const override;
        // The backend takes ownership of the returned Vulkan surface.
        FArdaNativeObject CreateVulkanSurface(FArdaNativeObject, eastl::string&) override;
    private:
        static LRESULT CALLBACK Procedure(HWND, UINT, WPARAM, LPARAM);
        HWND mWindow = nullptr;
        HMODULE mVulkanLoader = nullptr;
        bool mbResize = false, mbClosed = false;
    };
}
