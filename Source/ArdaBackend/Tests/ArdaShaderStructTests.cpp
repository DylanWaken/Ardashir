#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"
#include "ShaderStructs/ArdaShaderCompiler.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstdlib>
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

namespace
{
    bool ResolveLinkedTestShaderTarget(
        arda::backend::FArdaShaderTarget& OutTarget)
    {
        using namespace arda::backend;
        if (ResolveDefaultShaderTarget(DefaultBackend, OutTarget))
            return true;
        const auto Modules = EnumerateBackendModules();
        return !Modules.empty() &&
            ResolveShaderTarget(Modules.front().mName.c_str(), OutTarget);
    }

    std::vector<std::string> ParseFakeCompilerResponse(const std::string& Text)
    {
        std::vector<std::string> Arguments;
        size_t Cursor = 0;
        while (Cursor < Text.size())
        {
            while (Cursor < Text.size() &&
                   std::isspace(static_cast<unsigned char>(Text[Cursor])))
            {
                ++Cursor;
            }
            if (Cursor == Text.size())
                break;
            std::string Argument;
            if (Text[Cursor] == '"')
            {
                ++Cursor;
                while (Cursor < Text.size() && Text[Cursor] != '"')
                {
                    if (Text[Cursor] == '\\' && Cursor + 1 < Text.size() &&
                        Text[Cursor + 1] == '"')
                    {
                        Argument.push_back('"');
                        Cursor += 2;
                    }
                    else
                    {
                        Argument.push_back(Text[Cursor++]);
                    }
                }
                if (Cursor < Text.size())
                    ++Cursor;
            }
            else
            {
                while (Cursor < Text.size() &&
                       !std::isspace(static_cast<unsigned char>(Text[Cursor])))
                {
                    Argument.push_back(Text[Cursor++]);
                }
            }
            Arguments.push_back(std::move(Argument));
        }
        return Arguments;
    }

    std::vector<std::string> GetProcessArguments()
    {
        std::vector<std::string> Result;
#if defined(_WIN32)
        int Count = 0;
        wchar_t** Arguments = CommandLineToArgvW(GetCommandLineW(), &Count);
        if (Arguments == nullptr)
            return Result;
        for (int Index = 0; Index < Count; ++Index)
            Result.push_back(std::filesystem::path(Arguments[Index]).string());
        LocalFree(Arguments);
#elif defined(__linux__)
        std::ifstream Stream("/proc/self/cmdline", std::ios::binary);
        std::string Argument;
        while (std::getline(Stream, Argument, '\0'))
            Result.push_back(Argument);
#endif
        return Result;
    }

    std::filesystem::path GetTestExecutablePath()
    {
#if defined(_WIN32)
        std::wstring Buffer(32768, L'\0');
        const DWORD Length = GetModuleFileNameW(
            nullptr, Buffer.data(), static_cast<DWORD>(Buffer.size()));
        Buffer.resize(Length);
        return Buffer;
#elif defined(__linux__)
        std::vector<char> Buffer(32768);
        const ssize_t Length =
            readlink("/proc/self/exe", Buffer.data(), Buffer.size());
        return Length > 0
            ? std::filesystem::path(std::string(Buffer.data(), Length))
            : std::filesystem::path{};
#else
        return {};
#endif
    }

    struct FFakeDxcWorkerBootstrap
    {
        FFakeDxcWorkerBootstrap()
        {
            const std::vector<std::string> ProcessArguments =
                GetProcessArguments();
            const auto OutputArgument = std::find(
                ProcessArguments.begin(), ProcessArguments.end(), "-Fo");
            if (OutputArgument == ProcessArguments.end() ||
                std::next(OutputArgument) == ProcessArguments.end())
                return;
            std::ofstream Output(
                std::filesystem::path(*std::next(OutputArgument)),
                std::ios::binary | std::ios::trunc);
            Output << "fake-dxc-bytecode\n";
            for (const std::string& Argument : ProcessArguments)
            {
                Output << '"' << Argument << "\" ";
            }
            Output.close();
            std::_Exit(Output ? 0 : 1);
        }
    };

    const FFakeDxcWorkerBootstrap GFakeDxcWorkerBootstrap;
}

namespace
{
    using Stage = arda::rhi::EArdaRHIShaderStage;

    ARDA_SHADER_PERMUTATION_BOOL(FUseFeature, "USE_FEATURE");
    ARDA_SHADER_PERMUTATION_INT(FQualityLevel, "QUALITY_LEVEL", 3);

    struct FPermutationShaderPolicy
    {
        using FPermutationDomain =
            arda::backend::TArdaShaderPermutationDomain<
                FUseFeature,
                FQualityLevel>;

        static bool ShouldCompilePermutation(
            const arda::backend::FArdaShaderPermutationParameters& Parameters)
        {
            return (Parameters.mPermutationId % 2u) == 0;
        }

        static void ModifyCompilationEnvironment(
            const arda::backend::FArdaShaderPermutationParameters& Parameters,
            arda::backend::FArdaShaderCompileEnvironment& Environment)
        {
            Environment.SetDefine(
                "BACKEND_IS_VULKAN",
                Parameters.mBackend ==
                    arda::backend::EArdaBackendType::Vulkan);
            Environment.SetDefine("CUSTOM_VALUE", uint64_t{ 17 });
        }
    };

    class FMacroStyleShader
    {
        ARDA_DECLARE_GLOBAL_SHADER(FMacroStyleShader);

        using FPermutationDomain =
            arda::backend::TArdaShaderPermutationDomain<FUseFeature>;

        static bool ShouldCompilePermutation(
            const arda::backend::FArdaShaderPermutationParameters&)
        {
            return true;
        }

        static void ModifyCompilationEnvironment(
            const arda::backend::FArdaShaderPermutationParameters&,
            arda::backend::FArdaShaderCompileEnvironment& Environment)
        {
            Environment.SetDefine("MACRO_STYLE_HOOK", true);
        }
    };

    static_assert(
        arda::backend::detail::TShaderPermutationDomain<
            FMacroStyleShader>::PermutationCount == 2);
    static_assert(
        arda::backend::detail::THasShouldCompilePermutation<
            FMacroStyleShader>::value);
    static_assert(
        arda::backend::detail::THasModifyCompilationEnvironment<
            FMacroStyleShader>::value);

    struct FWrongHookSignatureShader
    {
        using FPermutationDomain =
            arda::backend::TArdaShaderPermutationDomain<FUseFeature>;
        static void ShouldCompilePermutation(
            const arda::backend::FArdaShaderPermutationParameters&)
        {
        }
    };

    static_assert(
        arda::backend::detail::THasNamedShouldCompilePermutation<
            FWrongHookSignatureShader>::value);
    static_assert(
        !arda::backend::detail::THasShouldCompilePermutation<
            FWrongHookSignatureShader>::value);

    ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FNestedValues)
        ARDA_SHADER_PARAMETER_VALUE(uint32_t, mMode)
    ARDA_END_SHADER_PARAMETER_STRUCT()

    ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FMetadataParameters)
        ARDA_SHADER_PARAMETER_STRUCT(FNestedValues, mValues)
        ARDA_SHADER_TEXTURE_SRV_ARRAY(mTextures, 2, 0, 2, Stage::Compute)
        ARDA_SHADER_BUFFER_UAV(mOutput, 0, 0, Stage::Compute)
        ARDA_SHADER_SAMPLER(mSampler, 0, 0, Stage::Compute)
    ARDA_END_SHADER_PARAMETER_STRUCT()

    ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FDirectParameters)
        ARDA_SHADER_BUFFER_UAV_ARRAY(mOutputs, 2, 3, 2, Stage::Compute)
        ARDA_SHADER_BUFFER_UAV(mOtherOutput, 0, 0, Stage::Compute)
    ARDA_END_SHADER_PARAMETER_STRUCT()

    ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FNestedResources)
        ARDA_SHADER_TEXTURE_SRV(mNestedTexture, 4, 2, Stage::Pixel)
    ARDA_END_SHADER_PARAMETER_STRUCT()

    ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FNestedResourceRoot)
        ARDA_SHADER_PARAMETER_VALUE(uint32_t, mPrefix)
        ARDA_SHADER_PARAMETER_STRUCT(FNestedResources, mResources)
    ARDA_END_SHADER_PARAMETER_STRUCT()

    struct FPushPayload
    {
        uint32_t mFirst = 0;
        uint32_t mSecond = 0;
    };

    ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FPushParameters)
        ARDA_SHADER_PARAMETER_VALUE(uint32_t, mPrefix)
        ARDA_SHADER_PUSH_CONSTANTS(
            FPushPayload, mPush, 0, 0, Stage::Compute)
    ARDA_END_SHADER_PARAMETER_STRUCT()

    const arda::backend::FArdaShaderParameterMetadata* DirectMetadata()
    {
        return &FDirectParameters::GetStaticMetadata();
    }

    struct FShaderCompilerConfigurationGuard
    {
        ~FShaderCompilerConfigurationGuard()
        {
            arda::backend::ResetShaderCompilerConfiguration();
        }
    };

    std::string ReadTestFile(const std::filesystem::path& Path)
    {
        std::ifstream Stream(Path, std::ios::binary);
        return std::string(
            std::istreambuf_iterator<char>(Stream),
            std::istreambuf_iterator<char>());
    }
}

