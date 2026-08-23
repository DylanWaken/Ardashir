#include "ShaderStructs/ArdaShaderCompiler.h"

#include "ArdaBackendProvider.h"

#include "ShaderStructs/ArdaGlobalShaderMap.h"
#include "ShaderStructs/ArdaShaderDirectories.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <set>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace arda::backend
{
    namespace
    {
        constexpr uint64_t FnvOffset = 14695981039346656037ull;
        constexpr uint64_t FnvPrime = 1099511628211ull;
        constexpr const char* CacheSchema = "ArdaShaderCompileKey-v3";

        std::mutex GConfigurationMutex;
        FArdaShaderCompilerConfiguration GConfiguration;
        std::mutex GOutputMutexMapMutex;
        std::map<std::string, std::weak_ptr<std::mutex>> GOutputMutexes;
        std::atomic<uint64_t> GTemporaryId{ 0 };

        eastl::string ToEastl(const std::string& Value)
        {
            return eastl::string(Value.data(), Value.size());
        }

        std::string ToStd(const eastl::string& Value)
        {
            return std::string(Value.data(), Value.size());
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
                    Result.mCompilerExecutable = Environment;
            }
            return Result;
        }

        std::filesystem::path ResolveCompiler(
            const FArdaShaderCompilerConfiguration& Configuration)
        {
            std::filesystem::path Result = Configuration.mCompilerExecutable;
            if (Result.empty())
                Result = MakeDefaultConfiguration().mCompilerExecutable;
            if (Result.empty())
                return {};
            std::error_code Error;
            if (!std::filesystem::is_regular_file(Result, Error) || Error)
                return {};
            return std::filesystem::absolute(Result, Error);
        }

        FArdaShaderCompileDiagnostic MakeDiagnostic(
            EArdaShaderCompileError Code,
            const FArdaShaderType* Type,
            EArdaBackendType Backend,
            uint32_t PermutationId,
            const std::filesystem::path& Source,
            const std::filesystem::path& Output,
            const std::string& Message)
        {
            FArdaShaderCompileDiagnostic Result;
            Result.mCode = Code;
            Result.mShaderType = Type != nullptr ? Type->GetName() : "";
            Result.mBackend = Backend;
            Result.mPermutationId = PermutationId;
            Result.mSourcePath = Source;
            Result.mOutputPath = Output;
            Result.mMessage = ToEastl(Message);
            return Result;
        }

        bool IsRayStage(rhi::EArdaRHIShaderStage Stage)
        {
            using StageType = rhi::EArdaRHIShaderStage;
            return Stage == StageType::RayGeneration ||
                Stage == StageType::AnyHit ||
                Stage == StageType::ClosestHit ||
                Stage == StageType::Miss ||
                Stage == StageType::Intersection ||
                Stage == StageType::Callable;
        }

        eastl::string ProfileForStage(rhi::EArdaRHIShaderStage Stage)
        {
            using StageType = rhi::EArdaRHIShaderStage;
            if (Stage == StageType::Vertex) return "vs_6_0";
            if (Stage == StageType::Pixel) return "ps_6_0";
            if (Stage == StageType::Compute) return "cs_6_0";
            if (Stage == StageType::Geometry) return "gs_6_0";
            if (Stage == StageType::Hull) return "hs_6_0";
            if (Stage == StageType::Domain) return "ds_6_0";
            if (Stage == StageType::Amplification) return "as_6_5";
            if (Stage == StageType::Mesh) return "ms_6_5";
            if (Stage == StageType::WorkGraph) return "lib_6_8";
            if (IsRayStage(Stage)) return "lib_6_3";
            return {};
        }

        bool ContainsControl(const std::string& Value)
        {
            for (const unsigned char Character : Value)
            {
                if (Character < 0x20 || Character == 0x7f)
                    return true;
            }
            return false;
        }

        bool IsValidDefineName(const std::string& Name)
        {
            if (Name.empty() ||
                !(std::isalpha(static_cast<unsigned char>(Name.front())) ||
                  Name.front() == '_'))
            {
                return false;
            }
            for (const unsigned char Character : Name)
            {
                if (!(std::isalnum(Character) || Character == '_'))
                    return false;
            }
            return true;
        }

        void HashBytes(uint64_t& Hash, const void* Data, size_t Size)
        {
            const auto* Bytes = static_cast<const uint8_t*>(Data);
            for (size_t Index = 0; Index < Size; ++Index)
            {
                Hash ^= Bytes[Index];
                Hash *= FnvPrime;
            }
        }

        void HashString(uint64_t& Hash, const std::string& Value)
        {
            const uint64_t Size = static_cast<uint64_t>(Value.size());
            for (uint32_t Shift = 0; Shift < 64; Shift += 8)
            {
                const uint8_t Byte = static_cast<uint8_t>(Size >> Shift);
                HashBytes(Hash, &Byte, 1);
            }
            HashBytes(Hash, Value.data(), Value.size());
        }

        void HashUint32(uint64_t& Hash, uint32_t Value)
        {
            for (uint32_t Shift = 0; Shift < 32; Shift += 8)
            {
                const uint8_t Byte = static_cast<uint8_t>(Value >> Shift);
                HashBytes(Hash, &Byte, 1);
            }
        }

        std::string ReadText(const std::filesystem::path& Path);

        bool HashFile(uint64_t& Hash, const std::filesystem::path& Path)
        {
            std::ifstream Stream(Path, std::ios::binary);
            if (!Stream)
                return false;
            char Buffer[64 * 1024];
            while (Stream)
            {
                Stream.read(Buffer, sizeof(Buffer));
                const std::streamsize Count = Stream.gcount();
                if (Count > 0)
                    HashBytes(Hash, Buffer, static_cast<size_t>(Count));
            }
            return Stream.eof();
        }

        bool HashLocalSourceTree(
            uint64_t& Hash,
            const std::filesystem::path& PhysicalPath,
            const std::string& LogicalIdentity,
            std::set<std::string>& Visiting,
            std::set<std::string>& Hashed,
            std::string& ErrorMessage)
        {
            std::error_code Error;
            const auto Canonical =
                std::filesystem::weakly_canonical(PhysicalPath, Error).generic_string();
            if (Error || !std::filesystem::is_regular_file(PhysicalPath, Error) || Error)
            {
                ErrorMessage = "Unable to resolve local shader include: " +
                    PhysicalPath.generic_string();
                return false;
            }
            if (Hashed.count(Canonical) != 0)
                return true;
            if (!Visiting.insert(Canonical).second)
                return true;
            const std::string Contents = ReadText(PhysicalPath);
            HashString(Hash, LogicalIdentity);
            HashString(Hash, Contents);

            std::istringstream Lines(Contents);
            std::string Line;
            while (std::getline(Lines, Line))
            {
                const size_t HashPosition = Line.find('#');
                if (HashPosition == std::string::npos)
                    continue;
                size_t Cursor = HashPosition + 1;
                while (Cursor < Line.size() &&
                       std::isspace(static_cast<unsigned char>(Line[Cursor])))
                    ++Cursor;
                if (Line.compare(Cursor, 7, "include") != 0)
                    continue;
                Cursor += 7;
                while (Cursor < Line.size() &&
                       std::isspace(static_cast<unsigned char>(Line[Cursor])))
                    ++Cursor;
                if (Cursor == Line.size() || Line[Cursor] != '"')
                    continue;
                const size_t End = Line.find('"', Cursor + 1);
                if (End == std::string::npos)
                    continue;
                const std::filesystem::path Relative =
                    Line.substr(Cursor + 1, End - Cursor - 1);
                const std::filesystem::path Included =
                    (PhysicalPath.parent_path() / Relative).lexically_normal();
                const std::string ChildIdentity =
                    (std::filesystem::path(LogicalIdentity).parent_path() / Relative)
                        .lexically_normal().generic_string();
                if (!HashLocalSourceTree(
                        Hash, Included, ChildIdentity, Visiting, Hashed, ErrorMessage))
                    return false;
            }
            Visiting.erase(Canonical);
            Hashed.insert(Canonical);
            return true;
        }

        bool HasShaderSourceExtension(const std::filesystem::path& Path)
        {
            std::string Extension = Path.extension().string();
            std::transform(
                Extension.begin(), Extension.end(), Extension.begin(),
                [](unsigned char Character)
                {
                    return static_cast<char>(std::tolower(Character));
                });
            return Extension == ".hlsl" || Extension == ".hlsli" ||
                Extension == ".usf" || Extension == ".ush";
        }

        bool HashShaderSourceDirectory(
            uint64_t& Hash,
            const std::filesystem::path& Directory,
            std::set<std::string>& HashedRoots,
            std::string& ErrorMessage)
        {
            std::error_code Error;
            const std::filesystem::path Root =
                std::filesystem::weakly_canonical(Directory, Error);
            if (Error || !std::filesystem::is_directory(Root, Error) || Error)
            {
                ErrorMessage = "Configured shader include root is missing or not a directory: " +
                    Directory.generic_string();
                return false;
            }
            if (!HashedRoots.insert(Root.generic_string()).second)
                return true;
            std::vector<std::filesystem::path> Files;
            std::filesystem::recursive_directory_iterator Iterator(
                Root, std::filesystem::directory_options::skip_permission_denied,
                Error);
            const std::filesystem::recursive_directory_iterator End;
            while (!Error && Iterator != End)
            {
                if (Iterator->is_regular_file(Error) && !Error &&
                    HasShaderSourceExtension(Iterator->path()))
                {
                    Files.push_back(Iterator->path());
                }
                Iterator.increment(Error);
            }
            if (Error)
            {
                ErrorMessage = "Unable to enumerate shader include root: " +
                    Root.generic_string();
                return false;
            }
            std::sort(
                Files.begin(), Files.end(),
                [&Root](const auto& Left, const auto& Right)
                {
                    return Left.lexically_relative(Root).generic_string() <
                        Right.lexically_relative(Root).generic_string();
                });
            HashString(Hash, Root.generic_string());
            for (const auto& File : Files)
            {
                HashString(Hash, File.lexically_relative(Root).generic_string());
                if (!HashFile(Hash, File))
                {
                    ErrorMessage = "Unable to hash shader dependency: " +
                        File.generic_string();
                    return false;
                }
            }
            return true;
        }

        bool ResolveSource(
            const FArdaShaderType& Type,
            const FArdaShaderCompilerConfiguration& Configuration,
            std::filesystem::path& Out)
        {
            const char* Stem = Type.GetSourceStem();
            if (Stem == nullptr || Stem[0] == '\0')
                return false;
            if (Stem[0] == '/')
                return static_cast<bool>(ResolveVirtualShaderSource(Stem, Out));
            std::filesystem::path Candidate = Stem;
            if (!Configuration.mSourceRoot.empty() && !Candidate.is_absolute())
                Candidate = Configuration.mSourceRoot / Candidate;
            std::error_code Error;
            if (!std::filesystem::is_regular_file(Candidate, Error) || Error)
                return false;
            Out = std::filesystem::absolute(Candidate, Error);
            return !Error;
        }

        std::string KeyText(uint64_t Key)
        {
            std::ostringstream Stream;
            Stream << std::hex << std::setfill('0') << std::setw(16) << Key << '\n';
            return Stream.str();
        }

        bool ReadKey(const std::filesystem::path& Path, uint64_t& Out)
        {
            std::ifstream Stream(Path);
            std::string Value;
            if (!(Stream >> Value) || Value.size() != 16)
                return false;
            std::istringstream Parser(Value);
            Parser >> std::hex >> Out;
            return !Parser.fail();
        }

        std::filesystem::path TemporaryPath(
            const std::filesystem::path& Base,
            const char* Kind)
        {
            const uint64_t ProcessId =
#if defined(_WIN32)
                static_cast<uint64_t>(GetCurrentProcessId());
#else
                static_cast<uint64_t>(getpid());
#endif
            return Base.string() + ".arda-" + Kind + "-" +
                std::to_string(ProcessId) + "-" +
                std::to_string(++GTemporaryId) + ".tmp";
        }

        bool IsContainedArtifactPath(
            const std::filesystem::path& Directory,
            const std::filesystem::path& Path)
        {
            std::error_code Error;
            const auto Root = std::filesystem::absolute(Directory, Error).lexically_normal();
            if (Error)
                return false;
            const auto Candidate = std::filesystem::absolute(Path, Error).lexically_normal();
            if (Error || Candidate.parent_path() != Root)
                return false;
            const std::string Name = Candidate.stem().string();
            return !Name.empty() && Name[0] != '.' &&
                Name.find("..") == std::string::npos;
        }

        bool IsRegularNonEmpty(const std::filesystem::path& Path)
        {
            std::error_code Error;
            return std::filesystem::is_regular_file(Path, Error) && !Error &&
                std::filesystem::file_size(Path, Error) > 0 && !Error;
        }

        bool AtomicReplace(
            const std::filesystem::path& Temporary,
            const std::filesystem::path& Destination)
        {
#if defined(_WIN32)
            return MoveFileExW(
                Temporary.c_str(),
                Destination.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
            std::error_code Error;
            std::filesystem::rename(Temporary, Destination, Error);
            return !Error;
#endif
        }

        bool AtomicWrite(
            const std::filesystem::path& Path,
            const std::string& Contents)
        {
            const std::filesystem::path Temporary = TemporaryPath(Path, "write");
            {
                std::ofstream Stream(Temporary, std::ios::binary | std::ios::trunc);
                Stream.write(Contents.data(), static_cast<std::streamsize>(Contents.size()));
                if (!Stream)
                {
                    std::error_code Error;
                    std::filesystem::remove(Temporary, Error);
                    return false;
                }
            }
            if (AtomicReplace(Temporary, Path))
                return true;
            std::error_code Error;
            std::filesystem::remove(Temporary, Error);
            return false;
        }

        std::shared_ptr<std::mutex> GetOutputMutex(
            const std::filesystem::path& Output)
        {
            std::error_code Error;
            const std::string Key =
                std::filesystem::absolute(Output, Error).lexically_normal().generic_string();
            std::lock_guard<std::mutex> Lock(GOutputMutexMapMutex);
            auto& Weak = GOutputMutexes[Key];
            auto Result = Weak.lock();
            if (!Result)
            {
                Result = std::make_shared<std::mutex>();
                Weak = Result;
            }
            return Result;
        }

        std::wstring QuoteWindowsArgument(const std::wstring& Value)
        {
            if (!Value.empty() &&
                Value.find_first_of(L" \t\n\v\"") == std::wstring::npos)
                return Value;
            std::wstring Result = L"\"";
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

        bool LaunchCompilerDirect(
            const std::filesystem::path& Compiler,
            const eastl::vector<eastl::string>& Arguments,
            const std::filesystem::path& Log,
            int& ExitCode)
        {
#if defined(_WIN32)
            SECURITY_ATTRIBUTES Security{ sizeof(Security), nullptr, TRUE };
            HANDLE LogHandle = CreateFileW(
                Log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &Security,
                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (LogHandle == INVALID_HANDLE_VALUE)
                return false;
            std::wstring Command = QuoteWindowsArgument(Compiler.wstring());
            for (const auto& Argument : Arguments)
            {
                Command += L" " + QuoteWindowsArgument(
                    std::filesystem::path(ToStd(Argument)).wstring());
            }
            STARTUPINFOW Startup{};
            Startup.cb = sizeof(Startup);
            Startup.dwFlags = STARTF_USESTDHANDLES;
            Startup.hStdOutput = LogHandle;
            Startup.hStdError = LogHandle;
            Startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            PROCESS_INFORMATION Process{};
            const BOOL Started = CreateProcessW(
                Compiler.c_str(), Command.data(), nullptr, nullptr, TRUE,
                CREATE_NO_WINDOW, nullptr, nullptr, &Startup, &Process);
            CloseHandle(LogHandle);
            if (!Started)
                return false;
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
                return false;
            if (Process == 0)
            {
                const int LogFd = open(Log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (LogFd < 0)
                    _exit(127);
                dup2(LogFd, STDOUT_FILENO);
                dup2(LogFd, STDERR_FILENO);
                close(LogFd);
                std::vector<std::string> Storage;
                Storage.push_back(Compiler.string());
                for (const auto& Argument : Arguments)
                    Storage.push_back(ToStd(Argument));
                std::vector<char*> Native;
                for (std::string& Argument : Storage)
                    Native.push_back(Argument.data());
                Native.push_back(nullptr);
                execv(Compiler.c_str(), Native.data());
                _exit(127);
            }
            int Status = 0;
            if (waitpid(Process, &Status, 0) < 0)
                return false;
            ExitCode = WIFEXITED(Status) ? WEXITSTATUS(Status) : 1;
            return true;
#endif
        }

        std::string ReadText(const std::filesystem::path& Path)
        {
            std::ifstream Stream(Path, std::ios::binary);
            return std::string(
                std::istreambuf_iterator<char>(Stream),
                std::istreambuf_iterator<char>());
        }

        bool PopulateJob(
            const FArdaShaderType& Type,
            const FArdaShaderTarget& Target,
            uint32_t PermutationId,
            const std::filesystem::path& OutputDirectory,
            const FArdaShaderCompilerConfiguration& Configuration,
            const std::filesystem::path& Compiler,
            FArdaShaderCompileJob& Job,
            FArdaShaderCompileDiagnostic& Diagnostic)
        {
            const EArdaBackendType Backend = Target.mBackend;
            Job.mType = Type;
            Job.mBackend = Backend;
            Job.mTarget = Target;
            Job.mCompilerExecutable = Compiler;
            Job.mPermutationId = PermutationId;
            const eastl::string Stem = Type.GetPermutationArtifactStem(PermutationId);
            Job.mOutputPath = OutputDirectory /
                (ToStd(Stem) + ToStd(Target.mArtifactExtension));
            if (!IsContainedArtifactPath(OutputDirectory, Job.mOutputPath))
            {
                Diagnostic = MakeDiagnostic(
                    EArdaShaderCompileError::InvalidPermutation, &Type, Backend,
                    PermutationId, {}, Job.mOutputPath,
                    "Generated shader artifact path escapes its output directory.");
                return false;
            }
            if (PermutationId >= Type.GetPermutationCount())
            {
                Diagnostic = MakeDiagnostic(
                    EArdaShaderCompileError::InvalidPermutation, &Type, Backend,
                    PermutationId, {}, Job.mOutputPath, "Invalid shader permutation identifier.");
                return false;
            }
            Job.mProfile = ProfileForStage(Type.GetStage());
            if (Job.mProfile.empty())
            {
                Diagnostic = MakeDiagnostic(
                    EArdaShaderCompileError::UnsupportedStage, &Type, Backend,
                    PermutationId, {}, Job.mOutputPath,
                    "Combined, empty, or unknown shader stages cannot map to one fallback compiler profile.");
                return false;
            }
            if (!ResolveSource(Type, Configuration, Job.mSourcePath))
            {
                Diagnostic = MakeDiagnostic(
                    EArdaShaderCompileError::SourceResolutionFailed, &Type, Backend,
                    PermutationId, {}, Job.mOutputPath,
                    std::string("Unable to resolve registered shader source stem: ") +
                        Type.GetSourceStem());
                return false;
            }
            {
                const std::filesystem::path Registered(Type.GetSourceStem());
                Job.mSourceIdentity = ToEastl(
                    (Type.GetSourceStem()[0] == '/' || !Registered.is_absolute()
                        ? Registered
                        : Registered.filename()).lexically_normal().generic_string());
            }
            Job.mEnvironment = Type.BuildCompilationEnvironment(Target, PermutationId);

            Job.mArguments.push_back("-nologo");
            Job.mArguments.push_back("-T");
            Job.mArguments.push_back(Job.mProfile);
            if (Job.mProfile.rfind("lib_", 0) != 0 &&
                Type.GetEntryPoint()[0] != '\0')
            {
                Job.mArguments.push_back("-E");
                Job.mArguments.push_back(Type.GetEntryPoint());
            }
            IArdaBackendModule* BackendModule =
                FindBackendModule(Target.mBackendName.c_str());
            for (const FArdaShaderDefine& Define : Job.mEnvironment.GetDefines())
            {
                const std::string Name = ToStd(Define.mName);
                const std::string Value = ToStd(Define.mValue);
                if (!IsValidDefineName(Name) || ContainsControl(Value))
                {
                    Diagnostic = MakeDiagnostic(
                        EArdaShaderCompileError::InvalidPermutation, &Type, Backend,
                        PermutationId, Job.mSourcePath, Job.mOutputPath,
                        "A shader define has an unsafe name or control character.");
                    return false;
                }
                Job.mArguments.push_back("-D");
                Job.mArguments.push_back(Define.mName + "=" + Define.mValue);
            }
            const auto AppendArguments = [&](const eastl::vector<eastl::string>& Arguments)
            {
                for (const auto& Argument : Arguments)
                    Job.mArguments.push_back(Argument);
            };
            AppendArguments(Configuration.mCommonArguments);
            for (const FArdaShaderCompilerModuleArguments& ModuleArguments :
                 Configuration.mModuleArguments)
            {
                if (ModuleArguments.mBackendName == Target.mBackendName)
                    AppendArguments(ModuleArguments.mArguments);
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
                const rhi::FArdaRHIStatus Status =
                    BackendModule->ConfigureShaderCompileInvocation(Invocation);
                if (!Status)
                {
                    Diagnostic = MakeDiagnostic(
                        EArdaShaderCompileError::UnsupportedStage, &Type, Backend,
                        PermutationId, Job.mSourcePath, Job.mOutputPath,
                        ToStd(Status.mMessage));
                    return false;
                }
                Job.mSourcePath = eastl::move(Invocation.mSourcePath);
                Job.mOutputPath = eastl::move(Invocation.mOutputPath);
                Job.mCompilerExecutable = eastl::move(Invocation.mCompilerExecutable);
                Job.mProfile = eastl::move(Invocation.mProfile);
                Job.mArguments = eastl::move(Invocation.mArguments);
                if (!IsContainedArtifactPath(OutputDirectory, Job.mOutputPath))
                {
                    Diagnostic = MakeDiagnostic(
                        EArdaShaderCompileError::InvalidPermutation, &Type, Backend,
                        PermutationId, Job.mSourcePath, Job.mOutputPath,
                        "The backend module selected an artifact outside the output directory.");
                    return false;
                }
            }
            for (const auto& Argument : Job.mArguments)
            {
                if (ContainsControl(ToStd(Argument)))
                {
                    Diagnostic = MakeDiagnostic(
                        EArdaShaderCompileError::InvalidPermutation, &Type, Backend,
                        PermutationId, Job.mSourcePath, Job.mOutputPath,
                        "A custom compiler argument contains a control character.");
                    return false;
                }
            }
            uint64_t Hash = FnvOffset;
            HashString(Hash, CacheSchema);
            HashString(Hash, Type.GetName());
            HashString(Hash, ToStd(Target.mBackendName));
            HashUint32(Hash, static_cast<uint32_t>(Backend));
            HashUint32(Hash, static_cast<uint32_t>(Target.mBinaryFormat));
            HashUint32(Hash, PermutationId);
            HashString(Hash, ToStd(Job.mProfile));
            for (const auto& Argument : Job.mArguments)
                HashString(Hash, ToStd(Argument));
            HashString(Hash, ToStd(Job.mSourceIdentity));
            if (!Job.mCompilerExecutable.empty() &&
                !HashFile(Hash, Job.mCompilerExecutable))
            {
                Diagnostic = MakeDiagnostic(
                    EArdaShaderCompileError::CompilerUnavailable, &Type, Backend,
                    PermutationId, Job.mSourcePath, Job.mOutputPath,
                    "Unable to hash the configured compiler executable.");
                return false;
            }
            if (Job.mCompilerExecutable.empty())
            {
                if (Target.mCompilerIdentity.empty())
                {
                    Diagnostic = MakeDiagnostic(
                        EArdaShaderCompileError::CompilerUnavailable, &Type, Backend,
                        PermutationId, Job.mSourcePath, Job.mOutputPath,
                        "The backend module has neither a compiler executable nor a stable in-process compiler identity.");
                    return false;
                }
                HashString(Hash, ToStd(Target.mCompilerIdentity));
            }
            for (const auto& File : EnumerateShaderSourceFiles())
            {
                HashString(Hash, ToStd(File.mVirtualPath));
                if (!HashFile(Hash, File.mPhysicalPath))
                {
                    Diagnostic = MakeDiagnostic(
                        EArdaShaderCompileError::SourceResolutionFailed, &Type, Backend,
                        PermutationId, Job.mSourcePath, Job.mOutputPath,
                        "Unable to hash a file in the frozen shader-source manifest.");
                    return false;
                }
            }
            if (Type.GetSourceStem()[0] != '/')
            {
                std::set<std::string> Visiting;
                std::set<std::string> Hashed;
                std::string DependencyError;
                if (!HashLocalSourceTree(
                        Hash, Job.mSourcePath, ToStd(Job.mSourceIdentity),
                        Visiting, Hashed, DependencyError))
                {
                    Diagnostic = MakeDiagnostic(
                        EArdaShaderCompileError::SourceResolutionFailed, &Type, Backend,
                        PermutationId, Job.mSourcePath, Job.mOutputPath,
                        DependencyError);
                    return false;
                }
                std::set<std::string> HashedRoots;
                if (!HashShaderSourceDirectory(
                        Hash, Job.mSourcePath.parent_path(), HashedRoots,
                        DependencyError))
                {
                    Diagnostic = MakeDiagnostic(
                        EArdaShaderCompileError::SourceResolutionFailed, &Type, Backend,
                        PermutationId, Job.mSourcePath, Job.mOutputPath,
                        DependencyError);
                    return false;
                }
                for (size_t ArgumentIndex = 0;
                     ArgumentIndex < Job.mArguments.size();
                     ++ArgumentIndex)
                {
                    const std::string Argument = ToStd(Job.mArguments[ArgumentIndex]);
                    std::string IncludeRoot;
                    if (Argument == "-I")
                    {
                        if (++ArgumentIndex >= Job.mArguments.size())
                        {
                            DependencyError =
                                "Custom shader compiler argument -I requires a directory.";
                        }
                        else
                        {
                            IncludeRoot = ToStd(Job.mArguments[ArgumentIndex]);
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
                    if (IncludeRoot.empty() || !HashShaderSourceDirectory(
                            Hash, std::filesystem::path(IncludeRoot), HashedRoots,
                            DependencyError))
                    {
                        Diagnostic = MakeDiagnostic(
                            EArdaShaderCompileError::SourceResolutionFailed, &Type,
                            Backend, PermutationId, Job.mSourcePath,
                            Job.mOutputPath, DependencyError);
                        return false;
                    }
                }
            }
            Job.mInputKey = Hash;
            Job.mArguments.push_back(ToEastl(Job.mSourcePath.string()));
            return true;
        }

        bool CompileJob(
            const FArdaShaderCompileJob& Job,
            const std::filesystem::path& Compiler,
            FArdaShaderCompileDiagnostic& Diagnostic,
            bool SkipIfCurrent = false,
            bool* OutCacheHit = nullptr)
        {
            std::error_code Error;
            std::filesystem::create_directories(Job.mOutputPath.parent_path(), Error);
            if (Error)
            {
                Diagnostic = MakeDiagnostic(
                    EArdaShaderCompileError::DirectoryCreationFailed, &Job.mType,
                    Job.mBackend, Job.mPermutationId, Job.mSourcePath, Job.mOutputPath,
                    "Unable to create the shader artifact directory.");
                return false;
            }
            const auto Mutex = GetOutputMutex(Job.mOutputPath);
            std::lock_guard<std::mutex> Lock(*Mutex);
            if (SkipIfCurrent)
            {
                uint64_t StoredKey = 0;
                if (ReadKey(
                        Job.mOutputPath.string() + ".arda-key",
                        StoredKey) &&
                    StoredKey == Job.mInputKey &&
                    IsRegularNonEmpty(Job.mOutputPath))
                {
                    if (OutCacheHit != nullptr)
                        *OutCacheHit = true;
                    return true;
                }
            }
            const std::filesystem::path TemporaryOutput =
                TemporaryPath(Job.mOutputPath, "output");
            const std::filesystem::path TemporarySidecar =
                TemporaryPath(Job.mOutputPath, "key");
            const std::filesystem::path Log =
                TemporaryPath(Job.mOutputPath, "log");
            {
                std::ofstream Stream(
                    TemporarySidecar, std::ios::binary | std::ios::trunc);
                Stream << KeyText(Job.mInputKey);
                if (!Stream)
                {
                    std::error_code CleanupError;
                    std::filesystem::remove(TemporarySidecar, CleanupError);
                    Diagnostic = MakeDiagnostic(
                        EArdaShaderCompileError::CacheWriteFailed, &Job.mType,
                        Job.mBackend, Job.mPermutationId, Job.mSourcePath, Job.mOutputPath,
                        "Unable to prepare the shader cache-key sidecar.");
                    return false;
                }
            }
            eastl::vector<eastl::string> DirectArguments = Job.mArguments;
            DirectArguments.push_back("-Fo");
            DirectArguments.push_back(ToEastl(TemporaryOutput.string()));
            int ExitCode = 1;
            bool bLaunched = false;
            eastl::string ModuleDiagnostics;
            IArdaBackendModule* BackendModule =
                FindBackendModule(Job.mTarget.mBackendName.c_str());
            EArdaBackendShaderCompileResult ModuleResult =
                EArdaBackendShaderCompileResult::NotHandled;
            if (BackendModule)
            {
                FArdaBackendShaderCompileInvocation Invocation;
                Invocation.mSourcePath = Job.mSourcePath;
                Invocation.mOutputPath = TemporaryOutput;
                Invocation.mCompilerExecutable = Job.mCompilerExecutable.empty()
                    ? Compiler
                    : Job.mCompilerExecutable;
                Invocation.mEntryPoint = Job.mType.GetEntryPoint();
                Invocation.mStage = Job.mType.GetStage();
                Invocation.mProfile = Job.mProfile;
                Invocation.mArguments = DirectArguments;
                ModuleResult = BackendModule->InvokeShaderCompiler(
                    Invocation, ModuleDiagnostics);
            }
            if (ModuleResult == EArdaBackendShaderCompileResult::NotHandled)
            {
                bLaunched = LaunchCompilerDirect(
                    Job.mCompilerExecutable.empty() ? Compiler : Job.mCompilerExecutable,
                    DirectArguments, Log, ExitCode);
            }
            else
            {
                bLaunched = true;
                ExitCode = ModuleResult == EArdaBackendShaderCompileResult::Success
                    ? 0
                    : 1;
            }
            std::string CompilerOutput = ModuleDiagnostics.empty()
                ? ReadText(Log)
                : ToStd(ModuleDiagnostics);
            std::filesystem::remove(Log, Error);
            if (!bLaunched)
            {
                std::filesystem::remove(TemporaryOutput, Error);
                std::filesystem::remove(TemporarySidecar, Error);
                Diagnostic = MakeDiagnostic(
                    EArdaShaderCompileError::ProcessLaunchFailed, &Job.mType,
                    Job.mBackend, Job.mPermutationId, Job.mSourcePath, Job.mOutputPath,
                    "Neither the backend module nor the configured fallback compiler launched the job.");
                return false;
            }
            if (ExitCode != 0 ||
                !std::filesystem::is_regular_file(TemporaryOutput, Error) || Error ||
                std::filesystem::file_size(TemporaryOutput, Error) == 0 || Error)
            {
                std::filesystem::remove(TemporaryOutput, Error);
                std::filesystem::remove(TemporarySidecar, Error);
                Diagnostic = MakeDiagnostic(
                    EArdaShaderCompileError::CompilationFailed, &Job.mType,
                    Job.mBackend, Job.mPermutationId, Job.mSourcePath, Job.mOutputPath,
                    "Shader compilation failed with exit code " + std::to_string(ExitCode) +
                        (CompilerOutput.empty() ? "." : ":\n" + CompilerOutput));
                return false;
            }
            const std::filesystem::path Sidecar =
                Job.mOutputPath.string() + ".arda-key";
            const std::filesystem::path BackupOutput =
                TemporaryPath(Job.mOutputPath, "backup");
            const std::filesystem::path BackupSidecar =
                TemporaryPath(Job.mOutputPath, "backup-key");
            const bool HadOutput = std::filesystem::exists(Job.mOutputPath, Error);
            const bool HadSidecar = std::filesystem::exists(Sidecar, Error);
            if ((HadOutput && !AtomicReplace(Job.mOutputPath, BackupOutput)) ||
                (HadSidecar && !AtomicReplace(Sidecar, BackupSidecar)))
            {
                std::filesystem::remove(TemporaryOutput, Error);
                std::filesystem::remove(TemporarySidecar, Error);
                if (HadOutput && std::filesystem::exists(BackupOutput, Error))
                    AtomicReplace(BackupOutput, Job.mOutputPath);
                Diagnostic = MakeDiagnostic(
                    EArdaShaderCompileError::CacheWriteFailed, &Job.mType,
                    Job.mBackend, Job.mPermutationId, Job.mSourcePath, Job.mOutputPath,
                    "Unable to prepare rollback backups for shader publication.");
                return false;
            }
            if (!AtomicReplace(TemporaryOutput, Job.mOutputPath) ||
                !AtomicReplace(TemporarySidecar, Sidecar))
            {
                std::filesystem::remove(Job.mOutputPath, Error);
                std::filesystem::remove(Sidecar, Error);
                if (HadOutput)
                    AtomicReplace(BackupOutput, Job.mOutputPath);
                if (HadSidecar)
                    AtomicReplace(BackupSidecar, Sidecar);
                std::filesystem::remove(TemporaryOutput, Error);
                std::filesystem::remove(TemporarySidecar, Error);
                Diagnostic = MakeDiagnostic(
                    EArdaShaderCompileError::CacheWriteFailed, &Job.mType,
                    Job.mBackend, Job.mPermutationId, Job.mSourcePath, Job.mOutputPath,
                    "Unable to publish artifact and sidecar together; previous files were restored.");
                return false;
            }
            std::filesystem::remove(BackupOutput, Error);
            std::filesystem::remove(BackupSidecar, Error);
            return true;
        }

        bool PublishFilesTransaction(
            const std::vector<std::pair<std::filesystem::path, std::filesystem::path>>& Files,
            const std::filesystem::path& BackupDirectory)
        {
            std::error_code Error;
            std::filesystem::create_directories(BackupDirectory, Error);
            if (Error)
                return false;
            std::vector<std::pair<std::filesystem::path, std::filesystem::path>> Backups;
            std::vector<std::filesystem::path> Published;
            for (const auto& File : Files)
            {
                if (!std::filesystem::exists(File.second, Error))
                    continue;
                const auto Backup = BackupDirectory /
                    (std::to_string(Backups.size()) + ".bak");
                if (!AtomicReplace(File.second, Backup))
                {
                    for (auto It = Backups.rbegin(); It != Backups.rend(); ++It)
                        AtomicReplace(It->second, It->first);
                    return false;
                }
                Backups.emplace_back(File.second, Backup);
            }
            for (const auto& File : Files)
            {
                if (!AtomicReplace(File.first, File.second))
                {
                    for (const auto& Destination : Published)
                        std::filesystem::remove(Destination, Error);
                    for (auto It = Backups.rbegin(); It != Backups.rend(); ++It)
                        AtomicReplace(It->second, It->first);
                    return false;
                }
                Published.push_back(File.second);
            }
            std::filesystem::remove_all(BackupDirectory, Error);
            return true;
        }

        std::string JsonEscape(const std::string& Value)
        {
            std::ostringstream Result;
            for (const unsigned char Character : Value)
            {
                switch (Character)
                {
                case '"': Result << "\\\""; break;
                case '\\': Result << "\\\\"; break;
                case '\b': Result << "\\b"; break;
                case '\f': Result << "\\f"; break;
                case '\n': Result << "\\n"; break;
                case '\r': Result << "\\r"; break;
                case '\t': Result << "\\t"; break;
                default:
                    if (Character < 0x20)
                        Result << "\\u" << std::hex << std::setw(4)
                               << std::setfill('0') << static_cast<int>(Character);
                    else
                        Result << static_cast<char>(Character);
                }
            }
            return Result.str();
        }

        std::string BuildManifest(
            const eastl::vector<FArdaShaderCompileJob>& Jobs)
        {
            std::ostringstream Stream;
            Stream << "{\n  \"schema\": 1,\n  \"jobs\": [\n";
            for (size_t Index = 0; Index < Jobs.size(); ++Index)
            {
                const auto& Job = Jobs[Index];
                Stream << "    {\n"
                    << "      \"type\": \"" << JsonEscape(Job.mType.GetName()) << "\",\n"
                    << "      \"backend\": \"" << JsonEscape(ToStd(Job.mTarget.mBackendName)) << "\",\n"
                    << "      \"permutation\": " << Job.mPermutationId << ",\n"
                    << "      \"source\": \"" << JsonEscape(ToStd(Job.mSourceIdentity)) << "\",\n"
                    << "      \"output\": \"" << JsonEscape(Job.mOutputPath.filename().generic_string()) << "\",\n"
                    << "      \"entry\": \"" << JsonEscape(Job.mType.GetEntryPoint()) << "\",\n"
                    << "      \"profile\": \"" << JsonEscape(ToStd(Job.mProfile)) << "\",\n"
                    << "      \"defines\": {";
                const auto& Defines = Job.mEnvironment.GetDefines();
                for (size_t DefineIndex = 0; DefineIndex < Defines.size(); ++DefineIndex)
                {
                    Stream << (DefineIndex == 0 ? "\n" : ",\n")
                        << "        \"" << JsonEscape(ToStd(Defines[DefineIndex].mName))
                        << "\": \"" << JsonEscape(ToStd(Defines[DefineIndex].mValue)) << "\"";
                }
                if (!Defines.empty())
                    Stream << '\n' << "      ";
                Stream << "},\n      \"key\": \"" << KeyText(Job.mInputKey).substr(0, 16)
                    << "\"\n    }" << (Index + 1 == Jobs.size() ? "\n" : ",\n");
            }
            Stream << "  ]\n}\n";
            return Stream.str();
        }
    }

    void ConfigureShaderCompiler(
        const FArdaShaderCompilerConfiguration& Configuration)
    {
        std::lock_guard<std::mutex> Lock(GConfigurationMutex);
        GConfiguration = Configuration;
    }

    FArdaShaderCompilerConfiguration GetShaderCompilerConfiguration()
    {
        std::lock_guard<std::mutex> Lock(GConfigurationMutex);
        return GConfiguration;
    }

    void ResetShaderCompilerConfiguration()
    {
        std::lock_guard<std::mutex> Lock(GConfigurationMutex);
        GConfiguration = MakeDefaultConfiguration();
    }

    static FArdaShaderCompileResult BuildRegisteredShaderCompileJobsWithSnapshot(
        const std::filesystem::path& OutputDirectory,
        const eastl::vector<FArdaShaderTarget>& Targets,
        const FArdaShaderCompilerConfiguration& Configuration,
        const std::filesystem::path& Compiler)
    {
        FArdaShaderCompileResult Result;
        const FArdaShaderRegistrationStatus Registration =
            FArdaShaderTypeRegistration::CommitAll();
        if (!Registration)
        {
            Result.mDiagnostics.push_back(MakeDiagnostic(
                EArdaShaderCompileError::RegistrationFailed, nullptr, DefaultBackend,
                0, {}, {}, ToStd(Registration.mMessage)));
            return Result;
        }
        eastl::vector<FArdaShaderTarget> UniqueTargets = Targets;
        std::sort(
            UniqueTargets.begin(), UniqueTargets.end(),
            [](const FArdaShaderTarget& Left, const FArdaShaderTarget& Right)
            {
                return Left.mBackendName < Right.mBackendName;
            });
        UniqueTargets.erase(
            std::unique(
                UniqueTargets.begin(), UniqueTargets.end(),
                [](const FArdaShaderTarget& Left, const FArdaShaderTarget& Right)
                {
                    return Left.mBackendName == Right.mBackendName;
                }),
            UniqueTargets.end());
        for (const FArdaShaderType& Type :
             FArdaShaderTypeRegistration::EnumerateSnapshots())
        {
            for (const FArdaShaderTarget& Target : UniqueTargets)
            {
                for (uint32_t PermutationId = 0;
                     PermutationId < Type.GetPermutationCount();
                     ++PermutationId)
                {
                    if (!Type.ShouldCompilePermutation(Target, PermutationId))
                    {
                        ++Result.mJobsSkipped;
                        continue;
                    }
                    FArdaShaderCompileJob Job;
                    FArdaShaderCompileDiagnostic Diagnostic;
                    if (!PopulateJob(
                            Type, Target, PermutationId, OutputDirectory,
                            Configuration, Compiler, Job, Diagnostic))
                    {
                        Diagnostic.mBackendName = Target.mBackendName;
                        Result.mDiagnostics.push_back(eastl::move(Diagnostic));
                        continue;
                    }
                    Result.mJobs.push_back(eastl::move(Job));
                }
            }
        }
        std::sort(
            Result.mJobs.begin(), Result.mJobs.end(),
            [](const auto& Left, const auto& Right)
            {
                const int TypeOrder =
                    std::string(Left.mType.GetName()).compare(Right.mType.GetName());
                if (TypeOrder != 0) return TypeOrder < 0;
                if (Left.mTarget.mBackendName != Right.mTarget.mBackendName)
                    return Left.mTarget.mBackendName < Right.mTarget.mBackendName;
                return Left.mPermutationId < Right.mPermutationId;
            });
        return Result;
    }

    static bool ResolveShaderTargets(
        const std::vector<EArdaBackendType>& Backends,
        eastl::vector<FArdaShaderTarget>& OutTargets,
        FArdaShaderCompileResult& OutResult)
    {
        for (const EArdaBackendType Backend : Backends)
        {
            FArdaShaderTarget Target;
            if (!ResolveDefaultShaderTarget(Backend, Target))
            {
                OutResult.mDiagnostics.push_back(MakeDiagnostic(
                    EArdaShaderCompileError::CompilerUnavailable, nullptr, Backend,
                    0, {}, {}, "No registered backend module can compile the requested shader target."));
                return false;
            }
            OutTargets.push_back(eastl::move(Target));
        }
        return true;
    }

    static bool ResolveShaderTargets(
        const eastl::vector<eastl::string>& BackendNames,
        eastl::vector<FArdaShaderTarget>& OutTargets,
        FArdaShaderCompileResult& OutResult)
    {
        for (const eastl::string& BackendName : BackendNames)
        {
            FArdaShaderTarget Target;
            if (!ResolveShaderTarget(BackendName.c_str(), Target))
            {
                auto Diagnostic = MakeDiagnostic(
                    EArdaShaderCompileError::CompilerUnavailable, nullptr, DefaultBackend,
                    0, {}, {}, "The requested shader backend module is not registered.");
                Diagnostic.mBackendName = BackendName;
                OutResult.mDiagnostics.push_back(eastl::move(Diagnostic));
                return false;
            }
            OutTargets.push_back(eastl::move(Target));
        }
        return true;
    }

    FArdaShaderCompileResult BuildRegisteredShaderCompileJobs(
        const std::filesystem::path& OutputDirectory,
        const std::vector<EArdaBackendType>& Backends)
    {
        FArdaShaderCompileResult Result;
        eastl::vector<FArdaShaderTarget> Targets;
        if (!ResolveShaderTargets(Backends, Targets, Result))
            return Result;
        const FArdaShaderCompilerConfiguration Configuration =
            GetShaderCompilerConfiguration();
        const std::filesystem::path Compiler = ResolveCompiler(Configuration);
        return BuildRegisteredShaderCompileJobsWithSnapshot(
            OutputDirectory, Targets, Configuration, Compiler);
    }

    FArdaShaderCompileResult BuildRegisteredShaderCompileJobs(
        const std::filesystem::path& OutputDirectory,
        const eastl::vector<eastl::string>& BackendNames)
    {
        FArdaShaderCompileResult Result;
        eastl::vector<FArdaShaderTarget> Targets;
        if (!ResolveShaderTargets(BackendNames, Targets, Result))
            return Result;
        const FArdaShaderCompilerConfiguration Configuration =
            GetShaderCompilerConfiguration();
        return BuildRegisteredShaderCompileJobsWithSnapshot(
            OutputDirectory, Targets, Configuration, ResolveCompiler(Configuration));
    }

    static FArdaShaderCompileResult CompileRegisteredShaderArtifactsWithTargets(
        const std::filesystem::path& OutputDirectory,
        const eastl::vector<FArdaShaderTarget>& Targets)
    {
        const FArdaShaderCompilerConfiguration Configuration =
            GetShaderCompilerConfiguration();
        const std::filesystem::path Compiler = ResolveCompiler(Configuration);
        FArdaShaderCompileResult Result =
            BuildRegisteredShaderCompileJobsWithSnapshot(
                OutputDirectory, Targets, Configuration, Compiler);
        if (!Result)
            return Result;
        const std::filesystem::path StagingDirectory =
            TemporaryPath(OutputDirectory, "staging");
        std::error_code Error;
        std::filesystem::create_directories(StagingDirectory, Error);
        if (Error)
        {
            Result.mDiagnostics.push_back(MakeDiagnostic(
                EArdaShaderCompileError::DirectoryCreationFailed, nullptr,
                DefaultBackend, 0, {}, StagingDirectory,
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
        const std::filesystem::path StagedManifest =
            StagingDirectory / "ArdaShaderManifest.json";
        if (!AtomicWrite(StagedManifest, BuildManifest(Result.mJobs)))
        {
            std::filesystem::remove_all(StagingDirectory, Error);
            Result.mDiagnostics.push_back(MakeDiagnostic(
                EArdaShaderCompileError::ManifestWriteFailed, nullptr, DefaultBackend,
                0, {}, OutputDirectory / "ArdaShaderManifest.json",
                "Unable to write the staged deterministic shader cook manifest."));
            return Result;
        }
        std::filesystem::create_directories(OutputDirectory, Error);
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> Files;
        for (const auto& Job : Result.mJobs)
        {
            const auto StagedArtifact = StagingDirectory / Job.mOutputPath.filename();
            Files.emplace_back(StagedArtifact, Job.mOutputPath);
            Files.emplace_back(
                StagedArtifact.string() + ".arda-key",
                Job.mOutputPath.string() + ".arda-key");
        }
        Files.emplace_back(
            StagedManifest,
            OutputDirectory / "ArdaShaderManifest.json");
        const std::filesystem::path BackupDirectory =
            TemporaryPath(OutputDirectory, "rollback");
        if (Error || !PublishFilesTransaction(Files, BackupDirectory))
        {
            std::filesystem::remove_all(StagingDirectory, Error);
            std::filesystem::remove_all(BackupDirectory, Error);
            Result.mDiagnostics.push_back(MakeDiagnostic(
                EArdaShaderCompileError::CacheWriteFailed, nullptr, DefaultBackend,
                0, {}, OutputDirectory,
                "Unable to publish the staged shader cook; previous outputs were restored."));
            return Result;
        }
        std::filesystem::remove_all(StagingDirectory, Error);
        return Result;
    }

    FArdaShaderCompileResult CompileRegisteredShaderArtifacts(
        const std::filesystem::path& OutputDirectory,
        const std::vector<EArdaBackendType>& Backends)
    {
        FArdaShaderCompileResult Result;
        eastl::vector<FArdaShaderTarget> Targets;
        if (!ResolveShaderTargets(Backends, Targets, Result))
            return Result;
        return CompileRegisteredShaderArtifactsWithTargets(OutputDirectory, Targets);
    }

    FArdaShaderCompileResult CompileRegisteredShaderArtifacts(
        const std::filesystem::path& OutputDirectory,
        const eastl::vector<eastl::string>& BackendNames)
    {
        FArdaShaderCompileResult Result;
        eastl::vector<FArdaShaderTarget> Targets;
        if (!ResolveShaderTargets(BackendNames, Targets, Result))
            return Result;
        return CompileRegisteredShaderArtifactsWithTargets(OutputDirectory, Targets);
    }

    FArdaShaderCompileResult CompileRegisteredShaderArtifacts(
        const std::filesystem::path& OutputDirectory,
        EArdaBackendType Backend)
    {
        return CompileRegisteredShaderArtifacts(
            OutputDirectory,
            std::vector<EArdaBackendType>{ Backend });
    }

    FArdaShaderCompileResult CompileRegisteredShaderArtifacts(
        const std::filesystem::path& OutputDirectory,
        const char* BackendName)
    {
        return CompileRegisteredShaderArtifacts(
            OutputDirectory,
            eastl::vector<eastl::string>{ BackendName ? BackendName : "" });
    }

    static FArdaShaderCompileResult EnsureRegisteredShaderArtifactWithSnapshot(
        const FArdaShaderType& Type,
        const FArdaShaderTarget& Target,
        uint32_t PermutationId,
        const std::filesystem::path& OutputDirectory,
        const FArdaShaderCompilerConfiguration& Configuration,
        const std::filesystem::path& Compiler);

    static FArdaShaderCompileResult EnsureRegisteredShaderArtifactsForTarget(
        const std::filesystem::path& OutputDirectory,
        const FArdaShaderTarget& Target)
    {
        const EArdaBackendType Backend = Target.mBackend;
        FArdaShaderCompileResult Result;
        const FArdaShaderRegistrationStatus Registration =
            FArdaShaderTypeRegistration::CommitAll();
        if (!Registration)
        {
            Result.mDiagnostics.push_back(MakeDiagnostic(
                EArdaShaderCompileError::RegistrationFailed, nullptr, Backend,
                0, {}, {}, ToStd(Registration.mMessage)));
            return Result;
        }

        const FArdaShaderCompilerConfiguration Configuration =
            GetShaderCompilerConfiguration();
        const std::filesystem::path Compiler = ResolveCompiler(Configuration);
        for (const FArdaShaderType& Type :
             FArdaShaderTypeRegistration::EnumerateSnapshots())
        {
            for (uint32_t PermutationId = 0;
                 PermutationId < Type.GetPermutationCount();
                 ++PermutationId)
            {
                if (!Type.ShouldCompilePermutation(Target, PermutationId))
                {
                    ++Result.mJobsSkipped;
                    continue;
                }
                FArdaShaderCompileResult JobResult =
                    EnsureRegisteredShaderArtifactWithSnapshot(
                        Type, Target, PermutationId, OutputDirectory,
                        Configuration, Compiler);
                Result.mJobsCompiled += JobResult.mJobsCompiled;
                Result.mCacheHits += JobResult.mCacheHits;
                Result.mJobsSkipped += JobResult.mJobsSkipped;
                for (auto& Job : JobResult.mJobs)
                    Result.mJobs.push_back(eastl::move(Job));
                for (auto& Diagnostic : JobResult.mDiagnostics)
                    Result.mDiagnostics.push_back(eastl::move(Diagnostic));
            }
        }
        return Result;
    }

    FArdaShaderCompileResult EnsureRegisteredShaderArtifacts(
        const std::filesystem::path& OutputDirectory,
        EArdaBackendType Backend)
    {
        FArdaShaderTarget Target;
        if (!ResolveDefaultShaderTarget(Backend, Target))
        {
            FArdaShaderCompileResult Result;
            Result.mDiagnostics.push_back(MakeDiagnostic(
                EArdaShaderCompileError::CompilerUnavailable, nullptr, Backend,
                0, {}, {}, "No registered backend module can compile the requested shader target."));
            return Result;
        }
        return EnsureRegisteredShaderArtifactsForTarget(OutputDirectory, Target);
    }

    FArdaShaderCompileResult EnsureRegisteredShaderArtifacts(
        const std::filesystem::path& OutputDirectory,
        const char* BackendName)
    {
        FArdaShaderTarget Target;
        if (!ResolveShaderTarget(BackendName, Target))
        {
            FArdaShaderCompileResult Result;
            auto Diagnostic = MakeDiagnostic(
                EArdaShaderCompileError::CompilerUnavailable, nullptr, DefaultBackend,
                0, {}, {}, "The requested shader backend module is not registered.");
            Diagnostic.mBackendName = BackendName ? BackendName : "";
            Result.mDiagnostics.push_back(eastl::move(Diagnostic));
            return Result;
        }
        return EnsureRegisteredShaderArtifactsForTarget(OutputDirectory, Target);
    }

    static FArdaShaderCompileResult EnsureRegisteredShaderArtifactWithSnapshot(
        const FArdaShaderType& Type,
        const FArdaShaderTarget& Target,
        uint32_t PermutationId,
        const std::filesystem::path& OutputDirectory,
        const FArdaShaderCompilerConfiguration& Configuration,
        const std::filesystem::path& Compiler)
    {
        const EArdaBackendType Backend = Target.mBackend;
        FArdaShaderCompileResult Result;
        const FArdaShaderRegistrationStatus Registration =
            FArdaShaderTypeRegistration::CommitAll();
        if (!Registration)
        {
            Result.mDiagnostics.push_back(MakeDiagnostic(
                EArdaShaderCompileError::RegistrationFailed, &Type, Backend,
                PermutationId, {}, {}, ToStd(Registration.mMessage)));
            return Result;
        }
        if (PermutationId >= Type.GetPermutationCount() ||
            !Type.ShouldCompilePermutation(Target, PermutationId))
        {
            Result.mDiagnostics.push_back(MakeDiagnostic(
                EArdaShaderCompileError::InvalidPermutation, &Type, Backend,
                PermutationId, {}, {},
                "The requested permutation is invalid or filtered by its registered compile policy."));
            return Result;
        }
        const eastl::string Stem = Type.GetPermutationArtifactStem(PermutationId);
        const std::filesystem::path Output = OutputDirectory /
            (ToStd(Stem) + ToStd(Target.mArtifactExtension));
        if (!IsContainedArtifactPath(OutputDirectory, Output))
        {
            Result.mDiagnostics.push_back(MakeDiagnostic(
                EArdaShaderCompileError::InvalidPermutation, &Type, Backend,
                PermutationId, {}, Output,
                "Generated shader artifact path escapes its output directory."));
            return Result;
        }
        std::error_code Error;
        const bool Exists = IsRegularNonEmpty(Output);
        const std::filesystem::path Sidecar = Output.string() + ".arda-key";
        const bool HasSidecar =
            std::filesystem::is_regular_file(Sidecar, Error) && !Error;
        const bool BytecodeOnly =
            !Configuration.mbCompileMissingArtifacts &&
            !Configuration.mbCompileOutdatedArtifacts;

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
            Result.mDiagnostics.push_back(MakeDiagnostic(
                EArdaShaderCompileError::ArtifactMissing, &Type, Backend,
                PermutationId, {}, Output,
                "Shader artifact is missing and development auto compilation is disabled."));
            return Result;
        }

        FArdaShaderCompileJob Job;
        FArdaShaderCompileDiagnostic Diagnostic;
        if (!PopulateJob(
                Type, Target, PermutationId, OutputDirectory, Configuration,
                Compiler, Job, Diagnostic))
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
            Result.mDiagnostics.push_back(MakeDiagnostic(
                EArdaShaderCompileError::ArtifactOutdated, &Type, Backend,
                PermutationId, Job.mSourcePath, Output,
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
            ++Result.mCacheHits;
        else
            ++Result.mJobsCompiled;
        return Result;
    }

    FArdaShaderCompileResult EnsureRegisteredShaderArtifact(
        const FArdaShaderType& Type,
        EArdaBackendType Backend,
        uint32_t PermutationId,
        const std::filesystem::path& OutputDirectory)
    {
        FArdaShaderTarget Target;
        if (!ResolveDefaultShaderTarget(Backend, Target))
        {
            FArdaShaderCompileResult Result;
            Result.mDiagnostics.push_back(MakeDiagnostic(
                EArdaShaderCompileError::CompilerUnavailable, &Type, Backend,
                PermutationId, {}, {}, "No registered backend module can compile the requested shader target."));
            return Result;
        }
        const FArdaShaderCompilerConfiguration Configuration =
            GetShaderCompilerConfiguration();
        const std::filesystem::path Compiler = ResolveCompiler(Configuration);
        return EnsureRegisteredShaderArtifactWithSnapshot(
            Type, Target, PermutationId, OutputDirectory,
            Configuration, Compiler);
    }

    FArdaShaderCompileResult EnsureRegisteredShaderArtifact(
        const FArdaShaderType& Type,
        const char* BackendName,
        uint32_t PermutationId,
        const std::filesystem::path& OutputDirectory)
    {
        FArdaShaderTarget Target;
        if (!ResolveShaderTarget(BackendName, Target))
        {
            FArdaShaderCompileResult Result;
            auto Diagnostic = MakeDiagnostic(
                EArdaShaderCompileError::CompilerUnavailable, &Type, DefaultBackend,
                PermutationId, {}, {}, "The requested shader backend module is not registered.");
            Diagnostic.mBackendName = BackendName ? BackendName : "";
            Result.mDiagnostics.push_back(eastl::move(Diagnostic));
            return Result;
        }
        const FArdaShaderCompilerConfiguration Configuration =
            GetShaderCompilerConfiguration();
        FArdaShaderCompileResult Result = EnsureRegisteredShaderArtifactWithSnapshot(
            Type, Target, PermutationId, OutputDirectory,
            Configuration, ResolveCompiler(Configuration));
        for (FArdaShaderCompileDiagnostic& Diagnostic : Result.mDiagnostics)
            Diagnostic.mBackendName = Target.mBackendName;
        return Result;
    }
}
