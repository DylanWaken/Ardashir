#include "ArdaDependencyGraph.h"

#include <gtest/gtest.h>
#include <cstring>

namespace arda
{
	namespace
	{
		struct FArdaPackageTestState
		{
			eastl::string mName;
			EArdaRHIDataType mDataType = EArdaRHIDataType::Float32;
			eastl::vector<uint64_t> mDimensions;
			eastl::vector<uint8_t> mBytes;
		};

		eastl::vector<FArdaGraphPackageStateSlot> MakePackageTestSlots()
		{
			return {{"weight", 0, {EArdaRHIDataType::Float32, {4}, {1}, 0}, 16},
			    {"bias", 1, {EArdaRHIDataType::Float32, {}, {}, 0}, 4}};
		}

		eastl::string MakePackageTestDescriptor(uint32_t Version = 2,
		    const eastl::vector<FArdaGraphPackageStateSlot>& Slots = MakePackageTestSlots(),
		    bool bWriteState = false)
		{
			FArdaGraphArchiveWriter Archive;
			Archive.WriteBytes("ARDAGPU\0", 8);
			Archive.Fields(Version, eastl::string("state-test-model"), uint64_t{0}, uint64_t{3});
			Archive.Fields(eastl::string("weights"),
			    uint64_t{16},
			    Version == 1 ? eastl::vector<uint8_t>(16, 11) : eastl::vector<uint8_t>{});
			Archive.Fields(eastl::string("bias"),
			    uint64_t{4},
			    Version == 1 ? eastl::vector<uint8_t>(4, 12) : eastl::vector<uint8_t>{});
			Archive.Fields(eastl::string("output"), uint64_t{16}, eastl::vector<uint8_t>{});
			Archive.Fields(uint64_t{1},
			    eastl::string("output"),
			    uint32_t{2},
			    uint8_t{2},
			    EArdaRHIDataType::Float32,
			    eastl::vector<uint64_t>{4},
			    eastl::vector<int64_t>{1},
			    uint64_t{0},
			    uint64_t{0},
			    uint64_t{2});
			Archive.Fields(uint8_t{2}, uint32_t{0}, uint64_t{0}, uint32_t{2}, uint64_t{0}, uint64_t{16});
			Archive
			    .Fields(uint8_t{2}, uint32_t{1}, uint64_t{0}, uint32_t(bWriteState ? 0 : 2), uint64_t{12}, uint64_t{4});
			if (Version == 2)
			{
				Archive.Write(uint64_t(Slots.size()));
				for (const auto& Slot : Slots)
				{
					Archive.Fields(Slot.mName,
					    Slot.mBuffer,
					    Slot.mDesc.mDataType,
					    Slot.mDesc.mDimensions,
					    Slot.mDesc.mStrides,
					    Slot.mDesc.mByteOffset);
				}
			}
			return Archive.GetData();
		}

		eastl::vector<FArdaPackageTestState> MakePackageTestState(uint8_t Value = 21)
		{
			return {{"weight", EArdaRHIDataType::Float32, {4}, eastl::vector<uint8_t>(16, Value)},
			    {"bias", EArdaRHIDataType::Float32, {}, eastl::vector<uint8_t>(4, uint8_t(Value + 1))}};
		}

		eastl::string SerializePackageTestState(const eastl::vector<FArdaPackageTestState>& State)
		{
			FArdaGraphArchiveWriter Archive;
			Archive.WriteBytes("ARDASTAT", 8);
			Archive.Fields(uint32_t{1}, uint64_t(State.size()));
			for (const auto& Entry : State)
			{
				Archive.Fields(Entry.mName, Entry.mDataType, Entry.mDimensions, Entry.mBytes);
			}
			return Archive.GetData();
		}

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaPackageStateSubgraphParameters)
			ARDA_NODE_BUFFER(mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaPackageStateSubgraphNode final : public TArdaNodeDefinition<FArdaPackageStateSubgraphNode,
		                                                FArdaPackageStateSubgraphParameters,
		                                                EArdaNodeType::Subgraph>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.package.state-subgraph", 1};
			}

			static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
			{
				FArdaRHIBufferDesc Desc;
				Desc.mByteSize = 16;
				Desc.mUsage = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::UnorderedAccess;
				return Context.Buffer(Parameters.mOutput, "Destination", Desc);
			}

