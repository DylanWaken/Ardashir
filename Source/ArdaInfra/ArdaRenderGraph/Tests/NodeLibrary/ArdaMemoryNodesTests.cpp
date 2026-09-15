#include "NodeLibrary/ArdaMemoryNodes.h"
#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ArdaTestBackend.h"

#include <gtest/gtest.h>
#include <cstring>
#include <limits>

namespace
{
	using namespace arda;

	FArdaDependencyResourceHandle CreateBuffer(FArdaDependencyGraph& Graph,
	    const char* Name,
	    uint64_t ByteSize = 64,
	    EArdaRHIBufferUsage Usage = EArdaRHIBufferUsage::None)
	{
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = ByteSize;
		Desc.mUsage = Usage;
		const auto Created = Graph.CreateBuffer(Name, Desc);
		EXPECT_TRUE(Created) << Created.mStatus.mMessage.c_str();
		return Created.mValue;
	}

	TEST(ArdaMemoryNodes, UploadDeclaresAnOutputAndRejectsChangesWithoutLeakingResources)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("empty", {}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("overflow",
		    {{}, {1, 2}, std::numeric_limits<uint64_t>::max()}));
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());

		FArdaMemoryUploadBufferParameters Parameters;
		Parameters.mBytes = {1, 2, 3, 4};
		Parameters.mDestinationOffset = 12;
		const auto Uploaded = Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("upload", Parameters);
		ASSERT_TRUE(Uploaded) << Uploaded.mStatus.mMessage.c_str();
		EXPECT_FALSE(Parameters.mDestination);
		const auto Destination = Graph.FindOutput(Uploaded.mValue, "Destination");
		ASSERT_TRUE(Destination);
		EXPECT_EQ(Destination.mIndex, 0u);
		const auto* Resource = Graph.FindResource(Destination);
		ASSERT_NE(Resource, nullptr);
		EXPECT_EQ(Resource->mBuffer.mByteSize, 16u);
		EXPECT_EQ(Resource->mBuffer.mCpuAccess, EArdaRHICpuAccess::None);
		EXPECT_FALSE(Resource->mBuffer.mbTiled);

		const auto Again = Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("upload", Parameters);
		ASSERT_TRUE(Again);
		EXPECT_EQ(Again.mValue, Uploaded.mValue);
		EXPECT_EQ(Graph.FindOutput(Again.mValue, "Destination"), Destination);
		Parameters.mBytes[0] = 9;
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("upload", Parameters));
		Parameters.mBytes[0] = 1;
		++Parameters.mDestinationOffset;
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("upload", Parameters));
		const auto Next = Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("next", {{}, {5, 6, 7, 8}});
		ASSERT_TRUE(Next);
		EXPECT_EQ(Graph.FindOutput(Next.mValue, "Destination").mIndex, 1u);
		ASSERT_TRUE(Graph.CancelGraphEdit());
		EXPECT_EQ(Graph.FindResource(Destination), nullptr);
		EXPECT_FALSE(Graph.FindNode("upload"));
	}

	TEST(ArdaMemoryNodes, CopyInfersOrdinaryGpuStorageAndResolvesTheSourceRemainder)
	{
		for (const bool TiledSource : {false, true})
		{
			SCOPED_TRACE(TiledSource);
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			FArdaRHIBufferDesc SourceDesc;
			SourceDesc.mByteSize = 128;
			SourceDesc.mStructureStride = 16;
			SourceDesc.mFormat = EArdaRHIFormat::R32UInt;
			SourceDesc.mUsage = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::Structured;
			SourceDesc.mCpuAccess = TiledSource ? EArdaRHICpuAccess::None : EArdaRHICpuAccess::Write;
			SourceDesc.mbTiled = TiledSource;
			const auto Source = Graph.CreateBuffer("source", SourceDesc);
			ASSERT_TRUE(Source);
			FArdaMemoryCopyBufferParameters Parameters;
			Parameters.mSource = Source.mValue;
			Parameters.mSourceOffset = 32;
			Parameters.mDestinationOffset = 16;
			const auto Copy = Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("copy", Parameters);
			ASSERT_TRUE(Copy) << Copy.mStatus.mMessage.c_str();
			EXPECT_FALSE(Parameters.mDestination);
			const auto Output = Graph.FindOutput(Copy.mValue, "Destination");
			const auto* Resource = Graph.FindResource(Output);
			ASSERT_NE(Resource, nullptr);
			EXPECT_EQ(Resource->mBuffer.mByteSize, 112u);
			EXPECT_EQ(Resource->mBuffer.mStructureStride, SourceDesc.mStructureStride);
			EXPECT_EQ(Resource->mBuffer.mFormat, SourceDesc.mFormat);
			EXPECT_EQ(Resource->mBuffer.mUsage, SourceDesc.mUsage);
			EXPECT_EQ(Resource->mBuffer.mCpuAccess, EArdaRHICpuAccess::None);
			EXPECT_FALSE(Resource->mBuffer.mbTiled);
			const auto Again = Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("copy", Parameters);
			ASSERT_TRUE(Again);
			EXPECT_EQ(Again.mValue, Copy.mValue);
			Parameters.mSourceOffset = 48;
			EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("copy", Parameters));
			ASSERT_TRUE(Graph.CancelGraphEdit());
		}
	}

	TEST(ArdaMemoryNodes, RejectsForeignStaleAndTextureHandlesAtAttachment)
	{
		FArdaDependencyGraph Graph;
		FArdaDependencyGraph Other;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Stale = CreateBuffer(Graph, "abandoned");
		ASSERT_TRUE(Graph.CancelGraphEdit());
		ASSERT_TRUE(Graph.BeginGraphEdit());
		ASSERT_TRUE(Other.BeginGraphEdit());
		const auto Buffer = CreateBuffer(Graph, "valid", 64, EArdaRHIBufferUsage::UnorderedAccess);
		const auto Foreign = CreateBuffer(Other, "foreign");
		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = TextureDesc.mHeight = 4;
		TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		const auto Texture = Graph.CreateTexture("texture", TextureDesc);
		ASSERT_TRUE(Texture);
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		for (const auto Invalid : {Stale, Foreign, Texture.mValue})
		{
			EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("bad upload", {Invalid, {1, 2, 3, 4}}));
			EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("bad source", {Invalid, Buffer, 4}));
			EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("bad destination", {Buffer, Invalid, 4}));
			EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("bad readback", {Invalid, Bytes}));
			EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryClearBufferNode>("bad clear", {Invalid, 0}));
		}
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("null source", {{}, Buffer, 4}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("null source", {{}, Bytes}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryClearBufferNode>("null destination", {}));
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		ASSERT_TRUE(Graph.CancelGraphEdit());
		ASSERT_TRUE(Other.CancelGraphEdit());
	}

	TEST(ArdaMemoryNodes, UploadValidatesEmptyAndOutOfBoundsRanges)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Buffer = CreateBuffer(Graph, "buffer", 16);
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("empty", {Buffer, {}}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("past end", {Buffer, {1}, 16}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("overrun", {Buffer, {1, 2, 3, 4}, 13}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("overflow",
		    {Buffer, {1, 2, 3, 4}, std::numeric_limits<uint64_t>::max() - 1}));
		const auto Valid = Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("exact end", {Buffer, {1, 2, 3, 4}, 12});
		ASSERT_TRUE(Valid) << Valid.mStatus.mMessage.c_str();
		EXPECT_EQ(Graph.FindOutput(Valid.mValue, "Destination"), Buffer);
		ASSERT_TRUE(Graph.CancelGraphEdit());
	}

	TEST(ArdaMemoryNodes, CopyRejectsEmptyOverflowingAndSelfCopyRanges)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Source = CreateBuffer(Graph, "source", 32);
		const auto Destination = CreateBuffer(Graph, "destination", 16);
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("empty", {Source, Destination, 0}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("source overrun", {Source, Destination, 8, 28}));
		EXPECT_FALSE(
		    Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("destination overrun", {Source, Destination, 8, 0, 12}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("whole overrun", {Source, Destination}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("empty remainder",
		    {Source, Destination, ArdaRHIWholeBuffer, 32}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("invalid remainder",
		    {Source, {}, ArdaRHIWholeBuffer, std::numeric_limits<uint64_t>::max()}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("destination overflow",
		    {Source, {}, 8, 0, std::numeric_limits<uint64_t>::max() - 3}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("source overflow",
		    {Source, Destination, 8, std::numeric_limits<uint64_t>::max() - 3}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("self overlap", {Source, Source, 8, 0, 4}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("self disjoint", {Source, Source, 8, 0, 16}));
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		const auto Valid = Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("exact end",
		    {Source, Destination, ArdaRHIWholeBuffer, 24, 8});
		ASSERT_TRUE(Valid) << Valid.mStatus.mMessage.c_str();
		EXPECT_EQ(Graph.FindOutput(Valid.mValue, "Destination"), Destination);
		const auto Automatic = Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("automatic", {Source, {}, 8});
		ASSERT_TRUE(Automatic);
		EXPECT_EQ(Graph.FindOutput(Automatic.mValue, "Destination").mIndex, 2u);
		ASSERT_TRUE(Graph.CancelGraphEdit());
	}

	TEST(ArdaMemoryNodes, ReadbackValidatesTheDestinationAndRangeAndDeduplicatesByBoth)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Source = CreateBuffer(Graph, "source", 32);
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("null sink", {Source, {}}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("empty", {Source, Bytes, 0, 0}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("empty remainder", {Source, Bytes, 32}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("overrun", {Source, Bytes, 28, 8}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("overflow",
		    {Source, Bytes, std::numeric_limits<uint64_t>::max() - 3, 8}));
		const auto Readback = Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("readback", {Source, Bytes, 16});
		ASSERT_TRUE(Readback) << Readback.mStatus.mMessage.c_str();
		const auto Again = Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("readback", {Source, Bytes, 16});
		ASSERT_TRUE(Again);
		EXPECT_EQ(Again.mValue, Readback.mValue);
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("readback", {Source, Bytes, 12}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("readback", {Source, Bytes, 16, 4}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("readback",
		    {Source, eastl::make_shared<eastl::vector<uint8_t>>(), 16}));
		ASSERT_TRUE(Graph.CancelGraphEdit());
	}

	TEST(ArdaMemoryNodes, ClearRequiresGpuUavStorageAndWholeUint32Elements)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto NoUav = CreateBuffer(Graph, "no uav");
		const auto Unaligned = CreateBuffer(Graph, "unaligned", 7, EArdaRHIBufferUsage::UnorderedAccess);
		FArdaRHIBufferDesc HostDesc;
		HostDesc.mByteSize = 64;
		HostDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		HostDesc.mCpuAccess = EArdaRHICpuAccess::Write;
		const auto Host = Graph.CreateBuffer("host", HostDesc);
		ASSERT_TRUE(Host);
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryClearBufferNode>("no uav", {NoUav, 7}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryClearBufferNode>("unaligned", {Unaligned, 7}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryClearBufferNode>("host", {Host.mValue, 7}));
		const auto Buffer = CreateBuffer(Graph, "gpu", 64, EArdaRHIBufferUsage::UnorderedAccess);
		const auto Clear = Graph.AttachOrFind<FArdaMemoryClearBufferNode>("clear", {Buffer, 7});
		ASSERT_TRUE(Clear) << Clear.mStatus.mMessage.c_str();
		const auto Again = Graph.AttachOrFind<FArdaMemoryClearBufferNode>("clear", {Buffer, 7});
		ASSERT_TRUE(Again);
		EXPECT_EQ(Again.mValue, Clear.mValue);
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryClearBufferNode>("clear", {Buffer, 8}));
		ASSERT_TRUE(Graph.CancelGraphEdit());
	}

	TEST(ArdaMemoryNodes, EnforcesCpuHeapDirectionsAndDropsVolatileAllocationPolicy)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Gpu = CreateBuffer(Graph, "gpu", 64);
		FArdaRHIBufferDesc HostDescription;
		HostDescription.mByteSize = 64;
		HostDescription.mCpuAccess = EArdaRHICpuAccess::Write;
		HostDescription.mUsage = EArdaRHIBufferUsage::Constant | EArdaRHIBufferUsage::Volatile;
		HostDescription.mMaxVersions = 3;
		HostDescription.mbKeepInitialState = true;
		HostDescription.mInitialState = EArdaRHIResourceState::CopySource;
		const auto Upload = Graph.CreateBuffer("upload heap", HostDescription);
		ASSERT_TRUE(Upload);
		HostDescription.mCpuAccess = EArdaRHICpuAccess::Read;
		HostDescription.mUsage = EArdaRHIBufferUsage::None;
		HostDescription.mMaxVersions = 0;
		HostDescription.mInitialState = EArdaRHIResourceState::CopyDest;
		const auto Readback = Graph.CreateBuffer("readback heap", HostDescription);
		ASSERT_TRUE(Readback);
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();

		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("host upload", {Upload.mValue, {1}}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("host readback", {Readback.mValue, {1}}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("wrong source", {Readback.mValue, Gpu, 4}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("wrong destination", {Gpu, Upload.mValue, 4}));
		EXPECT_FALSE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("wrong readback", {Readback.mValue, Bytes}));
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("readback destination", {Gpu, Readback.mValue, 4}));
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("upload source", {Upload.mValue, Bytes}));

		const auto Copy = Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("inferred gpu", {Upload.mValue, {}});
		ASSERT_TRUE(Copy) << Copy.mStatus.mMessage.c_str();
		const auto* Output = Graph.FindResource(Graph.FindOutput(Copy.mValue, "Destination"));
		ASSERT_NE(Output, nullptr);
		EXPECT_EQ(Output->mBuffer.mUsage, EArdaRHIBufferUsage::Constant);
		EXPECT_EQ(Output->mBuffer.mCpuAccess, EArdaRHICpuAccess::None);
		EXPECT_EQ(Output->mBuffer.mMaxVersions, 0u);
		EXPECT_FALSE(Output->mBuffer.mbKeepInitialState);
		EXPECT_EQ(Output->mBuffer.mInitialState, EArdaRHIResourceState::Common);
		ASSERT_TRUE(Graph.CancelGraphEdit());
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
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mFramesInFlight = 2;
		ASSERT_TRUE(Graph.SetOptions(Options));
		FArdaMemoryUploadBufferParameters UploadParameters;
		for (uint8_t Byte = 0; Byte < 48; ++Byte)
		{
			UploadParameters.mBytes.push_back(Byte);
		}
		const auto Uploaded = Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("upload", UploadParameters);
		ASSERT_TRUE(Uploaded) << Uploaded.mStatus.mMessage.c_str();
		const auto Source = Graph.FindOutput(Uploaded.mValue, "Destination");
		ASSERT_TRUE(Source);
		UploadParameters.mBytes.assign(48, 255);
		const auto Copied =
		    Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("copy source remainder", {Source, {}, ArdaRHIWholeBuffer, 8});
		ASSERT_TRUE(Copied) << Copied.mStatus.mMessage.c_str();
		const auto Destination = Graph.FindOutput(Copied.mValue, "Destination");
		ASSERT_TRUE(Destination);
		auto Readback = eastl::make_shared<eastl::vector<uint8_t>>();
		auto SourceSlice = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(
		    Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("read copied remainder", {Destination, Readback, 4}));
		ASSERT_TRUE(
		    Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("read source slice", {Source, SourceSlice, 4, 8}));
		const auto Compiled = Graph.EndGraphEdit();
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
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Initial =
		    Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("initialize", {{}, eastl::vector<uint8_t>(64, 17)});
		ASSERT_TRUE(Initial) << Initial.mStatus.mMessage.c_str();
		const auto Destination = Graph.FindOutput(Initial.mValue, "Destination");
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("patch upload",
		    {Destination, {21, 22, 23, 24, 25, 26, 27, 28}, 16}));
		const auto CopySource = Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("copy source",
		    {{}, {40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55}});
		ASSERT_TRUE(CopySource);
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("patch copy",
		    {Graph.FindOutput(CopySource.mValue, "Destination"), Destination, 8, 4, 36}));
		auto Readback = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("read all", {Destination, Readback}));
		const auto Compiled = Graph.EndGraphEdit();
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
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Buffer = CreateBuffer(Graph, "clear target", 64, EArdaRHIBufferUsage::UnorderedAccess);
		constexpr uint32_t Value = 0x1234abcd;
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryClearBufferNode>("clear", {Buffer, Value}));
		auto Readback = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("readback", {Buffer, Readback}));
		const auto Compiled = Graph.EndGraphEdit();
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
		ASSERT_TRUE(Graph.BeginGraphEdit());
		const auto Upload = Graph.AttachOrFind<FArdaMemoryUploadBufferNode>("upload odd size",
		    {{}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 1});
		ASSERT_TRUE(Upload) << Upload.mStatus.mMessage.c_str();
		const auto Source = Graph.FindOutput(Upload.mValue, "Destination");
		const auto Copy = Graph.AttachOrFind<FArdaMemoryCopyBufferNode>("copy odd range", {Source, {}, 5, 3, 3});
		ASSERT_TRUE(Copy) << Copy.mStatus.mMessage.c_str();
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(Graph.AttachOrFind<FArdaMemoryReadbackBufferNode>("read initialized range",
		    {Graph.FindOutput(Copy.mValue, "Destination"), Bytes, 3, 5}));
		const auto Compiled = Graph.EndGraphEdit();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		const auto Executed = Graph.Execute();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		EXPECT_EQ(*Bytes, (eastl::vector<uint8_t>{2, 3, 4, 5, 6}));
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaMemoryNodesGpu, testing::Values("native-d3d12", "native-vulkan"));
}