TEST(ArdaShaderStructs, EncodesCartesianPermutationDomainsAndSortedDefines)
{
    using namespace arda::backend;
    using Domain = FPermutationShaderPolicy::FPermutationDomain;
    static_assert(Domain::PermutationCount == 6);

    for (uint32_t Id = 0; Id < Domain::PermutationCount; ++Id)
    {
        const Domain Value(Id);
        ASSERT_TRUE(Value.IsValid());
        EXPECT_EQ(Value.ToId(), Id);
        EXPECT_EQ(Value.Get<FUseFeature>(), (Id % 2u) != 0);
        EXPECT_EQ(Value.Get<FQualityLevel>(), Id / 2u);

        FArdaShaderCompileEnvironment Environment;
        ASSERT_TRUE(Value.ModifyCompilationEnvironment(Environment));
        const auto& Defines = Environment.GetDefines();
        ASSERT_EQ(Defines.size(), 2u);
        EXPECT_EQ(Defines[0].mName, "QUALITY_LEVEL");
        EXPECT_EQ(Defines[0].mValue, std::to_string(Id / 2u).c_str());
        EXPECT_EQ(Defines[1].mName, "USE_FEATURE");
        EXPECT_EQ(Defines[1].mValue, (Id % 2u) != 0 ? "1" : "0");
    }

    Domain Mutable;
    EXPECT_TRUE(Mutable.Set<FQualityLevel>(2));
    EXPECT_TRUE(Mutable.Set<FUseFeature>(true));
    EXPECT_EQ(Mutable.ToId(), 5u);
    EXPECT_FALSE(Mutable.Set<FQualityLevel>(3));
    EXPECT_FALSE(Domain(6).IsValid());

    FArdaShaderCompileEnvironment Environment;
    EXPECT_TRUE(Environment.SetDefine("Z_LAST", uint64_t{ 9 }));
    EXPECT_TRUE(Environment.SetDefine("A_FIRST", true));
    EXPECT_TRUE(Environment.SetDefine("Z_LAST", uint64_t{ 11 }));
    EXPECT_FALSE(Environment.SetDefine("", true));
    ASSERT_EQ(Environment.GetDefines().size(), 2u);
    EXPECT_EQ(Environment.GetDefines()[0].mName, "A_FIRST");
    EXPECT_EQ(Environment.GetDefines()[1].mName, "Z_LAST");
    EXPECT_EQ(Environment.GetDefines()[1].mValue, "11");
}

TEST(ArdaShaderStructs, DetectsHooksDeclaredAfterGlobalShaderMacro)
{
    using namespace arda::backend;
    FArdaShaderCompileEnvironment Environment;
    const FArdaShaderPermutationParameters Parameters{
        nullptr, EArdaBackendType::Vulkan, 1
    };
    EXPECT_TRUE(
        detail::ShouldCompileShaderPermutation<FMacroStyleShader>(Parameters));
    detail::BuildShaderCompilationEnvironment<FMacroStyleShader>(
        Parameters, Environment);
    ASSERT_EQ(Environment.GetDefines().size(), 2u);
    EXPECT_EQ(Environment.GetDefines()[0].mName, "MACRO_STYLE_HOOK");
    EXPECT_EQ(Environment.GetDefines()[1].mName, "USE_FEATURE");
}

TEST(ArdaShaderStructs, RegistersOptionalPermutationPoliciesAndArtifactStems)
{
    using namespace arda::backend;
    FArdaShaderTypeRegistration::ResetForTests();
    FArdaShaderTypeRegistration Registration(
        "PermutationPolicy",
        "PermutationSource",
        "PermutationArtifact",
        "Main",
        Stage::Compute,
        nullptr,
        detail::TShaderPermutationDomain<
            FPermutationShaderPolicy>::PermutationCount,
        &detail::ShouldCompileShaderPermutation<FPermutationShaderPolicy>,
        &detail::BuildShaderCompilationEnvironment<FPermutationShaderPolicy>);

    ASSERT_TRUE(FArdaShaderTypeRegistration::CommitAll());
    const FArdaShaderType& Type = Registration.GetType();
    EXPECT_EQ(Type.GetPermutationCount(), 6u);
    EXPECT_TRUE(Type.ShouldCompilePermutation(EArdaBackendType::D3D12, 0));
    EXPECT_FALSE(Type.ShouldCompilePermutation(EArdaBackendType::D3D12, 1));
    EXPECT_FALSE(Type.ShouldCompilePermutation(EArdaBackendType::D3D12, 6));
    EXPECT_EQ(Type.GetPermutationArtifactStem(0), "PermutationArtifact_P0");
    EXPECT_EQ(Type.GetPermutationArtifactStem(5), "PermutationArtifact_P5");
    EXPECT_TRUE(Type.GetPermutationArtifactStem(6).empty());

    const auto Environment =
        Type.BuildCompilationEnvironment(EArdaBackendType::Vulkan, 4);
    const auto& Defines = Environment.GetDefines();
    ASSERT_EQ(Defines.size(), 4u);
    EXPECT_EQ(Defines[0].mName, "BACKEND_IS_VULKAN");
    EXPECT_EQ(Defines[0].mValue, "1");
    EXPECT_EQ(Defines[1].mName, "CUSTOM_VALUE");
    EXPECT_EQ(Defines[2].mName, "QUALITY_LEVEL");
    EXPECT_EQ(Defines[2].mValue, "2");
    EXPECT_EQ(Defines[3].mName, "USE_FEATURE");
    EXPECT_EQ(Defines[3].mValue, "0");
}

TEST(ArdaShaderStructs, ValidatesPermutationRegistrationAndStemCollisions)
{
    using namespace arda::backend;
    FArdaShaderTypeRegistration::ResetForTests();
    {
        FArdaShaderTypeRegistration Invalid(
            "InvalidPermutation", "Source", "Artifact", "Main",
            Stage::Compute, nullptr, 0);
        EXPECT_EQ(
            FArdaShaderTypeRegistration::CommitAll().mCode,
            EArdaShaderRegistrationError::InvalidPermutation);
    }
    {
        FArdaShaderTypeRegistration Invalid(
            "MissingCallbacks", "Source", "Artifact", "Main",
            Stage::Compute, nullptr, 2);
        EXPECT_EQ(
            FArdaShaderTypeRegistration::CommitAll().mCode,
            EArdaShaderRegistrationError::InvalidPermutation);
    }
    {
        FArdaShaderTypeRegistration Variant(
            "Variant", "SourceA", "Shared", "Main", Stage::Compute, nullptr, 6,
            &detail::ShouldCompileShaderPermutation<FPermutationShaderPolicy>,
            &detail::BuildShaderCompilationEnvironment<FPermutationShaderPolicy>);
        FArdaShaderTypeRegistration CollidingBase(
            "CollidingBase", "SourceB", "Shared_P2", "Main",
            Stage::Compute, nullptr);
        EXPECT_EQ(
            FArdaShaderTypeRegistration::CommitAll().mCode,
            EArdaShaderRegistrationError::ArtifactStemCollision);
    }
}

TEST(ArdaShaderStructs, RejectsUnsafeAndCaseCollidingArtifactStems)
{
    using namespace arda::backend;
    for (const char* Stem :
         { "", ".hidden", "../escape", "folder/name", "folder\\name",
           "drive:name", "two..dots", "-leading-dash" })
    {
        FArdaShaderTypeRegistration::ResetForTests();
        FArdaShaderTypeRegistration Invalid(
            "UnsafeStem", "Source", Stem, "Main",
            Stage::Compute, nullptr);
        EXPECT_EQ(
            FArdaShaderTypeRegistration::CommitAll().mCode,
            EArdaShaderRegistrationError::InvalidType)
            << Stem;
    }

    FArdaShaderTypeRegistration::ResetForTests();
    FArdaShaderTypeRegistration Upper(
        "Upper", "SourceA", "PortableArtifact", "Main",
        Stage::Compute, nullptr);
    FArdaShaderTypeRegistration Lower(
        "Lower", "SourceB", "portableartifact", "Main",
        Stage::Compute, nullptr);
    EXPECT_EQ(
        FArdaShaderTypeRegistration::CommitAll().mCode,
        EArdaShaderRegistrationError::ArtifactStemCollision);
}

TEST(ArdaShaderStructs, CommitsInDeterministicOrderAndSupportsLookup)
{
    using namespace arda::backend;
    FArdaShaderTypeRegistration::ResetForTests();
    FArdaShaderTypeRegistration Second(
        "ZSecond", "Source", "Second", "Main", Stage::Compute, nullptr);
    FArdaShaderTypeRegistration First(
        "AFirst", "Source", "First", "Main", Stage::Compute, nullptr);

    ASSERT_TRUE(FArdaShaderTypeRegistration::CommitAll());
    const auto Types = FArdaShaderTypeRegistration::Enumerate();
    ASSERT_EQ(Types.size(), 2u);
    EXPECT_STREQ(Types[0]->GetName(), "AFirst");
    EXPECT_STREQ(Types[1]->GetName(), "ZSecond");
    EXPECT_EQ(FArdaShaderTypeRegistration::Find("AFirst"), Types[0]);
}

