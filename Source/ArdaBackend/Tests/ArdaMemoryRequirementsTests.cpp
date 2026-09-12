#include "ArdaTestBackend.h"
#include "ArdaBackend.h"

#include <gtest/gtest.h>

namespace
{
	using namespace arda;

	class FArdaMemoryRequirementsTest : public testing::TestWithParam<const char*>
	{
	protected:
		void SetUp() override
		{
			ShutdownBackend();
			FArdaBackendConfiguration Configuration;
			Configuration.mBackendName = GetParam();
			Configuration.mbEnableValidation = true;
			Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
			ASSERT_TRUE(ConfigureBackend(Configuration));
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
			mDevice = GetDevice();
			ASSERT_TRUE(mDevice);
		}

		void TearDown() override
		{
			mDevice = {};
			ShutdownBackend();
		}

		FArdaRHIDeviceRef mDevice;
	};

	void ExpectEqualRequirements(const FArdaRHIMemoryRequirements& A, const FArdaRHIMemoryRequirements& B)
	{
		EXPECT_GT(A.mSize, 0u);
		EXPECT_GT(A.mAlignment, 0u);
		EXPECT_NE(A.mMemoryTypeBits, 0u);
		EXPECT_EQ(A.mSize, B.mSize);
		EXPECT_EQ(A.mAlignment, B.mAlignment);
		EXPECT_EQ(A.mMemoryTypeBits, B.mMemoryTypeBits);
	}

	TEST_P(FArdaMemoryRequirementsTest, BufferQueriesMatchNativeResourcesForCommittedAndVirtualDescriptors)
	{
		for (const bool Virtual : {false, true})
		{
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 65540;
			Desc.mUsage = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::UnorderedAccess;
			Desc.mbVirtual = Virtual;
			const auto Query = mDevice->QueryBufferMemoryRequirements(Desc);
			ASSERT_TRUE(Query) << Query.mStatus.mMessage.c_str();
			const auto Buffer = mDevice->CreateBuffer(Desc);
			ASSERT_TRUE(Buffer) << Buffer.mStatus.mMessage.c_str();
			const auto Existing = mDevice->GetBufferMemoryRequirements(Buffer.mValue);
			ASSERT_TRUE(Existing) << Existing.mStatus.mMessage.c_str();
			ExpectEqualRequirements(Query.mValue, Existing.mValue);
		}
	}

	TEST_P(FArdaMemoryRequirementsTest, TextureQueriesMatchNativeMipAndArrayCreationFlags)
	{
		for (const bool Virtual : {false, true})
		{
			FArdaRHITextureDesc Desc;
			Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
			Desc.mWidth = 127;
			Desc.mHeight = 65;
			Desc.mArraySize = 3;
			Desc.mMipLevels = 4;
			Desc.mFormat = EArdaRHIFormat::RGBA16Float;
			Desc.mUsage = EArdaRHITextureUsage::ShaderResource | EArdaRHITextureUsage::UnorderedAccess;
			Desc.mbVirtual = Virtual;
			const auto Query = mDevice->QueryTextureMemoryRequirements(Desc);
			ASSERT_TRUE(Query) << Query.mStatus.mMessage.c_str();
			const auto Texture = mDevice->CreateTexture(Desc);
			ASSERT_TRUE(Texture) << Texture.mStatus.mMessage.c_str();
			const auto Existing = mDevice->GetTextureMemoryRequirements(Texture.mValue);
			ASSERT_TRUE(Existing) << Existing.mStatus.mMessage.c_str();
			ExpectEqualRequirements(Query.mValue, Existing.mValue);
		}
	}

	TEST_P(FArdaMemoryRequirementsTest, RejectsInvalidDescriptorsBeforeNativeQueries)
	{
		FArdaRHIBufferDesc Buffer;
		Buffer.mByteSize = 0;
		EXPECT_EQ(mDevice->QueryBufferMemoryRequirements(Buffer).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		Buffer.mByteSize = 256;
		Buffer.mbVirtual = Buffer.mbTiled = true;
		EXPECT_EQ(mDevice->QueryBufferMemoryRequirements(Buffer).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		FArdaRHITextureDesc Texture;
		Texture.mFormat = EArdaRHIFormat::R32Float;
		Texture.mWidth = 0;
		EXPECT_EQ(mDevice->QueryTextureMemoryRequirements(Texture).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		Texture.mWidth = 16;
		Texture.mbVirtual = Texture.mbTiled = true;
		EXPECT_EQ(mDevice->QueryTextureMemoryRequirements(Texture).mStatus.mCode, EArdaRHIResult::InvalidArgument);
	}

	const char* const NativeBackends[] = {
#if defined(ARDA_TEST_NATIVE_D3D12)
	    "native-d3d12",
#endif
#if defined(ARDA_TEST_NATIVE_VULKAN)
	    "native-vulkan",
#endif
	};
	INSTANTIATE_TEST_SUITE_P(NativeProviders, FArdaMemoryRequirementsTest, testing::ValuesIn(NativeBackends));
}
