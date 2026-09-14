#pragma once

#include "ShaderStructs/ArdaShaderCompiler.h"

namespace arda
{
	/** Source-distributed examples opt into compilation in every build; LoadOnly still forbids it. */
	inline void ConfigureArdaExampleShaderCompiler(const std::filesystem::path& CompilerExecutable = {})
	{
		// Preserve application/tool discovery settings while choosing the examples' cache-update policy.
		auto Compiler = GetShaderCompilerConfiguration();
		if (!CompilerExecutable.empty())
		{
			Compiler.mCompilerExecutable = CompilerExecutable;
		}
		Compiler.mbCompileMissingArtifacts = true;
		Compiler.mbCompileOutdatedArtifacts = true;
		ConfigureShaderCompiler(Compiler);
	}
}
