#include "RHI/ArdaRHITypes.h"
#include <gtest/gtest.h>
#include <limits>

namespace
{
	using namespace arda;

	TEST(ArdaTextureBuffer, ValidatesPitchedRegionsAndBufferBounds)
	{
		FArdaRHITextureDesc Texture;
		Texture.mWidth = 75;
		Texture.mHeight = 31;
		Texture.mMipLevels = 2;
		Texture.mFormat = EArdaRHIFormat::R32UInt;
		FArdaRHITextureSlice Slice;
		Slice.mMipLevel = 1;
		Slice.mX = 3;
		Slice.mY = 2;
		Slice.mWidth = 29;
		Slice.mHeight = 11;
		FArdaRHIBufferDesc Buffer;
		Buffer.mByteSize = 512 + 256 * 11;
		FArdaRHITextureBufferLayout Layout{512, 256};
		FArdaRHITextureCopyExtent Extent;
		EXPECT_TRUE(ValidateArdaRHITextureBufferCopy(Texture, Slice, Buffer, Layout, Extent));
		EXPECT_EQ(Extent.mWidth, 29u);
		EXPECT_EQ(Extent.mHeight, 11u);
		EXPECT_EQ(Extent.mDepth, 1u);
		Layout.mRowPitch = 0;
		EXPECT_FALSE(ValidateArdaRHITextureBufferCopy(Texture, Slice, Buffer, Layout, Extent));
		Layout.mRowPitch = 128;
		EXPECT_FALSE(ValidateArdaRHITextureBufferCopy(Texture, Slice, Buffer, Layout, Extent));
		Layout = {4, 256};
		EXPECT_FALSE(ValidateArdaRHITextureBufferCopy(Texture, Slice, Buffer, Layout, Extent));
		Layout = {512, 256};
		Buffer.mByteSize = 512 + 256 * 10 + 29 * 4 - 1;
		EXPECT_FALSE(ValidateArdaRHITextureBufferCopy(Texture, Slice, Buffer, Layout, Extent));
		++Buffer.mByteSize;
		EXPECT_TRUE(ValidateArdaRHITextureBufferCopy(Texture, Slice, Buffer, Layout, Extent));
		Buffer.mByteSize = std::numeric_limits<uint64_t>::max();
		Layout.mByteOffset = Buffer.mByteSize & ~uint64_t(511);
		EXPECT_FALSE(ValidateArdaRHITextureBufferCopy(Texture, Slice, Buffer, Layout, Extent));
	}

	TEST(ArdaTextureBuffer, RejectsPitchAbovePortableSignedRowLimit)
	{
		FArdaRHITextureDesc Texture;
		Texture.mWidth = Texture.mHeight = 1;
		Texture.mFormat = EArdaRHIFormat::R32UInt;
		FArdaRHIBufferDesc Buffer;
		Buffer.mByteSize = 4;
		FArdaRHITextureBufferLayout Layout{0, uint32_t(INT32_MAX) & ~uint32_t(255)};
		FArdaRHITextureCopyExtent Extent;
		EXPECT_TRUE(ValidateArdaRHITextureBufferCopy(Texture, {}, Buffer, Layout, Extent));
		Layout.mRowPitch += 256;
		EXPECT_FALSE(ValidateArdaRHITextureBufferCopy(Texture, {}, Buffer, Layout, Extent));
		EXPECT_EQ(Extent.mWidth, 0u);
	}

	TEST(ArdaTextureBuffer, RejectsUnsupportedFormatsAndSubresources)
	{
		FArdaRHITextureDesc Texture;
		Texture.mWidth = 8;
		Texture.mHeight = 8;
		Texture.mFormat = EArdaRHIFormat::R32UInt;
		FArdaRHIBufferDesc Buffer;
		Buffer.mByteSize = 2048;
		FArdaRHITextureBufferLayout Layout{0, 256};
		FArdaRHITextureCopyExtent Extent;
		Texture.mSampleCount = 4;
		EXPECT_FALSE(ValidateArdaRHITextureBufferCopy(Texture, {}, Buffer, Layout, Extent));
		Texture.mSampleCount = 1;
		Texture.mFormat = EArdaRHIFormat::D32;
		EXPECT_FALSE(ValidateArdaRHITextureBufferCopy(Texture, {}, Buffer, Layout, Extent));
		Texture.mFormat = EArdaRHIFormat::R32UInt;
		FArdaRHITextureSlice Slice;
		Slice.mMipLevel = 1;
		EXPECT_FALSE(ValidateArdaRHITextureBufferCopy(Texture, Slice, Buffer, Layout, Extent));
		Slice = {};
		Slice.mArraySlice = 1;
		EXPECT_FALSE(ValidateArdaRHITextureBufferCopy(Texture, Slice, Buffer, Layout, Extent));
		Slice = {};
		Slice.mWidth = 0;
		EXPECT_FALSE(ValidateArdaRHITextureBufferCopy(Texture, Slice, Buffer, Layout, Extent));
	}

	TEST(ArdaTextureBuffer, RejectsInvalidTextureDimensions)
	{
		FArdaRHITextureDesc Texture;
		Texture.mWidth = Texture.mHeight = 8;
		Texture.mFormat = EArdaRHIFormat::R32UInt;
		FArdaRHIBufferDesc Buffer;
		Buffer.mByteSize = 4096;
		FArdaRHITextureCopyExtent Extent;
		auto Check = [&](FArdaRHITextureDesc Invalid)
		{
			EXPECT_FALSE(ValidateArdaRHITextureBufferCopy(Invalid, {}, Buffer, {0, 256}, Extent));
			EXPECT_EQ(Extent.mWidth, 0u);
		};
		for (auto Member : {&FArdaRHITextureDesc::mWidth, &FArdaRHITextureDesc::mHeight, &FArdaRHITextureDesc::mDepth})
		{
			auto Invalid = Texture;
			Invalid.*Member = 0;
			Check(Invalid);
		}
		auto Invalid = Texture;
		Invalid.mDimension = EArdaRHITextureDimension::Unknown;
		Check(Invalid);
		Invalid = Texture;
		Invalid.mDepth = 2;
		Check(Invalid);
		Invalid = Texture;
		Invalid.mDimension = EArdaRHITextureDimension::Texture1D;
		Check(Invalid);
		Invalid = Texture;
		Invalid.mDimension = EArdaRHITextureDimension::Texture3D;
		Invalid.mArraySize = 2;
		Check(Invalid);
	}
}