TEST(ArdaShaderStructs, RejectsDuplicateNamesAndArtifactIdentities)
{
    using namespace arda::backend;
    FArdaShaderTypeRegistration::ResetForTests();
    {
        FArdaShaderTypeRegistration One(
            "Duplicate", "Source", "One", "Main", Stage::Compute, nullptr);
        FArdaShaderTypeRegistration Two(
            "Duplicate", "Source", "Two", "Main", Stage::Compute, nullptr);
        const auto Status = FArdaShaderTypeRegistration::CommitAll();
        EXPECT_EQ(Status.mCode, EArdaShaderRegistrationError::DuplicateTypeName);
    }
    {
        FArdaShaderTypeRegistration One(
            "First", "Source", "Same", "Main", Stage::Compute, nullptr);
        FArdaShaderTypeRegistration Two(
            "Second", "Source", "Same", "Main", Stage::Compute, nullptr);
        const auto Status = FArdaShaderTypeRegistration::CommitAll();
        EXPECT_EQ(Status.mCode, EArdaShaderRegistrationError::DuplicateIdentity);
    }
}

TEST(ArdaShaderStructs, EnumeratesMetadataAndGeneratesStableLayout)
{
    using namespace arda;
    const auto& Metadata = FMetadataParameters::GetStaticMetadata();
    ASSERT_TRUE(Metadata.GetStatus());
    EXPECT_NE(Metadata.GetLayoutHash(), 0u);
    EXPECT_EQ(
        Metadata.GetLayoutHash(),
        FMetadataParameters::GetStaticMetadata().GetLayoutHash());
    ASSERT_NE(Metadata.FindMember("mTextures"), nullptr);
    EXPECT_EQ(Metadata.FindMember("mTextures")->mArrayCount, 2u);

    eastl::vector<rhi::FArdaRHIBindingLayoutDesc> Layouts;
    ASSERT_TRUE(Metadata.BuildBindingLayoutDescs(Layouts));
    ASSERT_EQ(Layouts.size(), 1u);
    EXPECT_EQ(Layouts[0].mVisibility, Stage::Compute);
    EXPECT_EQ(Layouts[0].mRegisterSpace, 0u);
    ASSERT_EQ(Layouts[0].mItems.size(), 3u);
    EXPECT_EQ(Layouts[0].mItems[0].mSlot, 2u);
    EXPECT_EQ(Layouts[0].mItems[0].mArraySize, 2u);
    EXPECT_EQ(Layouts[0].mItems[1].mType, rhi::EArdaRHIBindingType::StructuredBufferUAV);

    const auto& Nested = FNestedResourceRoot::GetStaticMetadata();
    size_t NestedOffset = 0;
    const auto* NestedTexture = Nested.FindFlattenedMember(
        "mResources.mNestedTexture",
        &NestedOffset);
    ASSERT_NE(NestedTexture, nullptr);
    EXPECT_EQ(NestedTexture->mSlot, 4u);
    EXPECT_EQ(
        NestedOffset,
        offsetof(FNestedResourceRoot, mResources) +
            offsetof(FNestedResources, mNestedTexture));
    eastl::vector<backend::FArdaFlattenedShaderParameterMember> Flattened;
    Nested.GetFlattenedMembers(Flattened);
    ASSERT_EQ(Flattened.size(), 2u);
    EXPECT_EQ(Flattened[1].mPath, "mResources.mNestedTexture");
}

TEST(ArdaShaderStructs, ValidatesRegistersVisibilityAndPushConstants)
{
    using namespace arda;
    using namespace backend;
    eastl::vector<FArdaShaderParameterMember> Duplicate = {
        { "A", EArdaShaderParameterKind::TextureSRV, rhi::EArdaRHIBindingType::TextureSRV,
          0, 0, 1, 0, sizeof(rhi::FArdaRHITextureRef), sizeof(rhi::FArdaRHITextureRef),
          Stage::Compute, nullptr },
        { "B", EArdaShaderParameterKind::BufferSRV, rhi::EArdaRHIBindingType::StructuredBufferSRV,
          0, 0, 1, sizeof(rhi::FArdaRHITextureRef), sizeof(rhi::FArdaRHIBufferRef),
          sizeof(rhi::FArdaRHIBufferRef), Stage::Compute, nullptr }
    };
    FArdaShaderParameterMetadata DuplicateMetadata(
        "Duplicate", 64, alignof(void*), eastl::move(Duplicate));
    EXPECT_EQ(
        DuplicateMetadata.GetStatus().mCode,
        EArdaShaderStructError::DuplicateRegister);

    eastl::vector<FArdaShaderParameterMember> Disjoint = {
        { "VertexTexture", EArdaShaderParameterKind::TextureSRV,
          rhi::EArdaRHIBindingType::TextureSRV, 0, 0, 1, 0,
          sizeof(rhi::FArdaRHITextureRef), sizeof(rhi::FArdaRHITextureRef),
          Stage::Vertex, nullptr },
        { "PixelTexture", EArdaShaderParameterKind::TextureSRV,
          rhi::EArdaRHIBindingType::TextureSRV, 0, 0, 1,
          sizeof(rhi::FArdaRHITextureRef), sizeof(rhi::FArdaRHITextureRef),
          sizeof(rhi::FArdaRHITextureRef), Stage::Pixel, nullptr },
        { "Constants", EArdaShaderParameterKind::ConstantBuffer,
          rhi::EArdaRHIBindingType::ConstantBuffer, 0, 0, 1, 32,
          sizeof(rhi::FArdaRHIBufferRef), sizeof(rhi::FArdaRHIBufferRef),
          Stage::Compute, nullptr },
        { "Push", EArdaShaderParameterKind::PushConstants,
          rhi::EArdaRHIBindingType::PushConstants, 0, 0, 1, 48, 8, 8,
          Stage::Compute, nullptr }
    };
    FArdaShaderParameterMetadata DisjointMetadata(
        "Disjoint", 64, alignof(void*), eastl::move(Disjoint));
    EXPECT_TRUE(DisjointMetadata.GetStatus());
    eastl::vector<rhi::FArdaRHIBindingLayoutDesc> DisjointLayouts;
    EXPECT_TRUE(DisjointMetadata.BuildBindingLayoutDescs(DisjointLayouts));

    eastl::vector<FArdaShaderParameterMember> Overlapping = {
        { "VertexTexture", EArdaShaderParameterKind::TextureSRV,
          rhi::EArdaRHIBindingType::TextureSRV, 0, 0, 1, 0,
          sizeof(rhi::FArdaRHITextureRef), sizeof(rhi::FArdaRHITextureRef),
          Stage::Vertex, nullptr },
        { "SharedTexture", EArdaShaderParameterKind::TextureSRV,
          rhi::EArdaRHIBindingType::TextureSRV, 0, 0, 1,
          sizeof(rhi::FArdaRHITextureRef), sizeof(rhi::FArdaRHITextureRef),
          sizeof(rhi::FArdaRHITextureRef), Stage::Vertex | Stage::Pixel, nullptr }
    };
    FArdaShaderParameterMetadata OverlapMetadata(
        "Overlap", 32, alignof(void*), eastl::move(Overlapping));
    EXPECT_EQ(
        OverlapMetadata.GetStatus().mCode,
        EArdaShaderStructError::DuplicateRegister);

    eastl::vector<FArdaShaderParameterMember> Invisible = {
        { "Texture", EArdaShaderParameterKind::TextureSRV, rhi::EArdaRHIBindingType::TextureSRV,
          0, 0, 1, 0, sizeof(rhi::FArdaRHITextureRef), sizeof(rhi::FArdaRHITextureRef),
          Stage::None, nullptr }
    };
    FArdaShaderParameterMetadata InvisibleMetadata(
        "Invisible", 16, alignof(void*), eastl::move(Invisible));
    EXPECT_EQ(
        InvisibleMetadata.GetStatus().mCode,
        EArdaShaderStructError::IncompatibleVisibility);

    eastl::vector<FArdaShaderParameterMember> Array = {
        { "Array", EArdaShaderParameterKind::TextureSRV, rhi::EArdaRHIBindingType::TextureSRV,
          0, 0, 0, 0, sizeof(rhi::FArdaRHITextureRef), sizeof(rhi::FArdaRHITextureRef),
          Stage::Compute, nullptr }
    };
    FArdaShaderParameterMetadata ArrayMetadata(
        "Array", 16, alignof(void*), eastl::move(Array));
    EXPECT_EQ(
        ArrayMetadata.GetStatus().mCode,
        EArdaShaderStructError::MalformedArray);

    eastl::vector<FArdaShaderParameterMember> Push = {
        { "Push", EArdaShaderParameterKind::PushConstants, rhi::EArdaRHIBindingType::PushConstants,
          0, 0, 1, 0, 6, 6, Stage::Compute, nullptr }
    };
    FArdaShaderParameterMetadata PushMetadata(
        "Push", 8, alignof(uint32_t), eastl::move(Push));
    EXPECT_EQ(
        PushMetadata.GetStatus().mCode,
        EArdaShaderStructError::MalformedPushConstants);

    eastl::vector<FArdaShaderParameterMember> MultiplePush = {
        { "First", EArdaShaderParameterKind::PushConstants,
          rhi::EArdaRHIBindingType::PushConstants, 0, 0, 1, 0, 8, 8,
          Stage::Compute, nullptr },
        { "Second", EArdaShaderParameterKind::PushConstants,
          rhi::EArdaRHIBindingType::PushConstants, 1, 0, 1, 8, 8, 8,
          Stage::Compute, nullptr }
    };
    FArdaShaderParameterMetadata MultiplePushMetadata(
        "MultiplePush", 16, alignof(uint32_t), eastl::move(MultiplePush));
    ASSERT_TRUE(MultiplePushMetadata.GetStatus());
    eastl::vector<rhi::FArdaRHIBindingLayoutDesc> PushLayouts;
    EXPECT_EQ(
        MultiplePushMetadata.BuildBindingLayoutDescs(PushLayouts).mCode,
        EArdaShaderStructError::MalformedPushConstants);
}

