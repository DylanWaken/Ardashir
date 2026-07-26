#include "ArdaNVRHITestPch.h"

#include "ArdaGlfwWindow.h"

#if defined(_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#endif

namespace arda::tests::nvrhi_test
{
    FArdaGlfwWindow::~FArdaGlfwWindow()
    {
        if (m_window)
        {
            glfwDestroyWindow(m_window);
        }
        if (m_glfwInitialized)
        {
            glfwTerminate();
        }
    }

    bool FArdaGlfwWindow::Create(const char* title, uint32_t width, uint32_t height, bool visible)
    {
        glfwSetErrorCallback([](int, const char* description)
        {
            std::fprintf(stderr, "[GLFW] %s\n", description);
        });

        if (!glfwInit())
        {
            m_error = "GLFW initialization failed. A desktop display server may be unavailable.";
            return false;
        }
        m_glfwInitialized = true;

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
        m_window = glfwCreateWindow(
            static_cast<int>(width),
            static_cast<int>(height),
            title,
            nullptr,
            nullptr);
        if (!m_window)
        {
            m_error = "GLFW window creation failed.";
            return false;
        }

        glfwSetWindowUserPointer(m_window, this);
        glfwSetFramebufferSizeCallback(m_window, FramebufferSizeCallback);

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
        m_width = static_cast<uint32_t>(std::max(framebufferWidth, 1));
        m_height = static_cast<uint32_t>(std::max(framebufferHeight, 1));
        return true;
    }

    bool FArdaGlfwWindow::PumpMessages()
    {
        glfwPollEvents();
        return m_window && !glfwWindowShouldClose(m_window);
    }

    void FArdaGlfwWindow::Close()
    {
        if (m_window)
        {
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }
    }

    bool FArdaGlfwWindow::ConsumeResize(uint32_t& width, uint32_t& height)
    {
        if (!m_resizePending)
        {
            return false;
        }

        m_resizePending = false;
        width = m_width;
        height = m_height;
        return true;
    }

    nvrhi::Object FArdaGlfwWindow::GetD3D12WindowHandle() const noexcept
    {
#if defined(_WIN32)
        return nvrhi::Object(m_window ? glfwGetWin32Window(m_window) : nullptr);
#else
        return nvrhi::Object(nullptr);
#endif
    }

    std::vector<const char*> FArdaGlfwWindow::GetVulkanInstanceExtensions() const
    {
        uint32_t ExtensionCount = 0;
        const char** Extensions = glfwGetRequiredInstanceExtensions(&ExtensionCount);
        if (!Extensions || ExtensionCount == 0)
        {
            return {};
        }

        return { Extensions, Extensions + ExtensionCount };
    }

    nvrhi::Object FArdaGlfwWindow::CreateVulkanSurface(
        nvrhi::Object VulkanInstance,
        std::string& OutError)
    {
        OutError.clear();
        if (!m_window)
        {
            OutError = "GLFW cannot create a Vulkan surface without a window.";
            return nvrhi::Object(nullptr);
        }

        const VkInstance Instance = static_cast<VkInstance>(VulkanInstance);
        if (Instance == VK_NULL_HANDLE)
        {
            OutError = "GLFW received a null Vulkan instance.";
            return nvrhi::Object(nullptr);
        }

        VkSurfaceKHR Surface = VK_NULL_HANDLE;
        const VkResult Result = glfwCreateWindowSurface(Instance, m_window, nullptr, &Surface);
        if (Result != VK_SUCCESS)
        {
            const char* GlfwError = nullptr;
            glfwGetError(&GlfwError);
            OutError = "glfwCreateWindowSurface failed with VkResult " +
                std::to_string(static_cast<int>(Result));
            if (GlfwError && GlfwError[0] != '\0')
            {
                OutError += ": ";
                OutError += GlfwError;
            }
            return nvrhi::Object(nullptr);
        }

#if VK_USE_64_BIT_PTR_DEFINES
        return nvrhi::Object(Surface);
#else
        return nvrhi::Object(static_cast<uint64_t>(Surface));
#endif
    }

    void FArdaGlfwWindow::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
    {
        auto* self = static_cast<FArdaGlfwWindow*>(glfwGetWindowUserPointer(window));
        if (self && width > 0 && height > 0)
        {
            self->m_width = static_cast<uint32_t>(width);
            self->m_height = static_cast<uint32_t>(height);
            self->m_resizePending = true;
        }
    }
}
