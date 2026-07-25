#include "NVRHITestPch.h"

#include "GlfwWindow.h"

namespace arda::tests::nvrhi_test
{
    GlfwWindow::~GlfwWindow()
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

    bool GlfwWindow::Create(const char* title, uint32_t width, uint32_t height, bool visible)
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

        if (!glfwVulkanSupported())
        {
            m_error = "GLFW could not find a Vulkan loader.";
            return false;
        }

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

    bool GlfwWindow::PumpMessages()
    {
        glfwPollEvents();
        return m_window && !glfwWindowShouldClose(m_window);
    }

    void GlfwWindow::Close()
    {
        if (m_window)
        {
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }
    }

    bool GlfwWindow::ConsumeResize(uint32_t& width, uint32_t& height)
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

    void GlfwWindow::FramebufferSizeCallback(GLFWwindow* window, int width, int height)
    {
        auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
        if (self && width > 0 && height > 0)
        {
            self->m_width = static_cast<uint32_t>(width);
            self->m_height = static_cast<uint32_t>(height);
            self->m_resizePending = true;
        }
    }
}
