#include "NVRHITestPch.h"

#if ARDASHIR_HAS_D3D12
    #include "D3D12Backend.h"
#endif
#include "GlfwWindow.h"
#include "TriangleRenderer.h"
#include "VulkanBackend.h"

#include <cstdio>

namespace arda::tests::nvrhi_test
{
    namespace
    {
        constexpr int SkippedExitCode = 77;

        struct Options
        {
#if ARDASHIR_HAS_D3D12
            BackendKind backend = BackendKind::D3D12;
#else
            BackendKind backend = BackendKind::Vulkan;
#endif
            uint32_t frameLimit = 0;
            bool hidden = false;
        };

        class MessageCallback final : public nvrhi::IMessageCallback
        {
        public:
            void message(nvrhi::MessageSeverity severity, const char* messageText) override
            {
                const char* prefix = "INFO";
                if (severity == nvrhi::MessageSeverity::Warning)
                {
                    prefix = "WARNING";
                }
                else if (severity == nvrhi::MessageSeverity::Error)
                {
                    prefix = "ERROR";
                    ++m_errorCount;
                }
                else if (severity == nvrhi::MessageSeverity::Fatal)
                {
                    prefix = "FATAL";
                    ++m_errorCount;
                }

                std::fprintf(stderr, "[NVRHI %s] %s\n", prefix, messageText);
#if defined(_WIN32)
                OutputDebugStringA("[NVRHI] ");
                OutputDebugStringA(messageText);
                OutputDebugStringA("\n");
#endif
            }

            [[nodiscard]] uint32_t GetErrorCount() const { return m_errorCount; }

        private:
            uint32_t m_errorCount = 0;
        };

        bool ParseOptions(int argumentCount, char** arguments, Options& options, std::string& error)
        {
            for (int index = 1; index < argumentCount; ++index)
            {
                const std::string_view argument(arguments[index]);
                if (argument == "--hidden")
                {
                    options.hidden = true;
                }
                else if (argument == "--backend" && index + 1 < argumentCount)
                {
                    const std::string_view backend(arguments[++index]);
                    if (backend == "d3d12")
                    {
                        options.backend = BackendKind::D3D12;
                    }
                    else if (backend == "vulkan")
                    {
                        options.backend = BackendKind::Vulkan;
                    }
                    else
                    {
                        error = "Unknown backend. Use d3d12 or vulkan.";
                        return false;
                    }
                }
                else if (argument == "--frames" && index + 1 < argumentCount)
                {
                    try
                    {
                        options.frameLimit = static_cast<uint32_t>(std::stoul(arguments[++index]));
                    }
                    catch (const std::exception&)
                    {
                        error = "--frames requires a non-negative integer.";
                        return false;
                    }
                }
                else
                {
                    error = "Unknown or incomplete command-line option.";
                    return false;
                }
            }

            return true;
        }

        std::filesystem::path GetExecutableDirectory(const char* executable)
        {
            return std::filesystem::absolute(executable).parent_path();
        }

        int Run(int argumentCount, char** arguments)
        {
            Options options;
            std::string error;
            if (!ParseOptions(argumentCount, arguments, options, error))
            {
                std::fprintf(stderr, "%s\n", error.c_str());
                return EXIT_FAILURE;
            }

            GlfwWindow window;
            if (!window.Create("Ardashir - NVRHI Triangle", 1280, 720, !options.hidden))
            {
                std::fprintf(stderr, "%s\n", window.GetError().c_str());
                return options.hidden ? SkippedExitCode : EXIT_FAILURE;
            }

            std::unique_ptr<Backend> backend;
            if (options.backend == BackendKind::D3D12)
            {
#if ARDASHIR_HAS_D3D12
                backend = std::make_unique<D3D12Backend>();
#else
                std::fprintf(stderr, "The D3D12 backend is only available on Windows.\n");
                return SkippedExitCode;
#endif
            }
            else
            {
                backend = std::make_unique<VulkanBackend>();
            }

            MessageCallback messageCallback;
            const InitializeResult result = backend->Initialize(
                window.GetHandle(),
                window.GetWidth(),
                window.GetHeight(),
                &messageCallback);
            if (result != InitializeResult::Success)
            {
                std::fprintf(stderr, "%s\n", backend->GetError().c_str());
                return result == InitializeResult::Unavailable ? SkippedExitCode : EXIT_FAILURE;
            }

            TriangleRenderer renderer;
            if (!renderer.Initialize(
                backend->GetDevice(),
                backend->GetSwapChainFormat(),
                backend->GetKind(),
                GetExecutableDirectory(arguments[0])))
            {
                std::fprintf(stderr, "%s\n", renderer.GetError().c_str());
                return EXIT_FAILURE;
            }

            uint32_t renderedFrames = 0;
            while (window.PumpMessages())
            {
                uint32_t width = 0;
                uint32_t height = 0;
                if (window.ConsumeResize(width, height) && !backend->Resize(width, height))
                {
                    std::fprintf(stderr, "%s\n", backend->GetError().c_str());
                    return EXIT_FAILURE;
                }

                if (!renderer.RenderFrame(*backend))
                {
                    std::fprintf(stderr, "%s\n", renderer.GetError().c_str());
                    return EXIT_FAILURE;
                }

                ++renderedFrames;
                if (options.frameLimit > 0 && renderedFrames >= options.frameLimit)
                {
                    break;
                }
            }

            backend->WaitForIdle();
            return messageCallback.GetErrorCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }
}

int main(int argumentCount, char** arguments)
{
    try
    {
        return arda::tests::nvrhi_test::Run(argumentCount, arguments);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return EXIT_FAILURE;
    }
}
