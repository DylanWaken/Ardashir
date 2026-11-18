#include "ArdaDependencyGraph.h"

#include <gtest/gtest.h>
#include <cstring>

namespace arda
{
	namespace
	{
		FArdaRHIBufferDesc MakeInitializedStorageDesc()
		{
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 16;
			return Desc;
		}
	}

	TEST(ArdaGraphBufferInitializer, DeclaredPrefixIsProducedButUninitializedSuffixIsNot)
	{
		for (const uint64_t ReadBytes : {uint64_t(8), uint64_t(16)})
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			const auto Buffer =
			    AllocateInitializedBuffer(Edit, MakeInitializedStorageDesc(), eastl::vector<uint8_t>(8, 23));
			ASSERT_TRUE(Buffer);
			const auto Output = Readback(Edit, Buffer, 0, ReadBytes);
			ASSERT_TRUE(Output);
			const auto Committed = Edit.Commit();
			const auto Status = Committed ? Graph.CompileAndWait() : Committed;
			EXPECT_EQ(bool(Status), ReadBytes == 8) << Status.mMessage.c_str();
		}
	}

	TEST(ArdaGraphBufferInitializer, InitializerHasValueSemanticsAndSharesImmutableBytes)
	{
		const FArdaGraphBufferInitializer Original(eastl::vector<uint8_t>{0, 7, 0xff, 0});
		const auto Copy = Original;
		EXPECT_EQ(Copy.GetData(), Original.GetData());
		EXPECT_EQ(Copy.GetSize(), 4u);
		EXPECT_EQ(Copy, FArdaGraphBufferInitializer(eastl::vector<uint8_t>{0, 7, 0xff, 0}));
		EXPECT_NE(Copy, FArdaGraphBufferInitializer(eastl::vector<uint8_t>{0, 7, 0xff, 1}));
		EXPECT_TRUE(FArdaGraphBufferInitializer{}.IsEmpty());
	}

	TEST(ArdaGraphBufferInitializer, SerializationPreservesInitializerWithoutNativeOrExternalBindings)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const eastl::vector<uint8_t> Bytes{0, 1, 0xff, 0, 42, 9, 7, 8};
		const auto Buffer = AllocateInitializedBuffer(Edit, MakeInitializedStorageDesc(), Bytes, "constants");
		ASSERT_TRUE(Buffer);
		Edit.Keep(Buffer);
		ASSERT_TRUE(Edit.Commit());
		const auto Encoded = Graph.Serialize();
		ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
		const auto Decoded = FArdaDependencyGraph::Deserialize(Encoded.mValue);
		ASSERT_TRUE(Decoded) << Decoded.mStatus.mMessage.c_str();
		const auto* Resource = Decoded.mValue->FindResource(Decoded.mValue->FindResource("constants"));
		ASSERT_NE(Resource, nullptr);
		EXPECT_TRUE(Resource->mbPersistent);
		EXPECT_FALSE(Resource->IsExternal());
		ASSERT_EQ(Resource->mBufferInitializer.GetSize(), Bytes.size());
		EXPECT_EQ(std::memcmp(Resource->mBufferInitializer.GetData(), Bytes.data(), Bytes.size()), 0);
		const auto Reencoded = Decoded.mValue->Serialize();
		ASSERT_TRUE(Reencoded);
		EXPECT_EQ(Reencoded.mValue, Encoded.mValue);
	}

	TEST(ArdaGraphBufferInitializer, RejectsEmptyOversizeAndCpuVisibleInitializers)
	{
		for (uint32_t Case = 0; Case < 3; ++Case)
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			auto Desc = MakeInitializedStorageDesc();
			eastl::vector<uint8_t> Bytes(Case == 0 ? 0 : Case == 1 ? 17 : 16, 1);
			if (Case == 2)
			{
				Desc.mCpuAccess = EArdaRHICpuAccess::Write;
			}
			EXPECT_FALSE(AllocateInitializedBuffer(Edit, Desc, eastl::move(Bytes)));
			EXPECT_FALSE(Edit.GetStatus());
		}
	}
}
