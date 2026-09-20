#include "ArdaGraphGlobalNodeTestFixture.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"

#include <gtest/gtest.h>

namespace
{
	using namespace arda;
	using namespace arda::global_node_tests;

	static_assert(eastl::is_same_v<decltype(EmitTestValue), const FArdaGlobalNodeTestNode>);
	static_assert(eastl::is_same_v<decltype(CopyBuffer), const FArdaMemoryCopyBufferNode>);
	static_assert(eastl::is_same_v<decltype(ClearBuffer), const FArdaMemoryClearBufferNode>);
	static_assert(eastl::is_invocable_v<decltype(EmitTestValue), FArdaGraphEdit&, FArdaGlobalNodeTestParameters>);
	static_assert(eastl::is_invocable_v<decltype(EmitTestValue), FArdaGraphEdit&, uint32_t>);
	static_assert(!eastl::is_invocable_v<decltype(EmitTestValue), FArdaGraphEdit&, const char*>);
	static_assert(!eastl::is_invocable_v<decltype(EmitTestValue), FArdaGraphEdit&, double>);

	TEST(ArdaGraphGlobalNodes, DeclarationAndDefinitionShareOneCallableAcrossTranslationUnits)
	{
		EXPECT_EQ(&EmitTestValue, GetArdaGlobalNodeTestDefinitionAddress());
	}

	TEST(ArdaGraphGlobalNodes, GlobalCallableRegistersLazilyAndEachCallCreatesAnIndependentOperation)
	{
		auto& Registry = FArdaNodeRegistry::Get();
		const auto Metadata = FArdaGlobalNodeTestNode::GetMetadata();
		EXPECT_FALSE(Registry.Find(Metadata.mName));
		EXPECT_EQ(GetArdaGlobalNodeTestStateCounts().mPreparedStates, 0u);
		EXPECT_EQ(GetArdaGlobalNodeTestStateCounts().mOperationInstances, 0u);

		FArdaDependencyGraph Graph;
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
		FArdaGraphEdit Edit(Graph);
		FArdaGlobalNodeTestParameters Parameters;
		Parameters.mValue = 23;
		const auto First = EmitTestValue(Edit, Parameters);
		ASSERT_TRUE(First.GetHandle()) << Edit.GetStatus().mMessage.c_str();
		const auto Definition = Registry.Find(Metadata.mName);
		ASSERT_TRUE(Definition);
		ASSERT_TRUE(FArdaGlobalNodeTestNode::Register());
		EXPECT_EQ(Registry.Find(Metadata.mName), Definition);
		EXPECT_EQ(GetArdaGlobalNodeTestStateCounts().mPreparedStates, 1u);
		EXPECT_EQ(GetArdaGlobalNodeTestStateCounts().mOperationInstances, 1u);

		const auto Second = EmitTestValue(Edit, uint32_t{23});
		ASSERT_TRUE(Second.GetHandle()) << Edit.GetStatus().mMessage.c_str();
		EXPECT_NE(First.GetHandle(), Second.GetHandle());
		const auto FirstOutput = First.GetBuffer("Result");
		const auto SecondOutput = Second.GetBuffer("Result");
		ASSERT_TRUE(FirstOutput);
		ASSERT_TRUE(SecondOutput);
		EXPECT_NE(FirstOutput.GetHandle(), SecondOutput.GetHandle());
		EXPECT_FALSE(Parameters.mOutput);
		EXPECT_EQ(GetArdaGlobalNodeTestStateCounts().mPreparedStates, 1u);
		EXPECT_EQ(GetArdaGlobalNodeTestStateCounts().mOperationInstances, 2u);
		const auto* FirstNode = Graph.GetTopology().TryGetNode(First.GetHandle());
		const auto* SecondNode = Graph.GetTopology().TryGetNode(Second.GetHandle());
		ASSERT_NE(FirstNode, nullptr);
		ASSERT_NE(SecondNode, nullptr);
		EXPECT_EQ(FirstNode->mPayload.mDefinition, Definition);
		EXPECT_EQ(SecondNode->mPayload.mDefinition, Definition);
		EXPECT_EQ(FirstNode->mPayload.mCanonicalKey,
		    FArdaDependencyKeyBuilder{}.Value(uint32_t{23}).Resource(FirstOutput.GetHandle()).Build());
		EXPECT_EQ(SecondNode->mPayload.mCanonicalKey,
		    FArdaDependencyKeyBuilder{}.Value(uint32_t{23}).Resource(SecondOutput.GetHandle()).Build());
		Edit.Keep(FirstOutput);
		Edit.Keep(SecondOutput);
		ASSERT_TRUE(Edit.Commit());
		EXPECT_EQ(Graph.GetRevisionStats().mPendingEditCount, 1u);
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 2u);
	}

	TEST(ArdaGraphGlobalNodes, BuiltinCallablesSupportParameterStructsAndPositionalFields)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 16;
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		const auto Source = AllocateBuffer(Edit, Desc);
		const auto Destination = AllocateBuffer(Edit, Desc);
		const auto Cleared = ClearBuffer(Edit, Source, uint32_t{42});
		const FArdaMemoryCopyBufferParameters Parameters{Source, Destination};
		const auto FirstCopy = CopyBuffer(Edit, Parameters);
		const auto SecondCopy = CopyBuffer(Edit, Source, Destination);
		ASSERT_TRUE(Edit.GetStatus()) << Edit.GetStatus().mMessage.c_str();
		ASSERT_TRUE(Cleared.GetHandle());
		ASSERT_TRUE(FirstCopy.GetHandle());
		ASSERT_TRUE(SecondCopy.GetHandle());
		EXPECT_NE(FirstCopy.GetHandle(), SecondCopy.GetHandle());
		EXPECT_EQ(FirstCopy.GetBuffer().GetHandle(), Destination.GetHandle());
		EXPECT_EQ(SecondCopy.GetBuffer().GetHandle(), Destination.GetHandle());
		EXPECT_EQ(Graph.GetTopology().GetNodes().size(), 3u);
		Edit.Keep(Destination);
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder.size(), 3u);
	}
}
