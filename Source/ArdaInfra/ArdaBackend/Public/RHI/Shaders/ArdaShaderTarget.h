/** Shader target identity and backend compiler invocation contract. */
#pragma once

#include "RHI/Resources/ArdaRHITypes.h"
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <filesystem>

namespace arda
{
	/** Identifies the bytecode family consumed by a backend module. */
	enum class EArdaShaderBinaryFormat : uint8_t
	{
		/** DirectX Intermediate Language produced from HLSL. */
		Dxil,
		/** Standard Portable Intermediate Representation for Vulkan. */
		Spirv,
		/** A format whose compiler invocation is entirely backend-defined. */
		BackendDefined
	};

	/** Describes whether a module handled a shader compiler process itself. */
	enum class EArdaBackendShaderCompileResult : uint8_t
	{
		/** Core should launch the configured executable and argument list. */
		NotHandled,
		/** Module produced the requested output artifact. */
		Success,
		/** Module handled the request but compilation failed. */
		Failure
	};

	/** Immutable shader-facing identity resolved from a registered backend module. */
	struct FArdaShaderTarget
	{
		/** Stable module registry name. */
		eastl::string mBackendName;
		/** Bytecode family consumed by the module. */
		EArdaShaderBinaryFormat mBinaryFormat = EArdaShaderBinaryFormat::BackendDefined;
		/** Artifact suffix, including its leading period. */
		eastl::string mArtifactExtension;
		/** Stable cache identity when no compiler executable is used. */
		eastl::string mCompilerIdentity;

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return !mBackendName.empty() && !mArtifactExtension.empty();
		}
	};

	/**
     * Represents a backend-owned shader compiler invocation.
     *
     * The core fills a DXC-compatible fallback invocation before offering it to
     * the selected module. A module may rewrite that command or execute the job
     * itself through InvokeShaderCompiler.
     */
	struct FArdaBackendShaderCompileInvocation
	{
		/** Source file passed to the compiler. */
		std::filesystem::path mSourcePath;
		/** Final artifact path expected by Arda's shader cache. */
		std::filesystem::path mOutputPath;
		/** Compiler executable selected by the core, or replaced by the module. */
		std::filesystem::path mCompilerExecutable;
		/** Shader entry point; empty for library profiles. */
		eastl::string mEntryPoint;
		/** Backend-neutral shader stage requested by the registered shader type. */
		arda::EArdaRHIShaderStage mStage = arda::EArdaRHIShaderStage::None;
		/** Backend compiler profile, such as cs_6_0 or lib_6_3. */
		eastl::string mProfile;
		/** Complete argument list excluding the executable. */
		eastl::vector<eastl::string> mArguments;
	};
}
