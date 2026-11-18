#include "ArdaDependencyGraph.h"
#include "Graph/Resources/ArdaGraphTensor.h"

#include <gtest/gtest.h>

namespace arda
{
	namespace
	{
		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaGraphTensorAccessParameters)
			ARDA_NODE_TENSOR(mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess)
			ARDA_NODE_TENSOR_ARRAY(mInputs, 2, EArdaDependencyAccess::Read, EArdaRHIResourceState::ShaderResource)
		ARDA_END_NODE_PARAMETER_STRUCT()

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaGraphTensorWriteParameters)
			ARDA_NODE_TENSOR(mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaGraphTensorWriteNode final
		    : public TArdaNodeDefinition<FArdaGraphTensorWriteNode, FArdaGraphTensorWriteParameters>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.tensor.write", 1};
			}

			static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
			    const FArdaParameters&,
			    const FArdaState&,
			    FArdaInstanceState&)
			{
				return {};
			}
		};

		ARDA_DECLARE_GRAPH_NODE(FArdaGraphTensorWriteNode, WriteGraphTensor);
		ARDA_DEFINE_GRAPH_NODE(FArdaGraphTensorWriteNode, WriteGraphTensor);

		FArdaRHIBufferDesc MakeTensorStorageDesc()
		{
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 64;
			Desc.mStructureStride = 4;
			return Desc;
		}
	}

	TEST(ArdaGraphTensor, MetadataPreservesBufferAliasesAndComputesTensorByteHazards)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = AllocateBuffer(Edit, MakeTensorStorageDesc());
		ASSERT_TRUE(Buffer);
		FArdaGraphTensorAccessParameters Parameters;
		Parameters.mOutput = {Buffer, {EArdaRHIDataType::Float32, {4}, {-1}, 28}};
		Parameters.mInputs[0] = {Buffer, {EArdaRHIDataType::Float32, {4, 4}, {0, 1}, 16}};
		Parameters.mInputs[1] = {Buffer, {EArdaRHIDataType::Float32, {0}, {1}, 64}};
		FArdaDependencyNodeDesc Description;
		const auto& Metadata = FArdaGraphTensorAccessParameters::GetNodeParameterMetadata();
		ASSERT_TRUE(Metadata.Apply(Parameters, Description));
		ASSERT_EQ(Description.mAccesses.size(), 3u);
		for (const auto& Access : Description.mAccesses)
		{
			EXPECT_EQ(Access.mResource, Buffer.GetHandle());
		}
		EXPECT_EQ(Description.mAccesses[0].mBufferRange, (FArdaRHIBufferRange{16, 16}));
		EXPECT_EQ(Description.mAccesses[1].mBufferRange, (FArdaRHIBufferRange{16, 16}));
		EXPECT_EQ(Description.mAccesses[2].mBufferRange, FArdaRHIBufferRange{});
		EXPECT_EQ(Description.mAccesses[0].mAccess, EArdaDependencyAccess::Write);
		EXPECT_EQ(Description.mAccesses[1].mAccess, EArdaDependencyAccess::Read);

		Parameters.mOutput.mDesc.mStrides = {0};
		EXPECT_FALSE(Metadata.Apply(Parameters, Description));
		EXPECT_EQ(Description.mAccesses.size(), 3u);
		Parameters.mOutput.mDesc.mStrides = {-1};
		Parameters.mInputs[1].mDesc.mStrides.clear();
		EXPECT_FALSE(Metadata.Apply(Parameters, Description));
		EXPECT_EQ(Description.mAccesses.size(), 3u);
	}

	TEST(ArdaGraphTensor, CanonicalIdentityIncludesStorageDtypeDimensionsStridesAndOrigin)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = AllocateBuffer(Edit, MakeTensorStorageDesc());
		const auto OtherBuffer = AllocateBuffer(Edit, MakeTensorStorageDesc());
		const FArdaGraphTensorWriteParameters Original{{Buffer, {EArdaRHIDataType::Float32, {2, 2}, {2, 1}, 0}}};
		const auto& Metadata = FArdaGraphTensorWriteParameters::GetNodeParameterMetadata();
		const auto Key = Metadata.GetCanonicalKey(Original);
		auto Changed = Original;
		EXPECT_EQ(Key, Metadata.GetCanonicalKey(Changed));
		Changed.mOutput.mBuffer = OtherBuffer;
		EXPECT_NE(Key, Metadata.GetCanonicalKey(Changed));
		Changed = Original;
		Changed.mOutput.mDesc.mDataType = EArdaRHIDataType::Int32;
		EXPECT_NE(Key, Metadata.GetCanonicalKey(Changed));
		Changed = Original;
		Changed.mOutput.mDesc.mDimensions = {4};
		EXPECT_NE(Key, Metadata.GetCanonicalKey(Changed));
		Changed = Original;
		Changed.mOutput.mDesc.mStrides = {1, 2};
		EXPECT_NE(Key, Metadata.GetCanonicalKey(Changed));
		Changed = Original;
		Changed.mOutput.mDesc.mByteOffset = 4;
		EXPECT_NE(Key, Metadata.GetCanonicalKey(Changed));
	}

	TEST(ArdaGraphTensor, PortableDescriptorCodecRejectsCorruptionWithoutReplacingTheDestination)
	{
		const FArdaRHITensorDesc Original{EArdaRHIDataType::Float16, {2, 4}, {-4, 1}, 8};
		FArdaGraphArchiveWriter Writer;
		Writer.Write(Original);
		ASSERT_TRUE(Writer.GetStatus());
		EXPECT_EQ(Writer.GetData().size(), 2u + 8u + 2u * 16u + 8u);
		FArdaGraphArchiveReader Reader(Writer.GetData());
		FArdaRHITensorDesc Restored;
		Reader.Read(Restored);
		ASSERT_TRUE(Reader.Finish());
		EXPECT_EQ(Restored, Original);

		for (size_t Length = 0; Length < Writer.GetData().size(); ++Length)
		{
			const auto Truncated = Writer.GetData().substr(0, Length);
			FArdaGraphArchiveReader TruncatedReader(Truncated);
			auto Unchanged = Original;
			TruncatedReader.Read(Unchanged);
			EXPECT_FALSE(TruncatedReader.Finish());
			EXPECT_EQ(Unchanged, Original);
		}
		auto Corrupt = Writer.GetData();
		Corrupt[0] = char(0xff);
		Corrupt[1] = char(0xff);
		FArdaGraphArchiveReader InvalidTypeReader(Corrupt);
		InvalidTypeReader.Read(Restored);
		EXPECT_FALSE(InvalidTypeReader.Finish());
		EXPECT_EQ(Restored, Original);

		FArdaGraphArchiveWriter InvalidWriter;
		InvalidWriter.Write(FArdaRHITensorDesc{EArdaRHIDataType::Float32, {4}, {-1}, 0});
		EXPECT_FALSE(InvalidWriter.GetStatus());
	}

	TEST(ArdaGraphTensor, SerializedNodesRemapBufferIdentityAndKeepTheBackendDescriptor)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = AllocateBuffer(Edit, MakeTensorStorageDesc());
		const FArdaGraphTensor Tensor{Buffer, {EArdaRHIDataType::Float32, {2, 3}, {4, 1}, 4}};
		const auto Node =
		    Edit.Invoke<FArdaGraphTensorWriteNode>(FArdaGraphTensorWriteParameters{Tensor}, "tensor-writer");
		ASSERT_TRUE(Node) << Edit.GetStatus().mMessage.c_str();
		Edit.Keep(Buffer);
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.CompileAndWait());
		const auto Encoded = Graph.Serialize();
		ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
		const auto Decoded = FArdaDependencyGraph::Deserialize(Encoded.mValue);
		ASSERT_TRUE(Decoded) << Decoded.mStatus.mMessage.c_str();
		const auto RestoredNode = Decoded.mValue->FindNode("tensor-writer");
		ASSERT_TRUE(RestoredNode);
		const auto& Payload = Decoded.mValue->GetTopology().TryGetNode(RestoredNode)->mPayload;
		const auto& Parameters = *static_cast<const FArdaGraphTensorWriteParameters*>(Payload.mSourceParameters.get());
		EXPECT_EQ(Parameters.mOutput.mDesc, Tensor.mDesc);
		EXPECT_NE(Parameters.mOutput.mBuffer.GetHandle().mGraph, Buffer.GetHandle().mGraph);
		ASSERT_TRUE(Decoded.mValue->CompileAndWait());
		const auto& Accesses = Decoded.mValue->GetNodeDescription(RestoredNode).mAccesses;
		ASSERT_EQ(Accesses.size(), 1u);
		EXPECT_EQ(Accesses[0].mResource, Parameters.mOutput.mBuffer.GetHandle());
		EXPECT_EQ(Accesses[0].mBufferRange, (FArdaRHIBufferRange{4, 28}));
		const auto Reencoded = Decoded.mValue->Serialize();
		ASSERT_TRUE(Reencoded);
		EXPECT_EQ(Reencoded.mValue, Encoded.mValue);
	}

	TEST(ArdaGraphTensor, CompilationRejectsTensorViewsBeyondTheSameBackingBuffer)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = AllocateBuffer(Edit, MakeTensorStorageDesc());
		const auto Node = Edit.Invoke<FArdaGraphTensorWriteNode>(
		    FArdaGraphTensorWriteParameters{{Buffer, {EArdaRHIDataType::Float32, {8}, {1}, 48}}},
		    "out-of-bounds");
		ASSERT_TRUE(Node);
		Edit.Keep(Buffer);
		const auto Status = Edit.Commit();
		EXPECT_FALSE(Status && Graph.CompileAndWait());
	}
}
