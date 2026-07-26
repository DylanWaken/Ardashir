#pragma once

#include "ArdaBackend.h"

#include <cstdint>
#include <string>

struct GLFWwindow;

namespace arda::tests::nvrhi_test
{
    class FArdaGlfwWindow final : public backend::IArdaWindowSurface
    {
    public:
        ~FArdaGlfwWindow();

        bool Create(const char* title, uint32_t width, uint32_t height, bool visible);
        bool PumpMessages();
        void Close();
        bool ConsumeResize(uint32_t& width, uint32_t& height);

        [[nodiscard]] GLFWwindow* GetHandle() const { return mWindow; }
        [[nodiscard]] uint32_t GetWidth() const { return mWidth; }
        [[nodiscard]] uint32_t GetHeight() const { return mHeight; }
        [[nodiscard]] const std::string& GetError() const { return mError; }

        [[nodiscard]] nvrhi::Object GetD3D12WindowHandle() const noexcept override;
        [[nodiscard]] std::vector<const char*> GetVulkanInstanceExtensions() const override;
        [[nodiscard]] nvrhi::Object CreateVulkanSurface(
            nvrhi::Object VulkanInstance,
            std::string& OutError) override;

    private:
        static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);

        GLFWwindow* mWindow = nullptr;
        uint32_t mWidth = 0;
        uint32_t mHeight = 0;
        bool mbResizePending = false;
        bool mbGlfwInitialized = false;
        std::string mError;
    };
}
