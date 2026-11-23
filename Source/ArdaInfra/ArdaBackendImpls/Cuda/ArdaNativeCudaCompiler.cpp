#include "ArdaNativeCudaCompiler.h"

#if defined(ARDA_NATIVE_COMPUTE_HAS_NVRTC)
#include <nvrtc.h>
#ifdef _WIN32
#include <cwchar>
#include <windows.h>
#endif

namespace arda
{
	namespace
	{
		// clang-format off
#define ARDA_NVRTC_FUNCTIONS(Apply)                                                        \
	Apply(Version, nvrtcVersion)                                                         \
	Apply(GetNumSupportedArchs, nvrtcGetNumSupportedArchs)                                  \
	Apply(GetSupportedArchs, nvrtcGetSupportedArchs)                                        \
	Apply(GetErrorString, nvrtcGetErrorString)                                              \
	Apply(CreateProgram, nvrtcCreateProgram)                                               \
	Apply(DestroyProgram, nvrtcDestroyProgram)                                             \
	Apply(CompileProgram, nvrtcCompileProgram)                                             \
	Apply(GetProgramLogSize, nvrtcGetProgramLogSize)                                        \
	Apply(GetProgramLog, nvrtcGetProgramLog)                                               \
	Apply(GetCUBINSize, nvrtcGetCUBINSize)                                                  \
	Apply(GetCUBIN, nvrtcGetCUBIN)
		// clang-format on

		struct FArdaNvrtcApi
		{
#ifdef _WIN32
			HMODULE mLibrary = nullptr;
			HMODULE mBuiltinsLibrary = nullptr;

#define ARDA_DECLARE_NVRTC_FUNCTION(Field, Name) decltype(&::Name) m##Field = nullptr;
			ARDA_NVRTC_FUNCTIONS(ARDA_DECLARE_NVRTC_FUNCTION)
#undef ARDA_DECLARE_NVRTC_FUNCTION

