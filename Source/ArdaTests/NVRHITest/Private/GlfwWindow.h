#pragma once

#include <cstdint>
#include <string>

struct GLFWwindow;

namespace arda::tests::nvrhi_test
{
    class GlfwWindow
    {
    public:
        ~GlfwWindow();

        bool Create(const char* title, uint32_t width, uint32_t height, bool visible);
        bool PumpMessages();
        void Close();
        bool ConsumeResize(uint32_t& width, uint32_t& height);

        [[nodiscard]] GLFWwindow* GetHandle() const { return m_window; }
        [[nodiscard]] uint32_t GetWidth() const { return m_width; }
        [[nodiscard]] uint32_t GetHeight() const { return m_height; }
        [[nodiscard]] const std::string& GetError() const { return m_error; }

    private:
        static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);

        GLFWwindow* m_window = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        bool m_resizePending = false;
        bool m_glfwInitialized = false;
        std::string m_error;
    };
}
