#include "ArdaBackend.h"
#include "ShaderStructs/ArdaShaderCompiler.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace
{
    class FArdaRuntimeShaderEnvironment final : public testing::Environment
    {
    public:
        void SetUp() override
        {
            using namespace arda;
            using namespace backend;
            using Stage = rhi::EArdaRHIShaderStage;

            const std::filesystem::path SourceDirectory =
                ARDA_BACKEND_TEST_SHADER_SOURCE_DIR;
            const std::string StructSource =
                (SourceDirectory / "ArdaShaderStructTest.hlsl").string();
            const std::string RayTracingSource =
                (SourceDirectory / "ArdaRayTracingTest.hlsl").string();

            FArdaShaderTypeRegistration Compute(
                "ArdaBackendTestCompute", StructSource.c_str(),
                "ArdaShaderStructTest", "ShaderStructTestCS",
                Stage::Compute, nullptr);
            FArdaShaderTypeRegistration Vertex(
                "ArdaBackendTestVertex", StructSource.c_str(),
                "ArdaPipelineStateTestVS", "PipelineStateTestVS",
                Stage::Vertex, nullptr);
            FArdaShaderTypeRegistration Pixel(
                "ArdaBackendTestPixel", StructSource.c_str(),
                "ArdaPipelineStateTestPS", "PipelineStateTestPS",
                Stage::Pixel, nullptr);
            FArdaShaderTypeRegistration BindingSpaceVertex(
                "ArdaBackendTestBindingSpaceVertex", StructSource.c_str(),
                "ArdaBindingSpaceVS", "BindingSpaceVS",
                Stage::Vertex, nullptr);
            FArdaShaderTypeRegistration BindingSpacePixel(
                "ArdaBackendTestBindingSpacePixel", StructSource.c_str(),
                "ArdaBindingSpacePS", "BindingSpacePS",
                Stage::Pixel, nullptr);
            FArdaShaderTypeRegistration RayGeneration(
                "ArdaBackendTestRayGeneration", RayTracingSource.c_str(),
                "ArdaRayTracingTest", "RayGen",
                Stage::RayGeneration, nullptr);

            const FArdaShaderCompilerConfiguration PreviousConfiguration =
                GetShaderCompilerConfiguration();
            FArdaShaderCompilerConfiguration RuntimeConfiguration =
                PreviousConfiguration;
            RuntimeConfiguration.mbCompileMissingArtifacts = true;
            RuntimeConfiguration.mbCompileOutdatedArtifacts = true;
            ConfigureShaderCompiler(RuntimeConfiguration);

            bool FoundShaderTarget = false;
            for (const FArdaBackendModuleDescriptor& Module :
                 EnumerateBackendModules())
            {
                if (Module.mShaderArtifactExtension.empty())
                    continue;
                FoundShaderTarget = true;
                const FArdaShaderCompileResult Result =
                    EnsureRegisteredShaderArtifacts(
                        std::filesystem::path(ARDA_BACKEND_TEST_SHADER_DIR),
                        Module.mName.c_str());
                if (!Result)
                {
                    const char* Message = Result.mDiagnostics.empty()
                        ? "Ardashir runtime shader compilation failed."
                        : Result.mDiagnostics.front().mMessage.c_str();
                    ADD_FAILURE() << Message;
                    ConfigureShaderCompiler(PreviousConfiguration);
                    return;
                }
            }
            ConfigureShaderCompiler(PreviousConfiguration);
            if (!FoundShaderTarget)
                ADD_FAILURE() << "No linked backend exposes a shader target.";
        }
    };
}

int main(int ArgumentCount, char** Arguments)
{
    testing::InitGoogleTest(&ArgumentCount, Arguments);
    testing::AddGlobalTestEnvironment(new FArdaRuntimeShaderEnvironment());
    return RUN_ALL_TESTS();
}