			FArdaNvrtcApi()
			{
#define ARDA_WIDE_LITERAL_IMPL(Literal) L##Literal
#define ARDA_WIDE_LITERAL(Literal) ARDA_WIDE_LITERAL_IMPL(Literal)
				if (!TryLoad(ARDA_WIDE_LITERAL(ARDA_NATIVE_NVRTC_DLL_NAME), LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))
				{
					TryLoad(ARDA_WIDE_LITERAL(ARDA_NATIVE_NVRTC_DLL_PATH),
					    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
				}
#undef ARDA_WIDE_LITERAL
#undef ARDA_WIDE_LITERAL_IMPL
			}

			~FArdaNvrtcApi()
			{
				if (mLibrary)
				{
					FreeLibrary(mLibrary);
				}
				if (mBuiltinsLibrary)
				{
					FreeLibrary(mBuiltinsLibrary);
				}
			}

		private:
			bool TryLoad(const wchar_t* Path, DWORD SearchFlags) noexcept
			{
				mLibrary = LoadLibraryExW(Path, nullptr, SearchFlags);
				if (!mLibrary)
				{
					return false;
				}
#define ARDA_LOAD_NVRTC_FUNCTION(Field, Name)                                                                          \
	m##Field = reinterpret_cast<decltype(m##Field)>(GetProcAddress(mLibrary, #Name));
				ARDA_NVRTC_FUNCTIONS(ARDA_LOAD_NVRTC_FUNCTION)
#undef ARDA_LOAD_NVRTC_FUNCTION
				if (HasFunctions())
				{
					mBuiltinsLibrary = LoadBuiltins();
					if (mBuiltinsLibrary)
					{
						return true;
					}
				}
				FreeLibrary(mLibrary);
				mLibrary = nullptr;
				return false;
			}

			HMODULE LoadBuiltins() const noexcept
			{
				int Major = 0, Minor = 0;
				if (mVersion(&Major, &Minor) != NVRTC_SUCCESS)
				{
					return nullptr;
				}
				constexpr DWORD Capacity = 32768;
				wchar_t Path[Capacity];
				const DWORD Length = GetModuleFileNameW(mLibrary, Path, Capacity);
				if (Length == 0 || Length >= Capacity)
				{
					return nullptr;
				}
				auto* Separator = std::wcsrchr(Path, L'\\');
				if (auto* ForwardSlash = std::wcsrchr(Path, L'/'); ForwardSlash && (!Separator || ForwardSlash > Separator))
				{
					Separator = ForwardSlash;
				}
				if (!Separator)
				{
					return nullptr;
				}
				auto* Filename = Separator + 1;
				const auto Available = Capacity - static_cast<size_t>(Filename - Path);
				const int Written = std::swprintf(Filename, Available, L"nvrtc-builtins64_%d%d.dll", Major, Minor);
				if (Written < 0 || static_cast<size_t>(Written) >= Available)
				{
					return nullptr;
				}
				// NVRTC opens this companion by name. Load the matching DLL without relying on PATH.
				return LoadLibraryExW(Path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
			}

		public:
#else
#define ARDA_DECLARE_NVRTC_FUNCTION(Field, Name) decltype(&::Name) m##Field = &::Name;
			ARDA_NVRTC_FUNCTIONS(ARDA_DECLARE_NVRTC_FUNCTION)
#undef ARDA_DECLARE_NVRTC_FUNCTION
#endif

			bool IsAvailable() const noexcept
			{
#ifdef _WIN32
				return mLibrary && mBuiltinsLibrary;
#else
				return HasFunctions();
#endif
			}

		private:
			bool HasFunctions() const noexcept
			{
#define ARDA_CHECK_NVRTC_FUNCTION(Field, Name) &&m##Field
				return true ARDA_NVRTC_FUNCTIONS(ARDA_CHECK_NVRTC_FUNCTION);
#undef ARDA_CHECK_NVRTC_FUNCTION
			}
		};

#undef ARDA_NVRTC_FUNCTIONS

		const FArdaNvrtcApi& GetNvrtcApi() noexcept
		{
			static const FArdaNvrtcApi Api;
			return Api;
		}

		struct FArdaNvrtcProgram
		{
			const FArdaNvrtcApi& mApi;
			nvrtcProgram mProgram{};

			~FArdaNvrtcProgram()
			{
				if (mProgram)
				{
					mApi.mDestroyProgram(&mProgram);
				}
			}
		};

		FArdaRHIStatus CompilerError(nvrtcResult Result, const char* Diagnostic)
		{
			eastl::string Message = Diagnostic;
			Message += " ";
			Message += GetNvrtcApi().mGetErrorString(Result);
			const auto Code = Result == NVRTC_ERROR_BUILTIN_OPERATION_FAILURE ? EArdaRHIResult::Unsupported
			                                                                  : EArdaRHIResult::BackendFailure;
			return {Code, eastl::move(Message)};
		}
	}

	bool HasArdaNativeCudaCompiler() noexcept
	{
		return GetNvrtcApi().IsAvailable();
	}

