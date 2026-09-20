#include "ArdaMemoryFacadeTest.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/Node/ArdaDependencyNode.h"
#include "ArdaDependencyGraph.h"
#include <gtest/gtest.h>
#include <EASTL/algorithm.h>

namespace arda::texturetransfer_tests
{
	using namespace arda;
	using namespace arda::memory_facade_tests;

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

		// Identify the semantic inputs for construction caching.
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

	ARDA_DECLARE_GRAPH_NODE(FArdaTransferWriteNode, WriteTransferTexture);
	ARDA_DEFINE_GRAPH_NODE(FArdaTransferWriteNode, WriteTransferTexture);

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
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = 4;
		TextureDesc.mHeight = 4;
		TextureDesc.mFormat = EArdaRHIFormat::R32UInt;
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 1024;
		const auto Texture = AllocateTexture(Edit, TextureDesc, "texture");
		const auto Buffer = AllocateBuffer(Edit, BufferDesc, "buffer");
		ASSERT_TRUE(Texture);
		ASSERT_TRUE(Buffer);
		const FArdaGraphTextureToBufferNode::FArdaParameters DownloadParameters{Texture.GetHandle(),
		    {},
		    Buffer.GetHandle(),
		    {0, 256}};
		const FArdaGraphBufferToTextureNode::FArdaParameters UploadParameters{Texture.GetHandle(),
		    {},
		    Buffer.GetHandle(),
		    {0, 256}};

