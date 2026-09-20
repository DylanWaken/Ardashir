#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"

#include <gtest/gtest.h>
#include <type_traits>
#include <utility>

namespace
{
	using namespace arda;

// Access checks use only public headers, so a private compiler-test adapter cannot hide a public API leak.
#define ARDA_EXPECT_PRIVATE_GRAPH_METHOD(Trait, Expression)                                                            \
	template <class Graph, class = void>                                                                               \
	struct Trait : std::false_type                                                                                     \
	{                                                                                                                  \
	};                                                                                                                 \
	template <class Graph>                                                                                             \
	struct Trait<Graph, std::void_t<decltype(Expression)>> : std::true_type                                            \
	{                                                                                                                  \
	};                                                                                                                 \
	static_assert(!Trait<FArdaDependencyGraph>::value, "Graph edits must use the operations facade.")

	// Explicit defaulted arguments avoid an MSVC access-detection bug in dependent call expressions.
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicBeginEdit, std::declval<Graph&>().BeginGraphEdit(false));
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicEndEdit, std::declval<Graph&>().EndGraphEdit());
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicCancelEdit, std::declval<Graph&>().CancelGraphEdit());
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicSetOptions,
	    std::declval<Graph&>().SetOptions(FArdaInductorOptions{}));
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicCreateResource,
	    std::declval<Graph&>().CreateResource(FArdaDependencyResourceDesc{}));
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicCreateBuffer,
	    std::declval<Graph&>().CreateBuffer(eastl::string{}, FArdaRHIBufferDesc{}));
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicCreateTexture,
	    std::declval<Graph&>().CreateTexture(eastl::string{}, FArdaRHITextureDesc{}));
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicImportBuffer,
	    std::declval<Graph&>().ImportBuffer(eastl::string{}, FArdaRHIBufferRef{}));
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicImportTexture,
	    std::declval<Graph&>().ImportTexture(eastl::string{}, FArdaRHITextureRef{}));
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicImportAccelerationStructure,
	    std::declval<Graph&>().ImportAccelerationStructure(eastl::string{}, FArdaRHIAccelStructRef{}));
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicMarkOutput,
	    std::declval<Graph&>().MarkOutput(FArdaDependencyResourceHandle{}, true));
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicAddDependency,
	    std::declval<Graph&>().AddDependency(FArdaGraphNodeHandle{}, FArdaGraphNodeHandle{}));
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicRemoveDependency,
	    std::declval<Graph&>().RemoveDependency(FArdaGraphNodeHandle{}, FArdaGraphNodeHandle{}));
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicRemoveNode,
	    std::declval<Graph&>().RemoveNode(FArdaGraphNodeHandle{}));
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicTypedAttach,
	    std::declval<Graph&>().template AttachOrFind<FArdaMemoryUploadBufferNode>(eastl::string{},
	        FArdaMemoryUploadBufferParameters{}));
	ARDA_EXPECT_PRIVATE_GRAPH_METHOD(TArdaHasPublicRegisteredAttach,
	    std::declval<Graph&>().AttachOrFind(eastl::string{}, eastl::string{}, FArdaMemoryUploadBufferParameters{}));

