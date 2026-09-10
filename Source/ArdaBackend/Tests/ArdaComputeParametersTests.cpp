#include "Compute/ArdaComputeOperand.h"
#include <gtest/gtest.h>

namespace
{
    using namespace arda;

    ARDA_BEGIN_COMPUTE_PARAMETER_STRUCT(FNestedOperandParameters)
        ARDA_COMPUTE_BUFFER(mInput, EArdaComputeAccess::Read)
        ARDA_COMPUTE_PARAMETER(float, mScale)
    ARDA_END_COMPUTE_PARAMETER_STRUCT()

    ARDA_BEGIN_COMPUTE_PARAMETER_STRUCT(FOperandParameters)
        ARDA_COMPUTE_PARAMETER_STRUCT_ARRAY(FNestedOperandParameters, mLayers, 2)
        ARDA_COMPUTE_BUFFER_ARRAY(mScratch, 2, EArdaComputeAccess::ReadWrite)
        ARDA_COMPUTE_TEXTURE(mOutput, EArdaComputeAccess::Write)
        ARDA_COMPUTE_TEXTURE_ARRAY(mHistory, 2, EArdaComputeAccess::Read)
        ARDA_COMPUTE_PARAMETER_ARRAY(uint32_t, mDimensions, 3)
        ARDA_COMPUTE_PARAMETER(eastl::vector<uint32_t>, mHostPlan)
    ARDA_END_COMPUTE_PARAMETER_STRUCT()

    ARDA_BEGIN_COMPUTE_PARAMETER_STRUCT(FScalarOperandParameters)
        ARDA_COMPUTE_PARAMETER(uint32_t, mValue)
        ARDA_COMPUTE_PARAMETER_STRUCT(FNestedOperandParameters, mNested)
    ARDA_END_COMPUTE_PARAMETER_STRUCT()

    class FParameterTestBuffer final : public IArdaRHIBuffer
    {
    public:
        int mReferences = 0;
        FArdaRHIBufferDesc mDesc;
        void AddRef() noexcept override { ++mReferences; }
        void Release() noexcept override { --mReferences; }
        EArdaRHIResourceType GetResourceType() const noexcept override { return EArdaRHIResourceType::Buffer; }
        const char* GetDebugName() const noexcept override { return "parameter-test-buffer"; }
        const FArdaRHIBufferDesc& GetDesc() const noexcept override { return mDesc; }
        const void* GetPhysicalIdentity() const noexcept override { return this; }
    };
    class FParameterTestTexture final : public IArdaRHITexture
    {
    public:
        int mReferences = 0;
        FArdaRHITextureDesc mDesc;
        void AddRef() noexcept override { ++mReferences; }
        void Release() noexcept override { --mReferences; }
        EArdaRHIResourceType GetResourceType() const noexcept override { return EArdaRHIResourceType::Texture; }
        const char* GetDebugName() const noexcept override { return "parameter-test-texture"; }
        const FArdaRHITextureDesc& GetDesc() const noexcept override { return mDesc; }
        const void* GetPhysicalIdentity() const noexcept override { return this; }
    };

