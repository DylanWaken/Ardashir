#include "ArdaRenderGraph.h"
#include <gtest/gtest.h>
#include <EASTL/algorithm.h>
#include <mutex>

namespace
{
	using namespace arda;

	struct FTransferWriteParameters
	{
		FArdaDependencyResourceHandle mResource;
	};

	FArdaGraphNodeHandle ProduceTexture(FArdaDependencyGraph& G, FArdaDependencyResourceHandle R)
	{
		static std::once_flag Once;
		std::call_once(Once,
		    []
		    {
			    TArdaDependencyNodeDefinition<FTransferWriteParameters> D;
			    D.mName = "test.transfer.produce";
			    D.mKind = EArdaDependencyNodeKind::Graphics;
			    D.mCanonicalKey = [](const auto& P)
			    {
				    eastl::string K;
				    for (uint64_t V : {P.mResource.mGraph, uint64_t(P.mResource.mIndex), P.mResource.mGeneration})
				    {
					    K.append(reinterpret_cast<const char*>(&V), sizeof(V));
				    }
				    return K;
			    };
			    D.mDescribe = [](const auto& P)
			    {
				    FArdaDependencyNodeDesc N;
				    N.mAccesses.push_back({P.mResource, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest});
				    return N;
			    };
			    D.mRecord = [](FArdaDependencyExecutionContext&, const auto&)
			    {
				    return FArdaRHIStatus{};
			    };
			    EXPECT_TRUE(FArdaNodeRegistry::Get().Register(eastl::move(D)));
		    });
		auto N = G.AttachOrFind("produce", "test.transfer.produce", FTransferWriteParameters{R});
		EXPECT_TRUE(N);
		return N.mValue;
	}

	TEST(ArdaGraphTextureTransfer, PitchedRoundTripTracksTexelsAndCullsUnusedCopies)
	{
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		FArdaRHITextureDesc T;
		T.mWidth = 13;
		T.mHeight = 7;
		T.mMipLevels = 2;
		T.mArraySize = 2;
		T.mDimension = EArdaRHITextureDimension::Texture2DArray;
		T.mFormat = EArdaRHIFormat::R32UInt;
		auto Source = G.CreateTexture("source", T);
		ASSERT_TRUE(Source);
		auto Producer = ProduceTexture(G, Source.mValue);
		FArdaRHIBufferDesc B;
		B.mByteSize = 2048;
		auto Intermediate = G.CreateBuffer("pitched", B);
		auto Unused = G.CreateBuffer("unused", B);
		ASSERT_TRUE(Intermediate);
		ASSERT_TRUE(Unused);
		FArdaRHITextureSlice Slice;
		Slice.mMipLevel = Slice.mArraySlice = 1;
		const FArdaRHITextureBufferLayout Layout{512, 256};
		auto Download = AttachArdaTextureToBuffer(G, "download", Source.mValue, Slice, Intermediate.mValue, Layout);
		auto Dead = AttachArdaTextureToBuffer(G, "dead", Source.mValue, Slice, Unused.mValue, Layout);
		ASSERT_TRUE(Download);
		ASSERT_TRUE(Dead);
		EXPECT_EQ(AttachArdaTextureToBuffer(G, "download", Source.mValue, Slice, Intermediate.mValue, Layout).mValue,
		    Download.mValue);
		T.mWidth = 6;
		T.mHeight = 3;
		T.mMipLevels = T.mArraySize = 1;
		T.mDimension = EArdaRHITextureDimension::Texture2D;
		auto Output = G.CreateTexture("output", T);
		ASSERT_TRUE(Output);
		auto Upload = AttachArdaBufferToTexture(G, "upload", Output.mValue, {}, Intermediate.mValue, Layout);
		ASSERT_TRUE(Upload);
		ASSERT_TRUE(G.MarkOutput(Output.mValue));
		auto Status = G.EndGraphEdit();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto& C = G.GetCompileResult();
		EXPECT_EQ(C.mExecutionOrder, (eastl::vector<FArdaGraphNodeHandle>{Producer, Download.mValue, Upload.mValue}));
		EXPECT_EQ(C.mCulledNodes, (eastl::vector<FArdaGraphNodeHandle>{Dead.mValue}));
		for (auto Q : C.mQueues)
		{
			EXPECT_EQ(Q, EArdaRHIQueueType::Graphics);
		}
		const auto& A = G.GetTopology().TryGetNode(Download.mValue)->mPayload.mDesc.mAccesses;
		const auto& U = G.GetTopology().TryGetNode(Upload.mValue)->mPayload.mDesc.mAccesses;
		ASSERT_EQ(A.size(), 4u);
		ASSERT_EQ(U.size(), 4u);
		EXPECT_EQ(A[0].mTextureRange, (FArdaRHITextureSubresourceRange{1, 1, 1, 1, 0, 1}));
		for (size_t Row = 0; Row < 3; ++Row)
		{
			const FArdaRHIBufferRange Expected{512 + Row * 256, 24};
			EXPECT_EQ(A[Row + 1].mBufferRange, Expected);
			EXPECT_EQ(U[Row + 1].mBufferRange, Expected);
			EXPECT_EQ(A[Row + 1].mAccess, EArdaDependencyAccess::Write);
			EXPECT_EQ(U[Row + 1].mAccess, EArdaDependencyAccess::Read);
		}
	}

