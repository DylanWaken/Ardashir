#include "ArdaCornellBoxPch.h"

#include "ArdaCornellBoxConfig.h"
#include "ArdaCornellBoxRenderer.h"
#include "ArdaCornellBoxWindow.h"
#include "ShaderStructs/ArdaShaderDirectories.h"

#include <chrono>
#include <limits>

ARDA_DEFINE_LOG_CATEGORY_NAMED(LogCornellBox, "CornellBox", Log);

namespace arda
{
	namespace
	{
		constexpr int SkippedExitCode = 77;

		struct FArdaOptions
		{
			eastl::string mBackendName;
			FArdaCornellBoxSettings mRenderer;
			uint32_t mFrameLimit = 0;
			uint32_t mWindowWidth = 1280;
			uint32_t mWindowHeight = 720;
			bool mbHidden = false;
			bool mbFullscreen = false;
			arda::EArdaShaderCompilationMode mShaderMode = arda::EArdaShaderCompilationMode::OnDemand;
			std::filesystem::path mShaderCacheDirectory;
			std::filesystem::path mShaderSourceDirectory;
		};

		class FArdaMessageCallback final : public arda::IArdaDiagnosticCallback
		{
		public:
			void Message(arda::EArdaDiagnosticSeverity Severity, const char* MessageText) override
			{
				const char* Text = MessageText ? MessageText : "";
				switch (Severity)
				{
				case arda::EArdaDiagnosticSeverity::Warning:
					ARDA_LOG(LogCornellBox, Warning, "%s", Text);
					break;
				case arda::EArdaDiagnosticSeverity::Error:
					++mErrorCount;
					ARDA_LOG(LogCornellBox, Error, "%s", Text);
					break;
				case arda::EArdaDiagnosticSeverity::Fatal:
					++mErrorCount;
					ARDA_LOG(LogCornellBox, Fatal, "%s", Text);
					break;
				default:
					ARDA_LOG(LogCornellBox, Log, "%s", Text);
					break;
				}
			}

			[[nodiscard]] uint32_t GetErrorCount() const noexcept
			{
				return mErrorCount;
			}

		private:
			uint32_t mErrorCount = 0;
		};

		class FBackendShutdownGuard final
		{
		public:
			explicit FBackendShutdownGuard(eastl::unique_ptr<arda::IArdaSwapChain>& SwapChain)
			    : mSwapChain(SwapChain)
			{
			}

			~FBackendShutdownGuard()
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

		bool ParseUInt(const char* Text, uint32_t Minimum, uint32_t Maximum, uint32_t& Output)
		{
			if (!Text || Text[0] == '\0')
			{
				return false;
			}
			char* End = nullptr;
			const unsigned long Value = std::strtoul(Text, &End, 10);
			if (!End || *End != '\0' || Value < Minimum || Value > Maximum)
			{
				return false;
			}
			Output = static_cast<uint32_t>(Value);
			return true;
		}