TEST(ArdaShaderStructs, SelectsExtensionsAndReportsMissingBytecode)
{
    using namespace arda::backend;
    EXPECT_STREQ(
        GetShaderArtifactExtension(EArdaBackendType::D3D12),
        FindBackendModule("native-d3d12") ? ".dxil" : "");
    EXPECT_STREQ(
        GetShaderArtifactExtension(EArdaBackendType::Vulkan),
        FindBackendModule("native-vulkan") ? ".spv" : "");
    const auto Missing = LoadShaderBytecode(
        std::filesystem::path(ARDA_BACKEND_TEST_SHADER_DIR) / "does-not-exist.spv");
    EXPECT_FALSE(Missing);
    EXPECT_EQ(Missing.mDiagnostic.mCode, EArdaGlobalShaderMapError::BytecodeMissing);
    EXPECT_FALSE(Missing.mDiagnostic.mPath.empty());
}

TEST(ArdaShaderStructs, BuildsAndCooksRegistrationDrivenShaderJobs)
{
    using namespace arda::backend;
    FShaderCompilerConfigurationGuard ConfigurationGuard;
    ResetShaderCompilerConfiguration();
    FArdaShaderCompilerConfiguration FakeCompilerConfiguration =
        GetShaderCompilerConfiguration();
    FakeCompilerConfiguration.mCompilerExecutable = GetTestExecutablePath();
    ASSERT_FALSE(FakeCompilerConfiguration.mCompilerExecutable.empty());
    ConfigureShaderCompiler(FakeCompilerConfiguration);
    FArdaShaderTypeRegistration::ResetForTests();

    const std::filesystem::path Directory =
        std::filesystem::temp_directory_path() /
        "ArdaRegistrationDrivenShaderCompilerTest";
    const std::filesystem::path Source = Directory / "CompilerTest.hlsl";
    const std::filesystem::path Include = Directory / "CompilerShared.hlsli";
    const std::filesystem::path AngleInclude = Directory / "AngleShared.hlsli";
    const std::filesystem::path MacroInclude = Directory / "MacroShared.hlsli";
    const std::filesystem::path Output = Directory / "Cooked";
    std::error_code Error;
    std::filesystem::remove_all(Directory, Error);
    ASSERT_TRUE(std::filesystem::create_directories(Directory, Error));
    {
        std::ofstream IncludeStream(Include);
        IncludeStream << "#define INCLUDED_VALUE 3\n";
        std::ofstream AngleStream(AngleInclude);
        AngleStream << "#define ANGLE_VALUE 5\n";
        std::ofstream MacroStream(MacroInclude);
        MacroStream << "#define MACRO_VALUE 7\n";
        std::ofstream Stream(Source);
        Stream << "#include \"CompilerShared.hlsli\"\n"
                  "#include <AngleShared.hlsli>\n"
                  "#define SELECTED_INCLUDE \"MacroShared.hlsli\"\n"
                  "#include SELECTED_INCLUDE\n"
                  "RWByteAddressBuffer Out : register(u0);\n"
                  "[numthreads(1,1,1)] void Main(uint3 id : SV_DispatchThreadID)"
                  "{ Out.Store(0, CUSTOM_VALUE + QUALITY_LEVEL + USE_FEATURE); }\n";
    }
    const std::string SourceName = Source.string();
    FArdaShaderTypeRegistration Registration(
        "CompilerPolicy",
        SourceName.c_str(),
        "CompilerPolicyArtifact",
        "Main",
        Stage::Compute,
        nullptr,
        detail::TShaderPermutationDomain<
            FPermutationShaderPolicy>::PermutationCount,
        &detail::ShouldCompileShaderPermutation<FPermutationShaderPolicy>,
        &detail::BuildShaderCompilationEnvironment<FPermutationShaderPolicy>);

    const bool bHasD3D12 = FindBackendModule("native-d3d12") != nullptr;
    const bool bHasVulkan = FindBackendModule("native-vulkan") != nullptr;
    ASSERT_TRUE(bHasD3D12 || bHasVulkan);
    std::vector<EArdaBackendType> Backends;
    if (bHasD3D12)
        Backends.push_back(EArdaBackendType::D3D12);
    if (bHasVulkan)
        Backends.push_back(EArdaBackendType::Vulkan);
    const EArdaBackendType PrimaryBackend = Backends.front();
    const char* PrimaryExtension = PrimaryBackend == EArdaBackendType::D3D12
        ? ".dxil"
        : ".spv";
    const std::string PrimaryArtifactFilename =
        std::string("CompilerPolicyArtifact_P0") + PrimaryExtension;
    const size_t ExpectedJobCount = Backends.size() * 3u;
    const FArdaShaderCompileResult FirstJobs =
        BuildRegisteredShaderCompileJobs(Output, Backends);
    ASSERT_TRUE(FirstJobs);
    ASSERT_EQ(FirstJobs.mJobs.size(), ExpectedJobCount);
    EXPECT_EQ(FirstJobs.mJobsSkipped, ExpectedJobCount);
    EXPECT_FALSE(FirstJobs.mJobs[0].mTarget.mBackendName.empty());
    EXPECT_EQ(FirstJobs.mJobs[0].mProfile, "cs_6_0");
    EXPECT_EQ(
        FirstJobs.mJobs[0].mOutputPath.filename(),
        std::filesystem::path(PrimaryArtifactFilename));
    ASSERT_EQ(FirstJobs.mJobs[0].mEnvironment.GetDefines().size(), 4u);
    EXPECT_EQ(
        FirstJobs.mJobs[0].mEnvironment.GetDefines()[0].mName,
        "BACKEND_IS_VULKAN");
    if (bHasVulkan)
    {
        const auto VulkanJob = std::find_if(
            FirstJobs.mJobs.begin(),
            FirstJobs.mJobs.end(),
            [](const FArdaShaderCompileJob& Job)
            {
                return Job.mBackend == EArdaBackendType::Vulkan;
            });
        ASSERT_NE(VulkanJob, FirstJobs.mJobs.end());
        EXPECT_EQ(VulkanJob->mTarget.mBackendName, "native-vulkan");
        EXPECT_EQ(
            VulkanJob->mTarget.mBinaryFormat,
            EArdaShaderBinaryFormat::Spirv);
        EXPECT_NE(
            std::find(
                VulkanJob->mArguments.begin(),
                VulkanJob->mArguments.end(),
                eastl::string("-spirv")),
            VulkanJob->mArguments.end());

        const FArdaShaderCompileResult NamedJobs =
            BuildRegisteredShaderCompileJobs(
                Output, eastl::vector<eastl::string>{ "native-vulkan" });
        ASSERT_TRUE(NamedJobs);
        ASSERT_EQ(NamedJobs.mJobs.size(), 3u);
        EXPECT_TRUE(std::all_of(
            NamedJobs.mJobs.begin(), NamedJobs.mJobs.end(),
            [](const FArdaShaderCompileJob& Job)
            {
                return Job.mTarget.mBackendName == "native-vulkan" &&
                    Job.mOutputPath.extension() == ".spv";
            }));
        EXPECT_NE(
            std::find(
                VulkanJob->mArguments.begin(),
                VulkanJob->mArguments.end(),
                eastl::string("-fspv-target-env=vulkan1.3")),
            VulkanJob->mArguments.end());
        bool bFoundSpaceThreeUavShift = false;
        for (size_t Index = 0; Index + 2 < VulkanJob->mArguments.size(); ++Index)
        {
            if (VulkanJob->mArguments[Index] == "-fvk-u-shift" &&
                VulkanJob->mArguments[Index + 1] == "384" &&
                VulkanJob->mArguments[Index + 2] == "3")
            {
                bFoundSpaceThreeUavShift = true;
                break;
            }
        }
        EXPECT_TRUE(bFoundSpaceThreeUavShift);
    }

    const FArdaShaderCompileResult SecondJobs =
        BuildRegisteredShaderCompileJobs(Output, Backends);
    ASSERT_TRUE(SecondJobs);
    ASSERT_EQ(SecondJobs.mJobs.size(), FirstJobs.mJobs.size());
    for (size_t Index = 0; Index < FirstJobs.mJobs.size(); ++Index)
        EXPECT_EQ(FirstJobs.mJobs[Index].mInputKey, SecondJobs.mJobs[Index].mInputKey);
    {
        std::ofstream Stream(AngleInclude, std::ios::app);
        Stream << "// angle include invalidation\n";
    }
    const auto AngleChangedJobs =
        BuildRegisteredShaderCompileJobs(Output, Backends);
    ASSERT_TRUE(AngleChangedJobs);
    EXPECT_NE(
        AngleChangedJobs.mJobs[0].mInputKey,
        FirstJobs.mJobs[0].mInputKey);
    {
        std::ofstream Stream(MacroInclude, std::ios::app);
        Stream << "// macro include invalidation\n";
    }
    const auto MacroChangedJobs =
        BuildRegisteredShaderCompileJobs(Output, Backends);
    ASSERT_TRUE(MacroChangedJobs);
    EXPECT_NE(
        MacroChangedJobs.mJobs[0].mInputKey,
        AngleChangedJobs.mJobs[0].mInputKey);
    const std::filesystem::path ExternalIncludes = Directory.parent_path() /
        "ArdaRegistrationDrivenExternalIncludes";
    std::filesystem::remove_all(ExternalIncludes, Error);
    ASSERT_TRUE(std::filesystem::create_directories(ExternalIncludes, Error));
    const auto ExternalInclude = ExternalIncludes / "External.hlsli";
    {
        std::ofstream Stream(ExternalInclude);
        Stream << "#define EXTERNAL_VALUE 11\n";
    }
    auto IncludeConfiguration = FakeCompilerConfiguration;
    IncludeConfiguration.mCommonArguments.push_back("-I");
    IncludeConfiguration.mCommonArguments.push_back(
        ExternalIncludes.string().c_str());
    ConfigureShaderCompiler(IncludeConfiguration);
    const auto ExternalFirstJobs =
        BuildRegisteredShaderCompileJobs(Output, Backends);
    ASSERT_TRUE(ExternalFirstJobs);
    {
        std::ofstream Stream(ExternalInclude, std::ios::app);
        Stream << "// external include invalidation\n";
    }
    const auto ExternalChangedJobs =
        BuildRegisteredShaderCompileJobs(Output, Backends);
    ASSERT_TRUE(ExternalChangedJobs);
    EXPECT_NE(
        ExternalChangedJobs.mJobs[0].mInputKey,
        ExternalFirstJobs.mJobs[0].mInputKey);
    ConfigureShaderCompiler(FakeCompilerConfiguration);
    std::filesystem::remove_all(ExternalIncludes, Error);
    const FArdaShaderCompileResult OtherDirectoryJobs =
        BuildRegisteredShaderCompileJobs(Directory / "OtherCooked", Backends);
    ASSERT_TRUE(OtherDirectoryJobs);
    ASSERT_EQ(OtherDirectoryJobs.mJobs.size(), MacroChangedJobs.mJobs.size());
    for (size_t Index = 0; Index < MacroChangedJobs.mJobs.size(); ++Index)
        EXPECT_EQ(
            MacroChangedJobs.mJobs[Index].mInputKey,
            OtherDirectoryJobs.mJobs[Index].mInputKey);
    {
        std::ofstream Stream(Include, std::ios::app);
        Stream << "// transitive dependency invalidation\n";
    }
    const FArdaShaderCompileResult IncludeChangedJobs =
        BuildRegisteredShaderCompileJobs(Output, Backends);
    ASSERT_TRUE(IncludeChangedJobs);
    ASSERT_EQ(IncludeChangedJobs.mJobs.size(), FirstJobs.mJobs.size());
    EXPECT_NE(
        IncludeChangedJobs.mJobs[0].mInputKey,
        FirstJobs.mJobs[0].mInputKey);

    FArdaShaderCompileResult Cook =
        CompileRegisteredShaderArtifacts(Output, Backends);
    ASSERT_TRUE(Cook) << Cook.mDiagnostics.front().mMessage.c_str();
    EXPECT_EQ(Cook.mJobsCompiled, ExpectedJobCount);
    const std::filesystem::path Artifact =
        Output / PrimaryArtifactFilename;
    const std::filesystem::path Manifest =
        Output / "ArdaShaderManifest.json";
    ASSERT_TRUE(std::filesystem::is_regular_file(Artifact));
    if (bHasD3D12)
        ASSERT_TRUE(std::filesystem::is_regular_file(
            Output / "CompilerPolicyArtifact_P0.dxil"));
    if (bHasVulkan)
        ASSERT_TRUE(std::filesystem::is_regular_file(
            Output / "CompilerPolicyArtifact_P0.spv"));
    EXPECT_NE(ReadTestFile(Artifact).find("\"-T\" \"cs_6_0\""), std::string::npos);
    ASSERT_TRUE(std::filesystem::is_regular_file(
        Artifact.string() + ".arda-key"));
    const std::string FirstManifest = ReadTestFile(Manifest);
    ASSERT_FALSE(FirstManifest.empty());

    FArdaShaderCompileResult Cached = EnsureRegisteredShaderArtifact(
        Registration.GetType(), PrimaryBackend, 0, Output);
    ASSERT_TRUE(Cached);
    EXPECT_EQ(Cached.mCacheHits, 1u);
    EXPECT_EQ(Cached.mJobsCompiled, 0u);
    const FArdaShaderCompileResult BulkCached =
        EnsureRegisteredShaderArtifacts(Output, PrimaryBackend);
    ASSERT_TRUE(BulkCached);
    EXPECT_EQ(BulkCached.mJobsCompiled, 0u);
    EXPECT_EQ(BulkCached.mCacheHits, 3u);
    EXPECT_EQ(BulkCached.mJobsSkipped, 3u);

    ASSERT_TRUE(std::filesystem::remove(
        Artifact.string() + ".arda-key", Error));
    FArdaShaderCompileResult Legacy = EnsureRegisteredShaderArtifact(
        Registration.GetType(), PrimaryBackend, 0, Output);
    ASSERT_TRUE(Legacy);
    EXPECT_EQ(Legacy.mCacheHits, 1u);
    Cook = CompileRegisteredShaderArtifacts(Output, Backends);
    ASSERT_TRUE(Cook);
    {
        std::ofstream Empty(Artifact, std::ios::binary | std::ios::trunc);
    }
    FArdaShaderCompileResult EmptyRebuilt = EnsureRegisteredShaderArtifact(
        Registration.GetType(), PrimaryBackend, 0, Output);
    ASSERT_TRUE(EmptyRebuilt);
    EXPECT_EQ(EmptyRebuilt.mJobsCompiled, 1u);
    EXPECT_GT(std::filesystem::file_size(Artifact), 0u);

    {
        std::ofstream Stream(Source, std::ios::app);
        Stream << "\n// invalidate deterministic input key\n";
    }
    const std::string BeforeStalePolicy = ReadTestFile(Artifact);
    FArdaShaderCompilerConfiguration StaleConfiguration =
        GetShaderCompilerConfiguration();
    StaleConfiguration.mbCompileMissingArtifacts = true;
    StaleConfiguration.mbCompileOutdatedArtifacts = false;
    ConfigureShaderCompiler(StaleConfiguration);
    FArdaShaderCompileResult StaleRejected = EnsureRegisteredShaderArtifact(
        Registration.GetType(), PrimaryBackend, 0, Output);
    ASSERT_FALSE(StaleRejected);
    ASSERT_FALSE(StaleRejected.mDiagnostics.empty());
    EXPECT_EQ(
        StaleRejected.mDiagnostics.front().mCode,
        EArdaShaderCompileError::ArtifactOutdated);
    EXPECT_EQ(ReadTestFile(Artifact), BeforeStalePolicy);
    StaleConfiguration.mbCompileMissingArtifacts = false;
    ConfigureShaderCompiler(StaleConfiguration);
    FArdaShaderCompileResult BytecodeOnly = EnsureRegisteredShaderArtifact(
        Registration.GetType(), PrimaryBackend, 0, Output);
    ASSERT_TRUE(BytecodeOnly);
    EXPECT_EQ(BytecodeOnly.mCacheHits, 1u);
    ConfigureShaderCompiler(FakeCompilerConfiguration);
    FArdaShaderCompileResult Rebuilt = EnsureRegisteredShaderArtifact(
        Registration.GetType(), PrimaryBackend, 0, Output);
    ASSERT_TRUE(Rebuilt) << Rebuilt.mDiagnostics.front().mMessage.c_str();
    EXPECT_EQ(Rebuilt.mJobsCompiled, 1u);

    Cook = CompileRegisteredShaderArtifacts(Output, PrimaryBackend);
    ASSERT_TRUE(Cook);
    const std::string SecondManifest = ReadTestFile(Manifest);
    Cook = CompileRegisteredShaderArtifacts(Output, PrimaryBackend);
    ASSERT_TRUE(Cook);
    EXPECT_EQ(ReadTestFile(Manifest), SecondManifest);

    const std::string ArtifactBeforeFailure = ReadTestFile(Artifact);
    const std::string ManifestBeforeFailure = ReadTestFile(Manifest);
    {
        std::ofstream Stream(Source, std::ios::app);
        Stream << "\n// force another stale key\n";
    }
    FArdaShaderCompilerConfiguration Configuration =
        GetShaderCompilerConfiguration();
    Configuration.mCompilerExecutable = Source;
    ConfigureShaderCompiler(Configuration);
    const FArdaShaderCompileResult Failed = EnsureRegisteredShaderArtifact(
        Registration.GetType(), PrimaryBackend, 0, Output);
    EXPECT_FALSE(Failed);
    ASSERT_FALSE(Failed.mDiagnostics.empty());
    EXPECT_TRUE(
        Failed.mDiagnostics.front().mCode ==
            EArdaShaderCompileError::ProcessLaunchFailed ||
        Failed.mDiagnostics.front().mCode ==
            EArdaShaderCompileError::CompilationFailed);
    EXPECT_EQ(ReadTestFile(Artifact), ArtifactBeforeFailure);
    const FArdaShaderCompileResult FailedCook =
        CompileRegisteredShaderArtifacts(Output, Backends);
    EXPECT_FALSE(FailedCook);
    EXPECT_EQ(ReadTestFile(Artifact), ArtifactBeforeFailure);
    EXPECT_EQ(ReadTestFile(Manifest), ManifestBeforeFailure);
    ConfigureShaderCompiler(FakeCompilerConfiguration);
    {
        std::ofstream Stream(Source, std::ios::app);
        Stream << "\n#include \"MissingLocalInclude.hlsli\"\n";
    }
    const FArdaShaderCompileResult MissingInclude =
        BuildRegisteredShaderCompileJobs(Output, Backends);
    EXPECT_FALSE(MissingInclude);
    ASSERT_FALSE(MissingInclude.mDiagnostics.empty());
    EXPECT_EQ(
        MissingInclude.mDiagnostics.front().mCode,
        EArdaShaderCompileError::SourceResolutionFailed);
    EXPECT_NE(
        std::string(MissingInclude.mDiagnostics.front().mMessage.c_str())
            .find("MissingLocalInclude.hlsli"),
        std::string::npos);

    std::filesystem::remove_all(Directory, Error);
}