	TEST(ArdaGraphTextureTransfer, RejectsForeignResourcesAndInvalidLayoutsWithoutAttaching)
	{
		FArdaDependencyGraph G, Other;
		ASSERT_TRUE(G.BeginGraphEdit());
		ASSERT_TRUE(Other.BeginGraphEdit());
		FArdaRHITextureDesc T;
		T.mWidth = T.mHeight = 4;
		T.mFormat = EArdaRHIFormat::R32UInt;
		FArdaRHIBufferDesc B;
		B.mByteSize = 1024;
		auto Texture = G.CreateTexture("texture", T).mValue;
		auto ForeignTexture = Other.CreateTexture("texture", T).mValue;
		auto Buffer = G.CreateBuffer("buffer", B).mValue;
		auto ForeignBuffer = Other.CreateBuffer("buffer", B).mValue;
		const FArdaRHITextureBufferLayout Layout{0, 256};
		EXPECT_FALSE(AttachArdaTextureToBuffer(G, "foreign", ForeignTexture, {}, Buffer, Layout));
		EXPECT_FALSE(AttachArdaBufferToTexture(G, "foreign", Texture, {}, ForeignBuffer, Layout));
		EXPECT_FALSE(AttachArdaTextureToBuffer(G, "null", {}, {}, Buffer, Layout));
		EXPECT_FALSE(AttachArdaBufferToTexture(G, "null", Texture, {}, {}, Layout));
		EXPECT_FALSE(AttachArdaTextureToBuffer(G, "pitch", Texture, {}, Buffer, {0, 4}));
		EXPECT_FALSE(AttachArdaBufferToTexture(G, "overflow", Texture, {}, Buffer, {512, 256}));
		EXPECT_FALSE(AttachArdaTextureToBuffer(G, "", Texture, {}, Buffer, Layout));
		FArdaRHITextureSlice Invalid;
		Invalid.mWidth = 5;
		EXPECT_FALSE(AttachArdaBufferToTexture(G, "extent", Texture, Invalid, Buffer, Layout));
		EXPECT_TRUE(G.GetTopology().GetNodes().empty());
		ASSERT_TRUE(AttachArdaTextureToBuffer(G, "valid", Texture, {}, Buffer, Layout));
		EXPECT_EQ(G.GetTopology().GetNodes().size(), 1u);
		ASSERT_TRUE(G.CancelGraphEdit());
		EXPECT_FALSE(AttachArdaTextureToBuffer(G, "outside edit", Texture, {}, Buffer, Layout));
	}

	TEST(ArdaGraphTextureTransfer, PaddedVolumeDeclaresOnlyCopiedRows)
	{
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		FArdaRHITextureDesc T;
		T.mWidth = 255;
		T.mHeight = T.mDepth = 64;
		T.mDimension = EArdaRHITextureDimension::Texture3D;
		T.mFormat = EArdaRHIFormat::R32UInt;
		auto Source = G.CreateTexture("source", T).mValue;
		auto Output = G.CreateTexture("output", T).mValue;
		auto Producer = ProduceTexture(G, Source);
		constexpr uint64_t RowCount = 64 * 64;
		FArdaRHIBufferDesc B;
		B.mByteSize = 1024 * RowCount;
		auto Buffer = G.CreateBuffer("buffer", B).mValue;
		const FArdaRHITextureBufferLayout Layout{0, 1024};
		auto Download = AttachArdaTextureToBuffer(G, "download", Source, {}, Buffer, Layout);
		auto Upload = AttachArdaBufferToTexture(G, "upload", Output, {}, Buffer, Layout);
		ASSERT_TRUE(Download);
		ASSERT_TRUE(Upload);
		ASSERT_TRUE(G.MarkOutput(Output));
		auto Status = G.EndGraphEdit();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(G.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{Producer, Download.mValue, Upload.mValue}));
		const auto& A = G.GetTopology().TryGetNode(Download.mValue)->mPayload.mDesc.mAccesses;
		ASSERT_EQ(A.size(), RowCount + 1);
		EXPECT_EQ(A.back().mBufferRange, (FArdaRHIBufferRange{1024 * (RowCount - 1), 1020}));
	}

	TEST(ArdaGraphTextureTransfer, PaddingCannotBeReadAsProducedTexels)
	{
		FArdaDependencyGraph G;
		ASSERT_TRUE(G.BeginGraphEdit());
		FArdaRHITextureDesc T;
		T.mWidth = 4;
		T.mHeight = 4;
		T.mFormat = EArdaRHIFormat::R32UInt;
		auto Texture = G.CreateTexture("texture", T).mValue;
		ProduceTexture(G, Texture);
		FArdaRHIBufferDesc B;
		B.mByteSize = 1024;
		auto Buffer = G.CreateBuffer("buffer", B).mValue;
		ASSERT_TRUE(AttachArdaTextureToBuffer(G, "download", Texture, {}, Buffer, {0, 256}));
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(G.AttachOrFind("read padding", "arda.readback", FArdaGraphReadbackParameters{Buffer, Bytes}));
		EXPECT_FALSE(G.EndGraphEdit());
	}
}
