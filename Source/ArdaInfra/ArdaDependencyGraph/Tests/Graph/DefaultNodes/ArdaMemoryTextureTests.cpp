#include "../../ArdaMemoryFacadeTest.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"
#include "Graph/DefaultNodes/ArdaMemoryTextureNodes.h"
#include "ArdaBackend.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "ArdaTestBackend.h"

#include <gtest/gtest.h>
#include <limits>
#include <string>

namespace arda::memorytexture_tests
{
	using namespace arda;
	using namespace arda::memory_facade_tests;

	FArdaRHITextureDesc MakeTextureDesc(uint32_t Width = 7, uint32_t Height = 3)
	{
		FArdaRHITextureDesc Desc;
		Desc.mWidth = Width;
		Desc.mHeight = Height;
		Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		return Desc;
	}

	FArdaGraphTexture MakeTestTexture(FArdaGraphEdit& Edit,
	    const char* Name,
	    const FArdaRHITextureDesc& Desc = MakeTextureDesc())
	{
		const auto Created = AllocateTexture(Edit, Desc, Name);
		EXPECT_TRUE(Created) << Edit.GetStatus().mMessage.c_str();
		return Created;
	}

	eastl::vector<uint8_t> MakeTextureBytes(size_t ByteCount)
	{
		eastl::vector<uint8_t> Bytes(ByteCount);
		for (size_t Index = 0; Index < ByteCount; ++Index)
		{
			Bytes[Index] = uint8_t((Index * 37 + Index / 11) % 251);
		}
		return Bytes;
	}

	TEST(ArdaMemoryTextures, RejectsMissingForeignStaleAndBufferHandles)
	{
		FArdaDependencyGraph Graph;
		FArdaDependencyGraph Other;
		FArdaGraphEdit AbandonedEdit(Graph);
		ASSERT_TRUE(AbandonedEdit.GetStatus());
		const auto Stale = MakeTestTexture(AbandonedEdit, "abandoned");
		ASSERT_TRUE(AbandonedEdit.Cancel());
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		FArdaGraphEdit OtherEdit(Other);
		ASSERT_TRUE(OtherEdit.GetStatus());
		const auto Valid = MakeTestTexture(Edit, "valid");
		const auto Foreign = MakeTestTexture(OtherEdit, "foreign");
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 256;
		const auto Buffer = AllocateBuffer(Edit, BufferDesc, "buffer");
		ASSERT_TRUE(Buffer);
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(Edit.Commit());
		for (const auto Invalid :
		    {FArdaDependencyResourceHandle{}, Stale.GetHandle(), Foreign.GetHandle(), Buffer.GetHandle()})
		{
			ExpectRejectedOperation(Graph,
			    UploadTexture,
			    FArdaMemoryUploadTextureNode::FArdaParameters{Invalid, MakeTextureBytes(84)},
			    "bad upload");
			ExpectRejectedOperation(Graph,
			    ReadbackTexture,
			    FArdaMemoryReadbackTextureNode::FArdaParameters{Invalid, Bytes},
			    "bad readback");
			ExpectRejectedOperation(Graph,
			    CopyTexture,
			    FArdaMemoryCopyTextureNode::FArdaParameters{Invalid, Valid},
			    "bad source");
			ExpectRejectedOperation(Graph,
			    CopyTexture,
			    FArdaMemoryCopyTextureNode::FArdaParameters{Valid, Invalid},
			    "bad destination");
			ExpectRejectedOperation(Graph,
			    ClearTexture,
			    FArdaMemoryClearTextureNode::FArdaParameters{Invalid},
			    "bad clear");
		}
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		ASSERT_TRUE(OtherEdit.Cancel());
	}