TEST(ArdaShaderStructs, ResolvesConcretePushConstantBytes)
{
    using namespace arda;
    FPushParameters Parameters;
    Parameters.mPrefix = 17;
    Parameters.mPush.mFirst = 23;
    Parameters.mPush.mSecond = 42;
    eastl::vector<rhi::FArdaRHIBindingLayoutDesc> Layouts;
    const auto& Metadata = FPushParameters::GetStaticMetadata();
    ASSERT_TRUE(Metadata.BuildBindingLayoutDescs(Layouts));
    ASSERT_EQ(Layouts.size(), 1u);
    ASSERT_EQ(Layouts[0].mItems.size(), 1u);
    EXPECT_EQ(
        Layouts[0].mItems[0].mType,
        rhi::EArdaRHIBindingType::PushConstants);

    const void* Data = nullptr;
    size_t Size = 0;
    ASSERT_TRUE(Metadata.GetPushConstantData(
        &Parameters,
        Layouts[0],
        Data,
        Size));
    EXPECT_EQ(Data, &Parameters.mPush);
    EXPECT_EQ(Size, sizeof(FPushPayload));
    EXPECT_EQ(static_cast<const FPushPayload*>(Data)->mSecond, 42u);
}

TEST(ArdaShaderStructs, RegistrationDestructionPreservesUnrelatedTypes)
{
    using namespace arda::backend;
    FArdaShaderTypeRegistration::ResetForTests();
    FArdaShaderTypeRegistration First(
        "LifetimeFirst", "Source", "First", "Main", Stage::Compute, nullptr);
    FArdaShaderTypeRegistration Second(
        "LifetimeSecond", "Source", "Second", "Main", Stage::Compute, nullptr);
    ASSERT_TRUE(FArdaShaderTypeRegistration::CommitAll());
    const FArdaShaderType* FirstType =
        FArdaShaderTypeRegistration::Find("LifetimeFirst");
    const FArdaShaderType* SecondType =
        FArdaShaderTypeRegistration::Find("LifetimeSecond");
    {
        FArdaShaderTypeRegistration Temporary(
            "LifetimeTemporary", "Source", "Temporary", "Main",
            Stage::Compute, nullptr);
        ASSERT_TRUE(FArdaShaderTypeRegistration::CommitAll());
        EXPECT_NE(
            FArdaShaderTypeRegistration::Find("LifetimeTemporary"),
            nullptr);
    }
    const auto Remaining = FArdaShaderTypeRegistration::Enumerate();
    ASSERT_EQ(Remaining.size(), 2u);
    EXPECT_EQ(FArdaShaderTypeRegistration::Find("LifetimeFirst"), FirstType);
    EXPECT_EQ(FArdaShaderTypeRegistration::Find("LifetimeSecond"), SecondType);
    EXPECT_EQ(FArdaShaderTypeRegistration::Find("LifetimeTemporary"), nullptr);
}