		ASSERT_TRUE(Edit.Commit());
		{
			FArdaGraphEdit WrongEdit(Graph);
			EXPECT_FALSE(WrongEdit.Invoke(DownloadDefinition->mName, UploadParameters, "wrong download"));
			EXPECT_EQ(WrongEdit.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
			EXPECT_FALSE(WrongEdit.Commit());
			ASSERT_TRUE(WrongEdit.Cancel());
		}
		{
			FArdaGraphEdit WrongEdit(Graph);
			EXPECT_FALSE(WrongEdit.Invoke(UploadDefinition->mName, DownloadParameters, "wrong upload"));
			EXPECT_EQ(WrongEdit.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
			EXPECT_FALSE(WrongEdit.Commit());
			ASSERT_TRUE(WrongEdit.Cancel());
		}
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		FArdaGraphEdit Operations(Graph);
		EXPECT_TRUE(TextureToBuffer(Operations, DownloadParameters, "download"));
		EXPECT_TRUE(BufferToTexture(Operations, UploadParameters, "upload"));
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 2u);
		ASSERT_TRUE(Operations.Cancel());
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
				FArdaGraphEdit Edit(Graph);
				ASSERT_TRUE(Edit.GetStatus());

				// Exercise both explicit/default whole extents and a partial extent or offset on every axis.
				FArdaRHITextureDesc TextureDesc;
				TextureDesc.mDimension = EArdaRHITextureDimension::Texture3D;
				TextureDesc.mWidth = TextureDesc.mHeight = TextureDesc.mDepth = 4;
				TextureDesc.mFormat = EArdaRHIFormat::R32UInt;
				const auto Texture = Imported
				    ? BindTexture(Edit, FArdaRHITextureRef(new FArdaImportedTransferTexture(TextureDesc)), "texture")
				    : AllocateTexture(Edit, TextureDesc, "texture");
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
				const auto Source = AllocateBuffer(Edit, BufferDesc, "source");
				const auto Destination = AllocateBuffer(Edit, BufferDesc, "destination");
				ASSERT_TRUE(Source && Destination);
				FArdaGraphUploadParameters Upload;
				Upload.mDestination = Source.GetHandle();
				Upload.mBytes.resize(4096, 0x7F);
				ASSERT_TRUE(UploadNode(Edit, Upload, "produce source"));
				FArdaGraphBufferToTextureParameters Parameters{Texture, Slice, Source, {0, 256}};
				Parameters.mbWholeSubresource = true; // A caller cannot bypass extent normalization.
				const auto Copy = BufferToTexture(Edit, Parameters, "upload");
				ASSERT_TRUE(Copy);
				ASSERT_TRUE(TextureToBuffer(Edit,
				    FArdaGraphTextureToBufferNode::FArdaParameters{Texture, {}, Destination.GetHandle(), {0, 256}},
				    "read entire texture"));
				Edit.Keep(Destination.GetHandle());
				ASSERT_TRUE(Edit.GetStatus());

				// A partial copy preserves existing texels; only imports provide them in this single-version graph.
				const bool Whole = Region < 2;
				const auto& Access = Graph.GetTopology().TryGetNode(Copy.GetHandle())->mPayload.mDesc.mAccesses.front();
				EXPECT_EQ(Access.mAccess, Whole ? EArdaDependencyAccess::Write : EArdaDependencyAccess::ReadWrite);
				ASSERT_TRUE(Edit.Commit());
				const auto Status = Graph.Compile();
				EXPECT_EQ(bool(Status), Imported || Whole) << Status.mMessage.c_str();
			}
		}
	}

	TEST(ArdaGraphTextureTransfer, PitchedRoundTripTracksTexelsAndCullsUnusedCopies)
	{
		FArdaDependencyGraph G;
		FArdaGraphEdit Edit(G);
		ASSERT_TRUE(Edit.GetStatus());
		FArdaRHITextureDesc T;
		T.mWidth = 13;
		T.mHeight = 7;
		T.mMipLevels = 2;
		T.mArraySize = 2;
		T.mDimension = EArdaRHITextureDimension::Texture2DArray;
		T.mFormat = EArdaRHIFormat::R32UInt;
		auto Source = AllocateTexture(Edit, T, "source");
		ASSERT_TRUE(Source);
		auto Producer = WriteTransferTexture(Edit, FArdaTransferWriteParameters{Source.GetHandle()}, "produce");
		FArdaRHIBufferDesc B;
		B.mByteSize = 2048;
		auto Intermediate = AllocateBuffer(Edit, B, "pitched");
		auto Unused = AllocateBuffer(Edit, B, "unused");
		ASSERT_TRUE(Intermediate);
		ASSERT_TRUE(Unused);
		FArdaRHITextureSlice Slice;
		Slice.mMipLevel = Slice.mArraySlice = 1;
		const FArdaRHITextureBufferLayout Layout{512, 256};
		auto Download = TextureToBuffer(Edit,
		    FArdaGraphTextureToBufferNode::FArdaParameters{Source.GetHandle(), Slice, Intermediate.GetHandle(), Layout},
		    "download");
		auto Dead = TextureToBuffer(Edit,
		    FArdaGraphTextureToBufferNode::FArdaParameters{Source.GetHandle(), Slice, Unused.GetHandle(), Layout},
		    "dead");
		ASSERT_TRUE(Download);
		ASSERT_TRUE(Dead);
		T.mWidth = 6;
		T.mHeight = 3;
		T.mMipLevels = T.mArraySize = 1;
		T.mDimension = EArdaRHITextureDimension::Texture2D;
		auto Output = AllocateTexture(Edit, T, "output");
		ASSERT_TRUE(Output);
		auto Upload = BufferToTexture(Edit,
		    FArdaGraphBufferToTextureNode::FArdaParameters{Output.GetHandle(), {}, Intermediate.GetHandle(), Layout},
		    "upload");
		ASSERT_TRUE(Upload);
		Edit.Keep(Output.GetHandle());
		ASSERT_TRUE(Edit.GetStatus());
		ASSERT_TRUE(Edit.Commit());
		auto Status = G.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto& C = G.GetCompileResult();
		EXPECT_EQ(C.mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{Producer.GetHandle(), Download.GetHandle(), Upload.GetHandle()}));
		EXPECT_EQ(C.mCulledNodes, (eastl::vector<FArdaGraphNodeHandle>{Dead.GetHandle()}));
		for (auto Q : C.mQueues)
		{
			EXPECT_EQ(Q, EArdaRHIQueueType::Graphics);
		}
		const auto& A = G.GetTopology().TryGetNode(Download.GetHandle())->mPayload.mDesc.mAccesses;
		const auto& U = G.GetTopology().TryGetNode(Upload.GetHandle())->mPayload.mDesc.mAccesses;
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
		FArdaGraphEdit Edit(G);
		ASSERT_TRUE(Edit.GetStatus());
		FArdaGraphEdit OtherEdit(Other);
		ASSERT_TRUE(OtherEdit.GetStatus());
		FArdaRHITextureDesc T;
		T.mWidth = T.mHeight = 4;
		T.mFormat = EArdaRHIFormat::R32UInt;
		FArdaRHIBufferDesc B;
		B.mByteSize = 1024;
		auto Texture = AllocateTexture(Edit, T, "texture").GetHandle();
		auto ForeignTexture = AllocateTexture(OtherEdit, T, "texture").GetHandle();
		auto Buffer = AllocateBuffer(Edit, B, "buffer").GetHandle();
		auto ForeignBuffer = AllocateBuffer(OtherEdit, B, "buffer").GetHandle();
		const FArdaRHITextureBufferLayout Layout{0, 256};
		ASSERT_TRUE(Edit.Commit());
		ExpectRejectedOperation(G,
		    TextureToBuffer,
		    FArdaGraphTextureToBufferNode::FArdaParameters{ForeignTexture, {}, Buffer, Layout},
		    "foreign");
		ExpectRejectedOperation(G,
		    BufferToTexture,
		    FArdaGraphBufferToTextureNode::FArdaParameters{Texture, {}, ForeignBuffer, Layout},
		    "foreign");
		ExpectRejectedOperation(G,
		    TextureToBuffer,
		    FArdaGraphTextureToBufferNode::FArdaParameters{{}, {}, Buffer, Layout},
		    "null");
		ExpectRejectedOperation(G,
		    BufferToTexture,
		    FArdaGraphBufferToTextureNode::FArdaParameters{Texture, {}, {}, Layout},
		    "null");
		ExpectRejectedOperation(G,
		    TextureToBuffer,
		    FArdaGraphTextureToBufferNode::FArdaParameters{Texture, {}, Buffer, {0, 4}},
		    "pitch");
		ExpectRejectedOperation(G,
		    BufferToTexture,
		    FArdaGraphBufferToTextureNode::FArdaParameters{Texture, {}, Buffer, {512, 256}},
		    "overflow");

		FArdaRHITextureSlice Invalid;
		Invalid.mWidth = 5;
		ExpectRejectedOperation(G,
		    BufferToTexture,
		    FArdaGraphBufferToTextureNode::FArdaParameters{Texture, Invalid, Buffer, Layout},
		    "extent");
		EXPECT_TRUE(G.GetTopology().GetNodes().empty());
		FArdaGraphEdit Operations(G);
		ASSERT_TRUE(TextureToBuffer(Operations,
		    FArdaGraphTextureToBufferNode::FArdaParameters{Texture, {}, Buffer, Layout},
		    "valid"));
		EXPECT_TRUE(TextureToBuffer(Operations, FArdaGraphTextureToBufferParameters{Texture, {}, Buffer, Layout}));
		EXPECT_EQ(G.GetTopology().GetNodes().size(), 2u);
		ASSERT_TRUE(Operations.Cancel());
		EXPECT_FALSE(TextureToBuffer(Operations,
		    FArdaGraphTextureToBufferNode::FArdaParameters{Texture, {}, Buffer, Layout},
		    "outside edit"));
	}

	TEST(ArdaGraphTextureTransfer, PaddedVolumeDeclaresOnlyCopiedRows)
	{
		FArdaDependencyGraph G;
		FArdaGraphEdit Edit(G);
		ASSERT_TRUE(Edit.GetStatus());
		FArdaRHITextureDesc T;
		T.mWidth = 255;
		T.mHeight = T.mDepth = 64;
		T.mDimension = EArdaRHITextureDimension::Texture3D;
		T.mFormat = EArdaRHIFormat::R32UInt;
		auto Source = AllocateTexture(Edit, T, "source").GetHandle();
		auto Output = AllocateTexture(Edit, T, "output").GetHandle();
		auto Producer = WriteTransferTexture(Edit, FArdaTransferWriteParameters{Source}, "produce");
		constexpr uint64_t RowCount = 64 * 64;
		FArdaRHIBufferDesc B;
		B.mByteSize = 1024 * RowCount;
		auto Buffer = AllocateBuffer(Edit, B, "buffer").GetHandle();
		const FArdaRHITextureBufferLayout Layout{0, 1024};
		auto Download = TextureToBuffer(Edit,
		    FArdaGraphTextureToBufferNode::FArdaParameters{Source, {}, Buffer, Layout},
		    "download");
		auto Upload =
		    BufferToTexture(Edit, FArdaGraphBufferToTextureNode::FArdaParameters{Output, {}, Buffer, Layout}, "upload");
		ASSERT_TRUE(Download);
		ASSERT_TRUE(Upload);
		Edit.Keep(Output);
		ASSERT_TRUE(Edit.GetStatus());
		ASSERT_TRUE(Edit.Commit());
		auto Status = G.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(G.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{Producer.GetHandle(), Download.GetHandle(), Upload.GetHandle()}));
		const auto& A = G.GetTopology().TryGetNode(Download.GetHandle())->mPayload.mDesc.mAccesses;
		ASSERT_EQ(A.size(), RowCount + 1);
		EXPECT_EQ(A.back().mBufferRange, (FArdaRHIBufferRange{1024 * (RowCount - 1), 1020}));
	}

	TEST(ArdaGraphTextureTransfer, PaddingCannotBeReadAsProducedTexels)
	{
		FArdaDependencyGraph G;
		FArdaGraphEdit Edit(G);
		ASSERT_TRUE(Edit.GetStatus());
		FArdaRHITextureDesc T;
		T.mWidth = 4;
		T.mHeight = 4;
		T.mFormat = EArdaRHIFormat::R32UInt;
		auto Texture = AllocateTexture(Edit, T, "texture").GetHandle();
		WriteTransferTexture(Edit, FArdaTransferWriteParameters{Texture}, "produce");
		FArdaRHIBufferDesc B;
		B.mByteSize = 1024;
		auto Buffer = AllocateBuffer(Edit, B, "buffer").GetHandle();
		ASSERT_TRUE(TextureToBuffer(Edit,
		    FArdaGraphTextureToBufferNode::FArdaParameters{Texture, {}, Buffer, {0, 256}},
		    "download"));
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(ReadbackNode(Edit, FArdaGraphReadbackParameters{Buffer, Bytes}, "read padding"));
		ASSERT_TRUE(Edit.Commit());
		EXPECT_FALSE(G.Compile());
	}
}
