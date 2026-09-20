#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/DefaultNodes/ArdaDependencyGraphNodes.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"
#include "Graph/Node/ArdaDependencyNode.h"
#include "ArdaBackend.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "ArdaTestBackend.h"

#include <gtest/gtest.h>
#include <EASTL/algorithm.h>

namespace arda::operation_tests
{
	using namespace arda;

	FArdaRHIBufferDesc MakeOperationBufferDesc()
	{
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 16;
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		return Desc;
	}

	FArdaRHITextureDesc MakeOperationTextureDesc()
	{
		FArdaRHITextureDesc Desc;
		Desc.mWidth = 4;
		Desc.mHeight = 4;
		Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		Desc.mUsage = EArdaRHITextureUsage::RenderTarget;
		return Desc;
	}

	struct FArdaOperationOutputParameters
	{
		FArdaDependencyResourceHandle mOutput;
		uint32_t mValue = 7;
		bool mbReject = false;
	};

	class FArdaOperationOutputNode final
	    : public TArdaCopyDependencyNode<FArdaOperationOutputNode, FArdaOperationOutputParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.operation-facade-output", 1};
		}

		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
		{
			return Context.Buffer(Parameters.mOutput, "Result", MakeOperationBufferDesc());
		}

		static FArdaRHIStatus Validate(const FArdaParameters& Parameters)
		{
			return Parameters.mbReject
			    ? FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Operation rejected after output declaration.")
			    : FArdaRHIStatus{};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters)
		{
			return FArdaDependencyKeyBuilder{}.Resource(Parameters.mOutput).Value(Parameters.mValue).Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mAccesses.push_back(
			    {Parameters.mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest});
			return Desc;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaOperationOutputNode, EmitOperationOutput);
	ARDA_DEFINE_GRAPH_NODE(FArdaOperationOutputNode, EmitOperationOutput);

	TEST(ArdaGraphOperations, IdenticalInvocationsRemainDistinctAndPreserveInterveningReads)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.IsOpen());
		const auto Buffer = AllocateBuffer(Edit, MakeOperationBufferDesc());
		ASSERT_TRUE(Buffer);
		const auto First = UploadBuffer(Edit, {Buffer, eastl::vector<uint8_t>(16, 7)});
		const auto Before = Readback(Edit, Buffer);
		const auto Second = UploadBuffer(Edit, {Buffer, eastl::vector<uint8_t>(16, 7)});
		const auto After = Readback(Edit, Buffer);
		ASSERT_TRUE(Edit.GetStatus()) << Edit.GetStatus().mMessage.c_str();
		ASSERT_TRUE(First.GetHandle());
		ASSERT_TRUE(Second.GetHandle());
		EXPECT_NE(First.GetHandle(), Second.GetHandle());
		EXPECT_EQ(First.GetBuffer().GetHandle(), Buffer.GetHandle());
		EXPECT_EQ(Second.GetBuffer().GetHandle(), Buffer.GetHandle());
		EXPECT_NE(Before, After);
		const auto Attached = Graph.GetTopology().GetNodes();
		ASSERT_EQ(Attached.size(), 4u);
		const auto Status = Edit.Commit();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_FALSE(Edit.IsOpen());
		EXPECT_FALSE(Graph.IsEditing());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder, Attached);
	}

	TEST(ArdaGraphOperations, CustomNodeRegistersAndReturnsIndependentInferredOutputs)
	{
		auto& Registry = FArdaNodeRegistry::Get();
		const auto Metadata = FArdaOperationOutputNode::GetMetadata();
		if (Registry.Find(Metadata.mName))
		{
			ASSERT_TRUE(Registry.Unregister(Metadata.mName));
		}
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaOperationOutputParameters Parameters;
		const auto First = EmitOperationOutput(Edit, Parameters);
		const auto Second = EmitOperationOutput(Edit, Parameters);
		ASSERT_TRUE(Edit.GetStatus()) << Edit.GetStatus().mMessage.c_str();
		const auto FirstOutput = First.GetBuffer("Result");
		const auto SecondOutput = Second.GetBuffer("Result");
		ASSERT_TRUE(FirstOutput);
		ASSERT_TRUE(SecondOutput);
		EXPECT_NE(FirstOutput.GetHandle(), SecondOutput.GetHandle());
		EXPECT_FALSE(Parameters.mOutput);
		Parameters.mValue = 99;
		const auto Definition = Registry.Find(Metadata.mName);
		ASSERT_TRUE(Definition);
		const auto* Node = Graph.GetTopology().TryGetNode(First.GetHandle());
		ASSERT_NE(Node, nullptr);
		EXPECT_EQ(Node->mPayload.mDefinition, Definition);
		EXPECT_EQ(Node->mPayload.mCanonicalKey,
		    FArdaDependencyKeyBuilder{}.Resource(FirstOutput.GetHandle()).Value(uint32_t{7}).Build());
		Edit.Keep(FirstOutput);
		Edit.Keep(SecondOutput);
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 2u);
	}

	TEST(ArdaGraphOperations, FirstFailurePreventsFurtherMutationAndRequiresCancellation)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaOperationOutputParameters Parameters;
		Parameters.mbReject = true;
		const auto Failed = EmitOperationOutput(Edit, Parameters);
		EXPECT_FALSE(Failed.GetHandle());
		const auto FirstError = Edit.GetStatus();
		ASSERT_EQ(FirstError.mCode, EArdaRHIResult::Unsupported);
		EXPECT_FALSE(AllocateBuffer(Edit, MakeOperationBufferDesc()));
		EXPECT_FALSE(EmitOperationOutput(Edit, {}).GetHandle());
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		const auto Commit = Edit.Commit();
		EXPECT_EQ(Commit.mCode, FirstError.mCode);
		EXPECT_EQ(Commit.mMessage, FirstError.mMessage);
		EXPECT_TRUE(Edit.IsOpen());
		EXPECT_TRUE(Graph.IsEditing());
		ASSERT_TRUE(Edit.Cancel());
		EXPECT_FALSE(Edit.IsOpen());
		EXPECT_FALSE(Graph.IsEditing());
		FArdaGraphEdit Retry(Graph);
		const auto Buffer = AllocateBuffer(Retry, MakeOperationBufferDesc());
		ASSERT_TRUE(Buffer);
		EXPECT_EQ(Buffer.GetHandle().mIndex, 0u);
		ASSERT_TRUE(Retry.Cancel());
	}

	TEST(ArdaGraphOperations, RegistryInvocationUsesFreshIdentitiesAndChecksThePublicParameterSchema)
	{
		ASSERT_TRUE(FArdaOperationOutputNode::Register());
		const auto Definition = FArdaOperationOutputNode::GetMetadata().mName;
		FArdaDependencyGraph Graph;
		{
			FArdaGraphEdit Edit(Graph);
			const auto First = Edit.Invoke(Definition, FArdaOperationOutputParameters{});
			const auto Second = Edit.Invoke(Definition, FArdaOperationOutputParameters{});
			ASSERT_TRUE(First.GetHandle());
			ASSERT_TRUE(Second.GetHandle());
			EXPECT_NE(First.GetHandle(), Second.GetHandle());
			const auto FirstOutput = First.GetBuffer("Result");
			const auto SecondOutput = Second.GetBuffer("Result");
			EXPECT_NE(FirstOutput.GetHandle(), SecondOutput.GetHandle());
			Edit.Keep(FirstOutput);
			Edit.Keep(SecondOutput);
			ASSERT_TRUE(Edit.Commit());
		}
		const auto OriginalNodes = Graph.GetTopology().GetNodes();
		{
			FArdaGraphEdit Edit(Graph);
			const auto Rejected = Edit.Invoke(Definition, FArdaMemoryClearBufferParameters{});
			EXPECT_FALSE(Rejected.GetHandle());
			EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
			EXPECT_EQ(Graph.GetTopology().GetNodes(), OriginalNodes);
			EXPECT_FALSE(Edit.Commit());
		}
		{
			FArdaGraphEdit Edit(Graph);
			EXPECT_FALSE(Edit.Invoke("test.operation-facade-missing", FArdaOperationOutputParameters{}).GetHandle());
			EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
			EXPECT_EQ(Graph.GetTopology().GetNodes(), OriginalNodes);
		}
	}

	TEST(ArdaGraphOperations, EmptyExplicitDestinationsNeverInferNewResources)
	{
		FArdaDependencyGraph Graph;
		{
			FArdaGraphEdit Edit(Graph);
			EXPECT_FALSE(Upload(Edit, FArdaGraphBuffer{}, {1, 2, 3, 4}));
			EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
			EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
			EXPECT_FALSE(AllocateBuffer(Edit, MakeOperationBufferDesc()));
		}
		{
			FArdaGraphEdit Edit(Graph);
			const auto Source = AllocateBuffer(Edit, MakeOperationBufferDesc());
			ASSERT_TRUE(Source);
			EXPECT_FALSE(CopyTo(Edit, Source, FArdaGraphBuffer{}));
			EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
			EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
			EXPECT_FALSE(AllocateBuffer(Edit, MakeOperationBufferDesc()));
		}
	}

	TEST(ArdaGraphOperations, DefaultMixedCopiesInferPortableRowPitchAndChooseTheTransferDefinitions)
	{
		const auto TextureDesc = MakeOperationTextureDesc();
		const auto Footprint = GetArdaRHITextureBufferFootprint(TextureDesc, {});
		ASSERT_TRUE(Footprint) << Footprint.mStatus.mMessage.c_str();
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = Footprint.mValue.mByteSize;
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Source = AllocateBuffer(Edit, BufferDesc);
		const auto Texture = AllocateTexture(Edit, TextureDesc);
		const auto Destination = AllocateBuffer(Edit, BufferDesc);
		Upload(Edit, Source, eastl::vector<uint8_t>(static_cast<size_t>(BufferDesc.mByteSize), 42));
		EXPECT_EQ(CopyTo(Edit, Source, Texture).GetHandle(), Texture.GetHandle());
		EXPECT_EQ(CopyTo(Edit, Texture, Destination).GetHandle(), Destination.GetHandle());
		ASSERT_TRUE(Edit.GetStatus()) << Edit.GetStatus().mMessage.c_str();
		const auto Nodes = Graph.GetTopology().GetNodes();
		ASSERT_EQ(Nodes.size(), 3u);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Nodes[1])->mPayload.mDefinition->mName,
		    FArdaGraphBufferToTextureNode::GetMetadata().mName);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Nodes[2])->mPayload.mDefinition->mName,
		    FArdaGraphTextureToBufferNode::GetMetadata().mName);
		Edit.Keep(Destination);
		const auto Compiled = Edit.Commit();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder, Nodes);
	}

	TEST(ArdaGraphOperations, AbandonedEditRestoresCommittedNodesAndInvalidatesNewResources)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphBuffer Committed;
		{
			FArdaGraphEdit Edit(Graph);
			Committed = AllocateBuffer(Edit, MakeOperationBufferDesc());
			Upload(Edit, Committed, eastl::vector<uint8_t>(16, 11));
			Edit.Keep(Committed);
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Graph.Compile());
		const auto Revision = Graph.GetCompileResult().mRevision;
		const auto OriginalOrder = Graph.GetCompileResult().mExecutionOrder;
		EXPECT_FALSE(Graph.Execute().mStatus);
		FArdaGraphBuffer Abandoned;
		{
			FArdaGraphEdit Edit(Graph);
			Abandoned = AllocateBuffer(Edit, MakeOperationBufferDesc());
			ASSERT_TRUE(Abandoned);
			ASSERT_TRUE(Edit.GetStatus());
		}
		EXPECT_FALSE(Graph.IsEditing());
		EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder, OriginalOrder);
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 1u);
		EXPECT_NE(Graph.FindResource(Committed.GetHandle()), nullptr);
		EXPECT_EQ(Graph.FindResource(Abandoned.GetHandle()), nullptr);
		FArdaGraphEdit Retry(Graph);
		const auto Replacement = AllocateBuffer(Retry, MakeOperationBufferDesc());
		ASSERT_TRUE(Replacement);
		EXPECT_EQ(Replacement.GetHandle().mIndex, Abandoned.GetHandle().mIndex);
		EXPECT_NE(Replacement.GetHandle().mGeneration, Abandoned.GetHandle().mGeneration);
		EXPECT_FALSE(Retry.UseBuffer(Abandoned.GetHandle()));
		EXPECT_EQ(Retry.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
	}

	TEST(ArdaGraphOperations, RejectedNestedEditNeverCancelsTheOwningTransaction)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Owner(Graph);
		const auto Buffer = AllocateBuffer(Owner, MakeOperationBufferDesc());
		{
			FArdaGraphEdit Rejected(Graph);
			EXPECT_FALSE(Rejected.IsOpen());
			EXPECT_FALSE(Rejected.GetStatus());
			EXPECT_FALSE(AllocateBuffer(Rejected, MakeOperationBufferDesc()));
		}
		EXPECT_TRUE(Graph.IsEditing());
		EXPECT_TRUE(Owner.IsOpen());
		ASSERT_TRUE(Owner.GetStatus());
		EXPECT_NE(Graph.FindResource(Buffer.GetHandle()), nullptr);
		Upload(Owner, Buffer, eastl::vector<uint8_t>(16, 3));
		Owner.Keep(Buffer);
		ASSERT_TRUE(Owner.Commit());
	}

	TEST(ArdaGraphOperations, FailedCompilationRetainsClosedEditsUntilTheDraftIsDiscarded)
	{
		FArdaDependencyGraph Graph;
		{
			FArdaGraphEdit Edit(Graph);
			const auto Buffer = AllocateBuffer(Edit, MakeOperationBufferDesc());
			Upload(Edit, Buffer, eastl::vector<uint8_t>(16, 19));
			Edit.Keep(Buffer);
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Graph.Compile());
		const auto Revision = Graph.GetCompileResult().mRevision;
		const auto Schedule = Graph.GetCompileResult().mExecutionOrder;
		EXPECT_FALSE(Graph.Execute().mStatus);
		FArdaGraphBuffer Uninitialized;
		{
			FArdaGraphEdit Edit(Graph);
			Uninitialized = AllocateBuffer(Edit, MakeOperationBufferDesc());
			ASSERT_TRUE(Readback(Edit, Uninitialized));
			ASSERT_TRUE(Edit.GetStatus());
			ASSERT_TRUE(Edit.Commit());
			EXPECT_FALSE(Graph.Compile());
			EXPECT_TRUE(Edit.GetStatus());
			EXPECT_FALSE(Edit.IsOpen());
			EXPECT_FALSE(Graph.IsEditing());
		}
		EXPECT_FALSE(Graph.IsEditing());
		EXPECT_NE(Graph.FindResource(Uninitialized.GetHandle()), nullptr);
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 1u);
		ASSERT_TRUE(Graph.DiscardPendingEdits());
		EXPECT_EQ(Graph.FindResource(Uninitialized.GetHandle()), nullptr);
		EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder, Schedule);
		EXPECT_EQ(Graph.GetTopology().GetNodes(), Schedule);
	}

	TEST(ArdaGraphOperations, ResourceConversionsRejectWrongKindsAndForeignGraphs)
	{
		FArdaDependencyGraph Graph;
		FArdaDependencyGraph Other;
		FArdaGraphEdit OtherEdit(Other);
		const auto Foreign = AllocateBuffer(OtherEdit, MakeOperationBufferDesc());
		{
			FArdaGraphEdit Edit(Graph);
			EXPECT_FALSE(Edit.UseBuffer(Foreign.GetHandle()));
			EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
		}
		{
			FArdaGraphEdit Edit(Graph);
			const auto Texture = AllocateTexture(Edit, MakeOperationTextureDesc());
			ASSERT_TRUE(Texture);
			EXPECT_FALSE(Edit.UseBuffer(Texture.GetHandle()));
			EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
		}
		{
			FArdaGraphEdit Edit(Graph);
			const auto Buffer = AllocateBuffer(Edit, MakeOperationBufferDesc());
			ASSERT_TRUE(Buffer);
			EXPECT_FALSE(Edit.UseTexture(Buffer.GetHandle()));
			EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
		}
	}

	TEST(ArdaGraphOperations, AClosedScopeCannotCancelOrModifyANewerTransaction)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Obsolete(Graph);
		ASSERT_TRUE(Obsolete.Cancel());
		FArdaGraphEdit Current(Graph);
		const auto Created = AllocateBuffer(Current, MakeOperationBufferDesc(), "new transaction");
		ASSERT_TRUE(Created);
		EXPECT_FALSE(Obsolete.IsOpen());
		EXPECT_FALSE(AllocateBuffer(Obsolete, MakeOperationBufferDesc()));
		EXPECT_EQ(Obsolete.GetStatus().mCode, EArdaRHIResult::InvalidState);
		EXPECT_FALSE(Obsolete.Cancel());
		EXPECT_TRUE(Graph.IsEditing());
		EXPECT_TRUE(Current.IsOpen());
		EXPECT_NE(Graph.FindResource(Created.GetHandle()), nullptr);
		ASSERT_TRUE(Current.Cancel());
	}

	TEST(ArdaGraphOperations, InvalidOutputSelectionPoisonsTheEdit)
	{
		FArdaDependencyGraph Graph;
		{
			FArdaGraphEdit Edit(Graph);
			const auto Operation = EmitOperationOutput(Edit, {});
			ASSERT_TRUE(Operation.GetHandle());
			EXPECT_FALSE(Operation.GetTexture("Result"));
			EXPECT_FALSE(Edit.Commit());
		}
		{
			FArdaGraphEdit Edit(Graph);
			const auto Operation = EmitOperationOutput(Edit, {});
			ASSERT_TRUE(Operation.GetHandle());
			EXPECT_FALSE(Operation.GetBuffer("Missing"));
			EXPECT_FALSE(Edit.Commit());
		}
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
	}

	TEST(ArdaGraphOperations, ClosedFacadeCannotModifyACommittedGraph)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = AllocateBuffer(Edit, MakeOperationBufferDesc());
		Upload(Edit, Buffer, eastl::vector<uint8_t>(16, 5));
		Edit.Keep(Buffer);
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		const auto Revision = Graph.GetCompileResult().mRevision;
		EXPECT_FALSE(Clear(Edit, Buffer));
		EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::InvalidState);
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 1u);
		EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
		EXPECT_FALSE(Graph.IsEditing());
	}

	class FArdaGraphOperationsGpu : public testing::TestWithParam<const char*>
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
			const bool bMissingValidation = testing::Test::IsSkipped() && ArdaTestValidationEnabled &&
			    GetBackendInitializeResult() == EArdaInitializeResult::ValidationUnavailable;
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
				mDevice->RunGarbageCollection();
				mDevice.Reset();
			}
			ShutdownBackend();
			if (!bMissingValidation)
			{
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
		}

		FArdaTestDiagnosticCallback mDiagnostics;
		FArdaRHIDeviceRef mDevice;
	};

	TEST_P(FArdaGraphOperationsGpu, BufferVariablesCaptureSnapshotsAndPreserveValuesAcrossInPlaceWrites)
	{
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = AllocateBuffer(Edit, MakeOperationBufferDesc());
		eastl::vector<uint8_t> SourceBytes{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
		const auto Expected = SourceBytes;
		Upload(Edit, Buffer, SourceBytes);
		const auto Saved = Clone(Edit, Buffer);
		Clear(Edit, Buffer);
		const auto Cleared = Readback(Edit, Buffer);
		CopyTo(Edit, Saved, Buffer, 8, 4, 8);
		const auto Patched = Readback(Edit, Buffer);
		const auto Original = Readback(Edit, Saved);
		SourceBytes.assign(16, 255);
		const auto Compiled = Edit.Commit();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		ASSERT_TRUE(Graph.Compile());
		ASSERT_TRUE(Cleared);
		ASSERT_TRUE(Patched);
		ASSERT_TRUE(Original);
		for (uint32_t Frame = 0; Frame < 2; ++Frame)
		{
			SCOPED_TRACE(Frame);
			const auto Executed = Graph.Execute();
			ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
			EXPECT_EQ(*Cleared, eastl::vector<uint8_t>(16, 0));
			EXPECT_EQ(*Original, Expected);
			EXPECT_EQ(*Patched, (eastl::vector<uint8_t>{0, 0, 0, 0, 0, 0, 0, 0, 4, 5, 6, 7, 8, 9, 10, 11}));
		}
	}

	TEST_P(FArdaGraphOperationsGpu, CopyToSelectsTransferNodesForEachResourcePermutation)
	{
		FArdaDependencyGraph Graph(mDevice);
		FArdaGraphEdit Edit(Graph);
		const auto Source = AllocateTexture(Edit, MakeOperationTextureDesc());
		const auto Intermediate = AllocateTexture(Edit, MakeOperationTextureDesc());
		const auto Destination = AllocateTexture(Edit, MakeOperationTextureDesc());
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 1536;
		const auto Rows = AllocateBuffer(Edit, BufferDesc);
		const auto CopiedRows = AllocateBuffer(Edit, BufferDesc);
		eastl::vector<uint8_t> Expected(64);
		for (size_t Index = 0; Index < Expected.size(); ++Index)
		{
			Expected[Index] = static_cast<uint8_t>(Index * 3);
		}
		Upload(Edit, Source, Expected);
		Upload(Edit, Rows, eastl::vector<uint8_t>(1536, 238));
		const FArdaRHITextureBufferLayout Layout{512, 256};
		CopyTo(Edit, Source, Rows, {}, Layout);
		CopyTo(Edit, Rows, CopiedRows);
		CopyTo(Edit, CopiedRows, Intermediate, {}, Layout);
		CopyTo(Edit, Intermediate, Destination);
		const auto Bytes = Readback(Edit, Destination);
		const auto Padded = Readback(Edit, CopiedRows);
		ASSERT_TRUE(Edit.GetStatus()) << Edit.GetStatus().mMessage.c_str();
		const auto Nodes = Graph.GetTopology().GetNodes();
		ASSERT_EQ(Nodes.size(), 8u);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Nodes[2])->mPayload.mDefinition->mName,
		    FArdaGraphTextureToBufferNode::GetMetadata().mName);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Nodes[3])->mPayload.mDefinition->mName,
		    FArdaMemoryCopyBufferNode::GetMetadata().mName);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Nodes[4])->mPayload.mDefinition->mName,
		    FArdaGraphBufferToTextureNode::GetMetadata().mName);
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Nodes[5])->mPayload.mDefinition->mName,
		    FArdaMemoryCopyTextureNode::GetMetadata().mName);
		const auto Compiled = Edit.Commit();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		ASSERT_TRUE(Graph.Compile());
		const auto Executed = Graph.Execute();
		ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
		ASSERT_TRUE(Bytes);
		ASSERT_TRUE(Padded);
		EXPECT_EQ(*Bytes, Expected);
		eastl::vector<uint8_t> ExpectedPadded(1536, 238);
		for (size_t Row = 0; Row < 4; ++Row)
		{
			eastl::copy_n(Expected.begin() + Row * 16, 16, ExpectedPadded.begin() + 512 + Row * 256);
		}
		EXPECT_EQ(*Padded, ExpectedPadded);
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaGraphOperationsGpu, testing::Values("native-d3d12", "native-vulkan"));
}