TEST(ArdaShaderStructs, CompileJobsOwnDescriptorsAfterRegistrationDestruction)
{
    using namespace arda::backend;
    FArdaShaderTarget Target;
    ASSERT_TRUE(ResolveLinkedTestShaderTarget(Target));
    FShaderCompilerConfigurationGuard ConfigurationGuard;
    FArdaShaderCompilerConfiguration Configuration =
        GetShaderCompilerConfiguration();
    Configuration.mCompilerExecutable = GetTestExecutablePath();
    ConfigureShaderCompiler(Configuration);
    FArdaShaderTypeRegistration::ResetForTests();
    const std::filesystem::path Directory =
        std::filesystem::temp_directory_path() / "ArdaShaderJobLifetimeTest";
    const std::filesystem::path Source = Directory / "Lifetime.hlsl";
    std::error_code Error;
    std::filesystem::remove_all(Directory, Error);
    ASSERT_TRUE(std::filesystem::create_directories(Directory, Error));
    {
        std::ofstream Stream(Source);
        Stream << "[numthreads(1,1,1)] void Main() {}\n";
    }
    FArdaShaderCompileJob Job;
    const FArdaShaderType* RawCommittedType = nullptr;
    {
        const std::string SourceName = Source.string();
        FArdaShaderTypeRegistration Registration(
            "OwnedJobType", SourceName.c_str(), "OwnedJobArtifact", "Main",
            Stage::Compute, nullptr);
        const FArdaShaderCompileResult Jobs = BuildRegisteredShaderCompileJobs(
            Directory / "Cooked",
            eastl::vector<eastl::string>{ Target.mBackendName });
        ASSERT_TRUE(Jobs);
        ASSERT_EQ(Jobs.mJobs.size(), 1u);
        Job = Jobs.mJobs.front();
        RawCommittedType = FArdaShaderTypeRegistration::Find("OwnedJobType");
        ASSERT_NE(RawCommittedType, nullptr);
    }
    EXPECT_STREQ(Job.mType.GetName(), "OwnedJobType");
    EXPECT_STREQ(Job.mType.GetOutputStem(), "OwnedJobArtifact");
    ASSERT_NE(RawCommittedType, nullptr);
    EXPECT_STREQ(RawCommittedType->GetName(), "OwnedJobType");
    EXPECT_EQ(FArdaShaderTypeRegistration::Find("OwnedJobType"), nullptr);
    std::filesystem::remove_all(Directory, Error);
}

TEST(ArdaShaderStructs, StartupModePersistsAndReusesRegisteredShaderCache)
{
    using namespace arda::backend;
    FArdaShaderTarget Target;
    ASSERT_TRUE(ResolveLinkedTestShaderTarget(Target));

    ShutdownBackend();
    FShaderCompilerConfigurationGuard CompilerGuard;
    FArdaShaderTypeRegistration::ResetForTests();
    const std::filesystem::path Directory =
        std::filesystem::temp_directory_path() / "ArdaStartupShaderCacheTest";
    const std::filesystem::path Source = Directory / "Startup.hlsl";
    const std::filesystem::path Cache = Directory / "Cache";
    std::error_code Error;
    std::filesystem::remove_all(Directory, Error);
    ASSERT_TRUE(std::filesystem::create_directories(Directory, Error));
    {
        std::ofstream Stream(Source);
        Stream << "[numthreads(1,1,1)] void Main() {}\n";
    }

    FArdaShaderCompilerConfiguration CompilerConfiguration;
    CompilerConfiguration.mCompilerExecutable = GetTestExecutablePath();
    ConfigureShaderCompiler(CompilerConfiguration);
    const std::string SourceName = Source.string();
    FArdaShaderTypeRegistration Registration(
        "StartupCacheShader", SourceName.c_str(), "StartupCacheArtifact",
        "Main", Stage::Compute, nullptr);

    FArdaBackendConfiguration Configuration;
    Configuration.mBackendName = Target.mBackendName;
    Configuration.mBackend = Target.mBackend;
    Configuration.mbEnableValidation = false;
    Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::Startup;
    Configuration.mShaderCacheDirectory = Cache;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    const bool FirstInitialized = InitializeBackend();
    const std::filesystem::path Artifact = Cache /
        (std::string("StartupCacheArtifact") +
         Target.mArtifactExtension.c_str());
    ASSERT_TRUE(std::filesystem::is_regular_file(Artifact))
        << GetBackendError().c_str();
    ASSERT_TRUE(std::filesystem::is_regular_file(
        Artifact.string() + ".arda-key"));
    const auto FirstWrite = std::filesystem::last_write_time(Artifact);
    if (FirstInitialized)
        ShutdownBackend();
    const FArdaShaderCompileResult Cached =
        EnsureRegisteredShaderArtifacts(Cache, Target.mBackendName.c_str());
    ASSERT_TRUE(Cached);
    EXPECT_EQ(Cached.mJobsCompiled, 0u);
    EXPECT_EQ(Cached.mCacheHits, 1u);

    ASSERT_TRUE(ConfigureBackend(Configuration));
    const bool SecondInitialized = InitializeBackend();
    EXPECT_EQ(std::filesystem::last_write_time(Artifact), FirstWrite);
    if (SecondInitialized)
        ShutdownBackend();
    if (!FirstInitialized || !SecondInitialized)
        GTEST_SKIP() << GetBackendError().c_str();

    std::filesystem::remove_all(Directory, Error);
}