		bool ParseOptions(int ArgumentCount, char** Arguments, FArdaOptions& Options, eastl::string& Error)
		{
			for (int Index = 1; Index < ArgumentCount; ++Index)
			{
				const eastl::string_view Argument(Arguments[Index]);
				if (Argument == "--hidden")
				{
					Options.mbHidden = true;
				}
				else if (Argument == "--fullscreen")
				{
					Options.mbFullscreen = true;
				}
				else if (Argument == "--no-compaction")
				{
					Options.mRenderer.mbCompactStaticBlas = false;
				}
				else if (Argument == "--backend" && Index + 1 < ArgumentCount)
				{
					const eastl::string_view Value(Arguments[++Index]);
					if (Value == "d3d12")
					{
						Options.mBackendName = "native-d3d12";
					}
					else if (Value == "vulkan")
					{
						Options.mBackendName = "native-vulkan";
					}
					else
					{
						Error = "--backend must be d3d12 or vulkan.";
						return false;
					}
				}
				else if (Argument == "--shader-mode" && Index + 1 < ArgumentCount)
				{
					const eastl::string_view Value(Arguments[++Index]);
					if (Value == "startup")
					{
						Options.mShaderMode = arda::EArdaShaderCompilationMode::Startup;
					}
					else if (Value == "ondemand")
					{
						Options.mShaderMode = arda::EArdaShaderCompilationMode::OnDemand;
					}
					else if (Value == "load-only")
					{
						Options.mShaderMode = arda::EArdaShaderCompilationMode::LoadOnly;
					}
					else
					{
						Error = "--shader-mode must be startup, ondemand, or load-only.";
						return false;
					}
				}
				else if ((Argument == "--shader-cache" || Argument == "--shader-source") && Index + 1 < ArgumentCount)
				{
					std::filesystem::path& Path =
					    Argument == "--shader-cache" ? Options.mShaderCacheDirectory : Options.mShaderSourceDirectory;
					Path = Arguments[++Index];
					if (Path.empty())
					{
						Error = "Shader path options require a directory.";
						return false;
					}
				}
				else if ((Argument == "--frames" || Argument == "--width" || Argument == "--height" ||
				             Argument == "--spp" || Argument == "--samples-per-dispatch" ||
				             Argument == "--max-samples" || Argument == "--max-bounces" || Argument == "--seed") &&
				    Index + 1 < ArgumentCount)
				{
					uint32_t* Destination = nullptr;
					uint32_t Minimum = 0;
					uint32_t Maximum = std::numeric_limits<uint32_t>::max();
					if (Argument == "--frames")
					{
						Destination = &Options.mFrameLimit;
					}
					else if (Argument == "--width")
					{
						Destination = &Options.mWindowWidth;
						Minimum = 1;
					}
					else if (Argument == "--height")
					{
						Destination = &Options.mWindowHeight;
						Minimum = 1;
					}
					else if (Argument == "--spp" || Argument == "--samples-per-dispatch")
					{
						Destination = &Options.mRenderer.mSamplesPerDispatch;
						Minimum = 1;
						Maximum = 64;
					}
					else if (Argument == "--max-samples")
					{
						Destination = &Options.mRenderer.mMaxSamples;
						Minimum = 1;
					}
					else if (Argument == "--max-bounces")
					{
						Destination = &Options.mRenderer.mMaxBounces;
						Minimum = 1;
						Maximum = 32;
					}
					else
					{
						Destination = &Options.mRenderer.mSeed;
					}
					if (!ParseUInt(Arguments[++Index], Minimum, Maximum, *Destination))
					{
						Error = "A numeric CornellBox option is out of range.";
						return false;
					}
				}
				else if (Argument == "--exposure" && Index + 1 < ArgumentCount)
				{
					char* End = nullptr;
					const float Value = std::strtof(Arguments[++Index], &End);
					if (!End || *End != '\0' || !std::isfinite(Value) || Value <= 0.0f)
					{
						Error = "--exposure requires a positive finite number.";
						return false;
					}
					Options.mRenderer.mExposure = Value;
				}
				else
				{
					Error = "Unknown or incomplete CornellBox command-line option.";
					return false;
				}
			}
			if (Options.mbHidden && Options.mbFullscreen)
			{
				Error = "--hidden and --fullscreen cannot be combined.";
				return false;
			}
			return true;
		}

