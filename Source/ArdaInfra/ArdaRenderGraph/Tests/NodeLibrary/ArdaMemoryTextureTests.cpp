#include "NodeLibrary/ArdaMemoryNodes.h"
#include "NodeLibrary/ArdaMemoryTextureNodes.h"
#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ArdaTestBackend.h"

#include <gtest/gtest.h>
#include <limits>
#include <string>

namespace
{
	using namespace arda;

	FArdaRHITextureDesc MakeTextureDesc(uint32_t Width = 7, uint32_t Height = 3)
	{
		FArdaRHITextureDesc Desc;
		Desc.mWidth = Width;
		Desc.mHeight = Height;
		Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		return Desc;
	}

	FArdaDependencyResourceHandle CreateTexture(FArdaDependencyGraph& Graph,
	    const char* Name,
	    const FArdaRHITextureDesc& Desc = MakeTextureDesc())
	{
		const auto Created = Graph.CreateTexture(Name, Desc);
		EXPECT_TRUE(Created) << Created.mStatus.mMessage.c_str();
		return Created.mValue;
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
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Stale = CreateTexture(Graph, "abandoned");
		ASSERT_TRUE(Graph.CancelGraphEdit());
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Other.BeginGraphEdit());
		const auto Valid = CreateTexture(Graph, "valid");
		const auto Foreign = CreateTexture(Other, "foreign");
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 256;
		const auto Buffer = Graph.CreateBuffer("buffer", BufferDesc);
		ASSERT_TRUE(Buffer);
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		for (const auto Invalid : {FArdaDependencyResourceHandle{}, Stale, Foreign, Buffer.mValue})
		{
			EXPECT_FALSE(
			    Graph.AttachOrFind<FArdaMemoryUploadTextureNode>("bad upload", {Invalid, MakeTextureBytes(84)}));
			EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryReadbackTextureNode>("bad readback", {Invalid, Bytes}));
			EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("bad source", {Invalid, Valid}));
			EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("bad destination", {Valid, Invalid}));
			EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryClearTextureNode>("bad clear", {Invalid}));
		}
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		ASSERT_TRUE(Graph.CancelGraphEdit());
		ASSERT_TRUE(Other.CancelGraphEdit());
	}

	TEST(ArdaMemoryTextures, ValidatesExactPackedSizeAndStrictSliceBounds)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		auto Desc = MakeTextureDesc();
		Desc.mMipLevels = 2;
		const auto Texture = CreateTexture(Graph, "texture", Desc);
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadTextureNode>("empty", {Texture, {}}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadTextureNode>("short", {Texture, MakeTextureBytes(83)}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadTextureNode>("long", {Texture, MakeTextureBytes(85)}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryReadbackTextureNode>("null sink", {Texture, {}}));
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
			EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadTextureNode>("invalid upload",
			    {Texture, MakeTextureBytes(84), Slice}));
			EXPECT_FALSE(
			    Graph.AttachOrFind<FArdaMemoryReadbackTextureNode>("invalid readback", {Texture, Bytes, Slice}));
		}
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		FArdaRHITextureSlice Mip;
		Mip.mMipLevel = 1;
		const auto Upload =
		    Graph.AttachOrFind<FArdaMemoryUploadTextureNode>("mip upload", {Texture, MakeTextureBytes(12), Mip});
		ASSERT_TRUE(Upload) << Upload.mStatus.mMessage.c_str();
		EXPECT_EQ(Graph.FindOutput(Upload.mValue, "Destination"), Texture);
		const auto Again =
		    Graph.AttachOrFind<FArdaMemoryUploadTextureNode>("mip upload", {Texture, MakeTextureBytes(12), Mip});
		ASSERT_TRUE(Again);
		EXPECT_EQ(Again.mValue, Upload.mValue);
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadTextureNode>("mip upload",
		    {Texture, eastl::vector<uint8_t>(12, 0), Mip}));
		ASSERT_TRUE(Graph.CancelGraphEdit());
	}

	TEST(ArdaMemoryTextures, RejectsDepthMultisampleAndCompressedHostTransfers)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
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
			const auto Texture = CreateTexture(Graph, ("unsupported " + std::to_string(Case)).c_str(), Desc);
			EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadTextureNode>("unsupported upload",
			    {Texture, MakeTextureBytes(64)}));
			EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryReadbackTextureNode>("unsupported readback", {Texture, Bytes}));
		}
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		ASSERT_TRUE(Graph.CancelGraphEdit());
	}

	TEST(ArdaMemoryTextures, CopyValidatesFormatsExtentsAndSubresourceIdentity)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		auto Desc = MakeTextureDesc(8, 4);
		Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
		Desc.mArraySize = 2;
		const auto Source = CreateTexture(Graph, "source", Desc);
		const auto Destination = CreateTexture(Graph, "destination", Desc);
		Desc.mFormat = EArdaRHIFormat::R32Float;
		const auto DifferentFormat = CreateTexture(Graph, "different format", Desc);
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("self", {Source, Source}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("format", {Source, DifferentFormat}));
		FArdaRHITextureSlice Region;
		Region.mWidth = Region.mHeight = 2;
		FArdaRHITextureSlice TooSmall = Region;
		TooSmall.mWidth = 1;
		EXPECT_FALSE(
		    Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("unequal extents", {Source, Destination, Region, TooSmall}));
		FArdaRHITextureSlice PastEnd = Region;
		PastEnd.mX = 7;
		EXPECT_FALSE(
		    Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("source overrun", {Source, Destination, PastEnd, Region}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("destination overrun",
		    {Source, Destination, Region, PastEnd}));
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		FArdaRHITextureSlice Layer;
		Layer.mArraySlice = 1;
		const auto Copy =
		    Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("different layers", {Source, Source, {}, Layer});
		ASSERT_TRUE(Copy) << Copy.mStatus.mMessage.c_str();
		EXPECT_EQ(Graph.FindOutput(Copy.mValue, "Destination"), Source);
		const auto Again =
		    Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("different layers", {Source, Source, {}, Layer});
		ASSERT_TRUE(Again);
		EXPECT_EQ(Again.mValue, Copy.mValue);
		ASSERT_TRUE(Graph.CancelGraphEdit());
	}

	TEST(ArdaMemoryTextures, CopyRejectsDifferentDimensionClassesEvenWhenExtentsMatch)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		auto Desc = MakeTextureDesc(8, 1);
		const auto Texture2D = CreateTexture(Graph, "2d", Desc);
		Desc.mDimension = EArdaRHITextureDimension::Texture1D;
		const auto Texture1D = CreateTexture(Graph, "1d", Desc);
		Desc.mDimension = EArdaRHITextureDimension::Texture3D;
		const auto Texture3D = CreateTexture(Graph, "3d", Desc);
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("1d to 2d", {Texture1D, Texture2D}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("2d to 1d", {Texture2D, Texture1D}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("2d to 3d", {Texture2D, Texture3D}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("3d to 2d", {Texture3D, Texture2D}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("1d to 3d", {Texture1D, Texture3D}));
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		ASSERT_TRUE(Graph.CancelGraphEdit());
	}

	TEST(ArdaMemoryTextures, FootprintsRetainOnlyFinalRowTexelsAndAlignWholeTexels)
	{
		auto Desc = MakeTextureDesc(13, 7);
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
		ASSERT_TRUE(Graph.BeginGraphEdit());
		auto Desc = MakeTextureDesc(8, 4);
		const auto NoRenderTarget = CreateTexture(Graph, "no render target", Desc);
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryClearTextureNode>("missing usage", {NoRenderTarget}));
		Desc.mUsage = EArdaRHITextureUsage::RenderTarget;
		Desc.mFormat = EArdaRHIFormat::RGBA8UInt;
		const auto Integer = CreateTexture(Graph, "integer target", Desc);
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryClearTextureNode>("integer", {Integer}));
		Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
		Desc.mArraySize = 2;
		Desc.mMipLevels = 2;
		const auto Target = CreateTexture(Graph, "target", Desc);
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
			EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryClearTextureNode>("invalid range", {Target, {}, Range}));
		}
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		const auto Clear = Graph.AttachOrFind<FArdaMemoryClearTextureNode>("valid", {Target, {1, 0, 0, 1}});
		ASSERT_TRUE(Clear) << Clear.mStatus.mMessage.c_str();
		EXPECT_EQ(Graph.FindOutput(Clear.mValue, "Destination"), Target);
		ASSERT_TRUE(Graph.CancelGraphEdit());
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
			ASSERT_TRUE(Graph.BeginGraphEdit());
			FArdaInductorOptions Options;
			Options.mFramesInFlight = 2;
			ASSERT_TRUE(Graph.SetOptions(Options));
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
			const auto Source = CreateTexture(Graph, "source", Desc);
			const auto Destination = CreateTexture(Graph, "destination", Desc);
			const auto Expected = MakeTextureBytes(ByteCount);
			FArdaMemoryUploadTextureParameters UploadParameters{Source, Expected, Slice};
			const auto Upload = Graph.AttachOrFind<FArdaMemoryUploadTextureNode>("upload", UploadParameters);
			ASSERT_TRUE(Upload) << Upload.mStatus.mMessage.c_str();
			UploadParameters.mBytes.assign(ByteCount, 255);
			const auto Copy =
			    Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("copy", {Source, Destination, Slice, Slice});
			ASSERT_TRUE(Copy) << Copy.mStatus.mMessage.c_str();
			auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
			ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryReadbackTextureNode>("readback", {Destination, Bytes, Slice}));
			const auto Compiled = Graph.EndGraphEdit();
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
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Destination = CreateTexture(Graph, "destination", MakeTextureDesc(7, 5));
		const auto Source = CreateTexture(Graph, "source", MakeTextureDesc(3, 3));
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryUploadTextureNode>("initialize destination",
		    {Destination, eastl::vector<uint8_t>(7 * 5 * 4, 17)}));
		const auto SourceBytes = MakeTextureBytes(3 * 3 * 4);
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryUploadTextureNode>("initialize source", {Source, SourceBytes}));
		FArdaRHITextureSlice UploadSlice;
		UploadSlice.mX = 1;
		UploadSlice.mY = 1;
		UploadSlice.mWidth = UploadSlice.mHeight = 2;
		const auto PatchBytes = MakeTextureBytes(16);
		ASSERT_TRUE(
		    Graph.AttachOrFind<FArdaMemoryUploadTextureNode>("patch upload", {Destination, PatchBytes, UploadSlice}));
		FArdaRHITextureSlice SourceSlice;
		SourceSlice.mX = SourceSlice.mY = 1;
		SourceSlice.mWidth = SourceSlice.mHeight = 2;
		FArdaRHITextureSlice DestinationSlice = SourceSlice;
		DestinationSlice.mX = 4;
		DestinationSlice.mY = 3;
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("patch copy",
		    {Source, Destination, SourceSlice, DestinationSlice}));
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		auto RegionBytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryReadbackTextureNode>("read full texture", {Destination, Bytes}));
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryReadbackTextureNode>("read copied region",
		    {Destination, RegionBytes, DestinationSlice}));
		const auto Compiled = Graph.EndGraphEdit();
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
		ASSERT_TRUE(Graph.BeginGraphEdit());
		auto Desc = MakeTextureDesc(8, 4);
		Desc.mUsage = EArdaRHITextureUsage::RenderTarget;
		Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
		Desc.mMipLevels = 2;
		Desc.mArraySize = 2;
		const auto Texture = CreateTexture(Graph, "target", Desc);
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryClearTextureNode>("initialize red", {Texture, {1, 0, 0, 1}}));
		FArdaRHITextureSubresourceRange SelectedRange;
		SelectedRange.mBaseMipLevel = 1;
		SelectedRange.mMipLevelCount = 1;
		SelectedRange.mBaseArraySlice = 1;
		SelectedRange.mArraySliceCount = 1;
		ASSERT_TRUE(
		    Graph.AttachOrFind<FArdaMemoryClearTextureNode>("selected green", {Texture, {0, 1, 0, 1}, SelectedRange}));
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
				ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryReadbackTextureNode>(Name.c_str(), {Texture, Bytes, Slice}));
				Outputs.push_back(Bytes);
			}
		}
		const auto Compiled = Graph.EndGraphEdit();
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
		ASSERT_TRUE(Graph.BeginGraphEdit());
		auto Desc = MakeTextureDesc();
		Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
		Desc.mArraySize = 2;
		const auto Texture = CreateTexture(Graph, "array", Desc);
		const auto Expected = MakeTextureBytes(84);
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryUploadTextureNode>("upload first layer", {Texture, Expected}));
		FArdaRHITextureSlice Layer;
		Layer.mArraySlice = 1;
		ASSERT_TRUE(
		    Graph.AttachOrFind<FArdaMemoryCopyTextureNode>("copy to second layer", {Texture, Texture, {}, Layer}));
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryReadbackTextureNode>("read second layer", {Texture, Bytes, Layer}));
		const auto Compiled = Graph.EndGraphEdit();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		const auto Executed = Graph.Execute();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		EXPECT_EQ(*Bytes, Expected);
	}

	TEST_P(FArdaMemoryTexturesGpu, ConstantBuffersSupportOwnedUploadsCopiesAndReadbacks)
	{
		FArdaDependencyGraph Graph(mDevice);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 256;
		Desc.mUsage = EArdaRHIBufferUsage::Constant;
		const auto Source = Graph.CreateBuffer("uniform source", Desc);
		const auto Destination = Graph.CreateBuffer("uniform destination", Desc);
		ASSERT_TRUE(Source);
		ASSERT_TRUE(Destination);
		const auto Expected = MakeTextureBytes(256);
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("upload constants", {Source.mValue, Expected}));
		ASSERT_TRUE(
		    Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("copy constants", {Source.mValue, Destination.mValue}));
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("read constants", {Destination.mValue, Bytes}));
		const auto Compiled = Graph.EndGraphEdit();
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

	TEST_P(FArdaMemoryTexturesGpu, FailedFramesInvalidateTextureAndBufferReadbacksAndPermitReruns)
	{
		FArdaDependencyGraph Graph(mDevice);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions CompileOptions;
		CompileOptions.mbEnableCopyQueue = false;
		ASSERT_TRUE(Graph.SetOptions(CompileOptions));
		const auto Texture = CreateTexture(Graph, "texture");
		const auto TextureExpected = MakeTextureBytes(84);
		const auto BufferExpected = MakeTextureBytes(32);
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryUploadTextureNode>("upload texture", {Texture, TextureExpected}));
		const auto Uploaded = Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("upload buffer", {{}, BufferExpected});
		ASSERT_TRUE(Uploaded);
		const auto Buffer = Graph.FindOutput(Uploaded.mValue, "Destination");
		auto TextureBytes = eastl::make_shared<eastl::vector<uint8_t>>();
		auto BufferBytes = eastl::make_shared<eastl::vector<uint8_t>>();
		const auto TextureReadback =
		    Graph.AttachOrFind<FArdaMemoryReadbackTextureNode>("read texture", {Texture, TextureBytes});
		const auto BufferReadback =
		    Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("read buffer", {Buffer, BufferBytes});
		ASSERT_TRUE(TextureReadback);
		ASSERT_TRUE(BufferReadback);
		const auto Failing = Graph.AttachOrFind<FArdaMemoryFailureNode>("fail after readback registration", {});
		ASSERT_TRUE(Failing);
		ASSERT_TRUE(Graph.AddDependency(TextureReadback.mValue, Failing.mValue));
		ASSERT_TRUE(Graph.AddDependency(BufferReadback.mValue, Failing.mValue));
		const auto Compiled = Graph.EndGraphEdit();
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
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Graph.RemoveNode(Failing.mValue));
		const auto Recompiled = Graph.EndGraphEdit();
		ASSERT_TRUE(Recompiled) << Recompiled.mMessage.c_str();
		const auto Repaired = Graph.Execute(ExecuteOptions);
		ASSERT_TRUE(Repaired.mStatus) << Repaired.mStatus.mMessage.c_str();
		EXPECT_EQ(*TextureBytes, TextureExpected);
		EXPECT_EQ(*BufferBytes, BufferExpected);
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaMemoryTexturesGpu, testing::Values("native-d3d12", "native-vulkan"));
}