	FArdaRHIStatus CheckArdaNativeCudaCompilerTarget(uint32_t ComputeCapability)
	{
		if (!HasArdaNativeCudaCompiler())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "The NVRTC compiler runtime is unavailable.");
		}
		static const auto Supported = []() -> TArdaRHIResult<eastl::vector<int>>
		{
			const auto& Api = GetNvrtcApi();
			int Count = 0;
			auto Result = Api.mGetNumSupportedArchs(&Count);
			if (Result != NVRTC_SUCCESS)
			{
				return {{}, CompilerError(Result, "Querying NVRTC architecture count failed.")};
			}
			eastl::vector<int> Targets(Count);
			Result = Api.mGetSupportedArchs(Targets.data());
			if (Result != NVRTC_SUCCESS)
			{
				return {{}, CompilerError(Result, "Querying NVRTC architectures failed.")};
			}
			return {eastl::move(Targets), {}};
		}();
		if (!Supported)
		{
			return Supported.mStatus;
		}
		for (const auto Target : Supported.mValue)
		{
			if (static_cast<uint32_t>(Target) == ComputeCapability)
			{
				return {};
			}
		}
		return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
		    "The installed NVRTC compiler does not support the selected CUDA architecture.");
	}

	TArdaRHIResult<FArdaCudaModuleDesc> CompileArdaNativeCudaSource(const eastl::string& Source,
	    uint32_t ComputeCapability)
	{
		if (Source.empty() || Source.find('\0') != eastl::string::npos || ComputeCapability == 0)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			        "CUDA C++ requires nonempty source text and a concrete compute capability.")};
		}

		if (auto Status = CheckArdaNativeCudaCompilerTarget(ComputeCapability); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		const auto& Api = GetNvrtcApi();
		FArdaNvrtcProgram Program{Api};
		auto Result = Api.mCreateProgram(&Program.mProgram, Source.c_str(), "ArdaNativeKernel.cu", 0, nullptr, nullptr);
		if (Result != NVRTC_SUCCESS)
		{
			return {{}, CompilerError(Result, "Creating the CUDA C++ compilation unit failed.")};
		}

		// A native image avoids requiring the driver to JIT the compiler's PTX ISA.
		eastl::string Target;
		Target.sprintf("--gpu-architecture=sm_%u", ComputeCapability);
		const char* Options[] = {Target.c_str(), "--std=c++17"};
		Result = Api.mCompileProgram(Program.mProgram, 2, Options);
		if (Result != NVRTC_SUCCESS)
		{
			auto Status = CompilerError(Result, "CUDA C++ compilation failed.");
			size_t LogSize = 0;
			if (Api.mGetProgramLogSize(Program.mProgram, &LogSize) == NVRTC_SUCCESS && LogSize > 1)
			{
				eastl::vector<char> Log(LogSize);
				if (Api.mGetProgramLog(Program.mProgram, Log.data()) == NVRTC_SUCCESS)
				{
					Status.mMessage += "\n";
					Status.mMessage += Log.data();
				}
			}
			return {{}, eastl::move(Status)};
		}

		size_t Size = 0;
		Result = Api.mGetCUBINSize(Program.mProgram, &Size);
		if (Result != NVRTC_SUCCESS)
		{
			return {{}, CompilerError(Result, "Querying compiled CUDA cubin size failed.")};
		}
		FArdaCudaModuleDesc Module;
		Module.mFormat = EArdaCudaModuleFormat::Cubin;
		Module.mImage.resize(Size);
		Result = Api.mGetCUBIN(Program.mProgram, reinterpret_cast<char*>(Module.mImage.data()));
		if (Result != NVRTC_SUCCESS)
		{
			return {{}, CompilerError(Result, "Reading compiled CUDA cubin failed.")};
		}
		int Major = 0;
		int Minor = 0;
		Api.mVersion(&Major, &Minor);
		Module.mBuild.mName = "ArdaNativeCuda";
		Module.mBuild.mIdentity.sprintf("NVRTC %d.%d sm_%u", Major, Minor, ComputeCapability);
		Module.mBuild.mArchitectures.push_back({ComputeCapability, false});
		return {eastl::move(Module), {}};
	}
}
#else
namespace arda
{
	bool HasArdaNativeCudaCompiler() noexcept
	{
		return false;
	}

	FArdaRHIStatus CheckArdaNativeCudaCompilerTarget(uint32_t)
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
		    "CUDA C++ source compilation requires an NVRTC-enabled ArdaNativeCompute build.");
	}

	TArdaRHIResult<FArdaCudaModuleDesc> CompileArdaNativeCudaSource(const eastl::string&, uint32_t)
	{
		return {{}, CheckArdaNativeCudaCompilerTarget(0)};
	}
}
#endif
