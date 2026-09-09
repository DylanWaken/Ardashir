#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ShaderStructs/ArdaShaderCompiler.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace
{
    void EnableLocalRecords(
        const arda::FArdaShaderPermutationParameters&,
        arda::FArdaShaderCompileEnvironment& Environment)
    {
        (void)Environment.SetDefine("ARDA_LOCAL_RECORDS", true);
    }
    bool CompileOnlyForD3D12(
        const arda::FArdaShaderPermutationParameters& Parameters)
    {
        return Parameters.mBinaryFormat ==
            arda::EArdaShaderBinaryFormat::Dxil;
    }

    class FArdaRuntimeShaderEnvironment final : public testing::Environment
    {
    public:
        void SetUp() override
        {
            using namespace arda;
            using Stage = arda::EArdaRHIShaderStage;

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
            const auto CudaFallbackSource = (SourceDirectory / "ArdaCudaFallback.hlsl").string();
            FArdaShaderTypeRegistration CudaFallback(
                "ArdaCudaFallback", CudaFallbackSource.c_str(), "ArdaCudaFallback",
                "AddFallbackCS", Stage::Compute, nullptr);
            FArdaShaderTypeRegistration ResourceCollection(
                "ArdaResourceCollection", StructSource.c_str(),
                "ArdaResourceCollection", "ResourceCollectionCS", Stage::Compute, nullptr);
            FArdaShaderTypeRegistration VulkanLayoutCompute(
                "ArdaBackendTestVulkanLayoutCompute", StructSource.c_str(),
                "ArdaVulkanLayoutTest", "VulkanLayoutTestCS",
                Stage::Compute, nullptr);
            FArdaShaderTypeRegistration DirectHeapSamplerCompute(
                "ArdaBackendTestDirectHeapSamplerCompute",
                StructSource.c_str(),
                "ArdaDirectHeapSamplerTest", "DirectHeapSamplerTestCS",
                Stage::Compute, nullptr);
            FArdaShaderTypeRegistration Vertex(
                "ArdaBackendTestVertex", StructSource.c_str(),
                "ArdaPipelineStateTestVS", "PipelineStateTestVS",
                Stage::Vertex, nullptr);
            FArdaShaderTypeRegistration Pixel(
                "ArdaBackendTestPixel", StructSource.c_str(),
                "ArdaPipelineStateTestPS", "PipelineStateTestPS",
                Stage::Pixel, nullptr);
            FArdaShaderTypeRegistration Mesh(
                "ArdaBackendTestMesh", StructSource.c_str(),
                "ArdaMeshPipelineTestMS", "MeshPipelineTestMS",
                Stage::Mesh, nullptr);
            FArdaShaderTypeRegistration MeshPixel(
                "ArdaBackendTestMeshPixel", StructSource.c_str(),
                "ArdaMeshPipelineTestPS", "MeshPipelineTestPS",
                Stage::Pixel, nullptr);
            FArdaShaderTypeRegistration MeshTablePixel(
                "ArdaMeshTablePixel", StructSource.c_str(),
                "ArdaMeshTablePS", "MeshTablePS", Stage::Pixel, nullptr);
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
            FArdaShaderTypeRegistration LocalRayGeneration(
                "ArdaBackendLocalRayGeneration", RayTracingSource.c_str(),
                "ArdaLocalRayTracingTest", "KnownSceneRayGen",
                Stage::RayGeneration, nullptr, 1, nullptr, &EnableLocalRecords);
            FArdaShaderTypeRegistration WorkGraph(
                "ArdaBackendTestWorkGraph", StructSource.c_str(),
                "ArdaWorkGraphTest", "WorkGraphMain",
                Stage::WorkGraph, nullptr, 1, &CompileOnlyForD3D12);

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
            const std::string ComputeSource =
                (SourceDirectory / "ArdaComputeConformance.hlsl").string();
            FArdaShaderTypeRegistration Subgroup(
                "ArdaSubgroup", ComputeSource.c_str(), "ArdaSubgroup", "SubgroupCS", Stage::Compute, nullptr);
            FArdaShaderTypeRegistration Float16(
                "ArdaFloat16", ComputeSource.c_str(), "ArdaFloat16", "Float16CS", Stage::Compute, nullptr);
            FArdaShaderTypeRegistration Int8(
                "ArdaInt8", ComputeSource.c_str(), "ArdaInt8", "Int8CS", Stage::Compute, nullptr);
            FArdaShaderTypeRegistration RuntimeDescriptors(
                "ArdaRuntimeDescriptors", ComputeSource.c_str(), "ArdaRuntimeDescriptors", "RuntimeDescriptorsCS", Stage::Compute, nullptr);
            FArdaShaderTypeRegistration InlineRayQuery(
                "ArdaInlineRayQuery", ComputeSource.c_str(), "ArdaInlineRayQuery", "InlineRayQueryCS", Stage::Compute, nullptr);
            const auto FeedbackSource = (SourceDirectory / "ArdaSamplerFeedbackConformance.hlsl").string();
            FArdaShaderTypeRegistration SamplerFeedback(
                "ArdaSamplerFeedback", FeedbackSource.c_str(), "ArdaSamplerFeedback", "SamplerFeedbackCS",
                Stage::Compute, nullptr, 1, &CompileOnlyForD3D12);
            FArdaShaderTypeRegistration SamplerFeedbackRegion(
                "ArdaSamplerFeedbackRegion", FeedbackSource.c_str(), "ArdaSamplerFeedbackRegion", "SamplerFeedbackRegionCS",
                Stage::Compute, nullptr, 1, &CompileOnlyForD3D12);

            RuntimeConfiguration.mCommonArguments = {"-T", "cs_6_6", "-HV", "2021", "-enable-16bit-types"};
            ConfigureShaderCompiler(RuntimeConfiguration);
            ASSERT_TRUE(FArdaShaderTypeRegistration::CommitAll());
            for (const auto& Module : EnumerateBackendModules())
            {
                if (Module.mShaderArtifactExtension.empty())
                {
                    continue;
                }
                FArdaShaderTarget Target;
                ASSERT_TRUE(ResolveShaderTarget(Module.mName.c_str(), Target));
                for (const auto* Type : {&Subgroup.GetType(), &Float16.GetType(), &Int8.GetType(),
                    &RuntimeDescriptors.GetType(), &InlineRayQuery.GetType(),
                    &SamplerFeedback.GetType(), &SamplerFeedbackRegion.GetType()})
                {
                    if (!Type->ShouldCompilePermutation(Target, 0))
                    {
                        continue;
                    }
                    auto ShaderConfiguration = RuntimeConfiguration;
                    if (Type == &SamplerFeedback.GetType() || Type == &SamplerFeedbackRegion.GetType())
                    {
                        ShaderConfiguration.mCommonArguments = {"-T", "cs_6_5"};
                    }
                    ConfigureShaderCompiler(ShaderConfiguration);
                    const auto Result = EnsureRegisteredShaderArtifact(
                        *Type, Module.mName.c_str(), 0, ARDA_BACKEND_TEST_SHADER_DIR);
                    EXPECT_TRUE(Result) << (Result.mDiagnostics.empty() ? "Shader compilation failed" :
                        Result.mDiagnostics.front().mMessage.c_str());
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
