#include "ArdaDevice.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace
{
    using Stage = arda::rhi::EArdaRHIShaderStage;

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
    EXPECT_STREQ(GetShaderArtifactExtension(EArdaBackendType::D3D12), ".dxil");
    EXPECT_STREQ(GetShaderArtifactExtension(EArdaBackendType::Vulkan), ".spv");
    const auto Missing = LoadShaderBytecode(
        std::filesystem::path(ARDA_BACKEND_TEST_SHADER_DIR) / "does-not-exist.spv");
    EXPECT_FALSE(Missing);
    EXPECT_EQ(Missing.mDiagnostic.mCode, EArdaGlobalShaderMapError::BytecodeMissing);
    EXPECT_FALSE(Missing.mDiagnostic.mPath.empty());
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

TEST(ArdaShaderStructs, LoadsGlobalMapIdempotentlyAndBuildsDirectBindings)
{
    using namespace arda;
    using namespace backend;
    using namespace rhi;

    ShutdownBackend();
    FArdaBackendConfiguration Configuration;
    Configuration.mBackend = DefaultBackend;
    Configuration.mbEnableValidation = false;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    if (!InitializeBackend())
        GTEST_SKIP() << GetBackendError().c_str();

    {
        FArdaShaderTypeRegistration Registration(
            "ShaderStructTest", "ArdaShaderStructTest", "ArdaShaderStructTest",
            "ShaderStructTestCS", Stage::Compute, &DirectMetadata);
        FArdaShaderTypeRegistration NoParameters(
            "NoParameterShader", "NoParameterSource", "ArdaShaderStructTest",
            "ShaderStructTestCS", Stage::Compute, nullptr);
        EXPECT_NE(
            Registration.GetType().GetIdentityHash(),
            NoParameters.GetType().GetIdentityHash());
        FArdaGlobalShaderMap Map;
        const std::filesystem::path Directory = ARDA_BACKEND_TEST_SHADER_DIR;
        {
            FArdaShaderTypeRegistration Missing(
                "ZMissingShader", "MissingSource", "missing-partial-artifact",
                "ShaderStructTestCS", Stage::Compute, nullptr);
            EXPECT_FALSE(Map.Initialize(GetDeviceContext(), Directory));
            ASSERT_FALSE(Map.GetDiagnostics().empty());
            EXPECT_EQ(
                Map.GetDiagnostics().back().mCode,
                EArdaGlobalShaderMapError::BytecodeMissing);
            EXPECT_FALSE(Map.IsInitialized());
        }
        ASSERT_TRUE(Map.Initialize(GetDeviceContext(), Directory));
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
        ASSERT_TRUE(Map.Initialize(GetDeviceContext(), Directory));
        const FArdaGlobalShaderInstance* Second = Map.Find(Registration.GetType());
        ASSERT_NE(Second, nullptr);
        EXPECT_EQ(Second, First);
        EXPECT_EQ(Second->GetShader().Get(), ShaderIdentity);
        EXPECT_FALSE(Map.Initialize(
            GetDeviceContext(),
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
        EXPECT_TRUE(Map.Initialize(GetDeviceContext(), Directory));
    }
    ShutdownBackend();
}
