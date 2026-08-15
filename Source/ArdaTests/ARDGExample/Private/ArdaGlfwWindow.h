#pragma once

#include "ArdaBackend.h"

#include <cstdint>
#include <EASTL/string.h>

struct GLFWwindow;

namespace arda::tests::ardg_example
{
    struct FArdaCameraInput
    {
        float mForward = 0.0f;
        float mRight = 0.0f;
        float mLookX = 0.0f;
        float mLookY = 0.0f;
    };

    class FArdaGlfwWindow final : public backend::IArdaWindowSurface
    {
    public:
        ~FArdaGlfwWindow();

        bool Create(
            const char* title,
            uint32_t width,
            uint32_t height,
            bool visible,
            bool fullscreen);
        bool PumpMessages();
        bool ConsumeResize(uint32_t& width, uint32_t& height);
        [[nodiscard]] FArdaCameraInput ConsumeCameraInput();

        [[nodiscard]] uint32_t GetWidth() const { return mWidth; }
        [[nodiscard]] uint32_t GetHeight() const { return mHeight; }
        [[nodiscard]] const eastl::string& GetError() const { return mError; }

        [[nodiscard]] backend::FArdaNativeObject GetD3D12WindowHandle() const noexcept override;
        [[nodiscard]] eastl::vector<const char*> GetVulkanInstanceExtensions() const override;
        [[nodiscard]] backend::FArdaNativeObject CreateVulkanSurface(
            backend::FArdaNativeObject vulkanInstance,
            eastl::string& outError) override;

    private:
        static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
        static void CursorPositionCallback(GLFWwindow* window, double x, double y);
        static void MouseButtonCallback(
            GLFWwindow* window,
            int button,
            int action,
            int modifiers);
        static void WindowFocusCallback(GLFWwindow* window, int focused);
        void SetCursorCaptured(bool captured);

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
