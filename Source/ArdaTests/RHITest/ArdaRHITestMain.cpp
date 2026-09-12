#include "ArdaRHITestPch.h"

#include "ArdaBackend.h"
#include "ArdaSwapChain.h"
#include "ArdaGlfwWindow.h"
#include "ArdaTriangleRenderer.h"

#include <cstdlib>

ARDA_DEFINE_LOG_CATEGORY_NAMED(LogRHITest, "RHITest", Log);

namespace arda
{
	namespace
	{
		constexpr int SkippedExitCode = 77;

		struct FArdaOptions
		{
			eastl::string mBackendName;
			uint32_t mFrameLimit = 0;
			bool mbHidden = false;
		};

		class FArdaMessageCallback final : public arda::IArdaDiagnosticCallback
		{
		public:
			void Message(arda::EArdaDiagnosticSeverity severity, const char* messageText) override
			{
				switch (severity)
				{
				case arda::EArdaDiagnosticSeverity::Warning:
					ARDA_LOG(LogRHITest, Warning, "%s", messageText ? messageText : "");
					break;
				case arda::EArdaDiagnosticSeverity::Error:
					++mErrorCount;
					ARDA_LOG(LogRHITest, Error, "%s", messageText ? messageText : "");
					break;
				case arda::EArdaDiagnosticSeverity::Fatal:
					++mErrorCount;
					ARDA_LOG(LogRHITest, Fatal, "%s", messageText ? messageText : "");
					break;
				default:
					ARDA_LOG(LogRHITest, Log, "%s", messageText ? messageText : "");
					break;
				}
			}

			[[nodiscard]] uint32_t GetErrorCount() const
			{
				return mErrorCount;
			}

		private:
			uint32_t mErrorCount = 0;
		};

		class FArdaBackendShutdownGuard final
		{
		public:
			explicit FArdaBackendShutdownGuard(eastl::unique_ptr<arda::IArdaSwapChain>& swapChain)
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
				if (arda::IsBackendInitialized())
				{
					arda::ShutdownBackend();
				}
			}

		private:
			eastl::unique_ptr<arda::IArdaSwapChain>& mSwapChain;
		};

		bool ParseOptions(int argumentCount, char** arguments, FArdaOptions& options, eastl::string& error)
		{
			for (int index = 1; index < argumentCount; ++index)
			{
				const eastl::string_view argument(arguments[index]);
				if (argument == "--hidden")
				{
					options.mbHidden = true;
				}
				else if (argument == "--backend" && index + 1 < argumentCount)
				{
					const eastl::string_view backendArgument(arguments[++index]);
					if (backendArgument == "d3d12")
					{
						options.mBackendName = "native-d3d12";
					}
					else if (backendArgument == "vulkan")
					{
						options.mBackendName = "native-vulkan";
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
					const unsigned long parsed = std::strtoul(framesText, &end, 10);
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

		int Run(int argumentCount, char** arguments)
		{
			FArdaOptions options;
			eastl::string error;
			if (!ParseOptions(argumentCount, arguments, options, error))
			{
				ARDA_LOG(LogRHITest, Error, "%s", error.c_str());
				return EXIT_FAILURE;
			}

			FArdaGlfwWindow window;
			if (!window.Create("Ardashir - RHI Triangle", 1280, 720, !options.mbHidden))
			{
				ARDA_LOG(LogRHITest, Error, "%s", window.GetError().c_str());
				return options.mbHidden ? SkippedExitCode : EXIT_FAILURE;
			}

			FArdaMessageCallback messageCallback;
			arda::FArdaBackendConfiguration configuration;
			configuration.mBackendName = options.mBackendName;
			configuration.mbEnableValidation = true;
			configuration.mMessageCallback = &messageCallback;
			if (!arda::ConfigureBackend(configuration))
			{
				const eastl::string backendError = arda::GetBackendError();
				ARDA_LOG(LogRHITest, Error, "%s", backendError.c_str());
				return options.mBackendName == "native-d3d12" ? SkippedExitCode : EXIT_FAILURE;
			}

			eastl::unique_ptr<arda::IArdaSwapChain> swapChain;
			FArdaBackendShutdownGuard shutdownGuard(swapChain);
			const arda::EArdaInitializeResult result =
			    arda::InitializeBackendForPresentation(window, window.GetWidth(), window.GetHeight(), swapChain);
			if (result != arda::EArdaInitializeResult::Success)
			{
				const eastl::string backendError = arda::GetBackendError();
				ARDA_LOG(LogRHITest, Error, "%s", backendError.c_str());
				return result == arda::EArdaInitializeResult::Unavailable ||
				        result == arda::EArdaInitializeResult::ValidationUnavailable
				    ? SkippedExitCode
				    : EXIT_FAILURE;
			}

			FArdaTriangleRenderer renderer;
			if (!renderer.Initialize(arda::GetDevice(), swapChain->GetFormat()))
			{
				ARDA_LOG(LogRHITest, Error, "%s", renderer.GetError().c_str());
				return EXIT_FAILURE;
			}

			uint32_t renderedFrames = 0;
			while (window.PumpMessages())
			{
				uint32_t width = 0;
				uint32_t height = 0;
				if (window.ConsumeResize(width, height))
				{
					renderer.ReleaseFrameGraphs();
					if (!swapChain->Resize(width, height))
					{
						ARDA_LOG(LogRHITest, Error, "%s", swapChain->GetError().c_str());
						return EXIT_FAILURE;
					}
				}

				if (!renderer.RenderFrame(*swapChain))
				{
					ARDA_LOG(LogRHITest, Error, "%s", renderer.GetError().c_str());
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
	return arda::Run(argumentCount, arguments);
}
