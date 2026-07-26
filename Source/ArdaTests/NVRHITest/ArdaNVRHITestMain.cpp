#include "ArdaNVRHITestPch.h"

#include "ArdaBackend.h"
#include "ArdaGlfwWindow.h"
#include "ArdaTriangleRenderer.h"

#include <cstdio>

namespace arda::tests::nvrhi_test
{
    namespace
    {
        constexpr int SkippedExitCode = 77;

        struct FArdaOptions
        {
            backend::EArdaBackendType backend = backend::DefaultBackend;
            uint32_t frameLimit = 0;
            bool hidden = false;
        };

        class FArdaMessageCallback final : public nvrhi::IMessageCallback
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

        class FArdaBackendShutdownGuard final
        {
        public:
            explicit FArdaBackendShutdownGuard(
                std::unique_ptr<backend::IArdaSwapChain>& swapChain)
                : m_swapChain(swapChain)
            {
            }

            ~FArdaBackendShutdownGuard()
            {
                if (m_swapChain)
                {
                    m_swapChain->WaitForIdle();
                    m_swapChain.reset();
                }
                if (backend::IsBackendInitialized())
                {
                    backend::ShutdownBackend();
                }
            }

        private:
            std::unique_ptr<backend::IArdaSwapChain>& m_swapChain;
        };

        bool ParseOptions(int argumentCount, char** arguments, FArdaOptions& options, std::string& error)
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
                    const std::string_view backendArgument(arguments[++index]);
                    if (backendArgument == "d3d12")
                    {
                        options.backend = backend::EArdaBackendType::D3D12;
                    }
                    else if (backendArgument == "vulkan")
                    {
                        options.backend = backend::EArdaBackendType::Vulkan;
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
            FArdaOptions options;
            std::string error;
            if (!ParseOptions(argumentCount, arguments, options, error))
            {
                std::fprintf(stderr, "%s\n", error.c_str());
                return EXIT_FAILURE;
            }

            FArdaGlfwWindow window;
            if (!window.Create("Ardashir - NVRHI Triangle", 1280, 720, !options.hidden))
            {
                std::fprintf(stderr, "%s\n", window.GetError().c_str());
                return options.hidden ? SkippedExitCode : EXIT_FAILURE;
            }

            FArdaMessageCallback messageCallback;
            backend::FArdaBackendConfiguration configuration;
            configuration.backend = options.backend;
            configuration.enableValidation = true;
            configuration.messageCallback = &messageCallback;
            if (!backend::ConfigureBackend(configuration))
            {
                const std::string backendError = backend::GetBackendError();
                std::fprintf(stderr, "%s\n", backendError.c_str());
                return options.backend == backend::EArdaBackendType::D3D12
                    ? SkippedExitCode
                    : EXIT_FAILURE;
            }

            std::unique_ptr<backend::IArdaSwapChain> swapChain;
            FArdaBackendShutdownGuard shutdownGuard(swapChain);
            const backend::EArdaInitializeResult result =
                backend::InitializeBackendForPresentation(
                window,
                window.GetWidth(),
                window.GetHeight(),
                swapChain);
            if (result != backend::EArdaInitializeResult::Success)
            {
                const std::string backendError = backend::GetBackendError();
                std::fprintf(stderr, "%s\n", backendError.c_str());
                return result == backend::EArdaInitializeResult::Unavailable
                    ? SkippedExitCode
                    : EXIT_FAILURE;
            }

            FArdaTriangleRenderer renderer;
            if (!renderer.Initialize(
                backend::GetDevice(),
                swapChain->GetFormat(),
                options.backend,
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
                if (window.ConsumeResize(width, height) && !swapChain->Resize(width, height))
                {
                    std::fprintf(stderr, "%s\n", swapChain->GetError().c_str());
                    return EXIT_FAILURE;
                }

                if (!renderer.RenderFrame(*swapChain))
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
