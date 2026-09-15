#include "NodeLibrary/ArdaDependencyGraphNodeLibrary.h"

#include <gtest/gtest.h>

namespace
{
	using namespace arda;

	TEST(ArdaMemoryNodes, RegistersAllNodesIdempotentlyWithoutADevice)
	{
		ASSERT_TRUE(RegisterArdaMemoryNodes());
		ASSERT_TRUE(RegisterArdaMemoryNodes());
		auto& Registry = FArdaNodeRegistry::Get();
		EXPECT_TRUE(Registry.Find(FArdaMemoryUploadBufferNode::GetMetadata().mName));
		EXPECT_TRUE(Registry.Find(FArdaMemoryCopyBufferNode::GetMetadata().mName));
		EXPECT_TRUE(Registry.Find(FArdaMemoryReadbackBufferNode::GetMetadata().mName));
		EXPECT_TRUE(Registry.Find(FArdaMemoryClearBufferNode::GetMetadata().mName));
		EXPECT_TRUE(Registry.Find(FArdaMemoryUploadTextureNode::GetMetadata().mName));
		EXPECT_TRUE(Registry.Find(FArdaMemoryReadbackTextureNode::GetMetadata().mName));
		EXPECT_TRUE(Registry.Find(FArdaMemoryCopyTextureNode::GetMetadata().mName));
		EXPECT_TRUE(Registry.Find(FArdaMemoryClearTextureNode::GetMetadata().mName));
		EXPECT_TRUE(Registry.Find(FArdaMemoryCopyAccelerationStructureNode::GetMetadata().mName));
		EXPECT_TRUE(Registry.Find(FArdaMemoryCompactAccelerationStructureNode::GetMetadata().mName));

		ASSERT_TRUE(Registry.Unregister(FArdaMemoryUploadBufferNode::GetMetadata().mName));
		ASSERT_TRUE(RegisterArdaMemoryNodes());
		EXPECT_TRUE(Registry.Find(FArdaMemoryUploadBufferNode::GetMetadata().mName));
	}
}
