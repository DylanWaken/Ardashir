#include "ArdaRHITestPch.h"

#include "ArdaGlfwWindow.h"
#include "ArdaLog.h"

#include <cstdio>

ARDA_DECLARE_LOG_CATEGORY_EXTERN(LogRHITest);

#if defined(_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#endif

namespace arda::tests::rhi_test
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

    bool FArdaGlfwWindow::Create(const char* title, uint32_t width, uint32_t height, bool visible)
    {
        glfwSetErrorCallback([](int, const char* description)
        {
            ARDA_LOG(
                LogRHITest,
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

        glfwSetWindowUserPointer(mWindow, this);
        glfwSetFramebufferSizeCallback(mWindow, FramebufferSizeCallback);

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
        return mWindow && !glfwWindowShouldClose(mWindow);
    }

    void FArdaGlfwWindow::Close()
    {
        if (mWindow)
        {
            glfwSetWindowShouldClose(mWindow, GLFW_TRUE);
        }
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
        uint32_t ExtensionCount = 0;
        const char** Extensions = glfwGetRequiredInstanceExtensions(&ExtensionCount);
        if (!Extensions || ExtensionCount == 0)
        {
            return {};
        }

        return { Extensions, Extensions + ExtensionCount };
    }

    backend::FArdaNativeObject FArdaGlfwWindow::CreateVulkanSurface(
        backend::FArdaNativeObject VulkanInstance,
        eastl::string& OutError)
    {
        OutError.clear();
        if (!mWindow)
        {
            OutError = "GLFW cannot create a Vulkan surface without a window.";
            return backend::FArdaNativeObject(nullptr);
        }

        const VkInstance Instance = VulkanInstance.As<VkInstance>();
        if (Instance == VK_NULL_HANDLE)
        {
            OutError = "GLFW received a null Vulkan instance.";
            return backend::FArdaNativeObject(nullptr);
        }

        VkSurfaceKHR Surface = VK_NULL_HANDLE;
        const VkResult Result = glfwCreateWindowSurface(Instance, mWindow, nullptr, &Surface);
        if (Result != VK_SUCCESS)
        {
            const char* GlfwError = nullptr;
            glfwGetError(&GlfwError);
            char ResultText[32];
            std::snprintf(
                ResultText,
                sizeof(ResultText),
                "%d",
                static_cast<int>(Result));
            OutError = "glfwCreateWindowSurface failed with VkResult ";
            OutError += ResultText;
            if (GlfwError && GlfwError[0] != '\0')
            {
                OutError += ": ";
                OutError += GlfwError;
            }
            return backend::FArdaNativeObject(nullptr);
        }

#if VK_USE_64_BIT_PTR_DEFINES
        return backend::FArdaNativeObject(Surface);
#else
        return backend::FArdaNativeObject(static_cast<uintptr_t>(Surface));
#endif
    }

    void FArdaGlfwWindow::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
    {
        auto* self = static_cast<FArdaGlfwWindow*>(glfwGetWindowUserPointer(window));
        if (self && width > 0 && height > 0)
        {
            self->mWidth = static_cast<uint32_t>(width);
            self->mHeight = static_cast<uint32_t>(height);
            self->mbResizePending = true;
        }
    }
}
