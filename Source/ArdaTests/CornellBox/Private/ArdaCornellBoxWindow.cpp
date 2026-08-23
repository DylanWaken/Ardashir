#include "ArdaCornellBoxPch.h"

#include "ArdaCornellBoxWindow.h"
#include "ArdaLog.h"

ARDA_DECLARE_LOG_CATEGORY_EXTERN(LogCornellBox);

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace arda::tests::cornell_box
{
    FArdaCornellBoxWindow::~FArdaCornellBoxWindow()
    {
        if (mWindow)
            glfwDestroyWindow(mWindow);
        if (mbGlfwInitialized)
            glfwTerminate();
    }

    bool FArdaCornellBoxWindow::Create(
        const char* Title,
        uint32_t Width,
        uint32_t Height,
        bool bVisible,
        bool bFullscreen)
    {
        glfwSetErrorCallback([](int, const char* Description)
        {
            ARDA_LOG(
                LogCornellBox, Error, "%s", Description ? Description : "");
        });
        if (!glfwInit())
        {
            mError = "GLFW initialization failed. A desktop display server may be unavailable.";
            return false;
        }
        mbGlfwInitialized = true;
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, bVisible ? GLFW_TRUE : GLFW_FALSE);

        int FullscreenX = 0;
        int FullscreenY = 0;
        if (bFullscreen)
        {
            GLFWmonitor* Monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* Mode = Monitor ? glfwGetVideoMode(Monitor) : nullptr;
            if (!Mode)
            {
                mError = "GLFW could not query the primary monitor.";
                return false;
            }
            glfwGetMonitorPos(Monitor, &FullscreenX, &FullscreenY);
            Width = static_cast<uint32_t>(Mode->width);
            Height = static_cast<uint32_t>(Mode->height);
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
            glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
        }

        mWindow = glfwCreateWindow(
            static_cast<int>(Width), static_cast<int>(Height),
            Title, nullptr, nullptr);
        if (!mWindow)
        {
            mError = "GLFW window creation failed.";
            return false;
        }
        if (bFullscreen)
            glfwSetWindowPos(mWindow, FullscreenX, FullscreenY);

        glfwSetWindowUserPointer(mWindow, this);
        glfwSetFramebufferSizeCallback(mWindow, FramebufferSizeCallback);
        glfwSetCursorPosCallback(mWindow, CursorPositionCallback);
        glfwSetMouseButtonCallback(mWindow, MouseButtonCallback);
        glfwSetWindowFocusCallback(mWindow, WindowFocusCallback);
        mbCanCaptureCursor = bVisible;
        SetCursorCaptured(bVisible);

        int FramebufferWidth = 0;
        int FramebufferHeight = 0;
        glfwGetFramebufferSize(mWindow, &FramebufferWidth, &FramebufferHeight);
        mWidth = static_cast<uint32_t>(eastl::max(FramebufferWidth, 1));
        mHeight = static_cast<uint32_t>(eastl::max(FramebufferHeight, 1));
        return true;
    }

    bool FArdaCornellBoxWindow::PumpMessages()
    {
        glfwPollEvents();
        if (mWindow && mbCanCaptureCursor)
        {
            const bool bShiftDown =
                glfwGetKey(mWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                glfwGetKey(mWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            if (bShiftDown && !mbShiftWasDown)
                SetCursorCaptured(false);
            mbShiftWasDown = bShiftDown;
        }
        if (mWindow && glfwGetKey(mWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(mWindow, GLFW_TRUE);
        return mWindow && !glfwWindowShouldClose(mWindow);
    }

    void FArdaCornellBoxWindow::SetCursorCaptured(bool bCaptured)
    {
        if (!mWindow || mbCursorCaptured == bCaptured)
            return;
        if (bCaptured)
        {
            glfwSetInputMode(mWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(mWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
        else
        {
            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(mWindow, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
            glfwSetInputMode(mWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        mbCursorCaptured = bCaptured;
        mbHasCursorPosition = false;
        mMouseDeltaX = 0.0;
        mMouseDeltaY = 0.0;
    }

    FArdaCameraInput FArdaCornellBoxWindow::ConsumeCameraInput()
    {
        FArdaCameraInput Input;
        if (!mWindow)
            return Input;
        Input.mForward =
            (glfwGetKey(mWindow, GLFW_KEY_W) == GLFW_PRESS ? 1.0f : 0.0f) -
            (glfwGetKey(mWindow, GLFW_KEY_S) == GLFW_PRESS ? 1.0f : 0.0f);
        Input.mRight =
            (glfwGetKey(mWindow, GLFW_KEY_D) == GLFW_PRESS ? 1.0f : 0.0f) -
            (glfwGetKey(mWindow, GLFW_KEY_A) == GLFW_PRESS ? 1.0f : 0.0f);
        Input.mLookX = static_cast<float>(mMouseDeltaX);
        Input.mLookY = static_cast<float>(mMouseDeltaY);
        mMouseDeltaX = 0.0;
        mMouseDeltaY = 0.0;
        return Input;
    }

    bool FArdaCornellBoxWindow::ConsumeResize(uint32_t& Width, uint32_t& Height)
    {
        if (!mbResizePending)
            return false;
        mbResizePending = false;
        Width = mWidth;
        Height = mHeight;
        return true;
    }

    backend::FArdaNativeObject
    FArdaCornellBoxWindow::GetD3D12WindowHandle() const noexcept
    {
#if defined(_WIN32)
        return backend::FArdaNativeObject(
            mWindow ? glfwGetWin32Window(mWindow) : nullptr);
#else
        return backend::FArdaNativeObject(nullptr);
#endif
    }

    eastl::vector<const char*>
    FArdaCornellBoxWindow::GetVulkanInstanceExtensions() const
    {
#if defined(ARDA_TEST_WITH_VULKAN)
        uint32_t Count = 0;
        const char** Extensions = glfwGetRequiredInstanceExtensions(&Count);
        return Extensions && Count ?
            eastl::vector<const char*>(Extensions, Extensions + Count) :
            eastl::vector<const char*>();
#else
        return {};
#endif
    }

    backend::FArdaNativeObject FArdaCornellBoxWindow::CreateVulkanSurface(
        backend::FArdaNativeObject VulkanInstance,
        eastl::string& OutError)
    {
#if defined(ARDA_TEST_WITH_VULKAN)
        OutError.clear();
        const VkInstance Instance = VulkanInstance.As<VkInstance>();
        if (!mWindow || Instance == VK_NULL_HANDLE)
        {
            OutError = "GLFW received an invalid window or Vulkan instance.";
            return backend::FArdaNativeObject(nullptr);
        }
        VkSurfaceKHR Surface = VK_NULL_HANDLE;
        const VkResult Result =
            glfwCreateWindowSurface(Instance, mWindow, nullptr, &Surface);
        if (Result != VK_SUCCESS)
        {
            char Text[96]{};
            std::snprintf(Text, sizeof(Text),
                "glfwCreateWindowSurface failed with VkResult %d.",
                static_cast<int>(Result));
            OutError = Text;
            return backend::FArdaNativeObject(nullptr);
        }
#if VK_USE_64_BIT_PTR_DEFINES
        return backend::FArdaNativeObject(Surface);
#else
        return backend::FArdaNativeObject(static_cast<uintptr_t>(Surface));
#endif
#else
        static_cast<void>(VulkanInstance);
        OutError = "The Vulkan backend module is not linked.";
        return backend::FArdaNativeObject(nullptr);
#endif
    }

    void FArdaCornellBoxWindow::FramebufferSizeCallback(
        GLFWwindow* Window, int Width, int Height)
    {
        auto* Self = static_cast<FArdaCornellBoxWindow*>(
            glfwGetWindowUserPointer(Window));
        if (Self && Width > 0 && Height > 0)
        {
            Self->mWidth = static_cast<uint32_t>(Width);
            Self->mHeight = static_cast<uint32_t>(Height);
            Self->mbResizePending = true;
        }
    }

    void FArdaCornellBoxWindow::CursorPositionCallback(
        GLFWwindow* Window, double X, double Y)
    {
        auto* Self = static_cast<FArdaCornellBoxWindow*>(
            glfwGetWindowUserPointer(Window));
        if (!Self)
            return;
        if (Self->mbCursorCaptured && Self->mbHasCursorPosition)
        {
            Self->mMouseDeltaX += X - Self->mLastCursorX;
            Self->mMouseDeltaY += Y - Self->mLastCursorY;
        }
        Self->mLastCursorX = X;
        Self->mLastCursorY = Y;
        Self->mbHasCursorPosition = true;
    }

    void FArdaCornellBoxWindow::MouseButtonCallback(
        GLFWwindow* Window, int Button, int Action, int)
    {
        auto* Self = static_cast<FArdaCornellBoxWindow*>(
            glfwGetWindowUserPointer(Window));
        if (Self && Self->mbCanCaptureCursor && !Self->mbCursorCaptured &&
            Button == GLFW_MOUSE_BUTTON_LEFT && Action == GLFW_PRESS)
        {
            Self->SetCursorCaptured(true);
        }
    }

    void FArdaCornellBoxWindow::WindowFocusCallback(
        GLFWwindow* Window, int Focused)
    {
        auto* Self = static_cast<FArdaCornellBoxWindow*>(
            glfwGetWindowUserPointer(Window));
        if (Self && Focused == GLFW_TRUE)
            Self->mbHasCursorPosition = false;
    }
}
