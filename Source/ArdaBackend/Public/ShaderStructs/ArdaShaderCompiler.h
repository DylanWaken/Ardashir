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
        /** No usable external DXC executable was configured. */
        CompilerUnavailable,
        /** A registered source stem could not be resolved. */
        SourceResolutionFailed,
        /** The registered shader stage cannot map to one DXC profile. */
        UnsupportedStage,
        /** A permutation identifier or compilation input was invalid. */
        InvalidPermutation,
        /** An output or temporary directory could not be created. */
        DirectoryCreationFailed,
        /** The external compiler process could not be started. */
        ProcessLaunchFailed,
        /** DXC returned failure or did not produce bytecode. */
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
    struct FArdaShaderCompilerConfiguration
    {
        /** External DXC executable; empty uses the build default, then ARDASHIR_DXC_EXECUTABLE. */
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
        /** Vulkan SPIR-V target environment passed to DXC. */
        eastl::string mVulkanTargetEnvironment = "vulkan1.3";
        /** Vulkan sampled-resource binding shift for register space zero. */
        uint32_t mVulkanTextureBindingShift = 0;
        /** Vulkan sampler binding shift for register space zero. */
        uint32_t mVulkanSamplerBindingShift = 128;
        /** Vulkan constant-buffer binding shift for register space zero. */
        uint32_t mVulkanConstantBufferBindingShift = 256;
        /** Vulkan unordered-access binding shift for register space zero. */
        uint32_t mVulkanUnorderedAccessBindingShift = 384;
        /** Deterministic arguments appended for every backend. */
        eastl::vector<eastl::string> mCommonArguments;
        /** Deterministic arguments appended only for DXIL jobs. */
        eastl::vector<eastl::string> mDxilArguments;
        /** Deterministic arguments appended only for SPIR-V jobs. */
        eastl::vector<eastl::string> mSpirvArguments;
    };

    /** Describes one deterministic unit of external shader compilation. */
    struct FArdaShaderCompileJob
    {
        /** Owned immutable shader-type snapshot driving the job. */
        FArdaShaderType mType;
        /** Target graphics backend. */
        EArdaBackendType mBackend = DefaultBackend;
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
        /** DXC target profile derived from the registered stage. */
        eastl::string mProfile;
        /** Sorted deterministic preprocessor environment. */
        FArdaShaderCompileEnvironment mEnvironment;
        /** Complete deterministic DXC arguments, excluding temporary output selection. */
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
        /** Target backend. */
        EArdaBackendType mBackend = DefaultBackend;
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

    /**
     * Enumerates deterministic compile jobs from committed static registrations.
     * The cache deliberately hashes every frozen shader-source file, so unrelated
     * include/source edits may conservatively invalidate all jobs.
     * @param OutputDirectory Directory receiving backend artifacts.
     * @param Backends Explicit target backends, typically D3D12 and Vulkan for cook.
     * @return Jobs, skipped count, or registration/source/input diagnostics.
     */
    [[nodiscard]] FArdaShaderCompileResult BuildRegisteredShaderCompileJobs(
        const std::filesystem::path& OutputDirectory,
        const std::vector<EArdaBackendType>& Backends);

    /**
     * Explicitly cooks all requested registered jobs, bypassing cache compatibility.
     * A deterministic ArdaShaderManifest.json is atomically written only after all
     * jobs succeed. Publication is serialized within this process and uses rollback
     * backups; concurrent writers in other processes are handled on a best-effort
     * basis because no portable cross-process lock is required.
     * @param OutputDirectory Directory receiving cooked artifacts and manifest.
     * @param Backends Explicit target backends.
     * @return Aggregate compilation counts, jobs, and diagnostics.
     */
    [[nodiscard]] FArdaShaderCompileResult CompileRegisteredShaderArtifacts(
        const std::filesystem::path& OutputDirectory,
        const std::vector<EArdaBackendType>& Backends);

    /**
     * Explicitly cooks registered jobs for one active backend.
     * @param OutputDirectory Directory receiving cooked artifacts and manifest.
     * @param Backend Active graphics backend.
     * @return Aggregate compilation counts, jobs, and diagnostics.
     */
    [[nodiscard]] FArdaShaderCompileResult CompileRegisteredShaderArtifacts(
        const std::filesystem::path& OutputDirectory,
        EArdaBackendType Backend);

    /**
     * Ensures every registered permutation selected for one active backend.
     *
     * Each selected permutation uses EnsureRegisteredShaderArtifact, preserving
     * persistent .arda-key cache hits across application executions. This is
     * not a force-cook operation and does not write a cook manifest.
     * @param OutputDirectory Persistent runtime shader cache directory.
     * @param Backend Active graphics backend.
     * @return Aggregate jobs, cache hits, compilations, skips, and diagnostics.
     */
    [[nodiscard]] FArdaShaderCompileResult EnsureRegisteredShaderArtifacts(
        const std::filesystem::path& OutputDirectory,
        EArdaBackendType Backend);

    /**
     * Ensures one artifact for development global-map loading.
     * Existing artifacts without an .arda-key sidecar remain accepted as legacy
     * prebuilt bytecode. Missing/stale artifacts compile only when configured;
     * otherwise this behaves as shipping-style bytecode-only loading. Same-process
     * writes to one output are serialized; cross-process exclusion is best-effort.
     * @param Type Registered shader type.
     * @param Backend Target graphics backend.
     * @param PermutationId Encoded permutation identifier.
     * @param OutputDirectory Directory containing runtime artifacts.
     * @return Compilation, cache-hit, compatibility, or actionable failure result.
     */
    [[nodiscard]] FArdaShaderCompileResult EnsureRegisteredShaderArtifact(
        const FArdaShaderType& Type,
        EArdaBackendType Backend,
        uint32_t PermutationId,
        const std::filesystem::path& OutputDirectory);
}
