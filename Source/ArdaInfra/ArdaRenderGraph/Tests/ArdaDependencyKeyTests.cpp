#include "ArdaDependencyKey.h"
#include "ArdaDependencyNode.h"
#include "ArdaInductorState.h"
#include <gtest/gtest.h>
#include <type_traits>

namespace
{
	using namespace arda;

	TEST(ArdaDependencyKey, ScalarEncodingHasExplicitWidthsAndStableByteOrder)
	{
		// Compare a fixed wire representation rather than reproducing the encoder's loop.
		const auto Key = FArdaDependencyKeyBuilder{}.Value(uint16_t(0x1234)).Value(uint32_t(0x89ABCDEF)).Build();
		const unsigned char Expected[] = {0x34, 0x12, 0xEF, 0xCD, 0xAB, 0x89};
		ASSERT_EQ(Key.size(), sizeof(Expected));
		EXPECT_EQ(std::memcmp(Key.data(), Expected, sizeof(Expected)), 0);

		// Field widths are part of the node schema and must not silently expand to a common integer width.
		EXPECT_NE(FArdaDependencyKeyBuilder{}.Value(uint8_t(1)).Build(),
		    FArdaDependencyKeyBuilder{}.Value(uint64_t(1)).Build());
	}

	TEST(ArdaDependencyKey, VariableLengthFieldsCannotCollideAtDifferentBoundaries)
	{
		// Concatenated text alone is ambiguous; the shared encoder must retain each field boundary.
		const auto First = FArdaDependencyKeyBuilder{}.String("a").String("bc").Build();
		const auto Second = FArdaDependencyKeyBuilder{}.String("ab").String("c").Build();
		EXPECT_NE(First, Second);

		// Embedded zero bytes and an empty final field remain semantic data.
		const eastl::string Binary("a\0b", 3);
		EXPECT_NE(FArdaDependencyKeyBuilder{}.String(Binary).Build(), FArdaDependencyKeyBuilder{}.String("a").Build());
		EXPECT_NE(FArdaDependencyKeyBuilder{}.Bytes(nullptr, 0).Build(), FArdaDependencyKeyBuilder{}.Build());
	}

	TEST(ArdaDependencyKey, ResourceIdentityAndEveryViewDimensionRemainDistinct)
	{
		const FArdaDependencyResourceHandle Resource{17, 3, 9};
		const auto Identity = FArdaDependencyKeyBuilder{}.Resource(Resource).Build();

		// Reusing an index in another graph or generation cannot reuse the old node identity.
		EXPECT_NE(Identity, FArdaDependencyKeyBuilder{}.Resource({18, 3, 9}).Build());
		EXPECT_NE(Identity, FArdaDependencyKeyBuilder{}.Resource({17, 4, 9}).Build());
		EXPECT_NE(Identity, FArdaDependencyKeyBuilder{}.Resource({17, 3, 10}).Build());

		// Buffer extents and texture planes are as significant as starting offsets and mip levels.
		EXPECT_NE(FArdaDependencyKeyBuilder{}.BufferRange({16, 32}).Build(),
		    FArdaDependencyKeyBuilder{}.BufferRange({16, 64}).Build());
		EXPECT_NE(FArdaDependencyKeyBuilder{}.BufferRange({16, 32}).Build(),
		    FArdaDependencyKeyBuilder{}.BufferRange({32, 32}).Build());
		EXPECT_NE(FArdaDependencyKeyBuilder{}.TextureRange({1, 2, 3, 4, 0, 1}).Build(),
		    FArdaDependencyKeyBuilder{}.TextureRange({1, 2, 3, 4, 1, 1}).Build());
		EXPECT_NE(FArdaDependencyKeyBuilder{}.TextureRange({1, 2, 3, 4, 0, 1}).Build(),
		    FArdaDependencyKeyBuilder{}.TextureRange({1, 2, 3, 4, 0, 2}).Build());
	}

	TEST(ArdaDependencyNodeContract, ExecutableStorageCannotBeAuthoredOutsideTheClassContract)
	{
		// Registry results are inspection data; callers cannot construct a second callback-based node API.
		EXPECT_FALSE(std::is_default_constructible_v<FArdaDependencyNodeExecutable>);
		EXPECT_FALSE(std::is_copy_constructible_v<FArdaDependencyNodeExecutable>);
		EXPECT_FALSE(std::is_move_constructible_v<FArdaDependencyNodeExecutable>);
	}

	TEST(ArdaDependencyKey, BindlessEqualityAndCacheKeysShareAllSemanticFields)
	{
		FArdaRHIBindlessLayoutDesc Baseline;
		Baseline.mRegisterSpaces = {{0, 4, EArdaRHIBindingType::TextureSRV}};
		const auto Encode = [](const FArdaRHIBindlessLayoutDesc& Desc)
		{
			FArdaDependencyKeyBuilder Key;
			Desc.VisitSemantics(
			    [&Key](uint64_t Value)
			    {
				    Key.Value(Value);
			    });
			return Key.Build();
		};

		// Each independently meaningful layout choice must invalidate both interning and pipeline identity.
		eastl::vector<FArdaRHIBindlessLayoutDesc> Variants(13, Baseline);
		Variants[0].mVisibility = EArdaRHIShaderStage::Compute;
		Variants[1].mFirstSlot = 1;
		Variants[2].mRegisterSpace = 1;
		Variants[3].mMaxCapacity = 8;
		Variants[4].mbUnbounded = true;
		Variants[5].mbUpdateAfterBind = true;
		Variants[6].mbVariableDescriptorCount = true;
		Variants[7].mbDirectHeapIndexing = true;
		Variants[8].mbDescriptorBuffer = true;
		Variants[9].mLayoutType = EArdaRHIBindlessLayoutType::MutableSrvUavCbv;
		Variants[10].mRegisterSpaces[0].mSlot = 1;
		Variants[11].mRegisterSpaces[0].mArraySize = 8;
		Variants[12].mRegisterSpaces[0].mType = EArdaRHIBindingType::Sampler;
		for (const auto& Variant : Variants)
		{
			EXPECT_FALSE(Baseline == Variant);
			EXPECT_NE(Encode(Baseline), Encode(Variant));
		}

		// Diagnostic labels must not split equivalent native layouts or PSOs.
		auto Relabeled = Baseline;
		Relabeled.mDebugName = "another label";
		EXPECT_TRUE(Baseline == Relabeled);
		EXPECT_EQ(Encode(Baseline), Encode(Relabeled));
	}

	TEST(ArdaDependencyState, MicromapWritesCannotBeMergedAsReadOnlyStates)
	{
		EXPECT_TRUE(IsWriteState(EArdaRHIResourceState::OpacityMicromapWrite));
		EXPECT_TRUE(IsWriteState(EArdaRHIResourceState::OpacityMicromapWrite | EArdaRHIResourceState::ShaderResource));
		EXPECT_FALSE(IsWriteState(EArdaRHIResourceState::OpacityMicromapBuildInput));
		EXPECT_FALSE(IsWriteState(EArdaRHIResourceState::ShaderResource));
	}
}
