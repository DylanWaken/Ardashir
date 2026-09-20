#include "../../ArdaMemoryFacadeTest.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"
#include "ArdaBackend.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "ArdaTestBackend.h"

#include <gtest/gtest.h>
#include <cstring>
#include <limits>

namespace arda::memorynodes_tests
{
	using namespace arda;
	using namespace arda::memory_facade_tests;

	FArdaGraphBuffer MakeTestBuffer(FArdaGraphEdit& Edit,
	    const char* Name,
	    uint64_t ByteSize = 64,
	    EArdaRHIBufferUsage Usage = EArdaRHIBufferUsage::None)
	{
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = ByteSize;
		Desc.mUsage = Usage;
		const auto Created = AllocateBuffer(Edit, Desc, Name);
		EXPECT_TRUE(Created) << Edit.GetStatus().mMessage.c_str();
		return Created;
	}

	TEST(ArdaMemoryNodes, UploadDeclaresDistinctOutputsAndRejectsDuplicateNamesWithoutLeakingResources)
	{
		FArdaDependencyGraph Graph;
		ExpectRejectedOperation(Graph, UploadBuffer, FArdaMemoryUploadBufferParameters{}, "empty");
		ExpectRejectedOperation(Graph,
		    UploadBuffer,
		    FArdaMemoryUploadBufferParameters{{}, {1, 2}, std::numeric_limits<uint64_t>::max()},
		    "overflow");
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		FArdaGraphEdit Edit(Graph);
		FArdaMemoryUploadBufferParameters Parameters;
		Parameters.mBytes = {1, 2, 3, 4};
		Parameters.mDestinationOffset = 12;
		const auto Uploaded = UploadBuffer(Edit, Parameters, "upload");
		ASSERT_TRUE(Uploaded) << Edit.GetStatus().mMessage.c_str();
		EXPECT_FALSE(Parameters.mDestination);
		const auto Destination = Uploaded.GetBuffer();
		ASSERT_TRUE(Destination);
		EXPECT_EQ(Destination.GetHandle().mIndex, 0u);
		const auto* Resource = Graph.FindResource(Destination);
		ASSERT_NE(Resource, nullptr);
		EXPECT_EQ(Resource->mBuffer.mByteSize, 16u);
		EXPECT_EQ(Resource->mBuffer.mCpuAccess, EArdaRHICpuAccess::None);
		EXPECT_FALSE(Resource->mBuffer.mbTiled);
		const auto Again = UploadBuffer(Edit, Parameters);
		ASSERT_TRUE(Again);
		EXPECT_NE(Again.GetHandle(), Uploaded.GetHandle());
		EXPECT_NE(Again.GetBuffer().GetHandle(), Destination.GetHandle());
		Parameters.mBytes[0] = 9;
		++Parameters.mDestinationOffset;
		const auto Next = UploadBuffer(Edit, Parameters);
		ASSERT_TRUE(Next);
		EXPECT_EQ(Next.GetBuffer().GetHandle().mIndex, 2u);
		EXPECT_FALSE(UploadBuffer(Edit, Parameters, "upload"));
		EXPECT_FALSE(Edit.Commit());
		ASSERT_TRUE(Edit.Cancel());
		EXPECT_EQ(Graph.FindResource(Destination), nullptr);
		EXPECT_FALSE(Graph.FindNode("upload"));
	}

