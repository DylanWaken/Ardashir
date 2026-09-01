/** @file ArdaShaderCompiler.h
 *  @brief Declares registration-driven development and cook shader compilation.
 */
#pragma once

#include "ShaderStructs/ArdaShaderType.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace arda::backend
{
    /**
     * Identifies failures from development/cook shader compilation.
     * Shipping-style runtime use can leave compilation unconfigured and load
     * bytecode-only artifacts, analogous to Unreal's cooked runtime behavior.
     */
    enum class EArdaShaderCompileError : uint8_t
    {
        /** The operation succeeded. */
        None,
        /** Static shader registration could not be committed. */
        RegistrationFailed,
        /** Neither the module nor the fallback process launcher can compile the job. */
        CompilerUnavailable,
        /** A registered source stem could not be resolved. */
        SourceResolutionFailed,
        /** The registered shader stage cannot map to the fallback compiler profile model. */
        UnsupportedStage,
        /** A permutation identifier or compilation input was invalid. */
        InvalidPermutation,
        /** An output or temporary directory could not be created. */
        DirectoryCreationFailed,
        /** The external compiler process could not be started. */
        ProcessLaunchFailed,
        /** The selected compiler service failed or did not produce bytecode. */
        CompilationFailed,
        /** Compilation was disabled and the requested artifact was absent. */
        ArtifactMissing,
        /** A cache sidecar exists but does not match the requested inputs. */
        ArtifactOutdated,
        /** An artifact or sidecar cache key could not be committed. */
        CacheWriteFailed,
        /** The deterministic cook manifest could not be committed. */
        ManifestWriteFailed
    };

    /**
     * Configures the process-wide external shader compiler.
     * This value is copied by ConfigureShaderCompiler. Development defaults
     * compile missing and stale artifacts; shipping-style applications should
     * disable both switches and deploy prebuilt bytecode.
     */
    /** Additional compiler arguments for one exact registered backend module. */
    struct FArdaShaderCompilerModuleArguments
    {
        /** Stable backend module name. */
        eastl::string mBackendName;
        /** Deterministic arguments appended before the module configure hook. */
        eastl::vector<eastl::string> mArguments;
    };

    struct FArdaShaderCompilerConfiguration
    {
        /** Optional fallback compiler executable; empty uses the build default, then ARDASHIR_DXC_EXECUTABLE. */
        std::filesystem::path mCompilerExecutable;
        /** Optional root prepended to non-virtual source stems. */
        std::filesystem::path mSourceRoot;
        /** Whether runtime development loading may compile absent artifacts. */
        bool mbCompileMissingArtifacts =
#if defined(NDEBUG)
            false;
#else
            true;
#endif
        /** Whether runtime development loading may rebuild sidecar-key mismatches. */
        bool mbCompileOutdatedArtifacts =
#if defined(NDEBUG)
            false;
#else
            true;
#endif
        /** Deterministic arguments appended for every backend. */
        eastl::vector<eastl::string> mCommonArguments;
        /** Per-module arguments selected by stable backend name. */
        eastl::vector<FArdaShaderCompilerModuleArguments> mModuleArguments;
    };

    /** Describes one deterministic unit of external shader compilation. */
    struct FArdaShaderCompileJob
    {
        /** Owned immutable shader-type snapshot driving the job. */
        FArdaShaderType mType;
        /** Immutable module target selected for this job. */
        FArdaShaderTarget mTarget;
        /** Encoded permutation identifier. */
        uint32_t mPermutationId = 0;
        /** Resolved physical shader source file. */
        std::filesystem::path mSourcePath;
        /** Virtual or logical source identity used by deterministic manifests and keys. */
        eastl::string mSourceIdentity;
        /** Final bytecode artifact path. */
        std::filesystem::path mOutputPath;
        /** Compiler executable selected or replaced by the backend module. */
        std::filesystem::path mCompilerExecutable;
        /** Compiler profile initially derived from the registered stage and mutable by the module. */
        eastl::string mProfile;
        /** Sorted deterministic preprocessor environment. */
        FArdaShaderCompileEnvironment mEnvironment;
        /** Complete deterministic fallback arguments, excluding temporary output selection. */
        eastl::vector<eastl::string> mArguments;
        /** Stable FNV-1a key over compiler, registry, source, and job inputs. */
        uint64_t mInputKey = 0;
    };

    /** Reports one job-specific or operation-wide compiler diagnostic. */
    struct FArdaShaderCompileDiagnostic
    {
        /** Compiler result code. */
        EArdaShaderCompileError mCode = EArdaShaderCompileError::None;
        /** Registered shader type name, when applicable. */
        eastl::string mShaderType;
        /** Stable backend module associated with the diagnostic. */
        eastl::string mBackendName;
        /** Encoded permutation identifier. */
        uint32_t mPermutationId = 0;
        /** Source path associated with the diagnostic. */
        std::filesystem::path mSourcePath;
        /** Output artifact associated with the diagnostic. */
        std::filesystem::path mOutputPath;
        /** Human-readable details, including external compiler output when available. */
        eastl::string mMessage;
    };

    /** Aggregates an editor/development or explicit cook compilation operation. */
    struct FArdaShaderCompileResult
    {
        /** Jobs successfully compiled by an external worker. */
        uint32_t mJobsCompiled = 0;
        /** Jobs whose current sidecar key matched. */
        uint32_t mCacheHits = 0;
        /** Registered permutations filtered out by policy. */
        uint32_t mJobsSkipped = 0;
        /** Deterministically enumerated jobs. */
        eastl::vector<FArdaShaderCompileJob> mJobs;
        /** Errors and actionable process diagnostics. */
        eastl::vector<FArdaShaderCompileDiagnostic> mDiagnostics;
        /** @return True when no error diagnostic was produced. */
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return mDiagnostics.empty();
        }
    };

    /**
     * Copies a new process-wide development compiler configuration.
     * @param Configuration Configuration to copy under the process mutex.
     */
    void ConfigureShaderCompiler(
        const FArdaShaderCompilerConfiguration& Configuration);

    /**
     * Returns a thread-safe copy of the process-wide configuration.
     * @return Independent compiler configuration copy.
     */
    [[nodiscard]] FArdaShaderCompilerConfiguration
    GetShaderCompilerConfiguration();

    /** Restores development defaults and build/environment executable resolution. */
    void ResetShaderCompilerConfiguration();

    /** Builds deterministic jobs for exact registered module names. */
    [[nodiscard]] FArdaShaderCompileResult BuildRegisteredShaderCompileJobs(
        const std::filesystem::path& OutputDirectory,
        const eastl::vector<eastl::string>& BackendNames);

    /** Cooks artifacts for exact registered module names. */
    [[nodiscard]] FArdaShaderCompileResult CompileRegisteredShaderArtifacts(
        const std::filesystem::path& OutputDirectory,
        const eastl::vector<eastl::string>& BackendNames);

    /** Cooks artifacts for one exact registered module. */
    [[nodiscard]] FArdaShaderCompileResult CompileRegisteredShaderArtifacts(
        const std::filesystem::path& OutputDirectory,
        const char* BackendName);

    /** Ensures all artifacts for one exact registered module. */
    [[nodiscard]] FArdaShaderCompileResult EnsureRegisteredShaderArtifacts(
        const std::filesystem::path& OutputDirectory,
        const char* BackendName);

    /** Ensures one artifact for an exact registered module. */
    [[nodiscard]] FArdaShaderCompileResult EnsureRegisteredShaderArtifact(
        const FArdaShaderType& Type,
        const char* BackendName,
        uint32_t PermutationId,
        const std::filesystem::path& OutputDirectory);
}
