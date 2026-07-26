#include "ArdaNVRHITestPch.h"

#include "ArdaBackend.h"
#include "ArdaGlfwWindow.h"
#include "ArdaTriangleRenderer.h"

#include <cstdlib>

ARDA_DEFINE_LOG_CATEGORY_NAMED(LogNVRHITest, "NVRHITest", Log);

namespace arda::tests::nvrhi_test
{
    namespace
    {
        constexpr int SkippedExitCode = 77;

        struct FArdaOptions
        {
            backend::EArdaBackendType mBackend = backend::DefaultBackend;
            uint32_t mFrameLimit = 0;
            bool mbHidden = false;
        };

        class FArdaMessageCallback final : public nvrhi::IMessageCallback
        {
        public:
            void message(nvrhi::MessageSeverity severity, const char* messageText) override
            {
                switch (severity)
                {
                case nvrhi::MessageSeverity::Warning:
                    ARDA_LOG(
                        LogNVRHITest,
                        Warning,
                        "%s",
                        messageText ? messageText : "");
                    break;
                case nvrhi::MessageSeverity::Error:
                    ++mErrorCount;
                    ARDA_LOG(
                        LogNVRHITest,
                        Error,
                        "%s",
                        messageText ? messageText : "");
                    break;
                case nvrhi::MessageSeverity::Fatal:
                    ++mErrorCount;
                    ARDA_LOG(
                        LogNVRHITest,
                        Fatal,
                        "%s",
                        messageText ? messageText : "");
                    break;
                default:
                    ARDA_LOG(
                        LogNVRHITest,
                        Log,
                        "%s",
                        messageText ? messageText : "");
                    break;
                }
            }

            [[nodiscard]] uint32_t GetErrorCount() const { return mErrorCount; }

        private:
            uint32_t mErrorCount = 0;
        };

        class FArdaBackendShutdownGuard final
        {
        public:
            explicit FArdaBackendShutdownGuard(
                std::unique_ptr<backend::IArdaSwapChain>& swapChain)
                : mSwapChain(swapChain)
            {
            }

            ~FArdaBackendShutdownGuard()
            {
                if (mSwapChain)
                {
                    mSwapChain->WaitForIdle();
                    mSwapChain.reset();
                }
                if (backend::IsBackendInitialized())
                {
                    backend::ShutdownBackend();
                }
            }

        private:
            std::unique_ptr<backend::IArdaSwapChain>& mSwapChain;
        };

        bool ParseOptions(int argumentCount, char** arguments, FArdaOptions& options, std::string& error)
        {
            for (int index = 1; index < argumentCount; ++index)
            {
                const std::string_view argument(arguments[index]);
                if (argument == "--hidden")
                {
                    options.mbHidden = true;
                }
                else if (argument == "--backend" && index + 1 < argumentCount)
                {
                    const std::string_view backendArgument(arguments[++index]);
                    if (backendArgument == "d3d12")
                    {
                        options.mBackend = backend::EArdaBackendType::D3D12;
                    }
                    else if (backendArgument == "vulkan")
                    {
                        options.mBackend = backend::EArdaBackendType::Vulkan;
                    }
                    else
                    {
                        error = "Unknown backend. Use d3d12 or vulkan.";
                        return false;
                    }
                }
                else if (argument == "--frames" && index + 1 < argumentCount)
                {
                    const char* framesText = arguments[++index];
                    char* end = nullptr;
                    const unsigned long parsed =
                        std::strtoul(framesText, &end, 10);
                    if (framesText[0] == '\0' || (end != nullptr && *end != '\0'))
                    {
                        error = "--frames requires a non-negative integer.";
                        return false;
                    }
                    options.mFrameLimit = static_cast<uint32_t>(parsed);
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
                ARDA_LOG(LogNVRHITest, Error, "%s", error.c_str());
                return EXIT_FAILURE;
            }

            FArdaGlfwWindow window;
            if (!window.Create("Ardashir - NVRHI Triangle", 1280, 720, !options.mbHidden))
            {
                ARDA_LOG(LogNVRHITest, Error, "%s", window.GetError().c_str());
                return options.mbHidden ? SkippedExitCode : EXIT_FAILURE;
            }

            FArdaMessageCallback messageCallback;
            backend::FArdaBackendConfiguration configuration;
            configuration.mBackend = options.mBackend;
            configuration.mbEnableValidation = true;
            configuration.mMessageCallback = &messageCallback;
            if (!backend::ConfigureBackend(configuration))
            {
                const std::string backendError = backend::GetBackendError();
                ARDA_LOG(LogNVRHITest, Error, "%s", backendError.c_str());
                return options.mBackend == backend::EArdaBackendType::D3D12
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
                ARDA_LOG(LogNVRHITest, Error, "%s", backendError.c_str());
                return result == backend::EArdaInitializeResult::Unavailable
                    ? SkippedExitCode
                    : EXIT_FAILURE;
            }

            FArdaTriangleRenderer renderer;
            if (!renderer.Initialize(
                backend::GetDeviceContext(),
                swapChain->GetFormat(),
                GetExecutableDirectory(arguments[0])))
            {
                ARDA_LOG(LogNVRHITest, Error, "%s", renderer.GetError().c_str());
                return EXIT_FAILURE;
            }

            uint32_t renderedFrames = 0;
            while (window.PumpMessages())
            {
                uint32_t width = 0;
                uint32_t height = 0;
                if (window.ConsumeResize(width, height) && !swapChain->Resize(width, height))
                {
                    ARDA_LOG(LogNVRHITest, Error, "%s", swapChain->GetError().c_str());
                    return EXIT_FAILURE;
                }

                if (!renderer.RenderFrame(*swapChain))
                {
                    ARDA_LOG(LogNVRHITest, Error, "%s", renderer.GetError().c_str());
                    return EXIT_FAILURE;
                }

                ++renderedFrames;
                if (options.mFrameLimit > 0 && renderedFrames >= options.mFrameLimit)
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
    return arda::tests::nvrhi_test::Run(argumentCount, arguments);
}