    TEST(ArdaComputeParameters, RecursesThroughArraysAndRetainsAliasedResourceRanges)
    {
        FParameterTestBuffer Buffer;
        FParameterTestTexture Texture;
        eastl::vector<FArdaComputeResourceAccess> Accesses;
        {
            FOperandParameters P;
            P.mLayers[0].mInput = {FArdaRHIBufferRef(&Buffer), {16, 64}};
            P.mScratch[1] = {FArdaRHIBufferRef(&Buffer), {80, 32}};
            P.mOutput = {FArdaRHITextureRef(&Texture), {2, 1, 3, 2}};
            P.mDimensions = {8, 4, 2};
            P.mHostPlan = {5, 9, 7};
            const auto& Metadata = FOperandParameters::GetStaticMetadata();
            ASSERT_TRUE(Metadata.GetStatus()) << Metadata.GetStatus().mMessage.c_str();
            EXPECT_EQ(Metadata.GetSize(), sizeof(P));
            ASSERT_TRUE(Metadata.FindMember("mHostPlan"));
            EXPECT_EQ(Metadata.FindMember("mHostPlan")->mKind, EArdaComputeParameterKind::Value);
            EXPECT_EQ(Metadata.FindMember("missing"), nullptr);
            eastl::vector<eastl::string> Paths;
            ASSERT_TRUE(Metadata.Enumerate(&P, [&](const FArdaComputeParameter& Leaf)
            {
                Paths.push_back(Leaf.mPath);
                EXPECT_EQ(static_cast<const uint8_t*>(Leaf.mValue) - reinterpret_cast<const uint8_t*>(&P), Leaf.mAbsoluteOffset);
                if (Leaf.mPath == "mHostPlan") EXPECT_EQ(*static_cast<const eastl::vector<uint32_t>*>(Leaf.mValue), P.mHostPlan);
                if (Leaf.mPath == "mDimensions[2]") EXPECT_EQ(*static_cast<const uint32_t*>(Leaf.mValue), 2u);
            }));
            ASSERT_EQ(Paths.size(), 13u);
            EXPECT_EQ(Paths.front(), "mLayers[0].mInput");
            EXPECT_EQ(Paths[2], "mLayers[1].mInput");
            ASSERT_TRUE(Metadata.GetResourceAccesses(&P, Accesses));
            ASSERT_EQ(Accesses.size(), 7u);
            EXPECT_EQ(Accesses[0].mResource.Get(), &Buffer);
            EXPECT_EQ(Accesses[0].mBufferRange, (FArdaRHIBufferRange{16,64}));
            EXPECT_EQ(Accesses[0].mAccess, EArdaComputeAccess::Read);
            EXPECT_FALSE(Accesses[1].mResource); // Optional/unbound field still has a declaration.
            EXPECT_EQ(Accesses[3].mResource.Get(), &Buffer);
            EXPECT_EQ(Accesses[3].mAccess, EArdaComputeAccess::ReadWrite);
            EXPECT_EQ(Accesses[4].mResource.Get(), &Texture);
            EXPECT_EQ(Accesses[4].mAccess, EArdaComputeAccess::Write);
            EXPECT_EQ(Accesses[4].mTextureRange, P.mOutput.mRange);
            auto Copy = P;
            Copy.mHostPlan[0] = 99;
            EXPECT_EQ(P.mHostPlan[0], 5u); // Actual C++ copies, never a raw-byte parameter blob.
        }
        EXPECT_EQ(Buffer.mReferences, 2);
        EXPECT_EQ(Texture.mReferences, 1);
        Accesses.clear();
        EXPECT_EQ(Buffer.mReferences, 0);
        EXPECT_EQ(Texture.mReferences, 0);
    }

    TEST(ArdaComputeParameters, RejectsInvalidMetadataBeforeDereferencingAndPreservesOutput)
    {
        const auto& Good = FScalarOperandParameters::GetStaticMetadata();
        FScalarOperandParameters P;
        eastl::vector<FArdaComputeResourceAccess> Out(1);
        Out[0].mPath = "preserve";
        EXPECT_FALSE(Good.GetResourceAccesses(nullptr, Out));
        EXPECT_EQ(Out[0].mPath, "preserve");
        EXPECT_FALSE(Good.Enumerate(&P, {}));
        const auto CheckBad = [&](eastl::vector<FArdaComputeParameterMember> Members)
        {
            FArdaComputeParameterMetadata Bad("bad", sizeof(P), alignof(FScalarOperandParameters), eastl::move(Members));
            EXPECT_FALSE(Bad.GetStatus());
            bool Visited = false;
            EXPECT_FALSE(Bad.Enumerate(&P, [&](const auto&) { Visited = true; }));
            EXPECT_FALSE(Visited);
            EXPECT_FALSE(Bad.GetResourceAccesses(&P, Out));
            EXPECT_EQ(Out[0].mPath, "preserve");
        };
        auto Members = Good.GetMembers();
        Members[0].mOffset = sizeof(P); CheckBad(Members);
        Members = Good.GetMembers(); Members[0].mElementCount = SIZE_MAX; CheckBad(Members);
        Members = Good.GetMembers(); Members[1].mNestedMetadata = nullptr; CheckBad(Members);
        Members = Good.GetMembers(); Members[1].mName = Members[0].mName; CheckBad(Members);
        Members = Good.GetMembers(); Members[1].mOffset = 0; CheckBad(Members);
        Members = Good.GetMembers(); Members[0].mKind = EArdaComputeParameterKind::Buffer; CheckBad(Members);
        Members = Good.GetMembers(); Members[0].mAlignment = 3; CheckBad(Members);
        FArdaComputeParameterMetadata BadStruct("bad", 4, 0, {});
        EXPECT_FALSE(BadStruct.GetStatus());
        EXPECT_FALSE(Good.Enumerate(reinterpret_cast<const uint8_t*>(&P) + 1, [](const auto&) {}));
    }

}