#undef ARDA_EXPECT_PRIVATE_GRAPH_METHOD

	template <class Node, class = void>
	struct TArdaHasPublicNodeAttach : std::false_type
	{
	};

	template <class Node>
	struct TArdaHasPublicNodeAttach<Node,
	    std::void_t<decltype(Node::
	            Attach(std::declval<FArdaDependencyGraph&>(), eastl::string{}, typename Node::FArdaParameters{}))>>
	    : std::true_type
	{
	};

	static_assert(!TArdaHasPublicNodeAttach<FArdaMemoryUploadBufferNode>::value,
	    "Node implementations must expose Invoke, not raw attachment.");
	static_assert(!std::is_constructible_v<FArdaGraphEdit, FArdaDependencyGraph&, bool>,
	    "Public edits record a construction rather than patching persistent nodes.");
	static_assert(
	    std::is_const_v<std::remove_reference_t<decltype(std::declval<FArdaDependencyGraph&>().GetTopology())>>,
	    "Graph inspection must not expose mutable topology.");

	TEST(ArdaGraphFacadeBoundary, InitializersAndNamesUseTheCommonInvocationPath)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		uint32_t InitializerCalls = 0;
		auto Initializer = [&](FArdaMemoryUploadBufferParameters& Parameters)
		{
			++InitializerCalls;
			Parameters.mBytes.assign(16, 29);
		};
		const auto First = UploadBuffer(Edit, Initializer, "first upload");
		const auto Second = Edit.Invoke<FArdaMemoryUploadBufferNode>(Initializer, "second upload");
		ASSERT_TRUE(First);
		ASSERT_TRUE(Second);
		EXPECT_EQ(InitializerCalls, 2u);
		EXPECT_NE(First.GetHandle(), Second.GetHandle());
		EXPECT_EQ(Graph.FindNode("first upload"), First.GetHandle());
		EXPECT_EQ(Graph.FindNode("second upload"), Second.GetHandle());
		const auto FirstBuffer = First.GetBuffer();
		const auto SecondBuffer = Second.GetBuffer();
		ASSERT_TRUE(FirstBuffer);
		ASSERT_TRUE(SecondBuffer);
		EXPECT_NE(FirstBuffer.GetHandle(), SecondBuffer.GetHandle());
		EXPECT_EQ(Graph.FindResource(FirstBuffer.GetHandle())->mBuffer.mByteSize, 16u);
		EXPECT_EQ(Graph.FindResource(SecondBuffer.GetHandle())->mBuffer.mByteSize, 16u);
		Edit.Keep(FirstBuffer);
		Edit.Keep(SecondBuffer);
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 2u);
	}

	TEST(ArdaGraphFacadeBoundary, DuplicateExplicitNamesFailInsteadOfFindingAnEarlierOperation)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto First = UploadBuffer(Edit, {{}, eastl::vector<uint8_t>(16, 29)}, "named upload");
		ASSERT_TRUE(First);
		const auto Duplicate = UploadBuffer(Edit, {{}, eastl::vector<uint8_t>(16, 29)}, "named upload");
		EXPECT_FALSE(Duplicate);
		EXPECT_FALSE(Edit.GetStatus());
		EXPECT_EQ(Graph.FindNode("named upload"), First.GetHandle());
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 1u);
		EXPECT_FALSE(Edit.Commit());
		ASSERT_TRUE(Edit.Cancel());
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
	}

	TEST(ArdaGraphFacadeBoundary, FailedInitializerStopsLaterOperationsAndOptionsChanges)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Failed = UploadBuffer(
		    Edit,
		    [](FArdaMemoryUploadBufferParameters& Parameters)
		    {
			    Parameters.mBytes.assign(16, 37);
			    return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Rejected operation initializer.");
		    },
		    "rejected upload");
		EXPECT_FALSE(Failed);
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		const auto Failure = Edit.GetStatus();
		EXPECT_EQ(Failure.mCode, EArdaRHIResult::InvalidArgument);
		uint32_t Calls = 0;
		EXPECT_FALSE(Edit.Invoke<FArdaMemoryUploadBufferNode>(
		    [&](FArdaMemoryUploadBufferParameters&)
		    {
			    ++Calls;
		    }));
		EXPECT_EQ(Calls, 0u);
		auto Options = Graph.GetOptions();
		const auto OriginalObjective = Options.mObjective;
		Options.mObjective = EArdaInductorObjective::Memory;
		EXPECT_FALSE(Edit.SetOptions(Options));
		EXPECT_EQ(Graph.GetOptions().mObjective, OriginalObjective);
		const auto Commit = Edit.Commit();
		EXPECT_EQ(Commit.mCode, Failure.mCode);
		EXPECT_EQ(Commit.mMessage, Failure.mMessage);
		ASSERT_TRUE(Edit.Cancel());
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 0u);
	}

	TEST(ArdaGraphFacadeBoundary, OperationOrderingSpansQueuedScopesWithoutOwningOldScopes)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphOperation Earlier;
		{
			FArdaGraphEdit Edit(Graph);
			Earlier = UploadBuffer(Edit, {{}, eastl::vector<uint8_t>(16, 17)});
			ASSERT_TRUE(Earlier);
			Edit.Keep(Earlier.GetBuffer());
			ASSERT_TRUE(Edit.Commit());
		}
		FArdaGraphEdit Edit(Graph);
		const auto Later = UploadBuffer(Edit, {{}, eastl::vector<uint8_t>(16, 31)});
		ASSERT_TRUE(Later);
		Edit.Keep(Later.GetBuffer());
		ASSERT_TRUE(Edit.Order(Later, Earlier));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		const auto& Order = Graph.GetCompileResult().mExecutionOrder;
		ASSERT_EQ(Order.size(), 2u);
		EXPECT_EQ(Order[0], Later.GetHandle());
		EXPECT_EQ(Order[1], Earlier.GetHandle());
	}

	TEST(ArdaGraphFacadeBoundary, ForeignOperationOrderingFailsAndPoisonsTheScope)
	{
		FArdaDependencyGraph OtherGraph;
		FArdaGraphEdit OtherEdit(OtherGraph);
		const auto Foreign = UploadBuffer(OtherEdit, {{}, eastl::vector<uint8_t>(16, 13)});
		ASSERT_TRUE(Foreign);
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Local = UploadBuffer(Edit, {{}, eastl::vector<uint8_t>(16, 17)});
		ASSERT_TRUE(Local);
		EXPECT_FALSE(Edit.Order(Foreign, Local));
		EXPECT_FALSE(Edit.GetStatus());
		EXPECT_FALSE(UploadBuffer(Edit, {{}, eastl::vector<uint8_t>(16, 19)}));
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 1u);
		EXPECT_FALSE(Edit.Commit());
		ASSERT_TRUE(Edit.Cancel());
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
	}

	TEST(ArdaGraphFacadeBoundary, EarlierFrameOperationsCannotOrderTheNextConstruction)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphOperation Old;
		{
			FArdaGraphEdit Edit(Graph);
			Old = UploadBuffer(Edit, {{}, eastl::vector<uint8_t>(16, 23)});
			ASSERT_TRUE(Old);
			Edit.Keep(Old.GetBuffer());
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Graph.Compile());
		EXPECT_FALSE(Graph.Execute().mStatus);
		FArdaGraphEdit Edit(Graph);
		const auto Current = UploadBuffer(Edit, {{}, eastl::vector<uint8_t>(16, 23)});
		ASSERT_TRUE(Current);
		EXPECT_FALSE(Edit.Order(Old, Current));
		EXPECT_FALSE(Edit.Commit());
		ASSERT_TRUE(Edit.Cancel());
	}

	TEST(ArdaGraphFacadeBoundary, AllocationLifetimeAndCompilerOptionsAreScopedFacadeOperations)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		auto Options = Graph.GetOptions();
		Options.mObjective = EArdaInductorObjective::Memory;
		ASSERT_TRUE(Edit.SetOptions(Options));
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 16;
		const auto Temporary = AllocateBuffer(Edit, BufferDesc);
		const auto Retained = AllocateBuffer(Edit, BufferDesc, "history", EArdaGraphResourceLifetime::Persistent);
		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = 4;
		TextureDesc.mHeight = 4;
		TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		const auto RetainedTexture =
		    AllocateTexture(Edit, TextureDesc, "history texture", EArdaGraphResourceLifetime::Persistent);
		ASSERT_TRUE(Temporary);
		ASSERT_TRUE(Retained);
		ASSERT_TRUE(RetainedTexture);
		EXPECT_FALSE(Graph.FindResource(Temporary.GetHandle())->mbPersistent);
		EXPECT_TRUE(Graph.FindResource(Retained.GetHandle())->mbPersistent);
		EXPECT_TRUE(Graph.FindResource(RetainedTexture.GetHandle())->mbPersistent);
		EXPECT_EQ(Graph.GetOptions().mObjective, EArdaInductorObjective::Memory);
		ASSERT_TRUE(Upload(Edit, Retained, eastl::vector<uint8_t>(16, 41)));
		Edit.Keep(Retained);
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 1u);
	}
}