		int Run(int ArgumentCount, char** Arguments)
		{
			FArdaOptions Options;
			eastl::string Error;
			if (!ParseOptions(ArgumentCount, Arguments, Options, Error))
			{
				ARDA_LOG(LogCornellBox, Error, "%s", Error.c_str());
				return EXIT_FAILURE;
			}

			const std::filesystem::path ExecutableDirectory = std::filesystem::absolute(Arguments[0]).parent_path();
			if (Options.mShaderCacheDirectory.empty())
			{
				Options.mShaderCacheDirectory = ExecutableDirectory / ".arda-cache" / "shaders";
			}
			if (Options.mShaderSourceDirectory.empty())
			{
				Options.mShaderSourceDirectory = GArdaCornellBoxShaderSourceDirectory;
			}
			const arda::FArdaShaderDirectoryStatus DirectoryStatus =
			    arda::AddShaderSourceDirectoryMapping("/ArdaTests/CornellBox", Options.mShaderSourceDirectory);
			if (!DirectoryStatus)
			{
				ARDA_LOG(LogCornellBox, Error, "%s", DirectoryStatus.mMessage.c_str());
				return EXIT_FAILURE;
			}

			FArdaMessageCallback Messages;
			arda::FArdaBackendConfiguration Configuration;
			Configuration.mBackendName = Options.mBackendName;
			Configuration.mbEnableValidation = true;
			Configuration.mMessageCallback = &Messages;
			Configuration.mShaderCompilationMode = Options.mShaderMode;
			Configuration.mShaderCacheDirectory = Options.mShaderCacheDirectory;
			Configuration.mRequiredFeatures.mbRequireHardwareRayTracing = true;
			Configuration.mRequiredFeatures.mbRequireRayTracingPipelines = true;
			Configuration.mRequiredFeatures.mbRequireAccelerationStructures = true;
			if (!arda::ConfigureBackend(Configuration))
			{
				ARDA_LOG(LogCornellBox, Error, "%s", arda::GetBackendError().c_str());
				return EXIT_FAILURE;
			}

			FArdaCornellBoxWindow Window;
			if (!Window.Create("Ardashir - RDG Cornell Box Path Tracer",
			        Options.mWindowWidth,
			        Options.mWindowHeight,
			        !Options.mbHidden,
			        Options.mbFullscreen))
			{
				ARDA_LOG(LogCornellBox, Error, "%s", Window.GetError().c_str());
				return Options.mbHidden ? SkippedExitCode : EXIT_FAILURE;
			}

			eastl::unique_ptr<arda::IArdaSwapChain> SwapChain;
			FBackendShutdownGuard Shutdown(SwapChain);
			const arda::EArdaInitializeResult InitializeResult =
			    arda::InitializeBackendForPresentation(Window, Window.GetWidth(), Window.GetHeight(), SwapChain);
			if (InitializeResult != arda::EArdaInitializeResult::Success)
			{
				ARDA_LOG(LogCornellBox, Error, "%s", arda::GetBackendError().c_str());
				return InitializeResult == arda::EArdaInitializeResult::Unavailable ||
				        InitializeResult == arda::EArdaInitializeResult::ValidationUnavailable
				    ? SkippedExitCode
				    : EXIT_FAILURE;
			}

			FArdaCornellBoxRenderer Renderer;
			if (!Renderer.Initialize(arda::GetDevice(), SwapChain->GetFormat(), Options.mRenderer))
			{
				ARDA_LOG(LogCornellBox, Error, "%s", Renderer.GetError().c_str());
				return EXIT_FAILURE;
			}

			uint32_t RenderedFrames = 0;
			auto PreviousFrameTime = std::chrono::steady_clock::now();
			while (Window.PumpMessages())
			{
				const auto FrameTime = std::chrono::steady_clock::now();
				const float DeltaSeconds = std::chrono::duration<float>(FrameTime - PreviousFrameTime).count();
				PreviousFrameTime = FrameTime;

				const FArdaCameraInput Input = Window.ConsumeCameraInput();
				Renderer.UpdateCamera(Input.mForward, Input.mRight, Input.mLookX, Input.mLookY, DeltaSeconds);

				uint32_t Width = 0;
				uint32_t Height = 0;
				if (Window.ConsumeResize(Width, Height))
				{
					// Cached persistent graphs retain swap-chain attachments until invalidated.
					Renderer.NotifyResize();
					if (!SwapChain->Resize(Width, Height))
					{
						ARDA_LOG(LogCornellBox, Error, "%s", SwapChain->GetError().c_str());
						return EXIT_FAILURE;
					}
				}

				if (!Renderer.RenderFrame(*SwapChain))
				{
					ARDA_LOG(LogCornellBox, Error, "%s", Renderer.GetError().c_str());
					return EXIT_FAILURE;
				}
				++RenderedFrames;
				if (Options.mFrameLimit > 0 && RenderedFrames >= Options.mFrameLimit)
				{
					break;
				}
			}

			ARDA_LOG(LogCornellBox,
			    Log,
			    "CornellBox completed with %u accumulated samples per pixel.",
			    Renderer.GetAccumulatedSamples());
			return Messages.GetErrorCount() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
		}
	}
}

int main(int ArgumentCount, char** Arguments)
{
	return arda::Run(ArgumentCount, Arguments);
}