	TEST(ArdaMemoryTextures, ValidatesExactPackedSizeAndStrictSliceBounds)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		auto Desc = MakeTextureDesc();
		Desc.mMipLevels = 2;
		const auto Texture = MakeTestTexture(Edit, "texture", Desc);
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(Edit.Commit());
		ExpectRejectedOperation(Graph,
		    UploadTexture,
		    FArdaMemoryUploadTextureNode::FArdaParameters{Texture, {}},
		    "empty");
		ExpectRejectedOperation(Graph,
		    UploadTexture,
		    FArdaMemoryUploadTextureNode::FArdaParameters{Texture, MakeTextureBytes(83)},
		    "short");
		ExpectRejectedOperation(Graph,
		    UploadTexture,
		    FArdaMemoryUploadTextureNode::FArdaParameters{Texture, MakeTextureBytes(85)},
		    "long");
		ExpectRejectedOperation(Graph,
		    ReadbackTexture,
		    FArdaMemoryReadbackTextureNode::FArdaParameters{Texture, {}},
		    "null sink");
		for (uint32_t Case = 0; Case < 8; ++Case)
		{
			SCOPED_TRACE(Case);
			FArdaRHITextureSlice Slice;
			switch (Case)
			{
			case 0:
				Slice.mX = Desc.mWidth;
				break;
			case 1:
				Slice.mY = Desc.mHeight;
				break;
			case 2:
				Slice.mWidth = 0;
				break;
			case 3:
				Slice.mX = 2;
				Slice.mWidth = 6;
				break;
			case 4:
				Slice.mMipLevel = Desc.mMipLevels;
				break;
			case 5:
				Slice.mArraySlice = 1;
				break;
			case 6:
				Slice.mPlane = 1;
				break;
			case 7:
				Slice.mX = std::numeric_limits<uint32_t>::max();
				Slice.mWidth = 2;
				break;
			}
			ExpectRejectedOperation(Graph,
			    UploadTexture,
			    FArdaMemoryUploadTextureNode::FArdaParameters{Texture, MakeTextureBytes(84), Slice},
			    "invalid upload");
			ExpectRejectedOperation(Graph,
			    ReadbackTexture,
			    FArdaMemoryReadbackTextureNode::FArdaParameters{Texture, Bytes, Slice},
			    "invalid readback");
		}
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		FArdaRHITextureSlice Mip;
		Mip.mMipLevel = 1;
		FArdaGraphEdit Operations(Graph);
		const auto Upload = UploadTexture(Operations,
		    FArdaMemoryUploadTextureNode::FArdaParameters{Texture, MakeTextureBytes(12), Mip},
		    "mip upload");
		ASSERT_TRUE(Upload) << Operations.GetStatus().mMessage.c_str();
		EXPECT_EQ(Graph.FindOutput(Upload.GetHandle(), "Destination"), Texture);
		const auto Again = UploadTexture(Operations,
		    FArdaMemoryUploadTextureNode::FArdaParameters{Texture, MakeTextureBytes(12), Mip});
		ASSERT_TRUE(Again);
		EXPECT_NE(Again.GetHandle(), Upload.GetHandle());
		ASSERT_TRUE(Operations.Commit());
		ExpectRejectedOperation(Graph,
		    UploadTexture,
		    FArdaMemoryUploadTextureNode::FArdaParameters{Texture, eastl::vector<uint8_t>(12, 0), Mip},
		    "mip upload");
	}

	TEST(ArdaMemoryTextures, RejectsDepthMultisampleAndCompressedHostTransfers)
	{
		FArdaDependencyGraph Graph;
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		for (uint32_t Case = 0; Case < 3; ++Case)
		{
			SCOPED_TRACE(Case);
			auto Desc = MakeTextureDesc(4, 4);
			if (Case == 0)
			{
				Desc.mFormat = EArdaRHIFormat::D32;
				Desc.mUsage = EArdaRHITextureUsage::DepthStencil;
			}
			else if (Case == 1)
			{
				Desc.mSampleCount = 4;
				Desc.mDimension = EArdaRHITextureDimension::Texture2DMS;
			}
			else
			{
				Desc.mFormat = EArdaRHIFormat::BC1UNorm;
			}
			FArdaGraphEdit Edit(Graph);
			const auto Texture = MakeTestTexture(Edit, ("unsupported " + std::to_string(Case)).c_str(), Desc);
			ASSERT_TRUE(Edit.Commit());
			ExpectRejectedOperation(Graph,
			    UploadTexture,
			    FArdaMemoryUploadTextureNode::FArdaParameters{Texture, MakeTextureBytes(64)},
			    "unsupported upload");
			ExpectRejectedOperation(Graph,
			    ReadbackTexture,
			    FArdaMemoryReadbackTextureNode::FArdaParameters{Texture, Bytes},
			    "unsupported readback");
		}
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
	}

	TEST(ArdaMemoryTextures, CopyValidatesFormatsExtentsAndSubresourceIdentity)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		auto Desc = MakeTextureDesc(8, 4);
		Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
		Desc.mArraySize = 2;
		const auto Source = MakeTestTexture(Edit, "source", Desc);
		const auto Destination = MakeTestTexture(Edit, "destination", Desc);
		Desc.mFormat = EArdaRHIFormat::R32Float;
		const auto DifferentFormat = MakeTestTexture(Edit, "different format", Desc);
		ASSERT_TRUE(Edit.Commit());
		ExpectRejectedOperation(Graph,
		    CopyTexture,
		    FArdaMemoryCopyTextureNode::FArdaParameters{Source, Source},
		    "self");
		ExpectRejectedOperation(Graph,
		    CopyTexture,
		    FArdaMemoryCopyTextureNode::FArdaParameters{Source, DifferentFormat},
		    "format");
		FArdaRHITextureSlice Region;
		Region.mWidth = Region.mHeight = 2;
		FArdaRHITextureSlice TooSmall = Region;
		TooSmall.mWidth = 1;
		ExpectRejectedOperation(Graph,
		    CopyTexture,
		    FArdaMemoryCopyTextureNode::FArdaParameters{Source, Destination, Region, TooSmall},
		    "unequal extents");
		FArdaRHITextureSlice PastEnd = Region;
		PastEnd.mX = 7;
		ExpectRejectedOperation(Graph,
		    CopyTexture,
		    FArdaMemoryCopyTextureNode::FArdaParameters{Source, Destination, PastEnd, Region},
		    "source overrun");
		ExpectRejectedOperation(Graph,
		    CopyTexture,
		    FArdaMemoryCopyTextureNode::FArdaParameters{Source, Destination, Region, PastEnd},
		    "destination overrun");
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		FArdaRHITextureSlice Layer;
		Layer.mArraySlice = 1;
		FArdaGraphEdit Operations(Graph);
		const auto Copy = CopyTexture(Operations,
		    FArdaMemoryCopyTextureNode::FArdaParameters{Source, Source, {}, Layer},
		    "different layers");
		ASSERT_TRUE(Copy) << Operations.GetStatus().mMessage.c_str();
		EXPECT_EQ(Graph.FindOutput(Copy.GetHandle(), "Destination"), Source);
		const auto Again =
		    CopyTexture(Operations, FArdaMemoryCopyTextureNode::FArdaParameters{Source, Source, {}, Layer});
		ASSERT_TRUE(Again);
		EXPECT_NE(Again.GetHandle(), Copy.GetHandle());
		ASSERT_TRUE(Operations.Cancel());
	}

	TEST(ArdaMemoryTextures, CopyRejectsDifferentDimensionClassesEvenWhenExtentsMatch)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		auto Desc = MakeTextureDesc(8, 1);
		const auto Texture2D = MakeTestTexture(Edit, "2d", Desc);
		Desc.mDimension = EArdaRHITextureDimension::Texture1D;
		const auto Texture1D = MakeTestTexture(Edit, "1d", Desc);
		Desc.mDimension = EArdaRHITextureDimension::Texture3D;
		const auto Texture3D = MakeTestTexture(Edit, "3d", Desc);
		ASSERT_TRUE(Edit.Commit());
		ExpectRejectedOperation(Graph,
		    CopyTexture,
		    FArdaMemoryCopyTextureNode::FArdaParameters{Texture1D, Texture2D},
		    "1d to 2d");
		ExpectRejectedOperation(Graph,
		    CopyTexture,
		    FArdaMemoryCopyTextureNode::FArdaParameters{Texture2D, Texture1D},
		    "2d to 1d");
		ExpectRejectedOperation(Graph,
		    CopyTexture,
		    FArdaMemoryCopyTextureNode::FArdaParameters{Texture2D, Texture3D},
		    "2d to 3d");
		ExpectRejectedOperation(Graph,
		    CopyTexture,
		    FArdaMemoryCopyTextureNode::FArdaParameters{Texture3D, Texture2D},
		    "3d to 2d");
		ExpectRejectedOperation(Graph,
		    CopyTexture,
		    FArdaMemoryCopyTextureNode::FArdaParameters{Texture1D, Texture3D},
		    "1d to 3d");
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
	}

	TEST(ArdaMemoryTextures, FootprintsRetainOnlyFinalRowTexelsAndAlignWholeTexels)
	{
		auto Desc = MakeTextureDesc(13, 7);
		Desc.mUsage = EArdaRHITextureUsage::None; // Direct RHI queries use a concrete descriptor.
		const auto Color = GetArdaRHITextureBufferFootprint(Desc, {});
		ASSERT_TRUE(Color) << Color.mStatus.mMessage.c_str();
		EXPECT_EQ(Color.mValue.mExtent.mWidth, 13u);
		EXPECT_EQ(Color.mValue.mExtent.mHeight, 7u);
		EXPECT_EQ(Color.mValue.mExtent.mDepth, 1u);
		EXPECT_EQ(Color.mValue.mLayout.mByteOffset, 0u);
		EXPECT_EQ(Color.mValue.mLayout.mRowPitch, 256u);
		EXPECT_EQ(Color.mValue.mRowBytes, 52u);
		EXPECT_EQ(Color.mValue.mRowCount, 7u);
		EXPECT_EQ(Color.mValue.mByteSize, 1588u);

		Desc.mFormat = EArdaRHIFormat::RGB32Float;
		const auto ThreeComponent = GetArdaRHITextureBufferFootprint(Desc, {});
		ASSERT_TRUE(ThreeComponent) << ThreeComponent.mStatus.mMessage.c_str();
		EXPECT_EQ(ThreeComponent.mValue.mLayout.mRowPitch, 768u);
		EXPECT_EQ(ThreeComponent.mValue.mRowBytes, 156u);
		EXPECT_EQ(ThreeComponent.mValue.mRowCount, 7u);
		EXPECT_EQ(ThreeComponent.mValue.mByteSize, 4764u);

		Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		Desc.mDimension = EArdaRHITextureDimension::Texture3D;
		Desc.mDepth = 3;
		const auto Volume = GetArdaRHITextureBufferFootprint(Desc, {});
		ASSERT_TRUE(Volume) << Volume.mStatus.mMessage.c_str();
		EXPECT_EQ(Volume.mValue.mExtent.mDepth, 3u);
		EXPECT_EQ(Volume.mValue.mRowCount, 21u);
		EXPECT_EQ(Volume.mValue.mRowBytes * Volume.mValue.mRowCount, 1092u);
		EXPECT_EQ(Volume.mValue.mByteSize, 5172u);
	}

	TEST(ArdaMemoryTextures, ClearRequiresColorRenderTargetsAndStrictNonemptyRanges)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		auto Desc = MakeTextureDesc(8, 4);
		Desc.mUsage = EArdaRHITextureUsage::ShaderResource; // Explicitly forbid render-target use.
		const auto NoRenderTarget = MakeTestTexture(Edit, "no render target", Desc);
		Desc.mUsage = EArdaRHITextureUsage::RenderTarget;
		Desc.mFormat = EArdaRHIFormat::RGBA8UInt;
		const auto Integer = MakeTestTexture(Edit, "integer target", Desc);
		Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
		Desc.mArraySize = 2;
		Desc.mMipLevels = 2;
		const auto Target = MakeTestTexture(Edit, "target", Desc);
		ASSERT_TRUE(Edit.Commit());
		ExpectRejectedOperation(Graph,
		    ClearTexture,
		    FArdaMemoryClearTextureNode::FArdaParameters{NoRenderTarget},
		    "missing usage");
		ExpectRejectedOperation(Graph, ClearTexture, FArdaMemoryClearTextureNode::FArdaParameters{Integer}, "integer");
		for (uint32_t Case = 0; Case < 7; ++Case)
		{
			SCOPED_TRACE(Case);
			FArdaRHITextureSubresourceRange Range;
			switch (Case)
			{
			case 0:
				Range.mMipLevelCount = 0;
				break;
			case 1:
				Range.mArraySliceCount = 0;
				break;
			case 2:
				Range.mBaseMipLevel = 2;
				break;
			case 3:
				Range.mBaseArraySlice = 2;
				break;
			case 4:
				Range.mBaseMipLevel = 1;
				Range.mMipLevelCount = 2;
				break;
			case 5:
				Range.mBaseArraySlice = 1;
				Range.mArraySliceCount = 2;
				break;
			case 6:
				Range.mBasePlane = 1;
				break;
			}
			ExpectRejectedOperation(Graph,
			    ClearTexture,
			    FArdaMemoryClearTextureNode::FArdaParameters{Target, {}, Range},
			    "invalid range");
		}
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		FArdaGraphEdit Operations(Graph);
		const auto Clear =
		    ClearTexture(Operations, FArdaMemoryClearTextureNode::FArdaParameters{Target, {1, 0, 0, 1}}, "valid");
		ASSERT_TRUE(Clear) << Operations.GetStatus().mMessage.c_str();
		EXPECT_EQ(Graph.FindOutput(Clear.GetHandle(), "Destination"), Target);
		ASSERT_TRUE(Operations.Cancel());
	}

	class FArdaMemoryTexturesGpu : public testing::TestWithParam<const char*>
	{
	protected:
		void SetUp() override
		{
			ShutdownBackend();
			if (!FindBackendModule(GetParam()))
			{
				GTEST_SKIP() << "Backend not built";
			}
			auto Configuration = MakeArdaTestBackendConfiguration();
			Configuration.mBackendName = GetParam();
			Configuration.mMessageCallback = &mDiagnostics;
			ASSERT_TRUE(ConfigureBackend(Configuration)) << GetBackendError().c_str();
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
			mDevice = GetDevice();
			ASSERT_TRUE(mDevice);
		}

		void TearDown() override
		{
			const bool MissingValidationSkip = testing::Test::IsSkipped() && ArdaTestValidationEnabled &&
			    GetBackendInitializeResult() == EArdaInitializeResult::ValidationUnavailable;
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
				mDevice->RunGarbageCollection();
				mDevice.Reset();
			}
			ShutdownBackend();
			if (!MissingValidationSkip)
			{
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
		}

		FArdaTestDiagnosticCallback mDiagnostics;
		FArdaRHIDeviceRef mDevice;
	};

	TEST_P(FArdaMemoryTexturesGpu, TextureCopiesRoundTripPackedMipArrayAndVolumeDataAfterWait)
	{
		for (uint32_t Shape = 0; Shape < 6; ++Shape)
		{
			SCOPED_TRACE(Shape);
			FArdaDependencyGraph Graph(mDevice);
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.GetStatus());
			FArdaInductorOptions Options;
			Options.mFramesInFlight = 2;
			ASSERT_TRUE(Edit.SetOptions(Options));
			auto Desc = MakeTextureDesc();
			FArdaRHITextureSlice Slice;
			size_t ByteCount = 7 * 3 * 4;
			if (Shape == 1)
			{
				Desc.mMipLevels = 2;
				Slice.mMipLevel = 1;
				ByteCount = 3 * 1 * 4;
			}
			else if (Shape == 2)
			{
				Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
				Desc.mArraySize = 3;
				Slice.mArraySlice = 2;
			}
			else if (Shape == 3)
			{
				Desc.mDimension = EArdaRHITextureDimension::Texture3D;
				Desc.mDepth = 3;
				ByteCount *= 3;
			}
			else if (Shape == 4)
			{
				Desc.mDimension = EArdaRHITextureDimension::Texture1D;
				Desc.mWidth = 13;
				Desc.mHeight = 1;
				Desc.mFormat = EArdaRHIFormat::R8UNorm;
				ByteCount = 13;
			}
			else if (Shape == 5)
			{
				Desc.mDimension = EArdaRHITextureDimension::TextureCube;
				Desc.mWidth = Desc.mHeight = 3;
				Desc.mArraySize = 6;
				Slice.mArraySlice = 5;
				ByteCount = 3 * 3 * 4;
			}
			const auto Source = MakeTestTexture(Edit, "source", Desc);
			const auto Destination = MakeTestTexture(Edit, "destination", Desc);
			const auto Expected = MakeTextureBytes(ByteCount);
			FArdaMemoryUploadTextureParameters UploadParameters{Source, Expected, Slice};
			const auto Upload = UploadTexture(Edit, UploadParameters, "upload");
			ASSERT_TRUE(Upload) << Edit.GetStatus().mMessage.c_str();
			UploadParameters.mBytes.assign(ByteCount, 255);
			const auto Copy = CopyTexture(Edit,
			    FArdaMemoryCopyTextureNode::FArdaParameters{Source, Destination, Slice, Slice},
			    "copy");
			ASSERT_TRUE(Copy) << Edit.GetStatus().mMessage.c_str();
			auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
			ASSERT_TRUE(ReadbackTexture(Edit,
			    FArdaMemoryReadbackTextureNode::FArdaParameters{Destination, Bytes, Slice},
			    "readback"));
			ASSERT_TRUE(Edit.Commit());
			const auto Compiled = Graph.Compile();
			ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			EXPECT_EQ(Graph.GetCompileResult().mWorkspaceResourceIds.size(), 2u);
			for (uint32_t Frame = 0; Frame < 3; ++Frame)
			{
				SCOPED_TRACE(Frame);
				*Bytes = {254};
				const auto Submitted = Graph.Submit();
				ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
				ASSERT_TRUE(mDevice->WaitForIdle());
				EXPECT_EQ(*Bytes, (eastl::vector<uint8_t>{254}));
				const auto Completed = Graph.Wait(Submitted.mValue);
				ASSERT_TRUE(Completed.mStatus) << Completed.mStatus.mMessage.c_str();
				EXPECT_EQ(*Bytes, Expected);
			}
		}
	}

	TEST_P(FArdaMemoryTexturesGpu, PartialUploadsAndCopiesPreserveOtherTexels)
	{
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		const auto Destination = MakeTestTexture(Edit, "destination", MakeTextureDesc(7, 5));
		const auto Source = MakeTestTexture(Edit, "source", MakeTextureDesc(3, 3));
		ASSERT_TRUE(UploadTexture(Edit,
		    FArdaMemoryUploadTextureNode::FArdaParameters{Destination, eastl::vector<uint8_t>(7 * 5 * 4, 17)},
		    "initialize destination"));
		const auto SourceBytes = MakeTextureBytes(3 * 3 * 4);
		ASSERT_TRUE(UploadTexture(Edit,
		    FArdaMemoryUploadTextureNode::FArdaParameters{Source, SourceBytes},
		    "initialize source"));
		FArdaRHITextureSlice UploadSlice;
		UploadSlice.mX = 1;
		UploadSlice.mY = 1;
		UploadSlice.mWidth = UploadSlice.mHeight = 2;
		const auto PatchBytes = MakeTextureBytes(16);
		ASSERT_TRUE(UploadTexture(Edit,
		    FArdaMemoryUploadTextureNode::FArdaParameters{Destination, PatchBytes, UploadSlice},
		    "patch upload"));
		FArdaRHITextureSlice SourceSlice;
		SourceSlice.mX = SourceSlice.mY = 1;
		SourceSlice.mWidth = SourceSlice.mHeight = 2;
		FArdaRHITextureSlice DestinationSlice = SourceSlice;
		DestinationSlice.mX = 4;
		DestinationSlice.mY = 3;
		ASSERT_TRUE(CopyTexture(Edit,
		    FArdaMemoryCopyTextureNode::FArdaParameters{Source, Destination, SourceSlice, DestinationSlice},
		    "patch copy"));
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		auto RegionBytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(ReadbackTexture(Edit,
		    FArdaMemoryReadbackTextureNode::FArdaParameters{Destination, Bytes},
		    "read full texture"));
		ASSERT_TRUE(ReadbackTexture(Edit,
		    FArdaMemoryReadbackTextureNode::FArdaParameters{Destination, RegionBytes, DestinationSlice},
		    "read copied region"));
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		eastl::vector<uint8_t> Expected(7 * 5 * 4, 17);
		eastl::vector<uint8_t> ExpectedRegion;
		for (uint32_t Y = 0; Y < 2; ++Y)
		{
			for (uint32_t X = 0; X < 2; ++X)
			{
				for (uint32_t Channel = 0; Channel < 4; ++Channel)
				{
					Expected[((Y + 1) * 7 + X + 1) * 4 + Channel] = PatchBytes[(Y * 2 + X) * 4 + Channel];
					const auto CopiedByte = SourceBytes[((Y + 1) * 3 + X + 1) * 4 + Channel];
					Expected[((Y + 3) * 7 + X + 4) * 4 + Channel] = CopiedByte;
					ExpectedRegion.push_back(CopiedByte);
				}
			}
		}
		for (uint32_t Frame = 0; Frame < 3; ++Frame)
		{
			const auto Executed = Graph.Execute();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			EXPECT_EQ(*Bytes, Expected);
			EXPECT_EQ(*RegionBytes, ExpectedRegion);
		}
	}

	TEST_P(FArdaMemoryTexturesGpu, ClearSelectsMipAndArrayLayerAndPreservesOtherSubresources)
	{
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		auto Desc = MakeTextureDesc(8, 4);
		Desc.mUsage = EArdaRHITextureUsage::RenderTarget;
		Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
		Desc.mMipLevels = 2;
		Desc.mArraySize = 2;
		const auto Texture = MakeTestTexture(Edit, "target", Desc);
		ASSERT_TRUE(
		    ClearTexture(Edit, FArdaMemoryClearTextureNode::FArdaParameters{Texture, {1, 0, 0, 1}}, "initialize red"));
		FArdaRHITextureSubresourceRange SelectedRange;
		SelectedRange.mBaseMipLevel = 1;
		SelectedRange.mMipLevelCount = 1;
		SelectedRange.mBaseArraySlice = 1;
		SelectedRange.mArraySliceCount = 1;
		ASSERT_TRUE(ClearTexture(Edit,
		    FArdaMemoryClearTextureNode::FArdaParameters{Texture, {0, 1, 0, 1}, SelectedRange},
		    "selected green"));
		eastl::vector<eastl::shared_ptr<eastl::vector<uint8_t>>> Outputs;
		for (uint32_t Layer = 0; Layer < 2; ++Layer)
		{
			for (uint32_t Mip = 0; Mip < 2; ++Mip)
			{
				FArdaRHITextureSlice Slice;
				Slice.mArraySlice = Layer;
				Slice.mMipLevel = Mip;
				auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
				const auto Name = "read " + std::to_string(Layer * 2 + Mip);
				ASSERT_TRUE(ReadbackTexture(Edit,
				    FArdaMemoryReadbackTextureNode::FArdaParameters{Texture, Bytes, Slice},
				    Name.c_str()));
				Outputs.push_back(Bytes);
			}
		}
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		for (uint32_t Frame = 0; Frame < 3; ++Frame)
		{
			const auto Executed = Graph.Execute();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			for (size_t Index = 0; Index < Outputs.size(); ++Index)
			{
				const auto& Bytes = *Outputs[Index];
				ASSERT_EQ(Bytes.size(), Index % 2 == 0 ? 128u : 32u);
				for (size_t Byte = 0; Byte < Bytes.size(); Byte += 4)
				{
					EXPECT_EQ(Bytes[Byte], Index == 3 ? 0 : 255);
					EXPECT_EQ(Bytes[Byte + 1], Index == 3 ? 255 : 0);
					EXPECT_EQ(Bytes[Byte + 2], 0);
					EXPECT_EQ(Bytes[Byte + 3], 255);
				}
			}
		}
	}

	TEST_P(FArdaMemoryTexturesGpu, CopiesBetweenArrayLayersOfOneTexture)
	{
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		auto Desc = MakeTextureDesc();
		Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
		Desc.mArraySize = 2;
		const auto Texture = MakeTestTexture(Edit, "array", Desc);
		const auto Expected = MakeTextureBytes(84);
		ASSERT_TRUE(UploadTexture(Edit,
		    FArdaMemoryUploadTextureNode::FArdaParameters{Texture, Expected},
		    "upload first layer"));
		FArdaRHITextureSlice Layer;
		Layer.mArraySlice = 1;
		ASSERT_TRUE(CopyTexture(Edit,
		    FArdaMemoryCopyTextureNode::FArdaParameters{Texture, Texture, {}, Layer},
		    "copy to second layer"));
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(ReadbackTexture(Edit,
		    FArdaMemoryReadbackTextureNode::FArdaParameters{Texture, Bytes, Layer},
		    "read second layer"));
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		const auto Executed = Graph.Execute();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		EXPECT_EQ(*Bytes, Expected);
	}

	TEST_P(FArdaMemoryTexturesGpu, ConstantBuffersSupportOwnedUploadsCopiesAndReadbacks)
	{
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 256;
		Desc.mUsage = EArdaRHIBufferUsage::Constant;
		const auto Source = AllocateBuffer(Edit, Desc, "uniform source");
		const auto Destination = AllocateBuffer(Edit, Desc, "uniform destination");
		ASSERT_TRUE(Source);
		ASSERT_TRUE(Destination);
		const auto Expected = MakeTextureBytes(256);
		ASSERT_TRUE(UploadBuffer(Edit,
		    FArdaMemoryUploadBufferNode::FArdaParameters{Source.GetHandle(), Expected},
		    "upload constants"));
		ASSERT_TRUE(CopyBuffer(Edit,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Source.GetHandle(), Destination.GetHandle()},
		    "copy constants"));
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(ReadbackBuffer(Edit,
		    FArdaMemoryReadbackBufferNode::FArdaParameters{Destination.GetHandle(), Bytes},
		    "read constants"));
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		for (uint32_t Frame = 0; Frame < 3; ++Frame)
		{
			const auto Executed = Graph.Execute();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			EXPECT_EQ(*Bytes, Expected);
		}
	}

	struct FArdaMemoryFailureParameters
	{
	};

	class FArdaMemoryFailureNode final
	    : public TArdaGraphicsDependencyNode<FArdaMemoryFailureNode, FArdaMemoryFailureParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.memory.recording-failure", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters&)
		{
			return "fail";
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mbSideEffect = true;
			return Desc;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "injected Memory recording failure");
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaMemoryFailureNode, FailMemoryRecording);
	ARDA_DEFINE_GRAPH_NODE(FArdaMemoryFailureNode, FailMemoryRecording);

	TEST_P(FArdaMemoryTexturesGpu, FailedFramesInvalidateTextureAndBufferReadbacksAndPermitReruns)
	{
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		FArdaInductorOptions CompileOptions;
		CompileOptions.mbEnableCopyQueue = false;
		ASSERT_TRUE(Edit.SetOptions(CompileOptions));
		const auto Texture = MakeTestTexture(Edit, "texture");
		const auto TextureExpected = MakeTextureBytes(84);
		const auto BufferExpected = MakeTextureBytes(32);
		ASSERT_TRUE(UploadTexture(Edit,
		    FArdaMemoryUploadTextureNode::FArdaParameters{Texture, TextureExpected},
		    "upload texture"));
		const auto Uploaded =
		    UploadBuffer(Edit, FArdaMemoryUploadBufferNode::FArdaParameters{{}, BufferExpected}, "upload buffer");
		ASSERT_TRUE(Uploaded);
		const auto Buffer = Graph.FindOutput(Uploaded.GetHandle(), "Destination");
		auto TextureBytes = eastl::make_shared<eastl::vector<uint8_t>>();
		auto BufferBytes = eastl::make_shared<eastl::vector<uint8_t>>();
		const auto TextureReadback = ReadbackTexture(Edit,
		    FArdaMemoryReadbackTextureNode::FArdaParameters{Texture, TextureBytes},
		    "read texture");
		const auto BufferReadback =
		    ReadbackBuffer(Edit, FArdaMemoryReadbackBufferNode::FArdaParameters{Buffer, BufferBytes}, "read buffer");
		ASSERT_TRUE(TextureReadback);
		ASSERT_TRUE(BufferReadback);
		const auto Failing =
		    FailMemoryRecording(Edit, FArdaMemoryFailureNode::FArdaParameters{}, "fail after readback registration");
		ASSERT_TRUE(Failing);
		ASSERT_TRUE(Edit.Order(TextureReadback, Failing));
		ASSERT_TRUE(Edit.Order(BufferReadback, Failing));
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		FArdaGraphExecuteOptions ExecuteOptions;
		ExecuteOptions.mbParallelRecording = false;
		*TextureBytes = {253};
		*BufferBytes = {254};
		const auto Failed = Graph.Execute(ExecuteOptions);
		EXPECT_FALSE(Failed.mStatus);
		EXPECT_EQ(Failed.mSubmittedCommandListCount, 0u);
		EXPECT_TRUE(TextureBytes->empty());
		EXPECT_TRUE(BufferBytes->empty());
		FArdaGraphEdit RebuiltEdit(Graph);
		ASSERT_TRUE(RebuiltEdit.SetOptions(CompileOptions));
		const auto RebuiltTexture = MakeTestTexture(RebuiltEdit, "texture");
		ASSERT_TRUE(UploadTexture(RebuiltEdit,
		    FArdaMemoryUploadTextureParameters{RebuiltTexture, TextureExpected},
		    "upload texture"));
		const auto RebuiltBuffer =
		    UploadBuffer(RebuiltEdit, FArdaMemoryUploadBufferParameters{{}, BufferExpected}, "upload buffer")
		        .GetBuffer();
		ASSERT_TRUE(RebuiltBuffer);
		ASSERT_TRUE(ReadbackTexture(RebuiltEdit,
		    FArdaMemoryReadbackTextureParameters{RebuiltTexture, TextureBytes},
		    "read texture"));
		ASSERT_TRUE(ReadbackBuffer(RebuiltEdit,
		    FArdaMemoryReadbackBufferParameters{RebuiltBuffer, BufferBytes},
		    "read buffer"));
		ASSERT_TRUE(RebuiltEdit.Commit());
		const auto Recompiled = Graph.Compile();
		ASSERT_TRUE(Recompiled) << Recompiled.mMessage.c_str();
		const auto Repaired = Graph.Execute(ExecuteOptions);
		ASSERT_TRUE(Repaired.mStatus) << Repaired.mStatus.mMessage.c_str();
		EXPECT_EQ(*TextureBytes, TextureExpected);
		EXPECT_EQ(*BufferBytes, BufferExpected);
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaMemoryTexturesGpu, testing::Values("native-d3d12", "native-vulkan"));
}