	TEST(ArdaMemoryNodes, CopyInfersOrdinaryGpuStorageAndResolvesTheSourceRemainder)
	{
		for (const bool TiledSource : {false, true})
		{
			SCOPED_TRACE(TiledSource);
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.GetStatus());
			FArdaRHIBufferDesc SourceDesc;
			SourceDesc.mByteSize = 128;
			SourceDesc.mStructureStride = 16;
			SourceDesc.mFormat = EArdaRHIFormat::R32UInt;
			SourceDesc.mUsage = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::Structured;
			SourceDesc.mCpuAccess = TiledSource ? EArdaRHICpuAccess::None : EArdaRHICpuAccess::Write;
			SourceDesc.mbTiled = TiledSource;
			const auto Source = AllocateBuffer(Edit, SourceDesc, "source");
			ASSERT_TRUE(Source);
			FArdaMemoryCopyBufferParameters Parameters;
			Parameters.mSource = Source.GetHandle();
			Parameters.mSourceOffset = 32;
			Parameters.mDestinationOffset = 16;
			const auto Copy = CopyBuffer(Edit, Parameters, "copy");
			ASSERT_TRUE(Copy) << Edit.GetStatus().mMessage.c_str();
			EXPECT_FALSE(Parameters.mDestination);
			const auto Output = Graph.FindOutput(Copy.GetHandle(), "Destination");
			const auto* Resource = Graph.FindResource(Output);
			ASSERT_NE(Resource, nullptr);
			EXPECT_EQ(Resource->mBuffer.mByteSize, 112u);
			EXPECT_EQ(Resource->mBuffer.mStructureStride, SourceDesc.mStructureStride);
			EXPECT_EQ(Resource->mBuffer.mFormat, SourceDesc.mFormat);
			EXPECT_EQ(Resource->mBuffer.mUsage, SourceDesc.mUsage);
			EXPECT_EQ(Resource->mBuffer.mCpuAccess, EArdaRHICpuAccess::None);
			EXPECT_FALSE(Resource->mBuffer.mbTiled);
			const auto Again = CopyBuffer(Edit, Parameters);
			ASSERT_TRUE(Again);
			EXPECT_NE(Again.GetHandle(), Copy.GetHandle());
			Parameters.mSourceOffset = 48;
			ASSERT_TRUE(Edit.Commit());
			ExpectRejectedOperation(Graph, CopyBuffer, Parameters, "copy");
		}
	}

	TEST(ArdaMemoryNodes, RejectsForeignStaleAndTextureHandlesAtAttachment)
	{
		FArdaDependencyGraph Graph;
		FArdaDependencyGraph Other;
		FArdaGraphEdit AbandonedEdit(Graph);
		ASSERT_TRUE(AbandonedEdit.GetStatus());
		const auto Stale = MakeTestBuffer(AbandonedEdit, "abandoned");
		ASSERT_TRUE(AbandonedEdit.Cancel());
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		FArdaGraphEdit OtherEdit(Other);
		ASSERT_TRUE(OtherEdit.GetStatus());
		const auto Buffer = MakeTestBuffer(Edit, "valid", 64, EArdaRHIBufferUsage::UnorderedAccess);
		const auto Foreign = MakeTestBuffer(OtherEdit, "foreign");
		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = TextureDesc.mHeight = 4;
		TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		const auto Texture = AllocateTexture(Edit, TextureDesc, "texture");
		ASSERT_TRUE(Texture);
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(Edit.Commit());
		for (const auto Invalid : {Stale.GetHandle(), Foreign.GetHandle(), Texture.GetHandle()})
		{
			ExpectRejectedOperation(Graph,
			    UploadBuffer,
			    FArdaMemoryUploadBufferNode::FArdaParameters{Invalid, {1, 2, 3, 4}},
			    "bad upload");
			ExpectRejectedOperation(Graph,
			    CopyBuffer,
			    FArdaMemoryCopyBufferNode::FArdaParameters{Invalid, Buffer, 4},
			    "bad source");
			ExpectRejectedOperation(Graph,
			    CopyBuffer,
			    FArdaMemoryCopyBufferNode::FArdaParameters{Buffer, Invalid, 4},
			    "bad destination");
			ExpectRejectedOperation(Graph,
			    ReadbackBuffer,
			    FArdaMemoryReadbackBufferNode::FArdaParameters{Invalid, Bytes},
			    "bad readback");
			ExpectRejectedOperation(Graph,
			    ClearBuffer,
			    FArdaMemoryClearBufferNode::FArdaParameters{Invalid, 0},
			    "bad clear");
		}
		ExpectRejectedOperation(Graph,
		    CopyBuffer,
		    FArdaMemoryCopyBufferNode::FArdaParameters{{}, Buffer, 4},
		    "null source");
		ExpectRejectedOperation(Graph,
		    ReadbackBuffer,
		    FArdaMemoryReadbackBufferNode::FArdaParameters{{}, Bytes},
		    "null source");
		ExpectRejectedOperation(Graph, ClearBuffer, FArdaMemoryClearBufferNode::FArdaParameters{}, "null destination");
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		ASSERT_TRUE(OtherEdit.Cancel());
	}

	TEST(ArdaMemoryNodes, UploadValidatesEmptyAndOutOfBoundsRanges)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		const auto Buffer = MakeTestBuffer(Edit, "buffer", 16);
		ASSERT_TRUE(Edit.Commit());
		ExpectRejectedOperation(Graph, UploadBuffer, FArdaMemoryUploadBufferNode::FArdaParameters{Buffer, {}}, "empty");
		ExpectRejectedOperation(Graph,
		    UploadBuffer,
		    FArdaMemoryUploadBufferNode::FArdaParameters{Buffer, {1}, 16},
		    "past end");
		ExpectRejectedOperation(Graph,
		    UploadBuffer,
		    FArdaMemoryUploadBufferNode::FArdaParameters{Buffer, {1, 2, 3, 4}, 13},
		    "overrun");
		ExpectRejectedOperation(Graph,
		    UploadBuffer,
		    FArdaMemoryUploadBufferNode::FArdaParameters{Buffer,
		        {1, 2, 3, 4},
		        std::numeric_limits<uint64_t>::max() - 1},
		    "overflow");
		FArdaGraphEdit Operations(Graph);
		const auto Valid = UploadBuffer(Operations,
		    FArdaMemoryUploadBufferNode::FArdaParameters{Buffer, {1, 2, 3, 4}, 12},
		    "exact end");
		ASSERT_TRUE(Valid) << Operations.GetStatus().mMessage.c_str();
		EXPECT_EQ(Graph.FindOutput(Valid.GetHandle(), "Destination"), Buffer);
		ASSERT_TRUE(Operations.Cancel());
	}

	TEST(ArdaMemoryNodes, CopyRejectsEmptyOverflowingAndSelfCopyRanges)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		const auto Source = MakeTestBuffer(Edit, "source", 32);
		const auto Destination = MakeTestBuffer(Edit, "destination", 16);
		ASSERT_TRUE(Edit.Commit());
		ExpectRejectedOperation(Graph,
		    CopyBuffer,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Source, Destination, 0},
		    "empty");
		ExpectRejectedOperation(Graph,
		    CopyBuffer,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Source, Destination, 8, 28},
		    "source overrun");
		ExpectRejectedOperation(Graph,
		    CopyBuffer,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Source, Destination, 8, 0, 12},
		    "destination overrun");
		ExpectRejectedOperation(Graph,
		    CopyBuffer,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Source, Destination},
		    "whole overrun");
		ExpectRejectedOperation(Graph,
		    CopyBuffer,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Source, Destination, ArdaRHIWholeBuffer, 32},
		    "empty remainder");
		ExpectRejectedOperation(Graph,
		    CopyBuffer,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Source,
		        {},
		        ArdaRHIWholeBuffer,
		        std::numeric_limits<uint64_t>::max()},
		    "invalid remainder");
		ExpectRejectedOperation(Graph,
		    CopyBuffer,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Source, {}, 8, 0, std::numeric_limits<uint64_t>::max() - 3},
		    "destination overflow");
		ExpectRejectedOperation(Graph,
		    CopyBuffer,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Source,
		        Destination,
		        8,
		        std::numeric_limits<uint64_t>::max() - 3},
		    "source overflow");
		ExpectRejectedOperation(Graph,
		    CopyBuffer,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Source, Source, 8, 0, 4},
		    "self overlap");
		ExpectRejectedOperation(Graph,
		    CopyBuffer,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Source, Source, 8, 0, 16},
		    "self disjoint");
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		FArdaGraphEdit Operations(Graph);
		const auto Valid = CopyBuffer(Operations,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Source, Destination, ArdaRHIWholeBuffer, 24, 8},
		    "exact end");
		ASSERT_TRUE(Valid) << Operations.GetStatus().mMessage.c_str();
		EXPECT_EQ(Graph.FindOutput(Valid.GetHandle(), "Destination"), Destination);
		const auto Automatic =
		    CopyBuffer(Operations, FArdaMemoryCopyBufferNode::FArdaParameters{Source, {}, 8}, "automatic");
		ASSERT_TRUE(Automatic);
		EXPECT_EQ(Graph.FindOutput(Automatic.GetHandle(), "Destination").mIndex, 2u);
		ASSERT_TRUE(Operations.Cancel());
	}

	TEST(ArdaMemoryNodes, ReadbackValidatesTheDestinationAndRangeAndRecordsDistinctInvocations)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		const auto Source = MakeTestBuffer(Edit, "source", 32);
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(Edit.Commit());
		ExpectRejectedOperation(Graph,
		    ReadbackBuffer,
		    FArdaMemoryReadbackBufferNode::FArdaParameters{Source, {}},
		    "null sink");
		ExpectRejectedOperation(Graph,
		    ReadbackBuffer,
		    FArdaMemoryReadbackBufferNode::FArdaParameters{Source, Bytes, 0, 0},
		    "empty");
		ExpectRejectedOperation(Graph,
		    ReadbackBuffer,
		    FArdaMemoryReadbackBufferNode::FArdaParameters{Source, Bytes, 32},
		    "empty remainder");
		ExpectRejectedOperation(Graph,
		    ReadbackBuffer,
		    FArdaMemoryReadbackBufferNode::FArdaParameters{Source, Bytes, 28, 8},
		    "overrun");
		ExpectRejectedOperation(Graph,
		    ReadbackBuffer,
		    FArdaMemoryReadbackBufferNode::FArdaParameters{Source, Bytes, std::numeric_limits<uint64_t>::max() - 3, 8},
		    "overflow");
		FArdaGraphEdit Operations(Graph);
		const auto Readback =
		    ReadbackBuffer(Operations, FArdaMemoryReadbackBufferNode::FArdaParameters{Source, Bytes, 16}, "readback");
		ASSERT_TRUE(Readback) << Operations.GetStatus().mMessage.c_str();
		const auto Again =
		    ReadbackBuffer(Operations, FArdaMemoryReadbackBufferNode::FArdaParameters{Source, Bytes, 16});
		ASSERT_TRUE(Again);
		EXPECT_NE(Again.GetHandle(), Readback.GetHandle());
		ASSERT_TRUE(Operations.Commit());
		ExpectRejectedOperation(Graph,
		    ReadbackBuffer,
		    FArdaMemoryReadbackBufferNode::FArdaParameters{Source, Bytes, 12},
		    "readback");
		ExpectRejectedOperation(Graph,
		    ReadbackBuffer,
		    FArdaMemoryReadbackBufferNode::FArdaParameters{Source, Bytes, 16, 4},
		    "readback");
		ExpectRejectedOperation(Graph,
		    ReadbackBuffer,
		    FArdaMemoryReadbackBufferNode::FArdaParameters{Source, eastl::make_shared<eastl::vector<uint8_t>>(), 16},
		    "readback");
	}

	TEST(ArdaMemoryNodes, ClearRequiresGpuUavStorageAndWholeUint32Elements)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		const auto NoUav = MakeTestBuffer(Edit, "no uav");
		const auto Unaligned = MakeTestBuffer(Edit, "unaligned", 7, EArdaRHIBufferUsage::UnorderedAccess);
		FArdaRHIBufferDesc HostDesc;
		HostDesc.mByteSize = 64;
		HostDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		HostDesc.mCpuAccess = EArdaRHICpuAccess::Write;
		const auto Buffer = MakeTestBuffer(Edit, "gpu", 64, EArdaRHIBufferUsage::UnorderedAccess);
		ASSERT_TRUE(Edit.Commit());
		{
			FArdaGraphEdit InvalidEdit(Graph);
			EXPECT_FALSE(AllocateBuffer(InvalidEdit, HostDesc, "host"));
			EXPECT_EQ(InvalidEdit.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
			EXPECT_FALSE(InvalidEdit.Commit());
			ASSERT_TRUE(InvalidEdit.Cancel());
		}
		ExpectRejectedOperation(Graph, ClearBuffer, FArdaMemoryClearBufferNode::FArdaParameters{NoUav, 7}, "no uav");
		ExpectRejectedOperation(Graph,
		    ClearBuffer,
		    FArdaMemoryClearBufferNode::FArdaParameters{Unaligned, 7},
		    "unaligned");
		FArdaGraphEdit Operations(Graph);
		const auto Clear = ClearBuffer(Operations, FArdaMemoryClearBufferNode::FArdaParameters{Buffer, 7}, "clear");
		ASSERT_TRUE(Clear) << Operations.GetStatus().mMessage.c_str();
		const auto Again = ClearBuffer(Operations, FArdaMemoryClearBufferNode::FArdaParameters{Buffer, 7});
		ASSERT_TRUE(Again);
		EXPECT_NE(Again.GetHandle(), Clear.GetHandle());
		ASSERT_TRUE(Operations.Commit());
		ExpectRejectedOperation(Graph, ClearBuffer, FArdaMemoryClearBufferNode::FArdaParameters{Buffer, 8}, "clear");
	}

	TEST(ArdaMemoryNodes, EnforcesCpuHeapDirectionsAndDropsVolatileAllocationPolicy)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		const auto Gpu = MakeTestBuffer(Edit, "gpu", 64);
		FArdaRHIBufferDesc HostDescription;
		HostDescription.mByteSize = 64;
		HostDescription.mCpuAccess = EArdaRHICpuAccess::Write;
		HostDescription.mUsage = EArdaRHIBufferUsage::Constant | EArdaRHIBufferUsage::Volatile;
		HostDescription.mMaxVersions = 3;
		HostDescription.mbKeepInitialState = true;
		HostDescription.mInitialState = EArdaRHIResourceState::CopySource;
		const auto Upload = AllocateBuffer(Edit, HostDescription, "upload heap");
		ASSERT_TRUE(Upload);
		HostDescription.mCpuAccess = EArdaRHICpuAccess::Read;
		HostDescription.mUsage = EArdaRHIBufferUsage::None;
		HostDescription.mMaxVersions = 0;
		HostDescription.mInitialState = EArdaRHIResourceState::CopyDest;
		const auto Readback = AllocateBuffer(Edit, HostDescription, "readback heap");
		ASSERT_TRUE(Readback);
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();

		ASSERT_TRUE(Edit.Commit());
		ExpectRejectedOperation(Graph,
		    UploadBuffer,
		    FArdaMemoryUploadBufferNode::FArdaParameters{Upload.GetHandle(), {1}},
		    "host upload");
		ExpectRejectedOperation(Graph,
		    UploadBuffer,
		    FArdaMemoryUploadBufferNode::FArdaParameters{Readback.GetHandle(), {1}},
		    "host readback");
		ExpectRejectedOperation(Graph,
		    CopyBuffer,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Readback.GetHandle(), Gpu, 4},
		    "wrong source");
		ExpectRejectedOperation(Graph,
		    CopyBuffer,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Gpu, Upload.GetHandle(), 4},
		    "wrong destination");
		ExpectRejectedOperation(Graph,
		    ReadbackBuffer,
		    FArdaMemoryReadbackBufferNode::FArdaParameters{Readback.GetHandle(), Bytes},
		    "wrong readback");
		FArdaGraphEdit Operations(Graph);
		ASSERT_TRUE(CopyBuffer(Operations,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Gpu, Readback.GetHandle(), 4},
		    "readback destination"));
		ASSERT_TRUE(ReadbackBuffer(Operations,
		    FArdaMemoryReadbackBufferNode::FArdaParameters{Upload.GetHandle(), Bytes},
		    "upload source"));

		const auto Copy =
		    CopyBuffer(Operations, FArdaMemoryCopyBufferNode::FArdaParameters{Upload.GetHandle(), {}}, "inferred gpu");
		ASSERT_TRUE(Copy) << Operations.GetStatus().mMessage.c_str();
		const auto* Output = Graph.FindResource(Graph.FindOutput(Copy.GetHandle(), "Destination"));
		ASSERT_NE(Output, nullptr);
		EXPECT_EQ(Output->mBuffer.mUsage, EArdaRHIBufferUsage::Constant);
		EXPECT_EQ(Output->mBuffer.mCpuAccess, EArdaRHICpuAccess::None);
		EXPECT_EQ(Output->mBuffer.mMaxVersions, 0u);
		EXPECT_FALSE(Output->mBuffer.mbKeepInitialState);
		EXPECT_EQ(Output->mBuffer.mInitialState, EArdaRHIResourceState::Common);
		ASSERT_TRUE(Operations.Cancel());
	}

	class FArdaMemoryNodesGpu : public testing::TestWithParam<const char*>
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

	TEST_P(FArdaMemoryNodesGpu, UploadSnapshotCopiesRangesAndPublishesOnlyWhenWaitRetiresTheFrame)
	{
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		FArdaInductorOptions Options;
		Options.mFramesInFlight = 2;
		ASSERT_TRUE(Edit.SetOptions(Options));
		FArdaMemoryUploadBufferParameters UploadParameters;
		for (uint8_t Byte = 0; Byte < 48; ++Byte)
		{
			UploadParameters.mBytes.push_back(Byte);
		}
		const auto Uploaded = UploadBuffer(Edit, UploadParameters, "upload");
		ASSERT_TRUE(Uploaded) << Edit.GetStatus().mMessage.c_str();
		const auto Source = Graph.FindOutput(Uploaded.GetHandle(), "Destination");
		ASSERT_TRUE(Source);
		UploadParameters.mBytes.assign(48, 255);
		const auto Copied = CopyBuffer(Edit,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Source, {}, ArdaRHIWholeBuffer, 8},
		    "copy source remainder");
		ASSERT_TRUE(Copied) << Edit.GetStatus().mMessage.c_str();
		const auto Destination = Graph.FindOutput(Copied.GetHandle(), "Destination");
		ASSERT_TRUE(Destination);
		auto Readback = eastl::make_shared<eastl::vector<uint8_t>>();
		auto SourceSlice = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(ReadbackBuffer(Edit,
		    FArdaMemoryReadbackBufferNode::FArdaParameters{Destination, Readback, 4},
		    "read copied remainder"));
		ASSERT_TRUE(ReadbackBuffer(Edit,
		    FArdaMemoryReadbackBufferNode::FArdaParameters{Source, SourceSlice, 4, 8},
		    "read source slice"));
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		for (uint32_t Frame = 0; Frame < 3; ++Frame)
		{
			SCOPED_TRACE(Frame);
			*Readback = {254};
			*SourceSlice = {253};
			const auto Submitted = Graph.Submit();
			ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
			ASSERT_TRUE(mDevice->WaitForIdle());
			EXPECT_EQ(*Readback, (eastl::vector<uint8_t>{254}));
			EXPECT_EQ(*SourceSlice, (eastl::vector<uint8_t>{253}));
			const auto Completed = Graph.Wait(Submitted.mValue);
			ASSERT_TRUE(Completed.mStatus) << Completed.mStatus.mMessage.c_str();
			ASSERT_EQ(Readback->size(), 36u);
			for (size_t Byte = 0; Byte < Readback->size(); ++Byte)
			{
				EXPECT_EQ((*Readback)[Byte], Byte + 12) << "byte " << Byte;
			}
			EXPECT_EQ(*SourceSlice, (eastl::vector<uint8_t>{4, 5, 6, 7, 8, 9, 10, 11}));
		}
	}

	TEST_P(FArdaMemoryNodesGpu, PartialUploadsAndCopiesPreserveUntouchedDestinationBytes)
	{
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		const auto Initial = UploadBuffer(Edit,
		    FArdaMemoryUploadBufferNode::FArdaParameters{{}, eastl::vector<uint8_t>(64, 17)},
		    "initialize");
		ASSERT_TRUE(Initial) << Edit.GetStatus().mMessage.c_str();
		const auto Destination = Graph.FindOutput(Initial.GetHandle(), "Destination");
		ASSERT_TRUE(UploadBuffer(Edit,
		    FArdaMemoryUploadBufferNode::FArdaParameters{Destination, {21, 22, 23, 24, 25, 26, 27, 28}, 16},
		    "patch upload"));
		const auto CopySource = UploadBuffer(Edit,
		    FArdaMemoryUploadBufferNode::FArdaParameters{{},
		        {40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55}},
		    "copy source");
		ASSERT_TRUE(CopySource);
		ASSERT_TRUE(CopyBuffer(Edit,
		    FArdaMemoryCopyBufferNode::FArdaParameters{Graph.FindOutput(CopySource.GetHandle(), "Destination"),
		        Destination,
		        8,
		        4,
		        36},
		    "patch copy"));
		auto Readback = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(
		    ReadbackBuffer(Edit, FArdaMemoryReadbackBufferNode::FArdaParameters{Destination, Readback}, "read all"));
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		for (uint32_t Frame = 0; Frame < 3; ++Frame)
		{
			SCOPED_TRACE(Frame);
			const auto Executed = Graph.Execute();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			ASSERT_EQ(Readback->size(), 64u);
			for (size_t Byte = 0; Byte < Readback->size(); ++Byte)
			{
				const uint8_t Expected = Byte >= 16 && Byte < 24 ? uint8_t(21 + Byte - 16)
				    : Byte >= 36 && Byte < 44                    ? uint8_t(44 + Byte - 36)
				                                                 : uint8_t(17);
				EXPECT_EQ((*Readback)[Byte], Expected) << "byte " << Byte;
			}
		}
	}

	TEST_P(FArdaMemoryNodesGpu, ClearWritesTheUint32PatternAcrossTheBuffer)
	{
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		const auto Buffer = MakeTestBuffer(Edit, "clear target", 64, EArdaRHIBufferUsage::UnorderedAccess);
		constexpr uint32_t Value = 0x1234abcd;
		ASSERT_TRUE(ClearBuffer(Edit, FArdaMemoryClearBufferNode::FArdaParameters{Buffer, Value}, "clear"));
		auto Readback = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(ReadbackBuffer(Edit, FArdaMemoryReadbackBufferNode::FArdaParameters{Buffer, Readback}, "readback"));
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		for (uint32_t Frame = 0; Frame < 3; ++Frame)
		{
			const auto Executed = Graph.Execute();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			ASSERT_EQ(Readback->size(), 64u);
			for (size_t Byte = 0; Byte < Readback->size(); Byte += sizeof(Value))
			{
				uint32_t Actual = 0;
				std::memcpy(&Actual, Readback->data() + Byte, sizeof(Actual));
				EXPECT_EQ(Actual, Value) << "frame " << Frame << ", byte " << Byte;
			}
		}
	}

	TEST_P(FArdaMemoryNodesGpu, CopiesAndReadsUnalignedByteRangesWithoutReadingUninitializedBytes)
	{
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		const auto Upload = UploadBuffer(Edit,
		    FArdaMemoryUploadBufferNode::FArdaParameters{{}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 1},
		    "upload odd size");
		ASSERT_TRUE(Upload) << Edit.GetStatus().mMessage.c_str();
		const auto Source = Graph.FindOutput(Upload.GetHandle(), "Destination");
		const auto Copy =
		    CopyBuffer(Edit, FArdaMemoryCopyBufferNode::FArdaParameters{Source, {}, 5, 3, 3}, "copy odd range");
		ASSERT_TRUE(Copy) << Edit.GetStatus().mMessage.c_str();
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(ReadbackBuffer(Edit,
		    FArdaMemoryReadbackBufferNode::FArdaParameters{Graph.FindOutput(Copy.GetHandle(), "Destination"),
		        Bytes,
		        3,
		        5},
		    "read initialized range"));
		ASSERT_TRUE(Edit.Commit());
		const auto Compiled = Graph.Compile();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		const auto Executed = Graph.Execute();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		EXPECT_EQ(*Bytes, (eastl::vector<uint8_t>{2, 3, 4, 5, 6}));
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaMemoryNodesGpu, testing::Values("native-d3d12", "native-vulkan"));
}
