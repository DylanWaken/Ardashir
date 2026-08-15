#include "ArdaARDGExamplePch.h"

#include "ArdaGlfwWindow.h"
#include "ArdaLog.h"

ARDA_DECLARE_LOG_CATEGORY_EXTERN(LogARDGExample);

#if defined(_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#endif

namespace arda::tests::ardg_example
{
    FArdaGlfwWindow::~FArdaGlfwWindow()
    {
        if (mWindow)
        {
            glfwDestroyWindow(mWindow);
        }
        if (mbGlfwInitialized)
        {
            glfwTerminate();
        }
    }

    bool FArdaGlfwWindow::Create(
        const char* title,
        uint32_t width,
        uint32_t height,
        bool visible,
        bool fullscreen)
    {
        glfwSetErrorCallback([](int, const char* description)
        {
            ARDA_LOG(
                LogARDGExample,
                Error,
                "%s",
                description ? description : "");
        });

        if (!glfwInit())
        {
            mError = "GLFW initialization failed. A desktop display server may be unavailable.";
            return false;
        }
        mbGlfwInitialized = true;

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
        int fullscreenX = 0;
        int fullscreenY = 0;
        if (fullscreen)
        {
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* videoMode =
                monitor ? glfwGetVideoMode(monitor) : nullptr;
            if (!videoMode)
            {
                mError = "GLFW could not query the primary monitor for fullscreen mode.";
                return false;
            }
            glfwGetMonitorPos(monitor, &fullscreenX, &fullscreenY);
            width = static_cast<uint32_t>(videoMode->width);
            height = static_cast<uint32_t>(videoMode->height);
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
            glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
        }
        mWindow = glfwCreateWindow(
            static_cast<int>(width),
            static_cast<int>(height),
            title,
            nullptr,
            nullptr);
        if (!mWindow)
        {
            mError = "GLFW window creation failed.";
            return false;
        }
        if (fullscreen)
        {
            glfwSetWindowPos(mWindow, fullscreenX, fullscreenY);
        }

        glfwSetWindowUserPointer(mWindow, this);
        glfwSetFramebufferSizeCallback(mWindow, FramebufferSizeCallback);
        glfwSetCursorPosCallback(mWindow, CursorPositionCallback);
        glfwSetMouseButtonCallback(mWindow, MouseButtonCallback);
        glfwSetWindowFocusCallback(mWindow, WindowFocusCallback);
        mbCanCaptureCursor = visible;
        SetCursorCaptured(visible);

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(mWindow, &framebufferWidth, &framebufferHeight);
        mWidth = static_cast<uint32_t>(eastl::max(framebufferWidth, 1));
        mHeight = static_cast<uint32_t>(eastl::max(framebufferHeight, 1));
        return true;
    }

