#include "ArdaARDGExamplePch.h"

#include "ArdaARDGExampleConfig.h"
#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ArdaSwapChain.h"
#include "ArdaGlfwWindow.h"
#include "ArdaTerrainRenderer.h"
#include "ShaderStructs/ArdaShaderCompiler.h"
#include "ShaderStructs/ArdaShaderDirectories.h"

#include <chrono>

ARDA_DEFINE_LOG_CATEGORY_NAMED(LogARDGExample, "ARDGExample", Log);

namespace arda
{
	namespace
	{
		constexpr int SkippedExitCode = 77;

		struct FArdaOptions
		{
			eastl::string mBackendName;
			uint32_t mFrameLimit = 0;
			uint32_t mWindowWidth = 1280;
			uint32_t mWindowHeight = 720;
			bool mbHidden = false;
			bool mbFullscreen = false;
			std::filesystem::path mShaderCookOutputDirectory;
			arda::EArdaShaderCompilationMode mShaderMode = arda::EArdaShaderCompilationMode::OnDemand;
			std::filesystem::path mShaderCacheDirectory;
			std::filesystem::path mShaderSourceDirectory;
		};

		class FArdaMessageCallback final : public arda::IArdaDiagnosticCallback
		{
		public:
			void Message(arda::EArdaDiagnosticSeverity severity, const char* messageText) override
			{
				const char* text = messageText ? messageText : "";
				switch (severity)
				{
				case arda::EArdaDiagnosticSeverity::Warning:
					ARDA_LOG(LogARDGExample, Warning, "%s", text);
					break;
				case arda::EArdaDiagnosticSeverity::Error:
					++mErrorCount;
					ARDA_LOG(LogARDGExample, Error, "%s", text);
					break;
				case arda::EArdaDiagnosticSeverity::Fatal:
					++mErrorCount;
					ARDA_LOG(LogARDGExample, Fatal, "%s", text);
					break;
				default:
					ARDA_LOG(LogARDGExample, Log, "%s", text);
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
				else if (argument == "--fullscreen")
				{
					options.mbFullscreen = true;
				}
				else if (argument == "--arda-cook-shaders" && index + 1 < argumentCount)
				{
					options.mShaderCookOutputDirectory = arguments[++index];
					if (options.mShaderCookOutputDirectory.empty())
					{
						error = "--arda-cook-shaders requires an output directory.";
						return false;
					}
				}
				else if (argument == "--shader-cache" && index + 1 < argumentCount)
				{
					options.mShaderCacheDirectory = arguments[++index];
					if (options.mShaderCacheDirectory.empty())
					{
						error = "--shader-cache requires a directory.";
						return false;
					}
				}
				else if (argument == "--shader-source" && index + 1 < argumentCount)
				{
					options.mShaderSourceDirectory = arguments[++index];
					if (options.mShaderSourceDirectory.empty())
					{
						error = "--shader-source requires a directory.";
						return false;
					}
				}
				else if (argument == "--shader-mode" && index + 1 < argumentCount)
				{
					const eastl::string_view mode(arguments[++index]);
					if (mode == "startup")
					{
						options.mShaderMode = arda::EArdaShaderCompilationMode::Startup;
					}
					else if (mode == "ondemand")
					{
						options.mShaderMode = arda::EArdaShaderCompilationMode::OnDemand;
					}
					else if (mode == "load-only")
					{
						options.mShaderMode = arda::EArdaShaderCompilationMode::LoadOnly;
					}
					else
					{
						error = "Unknown shader mode. Use startup, ondemand, or load-only.";
						return false;
					}
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
				else if ((argument == "--width" || argument == "--height") && index + 1 < argumentCount)
				{
					const char* dimensionText = arguments[++index];
					char* end = nullptr;
					const unsigned long parsed = std::strtoul(dimensionText, &end, 10);
					if (dimensionText[0] == '\0' || (end != nullptr && *end != '\0') || parsed == 0 ||
					    parsed > eastl::numeric_limits<uint32_t>::max())
					{
						error = "--width and --height require positive integers.";
						return false;
					}
					uint32_t& dimension = argument == "--width" ? options.mWindowWidth : options.mWindowHeight;
					dimension = static_cast<uint32_t>(parsed);
				}
				else
				{
					error = "Unknown or incomplete command-line option.";
					return false;
				}
			}
			if (options.mbHidden && options.mbFullscreen)
			{
				error = "--hidden and --fullscreen cannot be combined.";
				return false;
			}
			return true;
		}

		int CookRegisteredShaders(const std::filesystem::path& outputDirectory)
		{
			const arda::FArdaShaderDirectoryStatus scanStatus = arda::ScanAndFreezeShaderSourceDirectories();
			if (!scanStatus)
			{
				ARDA_LOG(LogARDGExample, Error, "Shader source scan failed: %s", scanStatus.mMessage.c_str());
				return EXIT_FAILURE;
			}

			eastl::vector<eastl::string> backends;
			for (const arda::FArdaBackendModuleDescriptor& module : arda::EnumerateBackendModules())
			{
				if (!module.mShaderArtifactExtension.empty())
				{
					backends.push_back(module.mName);
				}
			}
			const arda::FArdaShaderCompileResult result =
			    arda::CompileRegisteredShaderArtifacts(outputDirectory, backends);
			for (const arda::FArdaShaderCompileDiagnostic& diagnostic : result.mDiagnostics)
			{
				const std::string sourcePath = diagnostic.mSourcePath.string();
				const std::string outputPath = diagnostic.mOutputPath.string();
				ARDA_LOG(LogARDGExample,
				    Error,
				    "Shader cook diagnostic: shader=%s backend=%s "
				    "permutation=%u source=%s output=%s message=%s",
				    diagnostic.mShaderType.c_str(),
				    diagnostic.mBackendName.c_str(),
				    diagnostic.mPermutationId,
				    sourcePath.c_str(),
				    outputPath.c_str(),
				    diagnostic.mMessage.c_str());
			}

			const std::filesystem::path manifestPath = outputDirectory / "ArdaShaderManifest.json";
			ARDA_LOG(LogARDGExample,
			    Log,
			    "Shader cook: compiled=%u cache=%u skipped=%u manifest=%s",
			    result.mJobsCompiled,
			    result.mCacheHits,
			    result.mJobsSkipped,
			    manifestPath.string().c_str());
			return result ? EXIT_SUCCESS : EXIT_FAILURE;
		}

		std::filesystem::path GetExecutableDirectory(const char* executable)
		{
			return std::filesystem::absolute(executable).parent_path();
		}

		int Run(int argumentCount, char** arguments)
		{
			FArdaOptions options;
			eastl::string error;
			if (!ParseOptions(argumentCount, arguments, options, error))
			{
				ARDA_LOG(LogARDGExample, Error, "%s", error.c_str());
				return EXIT_FAILURE;
			}
			const std::filesystem::path executableDirectory = GetExecutableDirectory(arguments[0]);
			if (options.mShaderCacheDirectory.empty())
			{
				options.mShaderCacheDirectory = executableDirectory / ".arda-cache" / "shaders";
			}
			if (options.mShaderSourceDirectory.empty())
			{
				options.mShaderSourceDirectory = GArdaARDGShaderSourceDirectory;
			}

			const arda::FArdaShaderDirectoryStatus shaderDirectoryStatus =
			    arda::AddShaderSourceDirectoryMapping("/ArdaTests/ARDGExample", options.mShaderSourceDirectory);
			if (!shaderDirectoryStatus)
			{
				ARDA_LOG(LogARDGExample, Error, "%s", shaderDirectoryStatus.mMessage.c_str());
				return EXIT_FAILURE;
			}

			if (!options.mShaderCookOutputDirectory.empty())
			{
				return CookRegisteredShaders(options.mShaderCookOutputDirectory);
			}

			FArdaMessageCallback messageCallback;
			arda::FArdaBackendConfiguration configuration;
			configuration.mBackendName = options.mBackendName;
			configuration.mbEnableValidation = true;
			configuration.mMessageCallback = &messageCallback;
			configuration.mShaderCompilationMode = options.mShaderMode;
			configuration.mShaderCacheDirectory = options.mShaderCacheDirectory;
			if (!arda::ConfigureBackend(configuration))
			{
				ARDA_LOG(LogARDGExample, Error, "%s", arda::GetBackendError().c_str());
				return EXIT_FAILURE;
			}

			FArdaGlfwWindow window;
			if (!window.Create("Ardashir - Render Graph Terrain",
			        options.mWindowWidth,
			        options.mWindowHeight,
			        !options.mbHidden,
			        options.mbFullscreen))
			{
				ARDA_LOG(LogARDGExample, Error, "%s", window.GetError().c_str());
				return options.mbHidden ? SkippedExitCode : EXIT_FAILURE;
			}

			eastl::unique_ptr<arda::IArdaSwapChain> swapChain;
			FArdaBackendShutdownGuard shutdownGuard(swapChain);
			const arda::EArdaInitializeResult result =
			    arda::InitializeBackendForPresentation(window, window.GetWidth(), window.GetHeight(), swapChain);
			if (result != arda::EArdaInitializeResult::Success)
			{
				ARDA_LOG(LogARDGExample, Error, "%s", arda::GetBackendError().c_str());
				return result == arda::EArdaInitializeResult::Unavailable ||
				        result == arda::EArdaInitializeResult::ValidationUnavailable
				    ? SkippedExitCode
				    : EXIT_FAILURE;
			}

			FArdaTerrainRenderer renderer;
			if (!renderer.Initialize(arda::GetDevice(), swapChain->GetFormat()))
			{
				ARDA_LOG(LogARDGExample, Error, "%s", renderer.GetError().c_str());
				return EXIT_FAILURE;
			}

			uint32_t renderedFrames = 0;
			auto previousFrameTime = std::chrono::steady_clock::now();
			while (window.PumpMessages())
			{
				const auto frameTime = std::chrono::steady_clock::now();
				const float deltaSeconds = std::chrono::duration<float>(frameTime - previousFrameTime).count();
				previousFrameTime = frameTime;
				const FArdaCameraInput input = window.ConsumeCameraInput();
				renderer.UpdateCamera(input.mForward, input.mRight, input.mLookX, input.mLookY, deltaSeconds);

				uint32_t width = 0;
				uint32_t height = 0;
				if (window.ConsumeResize(width, height))
				{
					renderer.ReleaseFrameGraphs();
					if (!swapChain->Resize(width, height))
					{
						ARDA_LOG(LogARDGExample, Error, "%s", swapChain->GetError().c_str());
						return EXIT_FAILURE;
					}
				}

				if (!renderer.RenderFrame(*swapChain))
				{
					ARDA_LOG(LogARDGExample, Error, "%s", renderer.GetError().c_str());
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