			static FArdaRHIStatus Build(FArdaSubgraphContext& Context, const FArdaParameters&)
			{
				const auto Model = FArdaGraphPackage::Deserialize(MakePackageTestDescriptor());
				if (!Model)
				{
					return Model.mStatus;
				}
				const auto Loaded =
				    Model.mValue->DeserializeStateDict(SerializePackageTestState(MakePackageTestState()));
				if (!Loaded)
				{
					return Loaded.mStatus;
				}
				const auto Invocation = Loaded.mValue->Invoke(Context.GetEdit(), {}, "model");
				if (!Invocation)
				{
					return Invocation.mStatus;
				}
				return Context.DeclareOutput("Destination", Invocation.mValue.Find("output")->mBuffer);
			}
		};

		ARDA_DECLARE_GRAPH_NODE(FArdaPackageStateSubgraphNode, BuildPackageStateSubgraph);
		ARDA_DEFINE_GRAPH_NODE(FArdaPackageStateSubgraphNode, BuildPackageStateSubgraph);
	}

	TEST(ArdaGraphPackageState, DescriptorReflectsRequiredStateAndRejectsUnboundInvocation)
	{
		const auto Model = FArdaGraphPackage::Deserialize(MakePackageTestDescriptor());
		ASSERT_TRUE(Model) << Model.mStatus.mMessage.c_str();
		ASSERT_EQ(Model.mValue->GetStateSlots().size(), 2u);
		EXPECT_EQ(Model.mValue->GetStateSlots()[0].mName, "weight");
		EXPECT_EQ(Model.mValue->GetStateSlots()[0].mStorageByteSize, 16u);
		EXPECT_EQ(Model.mValue->GetStateSlots()[1].mDesc.mDimensions.size(), 0u);
		EXPECT_FALSE(Model.mValue->HasStateDict());
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		EXPECT_FALSE(Model.mValue->Invoke(Edit, {}, "unbound"));
		EXPECT_FALSE(Graph.FindResource("unbound/weights"));
		EXPECT_EQ(Graph.GetTopology().GetNodeCount(), 0u);
		EXPECT_FALSE(Edit.Commit());
		ASSERT_TRUE(Edit.Cancel());
	}

	TEST(ArdaGraphPackageState, LoadingDifferentCheckpointsPreservesDescriptorAndPreviousInvocations)
	{
		const auto Model = FArdaGraphPackage::Deserialize(MakePackageTestDescriptor());
		ASSERT_TRUE(Model);
		const auto First = Model.mValue->DeserializeStateDict(SerializePackageTestState(MakePackageTestState(21)));
		ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
		EXPECT_TRUE(First.mValue->HasStateDict());
		EXPECT_FALSE(Model.mValue->HasStateDict());
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(First.mValue->Invoke(Edit, {}, "first"));
		const auto Second = First.mValue->DeserializeStateDict(SerializePackageTestState(MakePackageTestState(41)));
		ASSERT_TRUE(Second) << Second.mStatus.mMessage.c_str();
		ASSERT_TRUE(Second.mValue->Invoke(Edit, {}, "second"));
		ASSERT_TRUE(First.mValue->Invoke(Edit, {}, "first-again"));
		for (const auto* Name : {"first", "second", "first-again"})
		{
			const auto* Resource = Graph.FindResource(Graph.FindResource(eastl::string(Name) + "/weights"));
			ASSERT_NE(Resource, nullptr);
			ASSERT_EQ(Resource->mBufferInitializer.GetSize(), 16u);
			const auto Expected = eastl::vector<uint8_t>(16, std::strcmp(Name, "second") == 0 ? 41 : 21);
			EXPECT_EQ(std::memcmp(Resource->mBufferInitializer.GetData(), Expected.data(), Expected.size()), 0);
		}
		ASSERT_TRUE(Edit.Commit());
	}

	TEST(ArdaGraphPackageState, RejectsMissingExtraDuplicateUnknownAndIncompatibleStateWithoutChangingSnapshot)
	{
		const auto Model = FArdaGraphPackage::Deserialize(MakePackageTestDescriptor());
		ASSERT_TRUE(Model);
		const auto Loaded = Model.mValue->DeserializeStateDict(SerializePackageTestState(MakePackageTestState()));
		ASSERT_TRUE(Loaded);
		for (uint32_t Case = 0; Case < 8; ++Case)
		{
			auto State = MakePackageTestState();
			switch (Case)
			{
			case 0:
				State.pop_back();
				break;
			case 1:
				State.push_back({"extra", EArdaRHIDataType::Float32, {}, {0, 0, 0, 0}});
				break;
			case 2:
				State[1] = State[0];
				break;
			case 3:
				State[0].mName = "unknown";
				break;
			case 4:
				State[0].mDataType = EArdaRHIDataType::Int32;
				break;
			case 5:
				State[0].mDimensions = {2, 2};
				break;
			case 6:
				State[0].mBytes.pop_back();
				break;
			case 7:
				State[1].mDimensions = {1};
				break;
			}
			SCOPED_TRACE(Case);
			EXPECT_FALSE(Loaded.mValue->DeserializeStateDict(SerializePackageTestState(State)));
			EXPECT_TRUE(Loaded.mValue->HasStateDict());
		}
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Loaded.mValue->Invoke(Edit, {}, "after-failure"));
		const auto* Weights = Graph.FindResource(Graph.FindResource("after-failure/weights"));
		ASSERT_NE(Weights, nullptr);
		ASSERT_EQ(Weights->mBufferInitializer.GetSize(), 16u);
		EXPECT_EQ(Weights->mBufferInitializer.GetData()[0], 21u);
		ASSERT_TRUE(Edit.Commit());
	}

	TEST(ArdaGraphPackageState, RejectsMalformedTruncatedAndTrailingCheckpointBytes)
	{
		const auto Model = FArdaGraphPackage::Deserialize(MakePackageTestDescriptor());
		ASSERT_TRUE(Model);
		const auto Bytes = SerializePackageTestState(MakePackageTestState());
		for (size_t Size = 0; Size < Bytes.size(); ++Size)
		{
			EXPECT_FALSE(Model.mValue->DeserializeStateDict(Bytes.substr(0, Size))) << Size;
		}
		EXPECT_FALSE(Model.mValue->DeserializeStateDict(Bytes + "trailing"));
		auto InvalidVersion = Bytes;
		InvalidVersion[8] = 2;
		EXPECT_FALSE(Model.mValue->DeserializeStateDict(InvalidVersion));
		EXPECT_FALSE(Model.mValue->HasStateDict());
	}

	TEST(ArdaGraphPackageState, RejectsInvalidSlotLayoutAliasesAndWrites)
	{
		for (uint32_t Case = 0; Case < 8; ++Case)
		{
			auto Slots = MakePackageTestSlots();
			switch (Case)
			{
			case 0:
				Slots[1].mName = Slots[0].mName;
				break;
			case 1:
				Slots[1].mBuffer = Slots[0].mBuffer;
				break;
			case 2:
				Slots[0].mBuffer = 3;
				break;
			case 3:
				Slots[0].mDesc.mStrides = {2};
				break;
			case 4:
				Slots[0].mDesc.mByteOffset = 4;
				break;
			case 5:
				Slots[0].mDesc.mDimensions = {3};
				break;
			case 6:
				Slots[0].mBuffer = 2;
				break;
			case 7:
				Slots[0].mDesc.mDataType = EArdaRHIDataType::Unknown;
				break;
			}
			SCOPED_TRACE(Case);
			EXPECT_FALSE(FArdaGraphPackage::Deserialize(MakePackageTestDescriptor(2, Slots)));
		}
		EXPECT_FALSE(FArdaGraphPackage::Deserialize(MakePackageTestDescriptor(2, MakePackageTestSlots(), true)));
	}

	TEST(ArdaGraphPackageState, RetainsLegacyEmbeddedWeightSupport)
	{
		const auto Model = FArdaGraphPackage::Deserialize(MakePackageTestDescriptor(1));
		ASSERT_TRUE(Model) << Model.mStatus.mMessage.c_str();
		EXPECT_TRUE(Model.mValue->GetStateSlots().empty());
		EXPECT_TRUE(Model.mValue->HasStateDict());
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Model.mValue->Invoke(Edit, {}, "legacy"));
		const auto* Weights = Graph.FindResource(Graph.FindResource("legacy/weights"));
		ASSERT_NE(Weights, nullptr);
		ASSERT_EQ(Weights->mBufferInitializer.GetSize(), 16u);
		EXPECT_EQ(Weights->mBufferInitializer.GetData()[0], 11u);
		ASSERT_TRUE(Edit.Commit());
	}

	TEST(ArdaGraphPackageState, LoadedModelExpandsInsideTypedSubgraphAndJoinsParentGraph)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Parent = BuildPackageStateSubgraph(Edit, FArdaPackageStateSubgraphParameters{});
		ASSERT_TRUE(Parent) << Edit.GetStatus().mMessage.c_str();
		Edit.Keep(Parent.GetBuffer());
		ASSERT_TRUE(Edit.Commit());
		ASSERT_EQ(Graph.GetTopology().GetNodeCount(), 1u);
		const auto Expanded = Graph.Preprocess();
		ASSERT_TRUE(Expanded) << Expanded.mStatus.mMessage.c_str();
		EXPECT_EQ(Expanded.mValue->GetTopology().GetNodeCount(), 2u);
		for (const auto Handle : Expanded.mValue->GetTopology().GetNodes())
		{
			EXPECT_FALSE(bool(Expanded.mValue->GetTopology().TryGetNode(Handle)->mPayload.mDefinition->mExpand));
		}
	}
}