    bool FArdaGlfwWindow::PumpMessages()
    {
        glfwPollEvents();
        if (mWindow && mbCanCaptureCursor)
        {
            const bool shiftDown =
                glfwGetKey(mWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                glfwGetKey(mWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            if (shiftDown && !mbShiftWasDown)
            {
                SetCursorCaptured(false);
            }
            mbShiftWasDown = shiftDown;
        }
        if (mWindow && glfwGetKey(mWindow, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(mWindow, GLFW_TRUE);
        }
        return mWindow && !glfwWindowShouldClose(mWindow);
    }

    void FArdaGlfwWindow::SetCursorCaptured(bool captured)
    {
        if (!mWindow || mbCursorCaptured == captured)
        {
            return;
        }

        if (captured)
        {
            glfwSetInputMode(mWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            if (glfwRawMouseMotionSupported())
            {
                glfwSetInputMode(mWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
            }
        }
        else
        {
            if (glfwRawMouseMotionSupported())
            {
                glfwSetInputMode(mWindow, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
            }
            glfwSetInputMode(mWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        mbCursorCaptured = captured;
        mbHasCursorPosition = false;
        mMouseDeltaX = 0.0;
        mMouseDeltaY = 0.0;
    }

    FArdaCameraInput FArdaGlfwWindow::ConsumeCameraInput()
    {
        FArdaCameraInput input;
        if (!mWindow)
        {
            return input;
        }

        input.mForward =
            (glfwGetKey(mWindow, GLFW_KEY_W) == GLFW_PRESS ? 1.0f : 0.0f) -
            (glfwGetKey(mWindow, GLFW_KEY_S) == GLFW_PRESS ? 1.0f : 0.0f);
        input.mRight =
            (glfwGetKey(mWindow, GLFW_KEY_D) == GLFW_PRESS ? 1.0f : 0.0f) -
            (glfwGetKey(mWindow, GLFW_KEY_A) == GLFW_PRESS ? 1.0f : 0.0f);
        input.mLookX = static_cast<float>(mMouseDeltaX);
        input.mLookY = static_cast<float>(mMouseDeltaY);
        mMouseDeltaX = 0.0;
        mMouseDeltaY = 0.0;
        return input;
    }

    bool FArdaGlfwWindow::ConsumeResize(uint32_t& width, uint32_t& height)
    {
        if (!mbResizePending)
        {
            return false;
        }

        mbResizePending = false;
        width = mWidth;
        height = mHeight;
        return true;
    }

    backend::FArdaNativeObject FArdaGlfwWindow::GetD3D12WindowHandle() const noexcept
    {
#if defined(_WIN32)
        return backend::FArdaNativeObject(mWindow ? glfwGetWin32Window(mWindow) : nullptr);
#else
        return backend::FArdaNativeObject(nullptr);
#endif
    }

    eastl::vector<const char*> FArdaGlfwWindow::GetVulkanInstanceExtensions() const
    {
        uint32_t extensionCount = 0;
        const char** extensions =
            glfwGetRequiredInstanceExtensions(&extensionCount);
        if (!extensions || extensionCount == 0)
        {
            return {};
        }

        return { extensions, extensions + extensionCount };
    }

    backend::FArdaNativeObject FArdaGlfwWindow::CreateVulkanSurface(
        backend::FArdaNativeObject vulkanInstance,
        eastl::string& outError)
    {
        outError.clear();
        if (!mWindow)
        {
            outError = "GLFW cannot create a Vulkan surface without a window.";
            return backend::FArdaNativeObject(nullptr);
        }

        const VkInstance instance = vulkanInstance.As<VkInstance>();
        if (instance == VK_NULL_HANDLE)
        {
            outError = "GLFW received a null Vulkan instance.";
            return backend::FArdaNativeObject(nullptr);
        }

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        const VkResult result =
            glfwCreateWindowSurface(instance, mWindow, nullptr, &surface);
        if (result != VK_SUCCESS)
        {
            const char* glfwError = nullptr;
            glfwGetError(&glfwError);
            char resultText[32];
            std::snprintf(
                resultText,
                sizeof(resultText),
                "%d",
                static_cast<int>(result));
            outError = "glfwCreateWindowSurface failed with VkResult ";
            outError += resultText;
            if (glfwError && glfwError[0] != '\0')
            {
                outError += ": ";
                outError += glfwError;
            }
            return backend::FArdaNativeObject(nullptr);
        }

#if VK_USE_64_BIT_PTR_DEFINES
        return backend::FArdaNativeObject(surface);
#else
        return backend::FArdaNativeObject(static_cast<uintptr_t>(surface));
#endif
    }

    void FArdaGlfwWindow::FramebufferSizeCallback(
        GLFWwindow* window,
        int width,
        int height)
    {
        auto* self =
            static_cast<FArdaGlfwWindow*>(glfwGetWindowUserPointer(window));
        if (self && width > 0 && height > 0)
        {
            self->mWidth = static_cast<uint32_t>(width);
            self->mHeight = static_cast<uint32_t>(height);
            self->mbResizePending = true;
        }
    }

    void FArdaGlfwWindow::CursorPositionCallback(
        GLFWwindow* window,
        double x,
        double y)
    {
        auto* self =
            static_cast<FArdaGlfwWindow*>(glfwGetWindowUserPointer(window));
        if (!self)
        {
            return;
        }

        if (self->mbCursorCaptured && self->mbHasCursorPosition)
        {
            self->mMouseDeltaX += x - self->mLastCursorX;
            self->mMouseDeltaY += y - self->mLastCursorY;
        }
        self->mLastCursorX = x;
        self->mLastCursorY = y;
        self->mbHasCursorPosition = true;
    }

    void FArdaGlfwWindow::MouseButtonCallback(
        GLFWwindow* window,
        int button,
        int action,
        int)
    {
        auto* self =
            static_cast<FArdaGlfwWindow*>(glfwGetWindowUserPointer(window));
        if (self &&
            self->mbCanCaptureCursor &&
            !self->mbCursorCaptured &&
            button == GLFW_MOUSE_BUTTON_LEFT &&
            action == GLFW_PRESS)
        {
            self->SetCursorCaptured(true);
        }
    }

    void FArdaGlfwWindow::WindowFocusCallback(GLFWwindow* window, int focused)
    {
        auto* self =
            static_cast<FArdaGlfwWindow*>(glfwGetWindowUserPointer(window));
        if (self && focused == GLFW_TRUE)
        {
            self->mbHasCursorPosition = false;
        }
    }
}
