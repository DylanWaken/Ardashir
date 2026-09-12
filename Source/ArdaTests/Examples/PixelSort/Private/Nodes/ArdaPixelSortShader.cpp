#include "Nodes/ArdaPixelSortNodes.h"
#include "ArdaPixelSortNodeInternal.h"
#include "ArdaBackend.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"
#include "ShaderStructs/ArdaShaderCompiler.h"
#include <string>

namespace arda
{
	FArdaRHIShaderRef LoadArdaPixelSortShader(FArdaRHIDeviceRef Device,
	    const std::filesystem::path& Directory,
	    const char* Name,
	    const char* Entry,
	    EArdaRHIShaderStage Stage)
	{
		// Scoped compiler overrides must not interleave when different node types prepare concurrently.
		static std::mutex ShaderMutex;
		std::lock_guard<std::mutex> Lock(ShaderMutex);
		const auto Source = (Directory / "Nodes/Shaders/ArdaPixelSort.hlsl").string();
		FArdaShaderTypeRegistration Registration(Name, Source.c_str(), Name, Entry, Stage, nullptr);
		const auto PreviousCompiler = GetShaderCompilerConfiguration();
		auto Compiler = PreviousCompiler;
		Compiler.mCompilerExecutable = (Directory / "ShaderCompiler/dxc.exe").string().c_str();
		Compiler.mbCompileMissingArtifacts = Compiler.mbCompileOutdatedArtifacts = true;
		ConfigureShaderCompiler(Compiler);
		const auto Cache = Directory / ".arda-cache/PixelSort";
		const auto Compiled = EnsureRegisteredShaderArtifacts(Cache, GetBackendConfiguration().mBackendName.c_str());
		ConfigureShaderCompiler(PreviousCompiler);
		if (!Compiled)
		{
			throw std::runtime_error(Compiled.mDiagnostics.empty() ? "Shader compilation failed."
			                                                       : Compiled.mDiagnostics.front().mMessage.c_str());
		}
		const auto Code = LoadShaderBytecode(
		    Cache / (std::string(Name) + GetShaderArtifactExtension(GetBackendConfiguration().mBackendName.c_str())));
		if (!Code)
		{
			throw std::runtime_error(Code.mDiagnostic.mMessage.c_str());
		}
		FArdaRHIShaderDesc D;
		D.mStage = Stage;
		D.mBytecode = Code.mBytecode.data();
		D.mBytecodeSize = Code.mBytecode.size();
		D.mEntryPoint = Entry;
		return Take(Device->CreateShader(D));
	}

}
