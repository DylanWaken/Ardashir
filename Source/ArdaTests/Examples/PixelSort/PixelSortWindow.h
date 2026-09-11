#pragma once
#include "ArdaSwapChain.h"
#include <Windows.h>

namespace arda
{
    class FPixelSortWindow final : public IArdaWindowSurface
    {
    public:
        ~FPixelSortWindow();
        void Create(uint32_t Width, uint32_t Height, bool Hidden);
        bool Pump();
        void Resize(uint32_t Width, uint32_t Height);
        bool ConsumeResize();
        void SetTitle(const char* Title);
        uint32_t mWidth = 0, mHeight = 0, mChannel = 0, mThreshold = 48;
        bool mbPaused = false, mbOriginal = false;
        FArdaNativeObject GetD3D12WindowHandle() const noexcept override;
        eastl::vector<const char*> GetVulkanInstanceExtensions() const override;
        FArdaNativeObject CreateVulkanSurface(FArdaNativeObject, eastl::string&) override;
    private:
        static LRESULT CALLBACK Procedure(HWND, UINT, WPARAM, LPARAM);
        HWND mWindow = nullptr;
        HMODULE mVulkanLoader = nullptr;
        bool mbResize = false, mbClosed = false;
    };
}
