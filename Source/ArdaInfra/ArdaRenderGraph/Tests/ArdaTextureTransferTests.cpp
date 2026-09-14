#include "ArdaDependencyNode.h"
#include "ArdaRenderGraph.h"
#include <gtest/gtest.h>
#include <EASTL/algorithm.h>

namespace
{
	using namespace arda;

	struct FArdaTransferWriteParameters
	{
		FArdaDependencyResourceHandle mResource;
	};

	struct FArdaTransferWriteNode
	    : TArdaDependencyNode<FArdaTransferWriteNode, FArdaTransferWriteParameters, EArdaDependencyNodeKind::Graphics>
	{
		using FArdaParameters = FArdaTransferWriteParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.transfer.produce", 1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Resource(P.mResource).Build();
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc N;
			N.mAccesses.push_back({P.mResource, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest});
			return N;
		}

		// Record the fixture operation and capture its observable results.
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return FArdaRHIStatus{};
		}
	};

	FArdaGraphNodeHandle ProduceTexture(FArdaDependencyGraph& G, FArdaDependencyResourceHandle R)
	{
		auto N = G.AttachOrFind<FArdaTransferWriteNode>("produce", {R});
		EXPECT_TRUE(N);
		return N.mValue;
	}

	/** Retained external storage for device-independent initialization and dependency checks. */
	class FArdaImportedTransferTexture final : public IArdaRHITexture
	{
	public:
		explicit FArdaImportedTransferTexture(FArdaRHITextureDesc Desc)
		    : mDesc(eastl::move(Desc))
		{
		}

		void AddRef() noexcept override
		{
			++mReferences;
		}

		void Release() noexcept override
		{
			if (--mReferences == 0)
			{
				delete this;
			}
		}

		EArdaRHIResourceType GetResourceType() const noexcept override
		{
			return EArdaRHIResourceType::Texture;
		}

		const char* GetDebugName() const noexcept override
		{
			return "imported transfer texture";
		}

		const FArdaRHITextureDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		const void* GetPhysicalIdentity() const noexcept override
		{
			return this;
		}

	private:
		FArdaRHITextureDesc mDesc;
		uint32_t mReferences = 0;
	};

	TEST(ArdaGraphTextureTransfer, DirectionsRejectEachOthersParameterSchema)
	{
		ASSERT_TRUE(FArdaGraphTextureToBufferNode::Register());
		ASSERT_TRUE(FArdaGraphBufferToTextureNode::Register());
		const auto DownloadDefinition =
		    FArdaNodeRegistry::Get().Find(FArdaGraphTextureToBufferNode::GetMetadata().mName);
		const auto UploadDefinition = FArdaNodeRegistry::Get().Find(FArdaGraphBufferToTextureNode::GetMetadata().mName);
		ASSERT_TRUE(DownloadDefinition);
		ASSERT_TRUE(UploadDefinition);
		EXPECT_NE(DownloadDefinition->mParameterType, UploadDefinition->mParameterType);

		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = 4;
		TextureDesc.mHeight = 4;
		TextureDesc.mFormat = EArdaRHIFormat::R32UInt;
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 1024;
		const auto Texture = Graph.CreateTexture("texture", TextureDesc);
		const auto Buffer = Graph.CreateBuffer("buffer", BufferDesc);
		ASSERT_TRUE(Texture);
		ASSERT_TRUE(Buffer);
		const FArdaGraphTextureToBufferNode::FArdaParameters DownloadParameters{Texture.mValue,
		    {},
		    Buffer.mValue,
		    {0, 256}};
		const FArdaGraphBufferToTextureNode::FArdaParameters UploadParameters{Texture.mValue,
		    {},
		    Buffer.mValue,
		    {0, 256}};

		const auto WrongDownload = Graph.AttachOrFind("wrong download", DownloadDefinition->mName, UploadParameters);
		const auto WrongUpload = Graph.AttachOrFind("wrong upload", UploadDefinition->mName, DownloadParameters);
		EXPECT_EQ(WrongDownload.mStatus.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(WrongUpload.mStatus.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		EXPECT_TRUE(Graph.AttachOrFind("download", DownloadDefinition->mName, DownloadParameters));
		EXPECT_TRUE(Graph.AttachOrFind("upload", UploadDefinition->mName, UploadParameters));
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 2u);
		ASSERT_TRUE(Graph.CancelGraphEdit());
	}

	TEST(ArdaGraphTextureTransfer, PartialUploadsRequireInitializedTextureContents)
	{
		for (const bool Imported : {false, true})
		{
			for (uint32_t Region = 0; Region < 8; ++Region)
			{
				SCOPED_TRACE(Imported);
				SCOPED_TRACE(Region);
				FArdaDependencyGraph Graph;
				ASSERT_TRUE(Graph.BeginGraphEdit());

				// Exercise both explicit/default whole extents and a partial extent or offset on every axis.
				FArdaRHITextureDesc TextureDesc;
				TextureDesc.mDimension = EArdaRHITextureDimension::Texture3D;
				TextureDesc.mWidth = TextureDesc.mHeight = TextureDesc.mDepth = 4;
				TextureDesc.mFormat = EArdaRHIFormat::R32UInt;
				const auto Texture = Imported
				    ? Graph.ImportTexture("texture", FArdaRHITextureRef(new FArdaImportedTransferTexture(TextureDesc)))
				    : Graph.CreateTexture("texture", TextureDesc);
				ASSERT_TRUE(Texture);
				FArdaRHITextureSlice Slice;
				if (Region == 1)
				{
					Slice.mWidth = Slice.mHeight = Slice.mDepth = 4;
				}
				else if (Region == 2)
				{
					Slice.mWidth = 1;
				}
				else if (Region == 3)
				{
					Slice.mHeight = 1;
				}
				else if (Region == 4)
				{
					Slice.mDepth = 1;
				}
				else if (Region == 5)
				{
					Slice.mX = 1;
				}
				else if (Region == 6)
				{
					Slice.mY = 1;
				}
				else if (Region == 7)
				{
					Slice.mZ = 1;
				}

				// Fully initialize the source buffer, then request a read of the entire updated texture.
				FArdaRHIBufferDesc BufferDesc;
				BufferDesc.mByteSize = 4096;
				const auto Source = Graph.CreateBuffer("source", BufferDesc);
				const auto Destination = Graph.CreateBuffer("destination", BufferDesc);
				ASSERT_TRUE(Source && Destination);
				FArdaGraphUploadParameters Upload;
				Upload.mDestination = Source.mValue;
				Upload.mBytes.resize(4096, 0x7F);
				ASSERT_TRUE(Graph.AttachOrFind<FArdaGraphUploadNode>("produce source", Upload));
				FArdaGraphBufferToTextureParameters Parameters{Texture.mValue, Slice, Source.mValue, {0, 256}};
				Parameters.mbWholeSubresource = true; // A caller cannot bypass extent normalization.
				const auto Copy = Graph.AttachOrFind<FArdaGraphBufferToTextureNode>("upload", Parameters);
				ASSERT_TRUE(Copy);
				ASSERT_TRUE(Graph.AttachOrFind<FArdaGraphTextureToBufferNode>("read entire texture",
				    {Texture.mValue, {}, Destination.mValue, {0, 256}}));
				ASSERT_TRUE(Graph.MarkOutput(Destination.mValue));

				// A partial copy preserves existing texels; only imports provide them in this single-version graph.
				const bool Whole = Region < 2;
				const auto& Access = Graph.GetTopology().TryGetNode(Copy.mValue)->mPayload.mDesc.mAccesses.front();
				EXPECT_EQ(Access.mAccess, Whole ? EArdaDependencyAccess::Write : EArdaDependencyAccess::ReadWrite);
				const auto Status = Graph.EndGraphEdit();
				EXPECT_EQ(bool(Status), Imported || Whole) << Status.mMessage.c_str();
			}
		}
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
		auto Download = G.AttachOrFind<FArdaGraphTextureToBufferNode>("download",
		    {Source.mValue, Slice, Intermediate.mValue, Layout});
		auto Dead =
		    G.AttachOrFind<FArdaGraphTextureToBufferNode>("dead", {Source.mValue, Slice, Unused.mValue, Layout});
		ASSERT_TRUE(Download);
		ASSERT_TRUE(Dead);
		EXPECT_EQ(G.AttachOrFind<FArdaGraphTextureToBufferNode>("download",
		               {Source.mValue, Slice, Intermediate.mValue, Layout})
		              .mValue,
		    Download.mValue);
		T.mWidth = 6;
		T.mHeight = 3;
		T.mMipLevels = T.mArraySize = 1;
		T.mDimension = EArdaRHITextureDimension::Texture2D;
		auto Output = G.CreateTexture("output", T);
		ASSERT_TRUE(Output);
		auto Upload =
		    G.AttachOrFind<FArdaGraphBufferToTextureNode>("upload", {Output.mValue, {}, Intermediate.mValue, Layout});
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
		EXPECT_FALSE(G.AttachOrFind<FArdaGraphTextureToBufferNode>("foreign", {ForeignTexture, {}, Buffer, Layout}));
		EXPECT_FALSE(G.AttachOrFind<FArdaGraphBufferToTextureNode>("foreign", {Texture, {}, ForeignBuffer, Layout}));
		EXPECT_FALSE(G.AttachOrFind<FArdaGraphTextureToBufferNode>("null", {{}, {}, Buffer, Layout}));
		EXPECT_FALSE(G.AttachOrFind<FArdaGraphBufferToTextureNode>("null", {Texture, {}, {}, Layout}));
		EXPECT_FALSE(G.AttachOrFind<FArdaGraphTextureToBufferNode>("pitch", {Texture, {}, Buffer, {0, 4}}));
		EXPECT_FALSE(G.AttachOrFind<FArdaGraphBufferToTextureNode>("overflow", {Texture, {}, Buffer, {512, 256}}));
		EXPECT_FALSE(G.AttachOrFind<FArdaGraphTextureToBufferNode>("", {Texture, {}, Buffer, Layout}));
		FArdaRHITextureSlice Invalid;
		Invalid.mWidth = 5;
		EXPECT_FALSE(G.AttachOrFind<FArdaGraphBufferToTextureNode>("extent", {Texture, Invalid, Buffer, Layout}));
		EXPECT_TRUE(G.GetTopology().GetNodes().empty());
		ASSERT_TRUE(G.AttachOrFind<FArdaGraphTextureToBufferNode>("valid", {Texture, {}, Buffer, Layout}));
		EXPECT_EQ(G.GetTopology().GetNodes().size(), 1u);
		ASSERT_TRUE(G.CancelGraphEdit());
		EXPECT_FALSE(G.AttachOrFind<FArdaGraphTextureToBufferNode>("outside edit", {Texture, {}, Buffer, Layout}));
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
		auto Download = G.AttachOrFind<FArdaGraphTextureToBufferNode>("download", {Source, {}, Buffer, Layout});
		auto Upload = G.AttachOrFind<FArdaGraphBufferToTextureNode>("upload", {Output, {}, Buffer, Layout});
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
		ASSERT_TRUE(G.AttachOrFind<FArdaGraphTextureToBufferNode>("download", {Texture, {}, Buffer, {0, 256}}));
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(G.AttachOrFind("read padding", "arda.readback", FArdaGraphReadbackParameters{Buffer, Bytes}));
		EXPECT_FALSE(G.EndGraphEdit());
	}
}
