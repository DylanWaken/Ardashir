#include "RHI/Shaders/ArdaShaderCompiler.h"

#include "RHI/Providers/ArdaBackendProvider.h"

#include "RHI/Resources/ArdaHash.h"
#include "FileOperations/ArdaFileOperations.h"
#include "RHI/Context/ArdaShaderCompilerContext.h"
#include "FileOperations/ArdaString.h"

#include "RHI/Shaders/ArdaGlobalShaderMap.h"
#include "RHI/Shaders/ArdaShaderDirectories.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <EASTL/map.h>
#include <EASTL/shared_ptr.h>
#include <mutex>
#include <sstream>
#include <EASTL/set.h>
#include <EASTL/string.h>
#include <string>
#include <system_error>
#include <EASTL/vector.h>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace arda
{
	namespace
	{
		constexpr const char* CacheSchema = "ArdaShaderCompileKey-v4";

		FArdaShaderCompilerContext& GetCompilerContext()
		{
			static FArdaShaderCompilerContext Context;
			return Context;
		}

		FArdaShaderCompilerConfiguration MakeDefaultConfiguration()
		{
			FArdaShaderCompilerConfiguration Result;
#if defined(ARDA_DEFAULT_DXC_EXECUTABLE)
			Result.mCompilerExecutable = ARDA_DEFAULT_DXC_EXECUTABLE;
#endif
			if (Result.mCompilerExecutable.empty())
			{
				if (const char* Environment = std::getenv("ARDASHIR_DXC_EXECUTABLE"))
				{
					Result.mCompilerExecutable = Environment;
				}
			}
			return Result;
		}

		std::filesystem::path ResolveCompiler(const FArdaShaderCompilerConfiguration& Configuration)
		{
			std::filesystem::path Result = Configuration.mCompilerExecutable;
			if (Result.empty())
			{
				Result = MakeDefaultConfiguration().mCompilerExecutable;
			}
			if (Result.empty())
			{
				return {};
			}
			std::error_code Error;
			if (!std::filesystem::is_regular_file(Result, Error) || Error)
			{
				return {};
			}
			return std::filesystem::absolute(Result, Error);
		}

		FArdaShaderCompileDiagnostic MakeDiagnostic(EArdaShaderCompileError Code,
		    const FArdaShaderType* Type,
		    const eastl::string& BackendName,
		    uint32_t PermutationId,
		    const std::filesystem::path& Source,
		    const std::filesystem::path& Output,
		    const eastl::string& Message)
		{
			FArdaShaderCompileDiagnostic Result;
			Result.mCode = Code;
			Result.mShaderType = Type != nullptr ? Type->GetName() : "";
			Result.mBackendName = BackendName;
			Result.mPermutationId = PermutationId;
			Result.mSourcePath = Source;
			Result.mOutputPath = Output;
			Result.mMessage = Message;
			return Result;
		}

		FArdaShaderCompileDiagnostic MakeDiagnostic(EArdaShaderCompileError Code,
		    const FArdaShaderCompileJob& Job,
		    const eastl::string& Message)
		{
			return MakeDiagnostic(Code,
			    &Job.mType,
			    Job.mTarget.mBackendName,
			    Job.mPermutationId,
			    Job.mSourcePath,
			    Job.mOutputPath,
			    Message);
		}

		eastl::string ProfileForStage(arda::EArdaRHIShaderStage Stage)
		{
			using FArdaStageType = arda::EArdaRHIShaderStage;
			if (Stage == FArdaStageType::Vertex)
			{
				return "vs_6_0";
			}
			if (Stage == FArdaStageType::Pixel)
			{
				return "ps_6_0";
			}
			if (Stage == FArdaStageType::Compute)
			{
				return "cs_6_0";
			}
			if (Stage == FArdaStageType::Geometry)
			{
				return "gs_6_0";
			}
			if (Stage == FArdaStageType::Hull)
			{
				return "hs_6_0";
			}
			if (Stage == FArdaStageType::Domain)
			{
				return "ds_6_0";
			}
			if (Stage == FArdaStageType::Amplification)
			{
				return "as_6_5";
			}
			if (Stage == FArdaStageType::Mesh)
			{
				return "ms_6_5";
			}
			if (Stage == FArdaStageType::WorkGraph)
			{
				return "lib_6_8";
			}
			if (arda::IsArdaRHIRayTracingShaderStage(Stage))
			{
				return "lib_6_3";
			}
			return {};
		}

		bool ContainsControl(const eastl::string& Value)
		{
			for (const unsigned char Character : Value)
			{
				if (Character < 0x20 || Character == 0x7f)
				{
					return true;
				}
			}
			return false;
		}

		bool IsValidDefineName(const eastl::string& Name)
		{
			if (Name.empty() || !(std::isalpha(static_cast<unsigned char>(Name.front())) || Name.front() == '_'))
			{
				return false;
			}
			for (const unsigned char Character : Name)
			{
				if (!(std::isalnum(Character) || Character == '_'))
				{
					return false;
				}
			}
			return true;
		}

		void HashBytes(uint64_t& Hash, const void* Data, size_t Size)
		{
			AppendArdaFnv1a64(Hash, Data, Size);
		}

		void HashString(uint64_t& Hash, const eastl::string& Value)
		{
			AppendArdaFnv1a64LittleEndian(Hash, Value.size());
			HashBytes(Hash, Value.data(), Value.size());
		}

		void HashUint32(uint64_t& Hash, uint32_t Value)
		{
			AppendArdaFnv1a64LittleEndian(Hash, Value, sizeof(Value));
		}

		bool HashFile(uint64_t& Hash, const std::filesystem::path& Path)
		{
			std::ifstream Stream(Path, std::ios::binary);
			if (!Stream)
			{
				return false;
			}
			char Buffer[64 * 1024];
			while (Stream)
			{
				Stream.read(Buffer, sizeof(Buffer));
				const std::streamsize Count = Stream.gcount();
				if (Count > 0)
				{
					HashBytes(Hash, Buffer, static_cast<size_t>(Count));
				}
			}
			return Stream.eof();
		}

		bool HashLocalSourceTree(uint64_t& Hash,
		    const std::filesystem::path& PhysicalPath,
		    const eastl::string& LogicalIdentity,
		    eastl::set<eastl::string>& Visiting,
		    eastl::set<eastl::string>& Hashed,
		    eastl::string& ErrorMessage)
		{
			std::error_code Error;
			const eastl::string Canonical = ToEastl(std::filesystem::weakly_canonical(PhysicalPath, Error).generic_string());
			if (Error || !std::filesystem::is_regular_file(PhysicalPath, Error) || Error)
			{
				ErrorMessage = "Unable to resolve local shader include: " + ToEastl(PhysicalPath.generic_string());
				return false;
			}
			if (Hashed.count(Canonical) != 0)
			{
				return true;
			}
			if (!Visiting.insert(Canonical).second)
			{
				return true;
			}
			const eastl::string Contents = fileops::ReadText(PhysicalPath);
			HashString(Hash, LogicalIdentity);
			HashString(Hash, Contents);

			std::istringstream Lines(ToStd(Contents));
			std::string Line;
			while (std::getline(Lines, Line))
			{
				const size_t HashPosition = Line.find('#');
				if (HashPosition == std::string::npos)
				{
					continue;
				}
				size_t Cursor = HashPosition + 1;
				while (Cursor < Line.size() && std::isspace(static_cast<unsigned char>(Line[Cursor])))
				{
					++Cursor;
				}
				if (Line.compare(Cursor, 7, "include") != 0)
				{
					continue;
				}
				Cursor += 7;
				while (Cursor < Line.size() && std::isspace(static_cast<unsigned char>(Line[Cursor])))
				{
					++Cursor;
				}
				if (Cursor == Line.size() || Line[Cursor] != '"')
				{
					continue;
				}
				const size_t End = Line.find('"', Cursor + 1);
				if (End == std::string::npos)
				{
					continue;
				}
				const std::filesystem::path Relative = Line.substr(Cursor + 1, End - Cursor - 1);
				const std::filesystem::path Included = (PhysicalPath.parent_path() / Relative).lexically_normal();
				const eastl::string ChildIdentity = ToEastl((std::filesystem::path(LogicalIdentity.c_str()).parent_path() / Relative)
				                                      .lexically_normal()
				                                      .generic_string());
				if (!HashLocalSourceTree(Hash, Included, ChildIdentity, Visiting, Hashed, ErrorMessage))
				{
					return false;
				}
			}
			Visiting.erase(Canonical);
			Hashed.insert(Canonical);
			return true;
		}

		bool HasShaderSourceExtension(const std::filesystem::path& Path)
		{
			eastl::string Extension = ToEastl(Path.extension().string());
			eastl::transform(Extension.begin(),
			    Extension.end(),
			    Extension.begin(),
			    [](unsigned char Character)
			    {
				    return static_cast<char>(std::tolower(Character));
			    });
			return Extension == ".hlsl" || Extension == ".hlsli" || Extension == ".usf" || Extension == ".ush";
		}

		bool HashShaderSourceDirectory(uint64_t& Hash,
		    const std::filesystem::path& Directory,
		    eastl::set<eastl::string>& HashedRoots,
		    eastl::string& ErrorMessage)
		{
			std::error_code Error;
			const std::filesystem::path Root = std::filesystem::weakly_canonical(Directory, Error);
			if (Error || !std::filesystem::is_directory(Root, Error) || Error)
			{
				ErrorMessage =
				    "Configured shader include root is missing or not a directory: " + ToEastl(Directory.generic_string());
				return false;
			}
			if (!HashedRoots.insert(ToEastl(Root.generic_string())).second)
			{
				return true;
			}
			eastl::vector<std::filesystem::path> Files;
			std::filesystem::recursive_directory_iterator Iterator(Root,
			    std::filesystem::directory_options::skip_permission_denied,
			    Error);
			const std::filesystem::recursive_directory_iterator End;
			while (!Error && Iterator != End)
			{
				if (Iterator->is_regular_file(Error) && !Error && HasShaderSourceExtension(Iterator->path()))
				{
					Files.push_back(Iterator->path());
				}
				Iterator.increment(Error);
			}
			if (Error)
			{
				ErrorMessage = "Unable to enumerate shader include root: " + ToEastl(Root.generic_string());
				return false;
			}
			eastl::sort(Files.begin(),
			    Files.end(),
			    [&Root](const auto& Left, const auto& Right)
			    {
				    return Left.lexically_relative(Root).generic_string() <
				        Right.lexically_relative(Root).generic_string();
			    });
			HashString(Hash, ToEastl(Root.generic_string()));
			for (const auto& File : Files)
			{
				HashString(Hash, ToEastl(File.lexically_relative(Root).generic_string()));
				if (!HashFile(Hash, File))
				{
					ErrorMessage = "Unable to hash shader dependency: " + ToEastl(File.generic_string());
					return false;
				}
			}
			return true;
		}

		bool ResolveSource(const FArdaShaderType& Type,
		    const FArdaShaderCompilerConfiguration& Configuration,
		    std::filesystem::path& Out)
		{
			const char* Stem = Type.GetSourceStem();
			if (Stem == nullptr || Stem[0] == '\0')
			{
				return false;
			}
			if (Stem[0] == '/')
			{
				return static_cast<bool>(ResolveVirtualShaderSource(Stem, Out));
			}
			std::filesystem::path Candidate = Stem;
			if (!Configuration.mSourceRoot.empty() && !Candidate.is_absolute())
			{
				Candidate = Configuration.mSourceRoot / Candidate;
			}
			std::error_code Error;
			if (!std::filesystem::is_regular_file(Candidate, Error) || Error)
			{
				return false;
			}
			Out = std::filesystem::absolute(Candidate, Error);
			return !Error;
		}

		eastl::string KeyText(uint64_t Key)
		{
			std::ostringstream Stream;
			Stream << std::hex << std::setfill('0') << std::setw(16) << Key << '\n';
			return ToEastl(Stream.str());
		}

		bool ReadKey(const std::filesystem::path& Path, uint64_t& Out)
		{
			std::ifstream Stream(Path);
			std::string Value;
			if (!(Stream >> Value) || Value.size() != 16)
			{
				return false;
			}
			std::istringstream Parser(Value);
			Parser >> std::hex >> Out;
			return !Parser.fail();
		}

		bool IsContainedArtifactPath(const std::filesystem::path& Directory, const std::filesystem::path& Path)
		{
			if (!fileops::IsDirectChildPath(Directory, Path))
			{
				return false;
			}
			const auto Candidate = Path.lexically_normal();
			const eastl::string Name = ToEastl(Candidate.stem().string());
			return !Name.empty() && Name[0] != '.' && Name.find("..") == eastl::string::npos;
		}

		eastl::shared_ptr<std::mutex> GetOutputMutex(const std::filesystem::path& Output)
		{
			std::error_code Error;
			const eastl::string Key = ToEastl(std::filesystem::absolute(Output, Error).lexically_normal().generic_string());
			std::lock_guard<std::mutex> Lock(GetCompilerContext().mOutputMutexMapMutex);
			auto& Weak = GetCompilerContext().mOutputMutexes[Key];
			auto Result = Weak.lock();
			if (!Result)
			{
				Result = eastl::make_shared<std::mutex>();
				Weak = Result;
			}
			return Result;
		}

		eastl::wstring QuoteWindowsArgument(const eastl::wstring& Value)
		{
			if (!Value.empty() && Value.find_first_of(L" \t\n\v\"") == eastl::wstring::npos)
			{
				return Value;
			}
			eastl::wstring Result = L"\"";
			size_t Backslashes = 0;
			for (const wchar_t Character : Value)
			{
				if (Character == L'\\')
				{
					++Backslashes;
					continue;
				}
				if (Character == L'"')
				{
					Result.append(Backslashes * 2 + 1, L'\\');
					Result.push_back(L'"');
					Backslashes = 0;
					continue;
				}
				Result.append(Backslashes, L'\\');
				Backslashes = 0;
				Result.push_back(Character);
			}
			Result.append(Backslashes * 2, L'\\');
			Result.push_back(L'"');
			return Result;
		}

		bool LaunchCompilerDirect(const std::filesystem::path& Compiler,
		    const eastl::vector<eastl::string>& Arguments,
		    const std::filesystem::path& Log,
		    int& ExitCode)
		{
#if defined(_WIN32)
			SECURITY_ATTRIBUTES Security{sizeof(Security), nullptr, TRUE};
			HANDLE LogHandle = CreateFileW(Log.c_str(),
			    GENERIC_WRITE,
			    FILE_SHARE_READ,
			    &Security,
			    CREATE_ALWAYS,
			    FILE_ATTRIBUTE_NORMAL,
			    nullptr);
			if (LogHandle == INVALID_HANDLE_VALUE)
			{
				return false;
			}
			eastl::wstring Command = QuoteWindowsArgument(Compiler.wstring().c_str());
			for (const auto& Argument : Arguments)
			{
				Command += L" " + QuoteWindowsArgument(std::filesystem::path(Argument.c_str()).wstring().c_str());
			}
			STARTUPINFOW Startup{};
			Startup.cb = sizeof(Startup);
			Startup.dwFlags = STARTF_USESTDHANDLES;
			Startup.hStdOutput = LogHandle;
			Startup.hStdError = LogHandle;
			Startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
			PROCESS_INFORMATION Process{};
			const BOOL Started = CreateProcessW(Compiler.c_str(),
			    Command.data(),
			    nullptr,
			    nullptr,
			    TRUE,
			    CREATE_NO_WINDOW,
			    nullptr,
			    nullptr,
			    &Startup,
			    &Process);
			CloseHandle(LogHandle);
			if (!Started)
			{
				return false;
			}
			WaitForSingleObject(Process.hProcess, INFINITE);
			DWORD NativeExit = 1;
			GetExitCodeProcess(Process.hProcess, &NativeExit);
			CloseHandle(Process.hThread);
			CloseHandle(Process.hProcess);
			ExitCode = static_cast<int>(NativeExit);
			return true;
#else
			const pid_t Process = fork();
			if (Process < 0)
			{
				return false;
			}
			if (Process == 0)
			{
				const int LogFd = open(Log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
				if (LogFd < 0)
				{
					_exit(127);
				}
				dup2(LogFd, STDOUT_FILENO);
				dup2(LogFd, STDERR_FILENO);
				close(LogFd);
				eastl::vector<eastl::string> Storage;
				Storage.push_back(ToEastl(Compiler.string()));
				for (const auto& Argument : Arguments)
				{
					Storage.push_back(Argument);
				}
				eastl::vector<char*> Native;
				for (eastl::string& Argument : Storage)
				{
					Native.push_back(Argument.data());
				}
				Native.push_back(nullptr);
				execv(Compiler.c_str(), Native.data());
				_exit(127);
			}
			int Status = 0;
			if (waitpid(Process, &Status, 0) < 0)
			{
				return false;
			}
			ExitCode = WIFEXITED(Status) ? WEXITSTATUS(Status) : 1;
			return true;
#endif
		}

		bool PopulateJob(const FArdaShaderType& Type,
		    const FArdaShaderTarget& Target,
		    uint32_t PermutationId,
		    const std::filesystem::path& OutputDirectory,
		    const FArdaShaderCompilerConfiguration& Configuration,
		    const std::filesystem::path& Compiler,
		    FArdaShaderCompileJob& Job,
		    FArdaShaderCompileDiagnostic& Diagnostic)
		{

			const auto Fail = [&](EArdaShaderCompileError Code, const eastl::string& Message)
			{
				Diagnostic = MakeDiagnostic(Code, Job, Message);
				return false;
			};
			Job.mType = Type;
			Job.mTarget = Target;
			Job.mCompilerExecutable = Compiler;
			Job.mPermutationId = PermutationId;
			const eastl::string Stem = Type.GetPermutationArtifactStem(PermutationId);
			Job.mOutputPath = OutputDirectory / (Stem + Target.mArtifactExtension).c_str();
			if (!IsContainedArtifactPath(OutputDirectory, Job.mOutputPath))
			{
				return Fail(EArdaShaderCompileError::InvalidPermutation,
				    "Generated shader artifact path escapes its output directory.");
			}
			if (PermutationId >= Type.GetPermutationCount())
			{
				return Fail(EArdaShaderCompileError::InvalidPermutation, "Invalid shader permutation identifier.");
			}
			Job.mProfile = ProfileForStage(Type.GetStage());
			if (Job.mProfile.empty())
			{
				return Fail(EArdaShaderCompileError::UnsupportedStage,
				    "Combined, empty, or unknown shader stages cannot map to one fallback compiler profile.");
			}
			if (!ResolveSource(Type, Configuration, Job.mSourcePath))
			{
				return Fail(EArdaShaderCompileError::SourceResolutionFailed,
				    eastl::string("Unable to resolve registered shader source stem: ") + Type.GetSourceStem());
			}
			{
				const std::filesystem::path Registered(Type.GetSourceStem());
				Job.mSourceIdentity = ToEastl(
				    (Type.GetSourceStem()[0] == '/' || !Registered.is_absolute() ? Registered : Registered.filename())
				        .lexically_normal()
				        .generic_string());
			}
			Job.mEnvironment = Type.BuildCompilationEnvironment(Target, PermutationId);

			Job.mArguments.push_back("-nologo");
			Job.mArguments.push_back("-T");
			Job.mArguments.push_back(Job.mProfile);
			if (Job.mProfile.rfind("lib_", 0) != 0 && Type.GetEntryPoint()[0] != '\0')
			{
				Job.mArguments.push_back("-E");
				Job.mArguments.push_back(Type.GetEntryPoint());
			}
			IArdaBackendModule* BackendModule = FindBackendModule(Target.mBackendName.c_str());
			for (const FArdaShaderDefine& Define : Job.mEnvironment.GetDefines())
			{
				const eastl::string Name = Define.mName;
				const eastl::string Value = Define.mValue;
				if (!IsValidDefineName(Name) || ContainsControl(Value))
				{
					return Fail(EArdaShaderCompileError::InvalidPermutation,
					    "A shader define has an unsafe name or control character.");
				}
				Job.mArguments.push_back("-D");
				Job.mArguments.push_back(Define.mName + "=" + Define.mValue);
			}
			const auto AppendArguments = [&](const eastl::vector<eastl::string>& Arguments)
			{
				for (const auto& Argument : Arguments)
				{
					Job.mArguments.push_back(Argument);
				}
			};
			AppendArguments(Configuration.mCommonArguments);
			for (const FArdaShaderCompilerModuleArguments& ModuleArguments : Configuration.mModuleArguments)
			{
				if (ModuleArguments.mBackendName == Target.mBackendName)
				{
					AppendArguments(ModuleArguments.mArguments);
				}
			}
			if (BackendModule)
			{
				FArdaBackendShaderCompileInvocation Invocation;
				Invocation.mSourcePath = Job.mSourcePath;
				Invocation.mOutputPath = Job.mOutputPath;
				Invocation.mCompilerExecutable = Job.mCompilerExecutable;
				Invocation.mEntryPoint = Type.GetEntryPoint();
				Invocation.mStage = Type.GetStage();
				Invocation.mProfile = Job.mProfile;
				Invocation.mArguments = Job.mArguments;
				const arda::FArdaRHIStatus Status = BackendModule->ConfigureShaderCompileInvocation(Invocation);
				if (!Status)
				{
					return Fail(EArdaShaderCompileError::UnsupportedStage, Status.mMessage);
				}
				Job.mSourcePath = eastl::move(Invocation.mSourcePath);
				Job.mOutputPath = eastl::move(Invocation.mOutputPath);
				Job.mCompilerExecutable = eastl::move(Invocation.mCompilerExecutable);
				Job.mProfile = eastl::move(Invocation.mProfile);
				Job.mArguments = eastl::move(Invocation.mArguments);
				if (!IsContainedArtifactPath(OutputDirectory, Job.mOutputPath))
				{
					return Fail(EArdaShaderCompileError::InvalidPermutation,
					    "The backend module selected an artifact outside the output directory.");
				}
			}
			for (const auto& Argument : Job.mArguments)
			{
				if (ContainsControl(Argument))
				{
					return Fail(EArdaShaderCompileError::InvalidPermutation,
					    "A custom compiler argument contains a control character.");
				}
			}
			uint64_t Hash = arda::ArdaFnv1a64OffsetBasis;
			HashString(Hash, CacheSchema);
			HashString(Hash, Type.GetName());
			HashString(Hash, Target.mBackendName);
			HashUint32(Hash, static_cast<uint32_t>(Target.mBinaryFormat));
			HashUint32(Hash, PermutationId);
			HashString(Hash, Job.mProfile);
			for (const auto& Argument : Job.mArguments)
			{
				HashString(Hash, Argument);
			}
			HashString(Hash, Job.mSourceIdentity);
			if (!Job.mCompilerExecutable.empty() && !HashFile(Hash, Job.mCompilerExecutable))
			{
				return Fail(EArdaShaderCompileError::CompilerUnavailable,
				    "Unable to hash the configured compiler executable.");
			}
			if (Job.mCompilerExecutable.empty())
			{
				if (Target.mCompilerIdentity.empty())
				{
					return Fail(EArdaShaderCompileError::CompilerUnavailable,
					    "The backend module has neither a compiler executable nor a stable in-process compiler identity.");
				}
				HashString(Hash, Target.mCompilerIdentity);
			}
			for (const auto& File : EnumerateShaderSourceFiles())
			{
				HashString(Hash, File.mVirtualPath);
				if (!HashFile(Hash, File.mPhysicalPath))
				{
					return Fail(EArdaShaderCompileError::SourceResolutionFailed,
					    "Unable to hash a file in the frozen shader-source manifest.");
				}
			}
			if (Type.GetSourceStem()[0] != '/')
			{
				eastl::set<eastl::string> Visiting;
				eastl::set<eastl::string> Hashed;
				eastl::string DependencyError;
				if (!HashLocalSourceTree(Hash,
				        Job.mSourcePath,
				        Job.mSourceIdentity,
				        Visiting,
				        Hashed,
				        DependencyError))
				{
					return Fail(EArdaShaderCompileError::SourceResolutionFailed, DependencyError);
				}
				eastl::set<eastl::string> HashedRoots;
				if (!HashShaderSourceDirectory(Hash, Job.mSourcePath.parent_path(), HashedRoots, DependencyError))
				{
					return Fail(EArdaShaderCompileError::SourceResolutionFailed, DependencyError);
				}
				for (size_t ArgumentIndex = 0; ArgumentIndex < Job.mArguments.size(); ++ArgumentIndex)
				{
					const eastl::string Argument = Job.mArguments[ArgumentIndex];
					eastl::string IncludeRoot;
					if (Argument == "-I")
					{
						if (++ArgumentIndex >= Job.mArguments.size())
						{
							DependencyError = "Custom shader compiler argument -I requires a directory.";
						}
						else
						{
							IncludeRoot = Job.mArguments[ArgumentIndex];
						}
					}
					else if (Argument.rfind("-I", 0) == 0 && Argument.size() > 2)
					{
						IncludeRoot = Argument.substr(2);
					}
					else
					{
						continue;
					}
					if (IncludeRoot.empty() ||
					    !HashShaderSourceDirectory(Hash,
					        std::filesystem::path(IncludeRoot.c_str()),
					        HashedRoots,
					        DependencyError))
					{
						return Fail(EArdaShaderCompileError::SourceResolutionFailed, DependencyError);
					}
				}
			}
			Job.mInputKey = Hash;
			Job.mArguments.push_back(ToEastl(Job.mSourcePath.string()));
			return true;
		}

		bool CompileJob(const FArdaShaderCompileJob& Job,
		    const std::filesystem::path& Compiler,
		    FArdaShaderCompileDiagnostic& Diagnostic,
		    bool SkipIfCurrent = false,
		    bool* OutCacheHit = nullptr)
		{
			const auto Fail = [&](EArdaShaderCompileError Code, const eastl::string& Message)
			{
				Diagnostic = MakeDiagnostic(Code, Job, Message);
				return false;
			};
			std::error_code Error;
			std::filesystem::create_directories(Job.mOutputPath.parent_path(), Error);
			if (Error)
			{
				return Fail(EArdaShaderCompileError::DirectoryCreationFailed,
				    "Unable to create the shader artifact directory.");
			}
			const auto Mutex = GetOutputMutex(Job.mOutputPath);
			std::lock_guard<std::mutex> Lock(*Mutex);
			if (SkipIfCurrent)
			{
				uint64_t StoredKey = 0;
				if (ReadKey(Job.mOutputPath.string() + ".arda-key", StoredKey) && StoredKey == Job.mInputKey &&
				    fileops::IsRegularNonEmpty(Job.mOutputPath))
				{
					if (OutCacheHit != nullptr)
					{
						*OutCacheHit = true;
					}
					return true;
				}
			}
			const std::filesystem::path TemporaryOutput = fileops::TemporaryPath(Job.mOutputPath, "output");
			const std::filesystem::path TemporarySidecar = fileops::TemporaryPath(Job.mOutputPath, "key");
			const std::filesystem::path Log = fileops::TemporaryPath(Job.mOutputPath, "log");
			const fileops::FArdaTemporaryFiles Cleanup{{TemporaryOutput, TemporarySidecar, Log}};
			{
				std::ofstream Stream(TemporarySidecar, std::ios::binary | std::ios::trunc);
				Stream << KeyText(Job.mInputKey).c_str();
				if (!Stream)
				{
					return Fail(EArdaShaderCompileError::CacheWriteFailed,
					    "Unable to prepare the shader cache-key sidecar.");
				}
			}
			eastl::vector<eastl::string> DirectArguments = Job.mArguments;
			DirectArguments.push_back("-Fo");
			DirectArguments.push_back(ToEastl(TemporaryOutput.string()));
			int ExitCode = 1;
			bool bLaunched = false;
			eastl::string ModuleDiagnostics;
			IArdaBackendModule* BackendModule = FindBackendModule(Job.mTarget.mBackendName.c_str());
			EArdaBackendShaderCompileResult ModuleResult = EArdaBackendShaderCompileResult::NotHandled;
			if (BackendModule)
			{
				FArdaBackendShaderCompileInvocation Invocation;
				Invocation.mSourcePath = Job.mSourcePath;
				Invocation.mOutputPath = TemporaryOutput;
				Invocation.mCompilerExecutable = Job.mCompilerExecutable.empty() ? Compiler : Job.mCompilerExecutable;
				Invocation.mEntryPoint = Job.mType.GetEntryPoint();
				Invocation.mStage = Job.mType.GetStage();
				Invocation.mProfile = Job.mProfile;
				Invocation.mArguments = DirectArguments;
				ModuleResult = BackendModule->InvokeShaderCompiler(Invocation, ModuleDiagnostics);
			}
			if (ModuleResult == EArdaBackendShaderCompileResult::NotHandled)
			{
				bLaunched = LaunchCompilerDirect(Job.mCompilerExecutable.empty() ? Compiler : Job.mCompilerExecutable,
				    DirectArguments,
				    Log,
				    ExitCode);
			}
			else
			{
				bLaunched = true;
				ExitCode = ModuleResult == EArdaBackendShaderCompileResult::Success ? 0 : 1;
			}
			eastl::string CompilerOutput = ModuleDiagnostics.empty() ? fileops::ReadText(Log) : ModuleDiagnostics;
			if (!bLaunched)
			{
				return Fail(EArdaShaderCompileError::ProcessLaunchFailed,
				    "Neither the backend module nor the configured fallback compiler launched the job.");
			}
			if (ExitCode != 0 || !fileops::IsRegularNonEmpty(TemporaryOutput))
			{
				return Fail(EArdaShaderCompileError::CompilationFailed,
				    "Shader compilation failed with exit code " + eastl::to_string(ExitCode) +
				        (CompilerOutput.empty() ? "." : ":\n" + CompilerOutput));
			}
			const std::filesystem::path Sidecar = Job.mOutputPath.string() + ".arda-key";
			if (!fileops::PublishFilesTransaction({{TemporaryOutput, Job.mOutputPath}, {TemporarySidecar, Sidecar}}))
			{
				return Fail(EArdaShaderCompileError::CacheWriteFailed,
				    "Unable to publish artifact and sidecar together; rollback was attempted.");
			}
			return true;
		}

		eastl::string JsonEscape(const eastl::string& Value)
		{
			std::ostringstream Result;
			for (const unsigned char Character : Value)
			{
				switch (Character)
				{
				case '"':
					Result << "\\\"";
					break;
				case '\\':
					Result << "\\\\";
					break;
				case '\b':
					Result << "\\b";
					break;
				case '\f':
					Result << "\\f";
					break;
				case '\n':
					Result << "\\n";
					break;
				case '\r':
					Result << "\\r";
					break;
				case '\t':
					Result << "\\t";
					break;
				default:
					if (Character < 0x20)
					{
						Result << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(Character);
					}
					else
					{
						Result << static_cast<char>(Character);
					}
				}
			}
			return ToEastl(Result.str());
		}

		eastl::string BuildManifest(const eastl::vector<FArdaShaderCompileJob>& Jobs)
		{
			std::ostringstream Stream;
			Stream << "{\n  \"schema\": 1,\n  \"jobs\": [\n";
			for (size_t Index = 0; Index < Jobs.size(); ++Index)
			{
				const auto& Job = Jobs[Index];
				Stream << "    {\n"
				       << "      \"type\": \"" << JsonEscape(Job.mType.GetName()).c_str() << "\",\n"
				       << "      \"backend\": \"" << JsonEscape(Job.mTarget.mBackendName).c_str() << "\",\n"
				       << "      \"permutation\": " << Job.mPermutationId << ",\n"
				       << "      \"source\": \"" << JsonEscape(Job.mSourceIdentity).c_str() << "\",\n"
				       << "      \"output\": \"" << JsonEscape(ToEastl(Job.mOutputPath.filename().generic_string())).c_str() << "\",\n"
				       << "      \"entry\": \"" << JsonEscape(Job.mType.GetEntryPoint()).c_str() << "\",\n"
				       << "      \"profile\": \"" << JsonEscape(Job.mProfile).c_str() << "\",\n"
				       << "      \"defines\": {";
				const auto& Defines = Job.mEnvironment.GetDefines();
				for (size_t DefineIndex = 0; DefineIndex < Defines.size(); ++DefineIndex)
				{
					Stream << (DefineIndex == 0 ? "\n" : ",\n") << "        \""
					       << JsonEscape(Defines[DefineIndex].mName).c_str() << "\": \""
					       << JsonEscape(Defines[DefineIndex].mValue).c_str() << "\"";
				}
				if (!Defines.empty())
				{
					Stream << '\n' << "      ";
				}
				Stream << "},\n      \"key\": \"" << KeyText(Job.mInputKey).substr(0, 16).c_str() << "\"\n    }"
				       << (Index + 1 == Jobs.size() ? "\n" : ",\n");
			}
			Stream << "  ]\n}\n";
			return ToEastl(Stream.str());
		}
	}

	void ConfigureShaderCompiler(const FArdaShaderCompilerConfiguration& Configuration)
	{
		std::lock_guard<std::mutex> Lock(GetCompilerContext().mConfigurationMutex);
		GetCompilerContext().mConfiguration = Configuration;
	}

	FArdaShaderCompilerConfiguration GetShaderCompilerConfiguration()
	{
		std::lock_guard<std::mutex> Lock(GetCompilerContext().mConfigurationMutex);
		return GetCompilerContext().mConfiguration;
	}

	void ResetShaderCompilerConfiguration()
	{
		std::lock_guard<std::mutex> Lock(GetCompilerContext().mConfigurationMutex);
		GetCompilerContext().mConfiguration = MakeDefaultConfiguration();
	}

	static FArdaShaderCompileResult BuildRegisteredShaderCompileJobsWithSnapshot(
	    const std::filesystem::path& OutputDirectory,
	    const eastl::vector<FArdaShaderTarget>& Targets,
	    const FArdaShaderCompilerConfiguration& Configuration,
	    const std::filesystem::path& Compiler)
	{
		FArdaShaderCompileResult Result;
		const FArdaShaderRegistrationStatus Registration = FArdaShaderTypeRegistration::CommitAll();
		if (!Registration)
		{
			Result.mDiagnostics.push_back(MakeDiagnostic(EArdaShaderCompileError::RegistrationFailed,
			    nullptr,
			    {},
			    0,
			    {},
			    {},
			    Registration.mMessage));
			return Result;
		}
		eastl::vector<FArdaShaderTarget> UniqueTargets = Targets;
		eastl::sort(UniqueTargets.begin(),
		    UniqueTargets.end(),
		    [](const FArdaShaderTarget& Left, const FArdaShaderTarget& Right)
		    {
			    return Left.mBackendName < Right.mBackendName;
		    });
		UniqueTargets.erase(eastl::unique(UniqueTargets.begin(),
		                        UniqueTargets.end(),
		                        [](const FArdaShaderTarget& Left, const FArdaShaderTarget& Right)
		                        {
			                        return Left.mBackendName == Right.mBackendName;
		                        }),
		    UniqueTargets.end());
		for (const FArdaShaderType& Type : FArdaShaderTypeRegistration::EnumerateSnapshots())
		{
			for (const FArdaShaderTarget& Target : UniqueTargets)
			{
				for (uint32_t PermutationId = 0; PermutationId < Type.GetPermutationCount(); ++PermutationId)
				{
					if (!Type.ShouldCompilePermutation(Target, PermutationId))
					{
						++Result.mJobsSkipped;
						continue;
					}
					FArdaShaderCompileJob Job;
					FArdaShaderCompileDiagnostic Diagnostic;
					if (!PopulateJob(Type,
					        Target,
					        PermutationId,
					        OutputDirectory,
					        Configuration,
					        Compiler,
					        Job,
					        Diagnostic))
					{
						Diagnostic.mBackendName = Target.mBackendName;
						Result.mDiagnostics.push_back(eastl::move(Diagnostic));
						continue;
					}
					Result.mJobs.push_back(eastl::move(Job));
				}
			}
		}
		eastl::sort(Result.mJobs.begin(),
		    Result.mJobs.end(),
		    [](const auto& Left, const auto& Right)
		    {
			    const int TypeOrder = eastl::string(Left.mType.GetName()).compare(Right.mType.GetName());
			    if (TypeOrder != 0)
			    {
				    return TypeOrder < 0;
			    }
			    if (Left.mTarget.mBackendName != Right.mTarget.mBackendName)
			    {
				    return Left.mTarget.mBackendName < Right.mTarget.mBackendName;
			    }
			    return Left.mPermutationId < Right.mPermutationId;
		    });
		return Result;
	}

	static bool ResolveShaderTargets(const eastl::vector<eastl::string>& BackendNames,
	    eastl::vector<FArdaShaderTarget>& OutTargets,
	    FArdaShaderCompileResult& OutResult)
	{
		for (const eastl::string& BackendName : BackendNames)
		{
			FArdaShaderTarget Target;
			if (!ResolveShaderTarget(BackendName.c_str(), Target))
			{
				auto Diagnostic = MakeDiagnostic(EArdaShaderCompileError::CompilerUnavailable,
				    nullptr,
				    {},
				    0,
				    {},
				    {},
				    "The requested shader backend module is not registered.");
				Diagnostic.mBackendName = BackendName;
				OutResult.mDiagnostics.push_back(eastl::move(Diagnostic));
				return false;
			}
			OutTargets.push_back(eastl::move(Target));
		}
		return true;
	}

	FArdaShaderCompileResult BuildRegisteredShaderCompileJobs(const std::filesystem::path& OutputDirectory,
	    const eastl::vector<eastl::string>& BackendNames)
	{
		FArdaShaderCompileResult Result;
		eastl::vector<FArdaShaderTarget> Targets;
		if (!ResolveShaderTargets(BackendNames, Targets, Result))
		{
			return Result;
		}
		const FArdaShaderCompilerConfiguration Configuration = GetShaderCompilerConfiguration();
		return BuildRegisteredShaderCompileJobsWithSnapshot(OutputDirectory,
		    Targets,
		    Configuration,
		    ResolveCompiler(Configuration));
	}

	static FArdaShaderCompileResult CompileRegisteredShaderArtifactsWithTargets(
	    const std::filesystem::path& OutputDirectory,
	    const eastl::vector<FArdaShaderTarget>& Targets)
	{
		const FArdaShaderCompilerConfiguration Configuration = GetShaderCompilerConfiguration();
		const std::filesystem::path Compiler = ResolveCompiler(Configuration);
		FArdaShaderCompileResult Result =
		    BuildRegisteredShaderCompileJobsWithSnapshot(OutputDirectory, Targets, Configuration, Compiler);
		if (!Result)
		{
			return Result;
		}
		const std::filesystem::path StagingDirectory = fileops::TemporaryPath(OutputDirectory, "staging");
		std::error_code Error;
		std::filesystem::create_directories(StagingDirectory, Error);
		if (Error)
		{
			Result.mDiagnostics.push_back(MakeDiagnostic(EArdaShaderCompileError::DirectoryCreationFailed,
			    nullptr,
			    {},
			    0,
			    {},
			    StagingDirectory,
			    "Unable to create the shader cook staging directory."));
			return Result;
		}
		for (const auto& Job : Result.mJobs)
		{
			FArdaShaderCompileJob StagedJob = Job;
			StagedJob.mOutputPath = StagingDirectory / Job.mOutputPath.filename();
			FArdaShaderCompileDiagnostic Diagnostic;
			if (!CompileJob(StagedJob, Compiler, Diagnostic))
			{
				std::filesystem::remove_all(StagingDirectory, Error);
				Result.mDiagnostics.push_back(eastl::move(Diagnostic));
				return Result;
			}
			++Result.mJobsCompiled;
		}
		const std::filesystem::path StagedManifest = StagingDirectory / "ArdaShaderManifest.json";
		if (!fileops::AtomicWrite(StagedManifest, BuildManifest(Result.mJobs)))
		{
			std::filesystem::remove_all(StagingDirectory, Error);
			Result.mDiagnostics.push_back(MakeDiagnostic(EArdaShaderCompileError::ManifestWriteFailed,
			    nullptr,
			    {},
			    0,
			    {},
			    OutputDirectory / "ArdaShaderManifest.json",
			    "Unable to write the staged deterministic shader cook manifest."));
			return Result;
		}
		std::filesystem::create_directories(OutputDirectory, Error);
		eastl::vector<eastl::pair<std::filesystem::path, std::filesystem::path>> Files;
		for (const auto& Job : Result.mJobs)
		{
			const auto StagedArtifact = StagingDirectory / Job.mOutputPath.filename();
			Files.emplace_back(StagedArtifact, Job.mOutputPath);
			Files.emplace_back(StagedArtifact.string() + ".arda-key", Job.mOutputPath.string() + ".arda-key");
		}
		Files.emplace_back(StagedManifest, OutputDirectory / "ArdaShaderManifest.json");
		if (Error || !fileops::PublishFilesTransaction(Files))
		{
			std::filesystem::remove_all(StagingDirectory, Error);
			Result.mDiagnostics.push_back(MakeDiagnostic(EArdaShaderCompileError::CacheWriteFailed,
			    nullptr,
			    {},
			    0,
			    {},
			    OutputDirectory,
			    "Unable to publish the staged shader cook; rollback was attempted."));
			return Result;
		}
		std::filesystem::remove_all(StagingDirectory, Error);
		return Result;
	}

	FArdaShaderCompileResult CompileRegisteredShaderArtifacts(const std::filesystem::path& OutputDirectory,
	    const eastl::vector<eastl::string>& BackendNames)
	{
		FArdaShaderCompileResult Result;
		eastl::vector<FArdaShaderTarget> Targets;
		if (!ResolveShaderTargets(BackendNames, Targets, Result))
		{
			return Result;
		}
		return CompileRegisteredShaderArtifactsWithTargets(OutputDirectory, Targets);
	}

	FArdaShaderCompileResult CompileRegisteredShaderArtifacts(const std::filesystem::path& OutputDirectory,
	    const char* BackendName)
	{
		return CompileRegisteredShaderArtifacts(OutputDirectory,
		    eastl::vector<eastl::string>{BackendName ? BackendName : ""});
	}

	static FArdaShaderCompileResult EnsureRegisteredShaderArtifactWithSnapshot(const FArdaShaderType& Type,
	    const FArdaShaderTarget& Target,
	    uint32_t PermutationId,
	    const std::filesystem::path& OutputDirectory,
	    const FArdaShaderCompilerConfiguration& Configuration,
	    const std::filesystem::path& Compiler);

	static FArdaShaderCompileResult EnsureRegisteredShaderArtifactsForTarget(
	    const std::filesystem::path& OutputDirectory,
	    const FArdaShaderTarget& Target)
	{
		const eastl::string& BackendName = Target.mBackendName;
		FArdaShaderCompileResult Result;
		const FArdaShaderRegistrationStatus Registration = FArdaShaderTypeRegistration::CommitAll();
		if (!Registration)
		{
			Result.mDiagnostics.push_back(MakeDiagnostic(EArdaShaderCompileError::RegistrationFailed,
			    nullptr,
			    BackendName,
			    0,
			    {},
			    {},
			    Registration.mMessage));
			return Result;
		}

		const FArdaShaderCompilerConfiguration Configuration = GetShaderCompilerConfiguration();
		const std::filesystem::path Compiler = ResolveCompiler(Configuration);
		for (const FArdaShaderType& Type : FArdaShaderTypeRegistration::EnumerateSnapshots())
		{
			for (uint32_t PermutationId = 0; PermutationId < Type.GetPermutationCount(); ++PermutationId)
			{
				if (!Type.ShouldCompilePermutation(Target, PermutationId))
				{
					++Result.mJobsSkipped;
					continue;
				}
				FArdaShaderCompileResult JobResult = EnsureRegisteredShaderArtifactWithSnapshot(Type,
				    Target,
				    PermutationId,
				    OutputDirectory,
				    Configuration,
				    Compiler);
				Result.mJobsCompiled += JobResult.mJobsCompiled;
				Result.mCacheHits += JobResult.mCacheHits;
				Result.mJobsSkipped += JobResult.mJobsSkipped;
				for (auto& Job : JobResult.mJobs)
				{
					Result.mJobs.push_back(eastl::move(Job));
				}
				for (auto& Diagnostic : JobResult.mDiagnostics)
				{
					Result.mDiagnostics.push_back(eastl::move(Diagnostic));
				}
			}
		}
		return Result;
	}

	FArdaShaderCompileResult EnsureRegisteredShaderArtifacts(const std::filesystem::path& OutputDirectory,
	    const char* BackendName)
	{
		FArdaShaderTarget Target;
		if (!ResolveShaderTarget(BackendName, Target))
		{
			FArdaShaderCompileResult Result;
			auto Diagnostic = MakeDiagnostic(EArdaShaderCompileError::CompilerUnavailable,
			    nullptr,
			    {},
			    0,
			    {},
			    {},
			    "The requested shader backend module is not registered.");
			Diagnostic.mBackendName = BackendName ? BackendName : "";
			Result.mDiagnostics.push_back(eastl::move(Diagnostic));
			return Result;
		}
		return EnsureRegisteredShaderArtifactsForTarget(OutputDirectory, Target);
	}

	static FArdaShaderCompileResult EnsureRegisteredShaderArtifactWithSnapshot(const FArdaShaderType& Type,
	    const FArdaShaderTarget& Target,
	    uint32_t PermutationId,
	    const std::filesystem::path& OutputDirectory,
	    const FArdaShaderCompilerConfiguration& Configuration,
	    const std::filesystem::path& Compiler)
	{
		const eastl::string& BackendName = Target.mBackendName;
		FArdaShaderCompileResult Result;
		const FArdaShaderRegistrationStatus Registration = FArdaShaderTypeRegistration::CommitAll();
		if (!Registration)
		{
			Result.mDiagnostics.push_back(MakeDiagnostic(EArdaShaderCompileError::RegistrationFailed,
			    &Type,
			    BackendName,
			    PermutationId,
			    {},
			    {},
			    Registration.mMessage));
			return Result;
		}
		if (PermutationId >= Type.GetPermutationCount() || !Type.ShouldCompilePermutation(Target, PermutationId))
		{
			Result.mDiagnostics.push_back(MakeDiagnostic(EArdaShaderCompileError::InvalidPermutation,
			    &Type,
			    BackendName,
			    PermutationId,
			    {},
			    {},
			    "The requested permutation is invalid or filtered by its registered compile policy."));
			return Result;
		}
		const eastl::string Stem = Type.GetPermutationArtifactStem(PermutationId);
		const std::filesystem::path Output = OutputDirectory / (Stem + Target.mArtifactExtension).c_str();
		if (!IsContainedArtifactPath(OutputDirectory, Output))
		{
			Result.mDiagnostics.push_back(MakeDiagnostic(EArdaShaderCompileError::InvalidPermutation,
			    &Type,
			    BackendName,
			    PermutationId,
			    {},
			    Output,
			    "Generated shader artifact path escapes its output directory."));
			return Result;
		}
		std::error_code Error;
		const bool Exists = fileops::IsRegularNonEmpty(Output);
		const std::filesystem::path Sidecar = Output.string() + ".arda-key";
		const bool HasSidecar = std::filesystem::is_regular_file(Sidecar, Error) && !Error;
		const bool BytecodeOnly = !Configuration.mbCompileMissingArtifacts && !Configuration.mbCompileOutdatedArtifacts;

		if (Exists && !HasSidecar)
		{
			++Result.mCacheHits;
			return Result;
		}
		if (Exists && HasSidecar && BytecodeOnly)
		{
			++Result.mCacheHits;
			return Result;
		}
		if (!Exists && !Configuration.mbCompileMissingArtifacts)
		{
			Result.mDiagnostics.push_back(MakeDiagnostic(EArdaShaderCompileError::ArtifactMissing,
			    &Type,
			    BackendName,
			    PermutationId,
			    {},
			    Output,
			    "Shader artifact is missing and development auto compilation is disabled."));
			return Result;
		}

		FArdaShaderCompileJob Job;
		FArdaShaderCompileDiagnostic Diagnostic;
		if (!PopulateJob(Type, Target, PermutationId, OutputDirectory, Configuration, Compiler, Job, Diagnostic))
		{
			Result.mDiagnostics.push_back(eastl::move(Diagnostic));
			return Result;
		}
		Result.mJobs.push_back(Job);
		uint64_t StoredKey = 0;
		if (Exists && ReadKey(Sidecar, StoredKey) && StoredKey == Job.mInputKey)
		{
			++Result.mCacheHits;
			return Result;
		}
		if (Exists && !Configuration.mbCompileOutdatedArtifacts)
		{
			Result.mDiagnostics.push_back(MakeDiagnostic(EArdaShaderCompileError::ArtifactOutdated,
			    &Type,
			    BackendName,
			    PermutationId,
			    Job.mSourcePath,
			    Output,
			    "Shader cache sidecar does not match current compiler/source/job inputs and rebuilding outdated artifacts is disabled."));
			return Result;
		}
		bool CacheHit = false;
		if (!CompileJob(Job, Compiler, Diagnostic, true, &CacheHit))
		{
			Result.mDiagnostics.push_back(eastl::move(Diagnostic));
			return Result;
		}
		if (CacheHit)
		{
			++Result.mCacheHits;
		}
		else
		{
			++Result.mJobsCompiled;
		}
		return Result;
	}

	FArdaShaderCompileResult EnsureRegisteredShaderArtifact(const FArdaShaderType& Type,
	    const char* BackendName,
	    uint32_t PermutationId,
	    const std::filesystem::path& OutputDirectory)
	{
		FArdaShaderTarget Target;
		if (!ResolveShaderTarget(BackendName, Target))
		{
			FArdaShaderCompileResult Result;
			auto Diagnostic = MakeDiagnostic(EArdaShaderCompileError::CompilerUnavailable,
			    &Type,
			    {},
			    PermutationId,
			    {},
			    {},
			    "The requested shader backend module is not registered.");
			Diagnostic.mBackendName = BackendName ? BackendName : "";
			Result.mDiagnostics.push_back(eastl::move(Diagnostic));
			return Result;
		}
		const FArdaShaderCompilerConfiguration Configuration = GetShaderCompilerConfiguration();
		FArdaShaderCompileResult Result = EnsureRegisteredShaderArtifactWithSnapshot(Type,
		    Target,
		    PermutationId,
		    OutputDirectory,
		    Configuration,
		    ResolveCompiler(Configuration));
		for (FArdaShaderCompileDiagnostic& Diagnostic : Result.mDiagnostics)
		{
			Diagnostic.mBackendName = Target.mBackendName;
		}
		return Result;
	}
}