TEST(ArdaShaderStructs, StartupModePropagatesCompilerFailureBeforeDevice)
{
    using namespace arda::backend;
    FArdaShaderTarget Target;
    ASSERT_TRUE(ResolveLinkedTestShaderTarget(Target));

    ShutdownBackend();
    FShaderCompilerConfigurationGuard CompilerGuard;
    FArdaShaderTypeRegistration::ResetForTests();
    const std::filesystem::path Directory =
        std::filesystem::temp_directory_path() / "ArdaStartupShaderFailureTest";
    const std::filesystem::path Source = Directory / "Failure.hlsl";
    std::error_code Error;
    std::filesystem::remove_all(Directory, Error);
    ASSERT_TRUE(std::filesystem::create_directories(Directory, Error));
    {
        std::ofstream Stream(Source);
        Stream << "[numthreads(1,1,1)] void Main() {}\n";
    }

    FArdaShaderCompilerConfiguration CompilerConfiguration;
    CompilerConfiguration.mCompilerExecutable = Source;
    ConfigureShaderCompiler(CompilerConfiguration);
    const std::string SourceName = Source.string();
    FArdaShaderTypeRegistration Registration(
        "StartupFailureShader", SourceName.c_str(), "StartupFailureArtifact",
        "Main", Stage::Compute, nullptr);
    FArdaBackendConfiguration Configuration;
    Configuration.mBackendName = Target.mBackendName;
    Configuration.mBackend = Target.mBackend;
    Configuration.mbEnableValidation = false;
    Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::Startup;
    Configuration.mShaderCacheDirectory = Directory / "Cache";
    ASSERT_TRUE(ConfigureBackend(Configuration));
    EXPECT_FALSE(InitializeBackend());
    EXPECT_EQ(GetDevice(), nullptr);
    EXPECT_NE(
        GetBackendError().find("Startup shader compilation failed"),
        eastl::string::npos);
    std::filesystem::remove_all(Directory, Error);
}

TEST(ArdaShaderStructs, GlobalMapIndexesOnlyCompiledPermutations)
{
    using namespace arda;
    using namespace backend;
    FArdaShaderTarget Target;
    ASSERT_TRUE(ResolveLinkedTestShaderTarget(Target));

    ShutdownBackend();
    FArdaBackendConfiguration Configuration;
    Configuration.mBackendName = Target.mBackendName;
    Configuration.mBackend = Target.mBackend;
    Configuration.mbEnableValidation = false;
    Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    if (!InitializeBackend())
        GTEST_SKIP() << GetBackendError().c_str();

    FArdaShaderTypeRegistration::ResetForTests();
    FArdaShaderTypeRegistration Registration(
        "PermutationMapShader",
        "ArdaShaderStructTest",
        "PermutationMapArtifact",
        "ShaderStructTestCS",
        Stage::Compute,
        nullptr,
        detail::TShaderPermutationDomain<
            FPermutationShaderPolicy>::PermutationCount,
        &detail::ShouldCompileShaderPermutation<FPermutationShaderPolicy>,
        &detail::BuildShaderCompilationEnvironment<FPermutationShaderPolicy>);

    const std::filesystem::path SourceDirectory =
        ARDA_BACKEND_TEST_SHADER_DIR;
    const std::filesystem::path TestDirectory =
        std::filesystem::temp_directory_path() /
        "ArdaPermutationShaderMapTest";
    std::error_code Error;
    std::filesystem::remove_all(TestDirectory, Error);
    ASSERT_TRUE(std::filesystem::create_directories(TestDirectory, Error));
    const char* Extension = Target.mArtifactExtension.c_str();
    const std::filesystem::path Source =
        SourceDirectory / (std::string("ArdaShaderStructTest") + Extension);
    for (const uint32_t Id : { 0u, 2u, 4u })
    {
        const std::filesystem::path Destination =
            TestDirectory /
            (std::string("PermutationMapArtifact_P") +
             std::to_string(Id) + Extension);
        ASSERT_TRUE(std::filesystem::copy_file(
            Source,
            Destination,
            std::filesystem::copy_options::overwrite_existing,
            Error));
    }

    FArdaGlobalShaderMap Map;
    ASSERT_TRUE(Map.Initialize(GetDevice(), TestDirectory));
    ASSERT_EQ(Map.Enumerate().size(), 3u);
    EXPECT_NE(Map.Find(Registration.GetType(), 0), nullptr);
    EXPECT_EQ(Map.Find(Registration.GetType(), 1), nullptr);
    EXPECT_NE(Map.Find("PermutationMapShader", 2), nullptr);
    EXPECT_EQ(Map.Find("PermutationMapShader", 3), nullptr);
    EXPECT_EQ(Map.Find(Registration.GetType(), 4)->GetPermutationId(), 4u);
    Map.Reset();
    std::filesystem::remove_all(TestDirectory, Error);
    ShutdownBackend();
}

TEST(ArdaShaderStructs, OnDemandMapDefersMissingArtifactAndHonorsOverride)
{
    using namespace arda::backend;
    FArdaShaderTarget Target;
    ASSERT_TRUE(ResolveLinkedTestShaderTarget(Target));

    ShutdownBackend();
    FShaderCompilerConfigurationGuard CompilerGuard;
    FArdaShaderTypeRegistration::ResetForTests();
    const std::filesystem::path Directory =
        std::filesystem::temp_directory_path() / "ArdaOnDemandShaderMapTest";
    const std::filesystem::path ConfiguredCache = Directory / "Configured";
    const std::filesystem::path OverrideCache = Directory / "Override";
    std::error_code Error;
    std::filesystem::remove_all(Directory, Error);
    ASSERT_TRUE(std::filesystem::create_directories(OverrideCache, Error));

    FArdaShaderCompilerConfiguration CompilerConfiguration =
        GetShaderCompilerConfiguration();
    CompilerConfiguration.mbCompileMissingArtifacts = false;
    CompilerConfiguration.mbCompileOutdatedArtifacts = false;
    ConfigureShaderCompiler(CompilerConfiguration);

    FArdaBackendConfiguration Configuration;
    Configuration.mBackendName = Target.mBackendName;
    Configuration.mBackend = Target.mBackend;
    Configuration.mbEnableValidation = false;
    Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::OnDemand;
    Configuration.mShaderCacheDirectory = ConfiguredCache;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    if (!InitializeBackend())
        GTEST_SKIP() << GetBackendError().c_str();

    FArdaShaderTypeRegistration Registration(
        "OnDemandOverrideShader", "UnusedLocalSource.hlsl",
        "OnDemandOverrideArtifact", "ShaderStructTestCS",
        Stage::Compute, nullptr);
    const std::string Extension = Target.mArtifactExtension.c_str();
    ASSERT_TRUE(std::filesystem::copy_file(
        std::filesystem::path(ARDA_BACKEND_TEST_SHADER_DIR) /
            (std::string("ArdaShaderStructTest") + Extension),
        OverrideCache /
            (std::string("OnDemandOverrideArtifact") + Extension),
        std::filesystem::copy_options::overwrite_existing,
        Error));

    FArdaGlobalShaderMap ConfiguredMap;
    ASSERT_TRUE(ConfiguredMap.Initialize(GetDevice()));
    EXPECT_FALSE(std::filesystem::exists(ConfiguredCache));
    EXPECT_EQ(ConfiguredMap.Find(Registration.GetType()), nullptr);
    ASSERT_FALSE(ConfiguredMap.GetDiagnostics().empty());
    EXPECT_EQ(
        ConfiguredMap.GetDiagnostics().back().mCode,
        EArdaGlobalShaderMapError::BytecodeMissing);
    ASSERT_TRUE(std::filesystem::create_directories(ConfiguredCache, Error));
    ASSERT_TRUE(std::filesystem::copy_file(
        std::filesystem::path(ARDA_BACKEND_TEST_SHADER_DIR) /
            (std::string("ArdaShaderStructTest") + Extension),
        ConfiguredCache /
            (std::string("OnDemandOverrideArtifact") + Extension),
        std::filesystem::copy_options::overwrite_existing,
        Error));
    EXPECT_NE(ConfiguredMap.Find(Registration.GetType()), nullptr);

    FArdaGlobalShaderMap OverrideMap;
    ASSERT_TRUE(OverrideMap.Initialize(GetDevice(), OverrideCache));
    std::vector<const FArdaGlobalShaderInstance*> ConcurrentResults(8);
    std::vector<std::thread> Finders;
    for (size_t Index = 0; Index < ConcurrentResults.size(); ++Index)
    {
        Finders.emplace_back([&OverrideMap, &Registration, &ConcurrentResults, Index]
        {
            ConcurrentResults[Index] =
                OverrideMap.Find(Registration.GetType());
        });
    }
    for (std::thread& Finder : Finders)
        Finder.join();
    const FArdaGlobalShaderInstance* Shader = ConcurrentResults.front();
    ASSERT_NE(Shader, nullptr);
    EXPECT_TRUE(Shader->IsLoaded());
    for (const auto* Result : ConcurrentResults)
        EXPECT_EQ(Result, Shader);

    OverrideMap.Reset();
    ConfiguredMap.Reset();
    ShutdownBackend();
    std::filesystem::remove_all(Directory, Error);
}

