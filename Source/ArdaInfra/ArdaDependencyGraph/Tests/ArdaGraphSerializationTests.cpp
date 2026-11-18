#include "ArdaDependencyGraph.h"
#include "FileOperations/ArdaFileOperations.h"
#include <gtest/gtest.h>

namespace arda
{
	namespace
	{
		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaArchiveTestParameters)
			ARDA_NODE_BUFFER(mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mBytes, 64)
			ARDA_NODE_PARAMETER(eastl::string, mLabel)
			ARDA_NODE_PARAMETER(eastl::vector<uint32_t>, mValues)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(double, mWeight, 1.5)
		ARDA_END_NODE_PARAMETER_STRUCT()

		struct FArdaArchiveTestNode
		    : TArdaNodeDefinition<FArdaArchiveTestNode, FArdaArchiveTestParameters, EArdaNodeType::Compute>
		{
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.archive-output", 3};
			}

			static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
			{
				FArdaRHIBufferDesc Desc;
				Desc.mByteSize = Parameters.mBytes;
				return Context.Buffer(Parameters.mOutput, "Destination", Desc);
			}

			static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
			    const FArdaParameters&,
			    const FArdaState&,
			    FArdaInstanceState&)
			{
				return {};
			}
		};

		ARDA_DECLARE_GRAPH_NODE(FArdaArchiveTestNode, ArchiveTestNode);
		ARDA_DEFINE_GRAPH_NODE(FArdaArchiveTestNode, ArchiveTestNode);

		FArdaRHIStatus RecordArchiveTestGraph(FArdaDependencyGraph& Graph)
		{
			FArdaGraphEdit Edit(Graph);
			FArdaArchiveTestParameters Parameters;
			Parameters.mLabel = eastl::string("a\0b", 3);
			Parameters.mValues = {0, 7, UINT32_MAX};
			const auto First = Edit.Invoke<FArdaArchiveTestNode>(Parameters, "first");
			const auto Second = Edit.Invoke<FArdaArchiveTestNode>(Parameters, "second");
			Edit.Keep(First.GetBuffer());
			Edit.Keep(Second.GetBuffer());
			Edit.Order(First, Second);
			return Edit.Commit();
		}
	}

	TEST(ArdaGraphSerialization, RoundTripRestoresDeclaredOutputsAndPortableParameters)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(RecordArchiveTestGraph(Graph));
		auto Encoded = Graph.Serialize();
		ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
		auto Decoded = FArdaDependencyGraph::Deserialize(Encoded.mValue);
		ASSERT_TRUE(Decoded) << Decoded.mStatus.mMessage.c_str();
		EXPECT_EQ(Decoded.mValue->GetTopology().GetNodeCount(), 2u);
		const auto First = Decoded.mValue->FindNode("first");
		ASSERT_TRUE(First);
		const auto& Node = Decoded.mValue->GetTopology().TryGetNode(First)->mPayload;
		const auto& Parameters = *static_cast<const FArdaArchiveTestParameters*>(Node.mSourceParameters.get());
		EXPECT_EQ(Parameters.mLabel, eastl::string("a\0b", 3));
		EXPECT_EQ(Parameters.mValues, (eastl::vector<uint32_t>{0, 7, UINT32_MAX}));
		EXPECT_DOUBLE_EQ(Parameters.mWeight, 1.5);
		const auto Output = Decoded.mValue->FindOutput(First, "Destination");
		ASSERT_TRUE(Output);
		ASSERT_NE(Decoded.mValue->FindResource(Output), nullptr);
		EXPECT_TRUE(Decoded.mValue->FindResource(Output)->mbOutput);
		EXPECT_EQ(Decoded.mValue->FindResource(Output)->mBuffer.mByteSize, 64u);
		EXPECT_NE(Output.mGraph, Graph.FindOutput(Graph.FindNode("first"), "Destination").mGraph);
		auto Reencoded = FArdaGraphSerialization::Serialize(*Decoded.mValue);
		ASSERT_TRUE(Reencoded) << Reencoded.mStatus.mMessage.c_str();
		EXPECT_EQ(Reencoded.mValue, Encoded.mValue);
		ASSERT_TRUE(Decoded.mValue->CompileAndWait());
		EXPECT_EQ(Decoded.mValue->GetTopology().GetNodeCount(), 2u);
	}

	TEST(ArdaGraphSerialization, RejectsTruncationTrailingBytesAndUnknownNodeTypes)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(RecordArchiveTestGraph(Graph));
		const auto Encoded = FArdaGraphSerialization::Serialize(Graph);
		ASSERT_TRUE(Encoded);
		for (size_t Length = 0; Length < Encoded.mValue.size(); ++Length)
		{
			const auto Decoded = FArdaDependencyGraph::Deserialize(Encoded.mValue.substr(0, Length));
			EXPECT_FALSE(Decoded) << "Truncated archive accepted at " << Length;
		}
		EXPECT_FALSE(FArdaGraphSerialization::Deserialize(Encoded.mValue + "x"));
		auto Unknown = Encoded.mValue;
		const auto Position = Unknown.find("test.archive-output");
		ASSERT_NE(Position, eastl::string::npos);
		Unknown[Position] = 'X';
		const auto Decoded = FArdaGraphSerialization::Deserialize(Unknown);
		ASSERT_FALSE(Decoded);
		EXPECT_EQ(Decoded.mStatus.mCode, EArdaRHIResult::Unsupported);
	}

	TEST(ArdaGraphSerialization, SavesAndLoadsThroughBackendFileOperations)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(RecordArchiveTestGraph(Graph));
		const auto Path = fileops::TemporaryPath(std::filesystem::temp_directory_path() / "ArdaGraph.archive", "test");
		fileops::FArdaTemporaryFiles Temporary;
		Temporary.mPaths.push_back(Path);
		ASSERT_TRUE(Graph.Save(Path));
		auto Loaded = FArdaDependencyGraph::Load(Path);
		ASSERT_TRUE(Loaded) << Loaded.mStatus.mMessage.c_str();
		EXPECT_EQ(Loaded.mValue->GetTopology().GetNodeCount(), 2u);
		EXPECT_FALSE(FArdaDependencyGraph::Load(Path / "missing"));
	}

	TEST(ArdaGraphSerialization, PreprocessedArchivesPreserveCullingTopologyAndRuleOptions)
	{
		FArdaDependencyGraph Graph;
		{
			FArdaGraphEdit Edit(Graph);
			FArdaInductorOptions Options;
			Options.mMinimumAsyncChain = 7;
			Options.mFramesInFlight = 3;
			ASSERT_TRUE(Edit.SetOptions(Options));
			const auto Used = Edit.Invoke<FArdaArchiveTestNode>(FArdaArchiveTestParameters{}, "retained");
			Edit.Invoke<FArdaArchiveTestNode>(FArdaArchiveTestParameters{}, "discarded");
			Edit.Keep(Used.GetBuffer());
			ASSERT_TRUE(Edit.Commit());
		}
		auto Preprocessed = Graph.Preprocess();
		ASSERT_TRUE(Preprocessed) << Preprocessed.mStatus.mMessage.c_str();
		ASSERT_TRUE(Preprocessed.mValue->IsPreprocessed());
		const auto Encoded = Preprocessed.mValue->Serialize();
		ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
		auto Decoded = FArdaDependencyGraph::Deserialize(Encoded.mValue);
		ASSERT_TRUE(Decoded) << Decoded.mStatus.mMessage.c_str();
		ASSERT_TRUE(Decoded.mValue->IsPreprocessed());
		EXPECT_EQ(Decoded.mValue->GetOptions().mMinimumAsyncChain, 7u);
		EXPECT_EQ(Decoded.mValue->GetOptions().mFramesInFlight, 3u);
		const auto Discarded = Decoded.mValue->FindNode("discarded");
		ASSERT_TRUE(Discarded);
		EXPECT_TRUE(Decoded.mValue->GetTopology().TryGetNode(Discarded)->mPayload.mbCulled);
		const auto Reencoded = Decoded.mValue->Serialize();
		ASSERT_TRUE(Reencoded) << Reencoded.mStatus.mMessage.c_str();
		EXPECT_EQ(Reencoded.mValue, Encoded.mValue);
		const auto Compiled = Decoded.mValue->CompileAndWait();
		EXPECT_TRUE(Compiled) << Compiled.mMessage.c_str();
		EXPECT_TRUE(Decoded.mValue->GetTopology().TryGetNode(Discarded)->mPayload.mbCulled);
	}

	TEST(ArdaGraphSerialization, RejectsInvalidResourceReferencesAndChangedSchemas)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(RecordArchiveTestGraph(Graph));
		const auto Encoded = FArdaGraphSerialization::Serialize(Graph);
		ASSERT_TRUE(Encoded);
		const auto Member = Encoded.mValue.find("mOutput");
		ASSERT_NE(Member, eastl::string::npos);
		auto InvalidReference = Encoded.mValue;
		for (size_t Index = Member + 7; Index < Member + 11; ++Index)
		{
			InvalidReference[Index] = static_cast<char>(0xff);
		}
		// Even an empty output must not silently allocate a resource omitted from the document.
		EXPECT_FALSE(FArdaGraphSerialization::Deserialize(InvalidReference));
		InvalidReference[Member + 7] = static_cast<char>(0xfe);
		EXPECT_FALSE(FArdaGraphSerialization::Deserialize(InvalidReference));
		auto ChangedSchema = Encoded.mValue;
		ChangedSchema[Member] = 'X';
		EXPECT_FALSE(FArdaGraphSerialization::Deserialize(ChangedSchema));
		auto ChangedVersion = Encoded.mValue;
		const eastl::string Type = "test.archive-output";
		const auto TypePosition = ChangedVersion.find(Type);
		ASSERT_NE(TypePosition, eastl::string::npos);
		ChangedVersion[TypePosition + Type.size()] = 4;
		const auto VersionResult = FArdaGraphSerialization::Deserialize(ChangedVersion);
		EXPECT_FALSE(VersionResult);
		EXPECT_EQ(VersionResult.mStatus.mCode, EArdaRHIResult::Unsupported);
	}

	TEST(ArdaGraphSerialization, RestoresArrayFamiliesAndRejectsDuplicateLanes)
	{
		FArdaDependencyGraph Graph;
		{
			FArdaGraphEdit Edit(Graph);
			TArdaNodeArray<FArdaArchiveTestParameters> Parameters{{}, {}};
			const auto Operations = Edit.Invoke<FArdaArchiveTestNode>(Parameters);
			ASSERT_EQ(Operations.size(), 2u);
			ASSERT_TRUE(Edit.Commit());
		}
		auto Encoded = FArdaGraphSerialization::Serialize(Graph);
		ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
		auto Decoded = FArdaGraphSerialization::Deserialize(Encoded.mValue);
		ASSERT_TRUE(Decoded) << Decoded.mStatus.mMessage.c_str();
		const auto& Topology = Decoded.mValue->GetTopology();
		const auto& First = Topology.TryGetNode(Topology.GetNodes()[0])->mPayload;
		const auto& Second = Topology.TryGetNode(Topology.GetNodes()[1])->mPayload;
		EXPECT_EQ(First.mArrayInvocation, Second.mArrayInvocation);
		EXPECT_EQ(First.mArrayLane, 0u);
		EXPECT_EQ(Second.mArrayLane, 1u);
		const eastl::string Type = "test.archive-output";
		FArdaGraphArchiveWriter Signature;
		Signature.Fields(Type, uint32_t{3});
		const auto FirstType = Encoded.mValue.find(Signature.GetData());
		const auto LastType = Encoded.mValue.find(Signature.GetData(), FirstType + Signature.GetData().size());
		ASSERT_NE(LastType, eastl::string::npos);
		const size_t PayloadSizeOffset = LastType + Signature.GetData().size();
		uint64_t PayloadBytes = 0;
		for (size_t Byte = 0; Byte < sizeof(uint64_t); ++Byte)
		{
			PayloadBytes |= uint64_t(static_cast<uint8_t>(Encoded.mValue[PayloadSizeOffset + Byte])) << (Byte * 8);
		}
		const auto LaneOffset =
		    PayloadSizeOffset + sizeof(uint64_t) + PayloadBytes + 1 + sizeof(uint32_t) + sizeof(uint64_t);
		ASSERT_LT(LaneOffset + sizeof(uint64_t), Encoded.mValue.size());
		for (size_t Index = LaneOffset; Index < LaneOffset + sizeof(uint64_t); ++Index)
		{
			Encoded.mValue[Index] = 0;
		}
		EXPECT_FALSE(FArdaGraphSerialization::Deserialize(Encoded.mValue));
	}

	TEST(ArdaGraphSerialization, RejectsOpenEditsAndUnsupportedNativeFields)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		EXPECT_FALSE(Graph.Serialize());
		FArdaGraphArchiveWriter Archive;
		const void* Pointer = &Graph;
		Archive.Write(Pointer);
		EXPECT_EQ(Archive.GetStatus().mCode, EArdaRHIResult::Unsupported);
	}

	TEST(ArdaGraphSerialization, RestoresBuiltinUploadsAndResourceConnections)
	{
		FArdaDependencyGraph Graph;
		{
			FArdaGraphEdit Edit(Graph);
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 64;
			const auto Input = AllocateBuffer(Edit, Desc, "Input");
			const auto Uploaded = Upload(Edit, Input, eastl::vector<uint8_t>(64, 39));
			const auto Copied = Clone(Edit, Uploaded);
			Edit.Keep(Copied);
			ASSERT_TRUE(Edit.Commit());
		}
		const auto Encoded = FArdaGraphSerialization::Serialize(Graph);
		ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
		auto Decoded = FArdaGraphSerialization::Deserialize(Encoded.mValue);
		ASSERT_TRUE(Decoded) << Decoded.mStatus.mMessage.c_str();
		const auto Reencoded = FArdaGraphSerialization::Serialize(*Decoded.mValue);
		ASSERT_TRUE(Reencoded);
		EXPECT_EQ(Reencoded.mValue, Encoded.mValue);
		const auto Compiled = Decoded.mValue->CompileAndWait();
		EXPECT_TRUE(Compiled) << Compiled.mMessage.c_str();
		EXPECT_EQ(Decoded.mValue->GetTopology().GetNodeCount(), 2u);
	}

	TEST(ArdaGraphArchive, RejectsInvalidHandlesBooleansAndOversizedCollections)
	{
		FArdaGraphArchiveWriter Writer;
		Writer.WriteInteger(2, 1);
		FArdaGraphArchiveReader Boolean(Writer.GetData());
		bool bValue = false;
		Boolean.Read(bValue);
		EXPECT_FALSE(Boolean.GetStatus());
		FArdaGraphArchiveWriter Counts;
		Counts.WriteInteger(UINT64_MAX, 8);
		FArdaGraphArchiveReader Reader(Counts.GetData());
		eastl::vector<uint8_t> Values;
		Reader.Read(Values);
		EXPECT_FALSE(Reader.GetStatus());
		EXPECT_TRUE(Values.empty());
		FArdaGraphArchiveWriter Handles;
		Handles.WriteInteger(7, 4);
		FArdaGraphArchiveReader Resource(Handles.GetData());
		EXPECT_FALSE(Resource.ReadResource());
		EXPECT_FALSE(Resource.GetStatus());
		FArdaGraphArchiveReader Node(Handles.GetData());
		EXPECT_FALSE(Node.ReadNode());
		EXPECT_FALSE(Node.GetStatus());
	}
}
