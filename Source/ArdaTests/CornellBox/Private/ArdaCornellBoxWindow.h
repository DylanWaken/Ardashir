#pragma once

#include "ArdaSwapChain.h"

#include <cstdint>
#include <EASTL/string.h>

struct GLFWwindow;

namespace arda::tests::cornell_box
{
    struct FArdaCameraInput
    {
        float mForward = 0.0f;
        float mRight = 0.0f;
        float mLookX = 0.0f;
        float mLookY = 0.0f;
    };

    class FArdaCornellBoxWindow final : public backend::IArdaWindowSurface
    {
    public:
        ~FArdaCornellBoxWindow();

        bool Create(
            const char* Title,
            uint32_t Width,
            uint32_t Height,
            bool bVisible,
            bool bFullscreen);
        bool PumpMessages();
        bool ConsumeResize(uint32_t& Width, uint32_t& Height);
        [[nodiscard]] FArdaCameraInput ConsumeCameraInput();

        [[nodiscard]] uint32_t GetWidth() const noexcept { return mWidth; }
        [[nodiscard]] uint32_t GetHeight() const noexcept { return mHeight; }
        [[nodiscard]] const eastl::string& GetError() const noexcept
        {
            return mError;
        }

        [[nodiscard]] backend::FArdaNativeObject
        GetD3D12WindowHandle() const noexcept override;
        [[nodiscard]] eastl::vector<const char*>
        GetVulkanInstanceExtensions() const override;
        [[nodiscard]] backend::FArdaNativeObject CreateVulkanSurface(
            backend::FArdaNativeObject VulkanInstance,
            eastl::string& OutError) override;

    private:
        static void FramebufferSizeCallback(
            GLFWwindow* Window, int Width, int Height);
        static void CursorPositionCallback(GLFWwindow* Window, double X, double Y);
        static void MouseButtonCallback(
            GLFWwindow* Window, int Button, int Action, int Modifiers);
        static void WindowFocusCallback(GLFWwindow* Window, int Focused);
        void SetCursorCaptured(bool bCaptured);

        GLFWwindow* mWindow = nullptr;
        uint32_t mWidth = 0;
        uint32_t mHeight = 0;
        double mLastCursorX = 0.0;
        double mLastCursorY = 0.0;
        double mMouseDeltaX = 0.0;
        double mMouseDeltaY = 0.0;
        bool mbResizePending = false;
        bool mbHasCursorPosition = false;
        bool mbCanCaptureCursor = false;
        bool mbCursorCaptured = false;
        bool mbShiftWasDown = false;
        bool mbGlfwInitialized = false;
        eastl::string mError;
    };
}