TEST(ArdaShaderStructs, LoadsGlobalMapIdempotentlyAndBuildsDirectBindings)
{
    using namespace arda;
    using namespace backend;
    using namespace rhi;
    FArdaShaderTarget Target;
    ASSERT_TRUE(ResolveLinkedTestShaderTarget(Target));

    ShutdownBackend();
    FArdaBackendConfiguration Configuration;
    Configuration.mBackendName = Target.mBackendName;
    Configuration.mBackend = Target.mBackend;
    Configuration.mbEnableValidation = false;
    Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    if (!InitializeBackend())
        GTEST_SKIP() << GetBackendError().c_str();

    {
        FArdaShaderTypeRegistration Registration(
            "ShaderStructTest", "ArdaShaderStructTest", "ArdaShaderStructTest",
            "ShaderStructTestCS", Stage::Compute, &DirectMetadata);
        FArdaShaderTypeRegistration NoParameters(
            "NoParameterShader", "NoParameterSource", "ArdaShaderStructTestNoParameters",
            "ShaderStructTestCS", Stage::Compute, nullptr);
        EXPECT_NE(
            Registration.GetType().GetIdentityHash(),
            NoParameters.GetType().GetIdentityHash());
        FArdaGlobalShaderMap Map;
        const std::filesystem::path Directory =
            std::filesystem::temp_directory_path() / "ArdaDistinctShaderArtifactTest";
        std::error_code ArtifactError;
        std::filesystem::remove_all(Directory, ArtifactError);
        ASSERT_TRUE(std::filesystem::create_directories(Directory, ArtifactError));
        const std::string Extension = Target.mArtifactExtension.c_str();
        const std::filesystem::path CopiedSource =
            std::filesystem::path(ARDA_BACKEND_TEST_SHADER_DIR) /
            (std::string("ArdaShaderStructTest") + Extension);
        for (const char* Stem :
             { "ArdaShaderStructTest", "ArdaShaderStructTestNoParameters" })
        {
            ASSERT_TRUE(std::filesystem::copy_file(
                CopiedSource,
                Directory / (std::string(Stem) + Extension),
                std::filesystem::copy_options::overwrite_existing,
                ArtifactError));
        }
        {
            FArdaShaderTypeRegistration Missing(
                "ZMissingShader", "MissingSource", "missing-partial-artifact",
                "ShaderStructTestCS", Stage::Compute, nullptr);
            EXPECT_FALSE(Map.Initialize(GetDevice(), Directory));
            ASSERT_FALSE(Map.GetDiagnostics().empty());
            EXPECT_EQ(
                Map.GetDiagnostics().back().mCode,
                EArdaGlobalShaderMapError::BytecodeMissing);
            EXPECT_FALSE(Map.IsInitialized());
        }
        ASSERT_TRUE(Map.Initialize(GetDevice(), Directory));
        const FArdaGlobalShaderInstance* First = Map.Find(Registration.GetType());
        ASSERT_NE(First, nullptr);
        ASSERT_EQ(First->GetBindingLayouts().size(), 2u);
        const FArdaGlobalShaderInstance* NoParameterShader =
            Map.Find(NoParameters.GetType());
        ASSERT_NE(NoParameterShader, nullptr);
        EXPECT_EQ(NoParameterShader->GetParameterMetadata(), nullptr);
        EXPECT_TRUE(NoParameterShader->GetBindingLayouts().empty());
        IArdaRHIShader* ShaderIdentity = First->GetShader().Get();
        const rhi::FArdaRHIBindingLayoutRef* ArrayLayout = nullptr;
        for (const rhi::FArdaRHIBindingLayoutRef& Layout :
             First->GetBindingLayouts())
        {
            if (Layout->GetDesc().mRegisterSpace == 3)
                ArrayLayout = &Layout;
        }
        ASSERT_NE(ArrayLayout, nullptr);
        IArdaRHIBindingLayout* LayoutIdentity = ArrayLayout->Get();
        ASSERT_TRUE(Map.Initialize(GetDevice(), Directory));
        const FArdaGlobalShaderInstance* Second = Map.Find(Registration.GetType());
        ASSERT_NE(Second, nullptr);
        EXPECT_EQ(Second, First);
        EXPECT_EQ(Second->GetShader().Get(), ShaderIdentity);
        EXPECT_FALSE(Map.Initialize(
            GetDevice(),
            Directory / "different-directory"));
        ASSERT_FALSE(Map.GetDiagnostics().empty());
        EXPECT_EQ(
            Map.GetDiagnostics().back().mCode,
            EArdaGlobalShaderMapError::ResetRequired);
        EXPECT_EQ(Map.Find(Registration.GetType()), First);
        EXPECT_EQ(ArrayLayout->Get(), LayoutIdentity);

        FArdaRHIBufferDesc BufferDesc;
        BufferDesc.mByteSize = sizeof(uint32_t);
        BufferDesc.mStructureStride = sizeof(uint32_t);
        BufferDesc.mUsage =
            EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
        auto Buffer = GetDevice()->CreateBuffer(BufferDesc);
        ASSERT_TRUE(Buffer);
        FDirectParameters Parameters;
        auto IncompleteLayoutDesc = (*ArrayLayout)->GetDesc();
        IncompleteLayoutDesc.mItems[0].mArraySize = 1;
        auto IncompleteLayout =
            GetDevice()->CreateBindingLayout(IncompleteLayoutDesc);
        ASSERT_TRUE(IncompleteLayout);
        FArdaRHIBindingSetDesc IncompleteDesc;
        EXPECT_FALSE(FDirectParameters::GetStaticMetadata().BuildBindingSetDesc(
            &Parameters,
            IncompleteLayout.mValue,
            IncompleteDesc));
        FArdaRHIBindingSetDesc NullDesc;
        EXPECT_FALSE(FDirectParameters::GetStaticMetadata().BuildBindingSetDesc(
            &Parameters, *ArrayLayout, NullDesc));
        Parameters.mOutputs[0] = Buffer.mValue;
        Parameters.mOutputs[1] = Buffer.mValue;
        FArdaRHIBindingSetDesc Desc;
        ASSERT_TRUE(FDirectParameters::GetStaticMetadata().BuildBindingSetDesc(
            &Parameters, *ArrayLayout, Desc));
        ASSERT_EQ(Desc.mItems.size(), 2u);
        EXPECT_EQ(Desc.mItems[0].mSlot, 2u);
        EXPECT_EQ(Desc.mItems[0].mArrayElement, 0u);
        EXPECT_EQ(Desc.mItems[1].mArrayElement, 1u);
        EXPECT_EQ(Desc.mItems[0].mResource.Get(), Buffer.mValue.Get());
        FArdaRHIBindingSetRef BindingSet;
        EXPECT_TRUE(FDirectParameters::GetStaticMetadata().CreateBindingSet(
            *GetDevice(), &Parameters, *ArrayLayout, BindingSet));
        EXPECT_TRUE(BindingSet);

        eastl::vector<FArdaRHIBindingLayoutDesc> PushLayoutDescs;
        ASSERT_TRUE(FPushParameters::GetStaticMetadata().BuildBindingLayoutDescs(
            PushLayoutDescs));
        auto PushLayout =
            GetDevice()->CreateBindingLayout(PushLayoutDescs[0]);
        ASSERT_TRUE(PushLayout);
        FPushParameters PushParameters;
        FArdaRHIBindingSetDesc PushBindingDesc;
        ASSERT_TRUE(FPushParameters::GetStaticMetadata().BuildBindingSetDesc(
            &PushParameters,
            PushLayout.mValue,
            PushBindingDesc));
        ASSERT_EQ(PushBindingDesc.mItems.size(), 1u);
        EXPECT_FALSE(PushBindingDesc.mItems[0].mResource);
        EXPECT_EQ(
            PushBindingDesc.mItems[0].mView.mBufferRange.mByteSize,
            sizeof(FPushPayload));

        Map.Reset();
        EXPECT_TRUE(Map.Initialize(GetDevice(), Directory));
        std::filesystem::remove_all(Directory, ArtifactError);
    }
    ShutdownBackend();
}
