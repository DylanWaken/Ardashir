#include "../../ArdaBackendImpls/ArdaSparseMapping.h"
#include <EASTL/shared_ptr.h>
#include <EASTL/weak_ptr.h>
#include <gtest/gtest.h>

namespace
{
	using FArdaSparseOwner = eastl::shared_ptr<int>;
	using FArdaSparseMappings = arda::TArdaSparseMappingSet<FArdaSparseOwner>;
}

TEST(ArdaSparseMappings, SplitsOverlapPreservesOffsetsAndReleasesUnmappedOwners)
{
	FArdaSparseMappings Mappings;
	auto First = eastl::make_shared<int>(1);
	auto Second = eastl::make_shared<int>(2);
	eastl::weak_ptr<int> FirstLifetime = First;
	ASSERT_TRUE(Mappings.Replace(0, 0, 100, First, 200));
	ASSERT_TRUE(Mappings.Replace(0, 30, 40, Second, 500));
	ASSERT_EQ(Mappings.GetRanges().size(), 3u);
	EXPECT_EQ(Mappings.GetRanges()[0].mEnd, 30u);
	EXPECT_EQ(Mappings.GetRanges()[1].mOwnerOffset, 500u);
	EXPECT_EQ(Mappings.GetRanges()[2].mOwnerOffset, 270u);
	EXPECT_EQ(Mappings.GetPrefixSize(), 100u);
	First.reset();
	EXPECT_FALSE(FirstLifetime.expired());
	ASSERT_TRUE(Mappings.Replace(0, 0, 30, {}));
	EXPECT_FALSE(FirstLifetime.expired());
	ASSERT_TRUE(Mappings.Replace(0, 70, 30, {}));
	EXPECT_TRUE(FirstLifetime.expired());
	EXPECT_EQ(Mappings.GetPrefixSize(), 0u);
	ASSERT_EQ(Mappings.GetRanges().size(), 1u);
}

TEST(ArdaSparseMappings, CoalescesContiguousOwnershipAndKeepsDomainsIndependent)
{
	FArdaSparseMappings Mappings;
	auto Owner = eastl::make_shared<int>(1);
	ASSERT_TRUE(Mappings.Replace(1, 0, 20, Owner, 0));
	ASSERT_TRUE(Mappings.Replace(0, 0, 10, Owner, 100));
	ASSERT_TRUE(Mappings.Replace(0, 10, 10, Owner, 110));
	ASSERT_EQ(Mappings.GetRanges().size(), 2u);
	EXPECT_EQ(Mappings.GetPrefixSize(0), 20u);
	EXPECT_EQ(Mappings.GetPrefixSize(1), 20u);
	ASSERT_TRUE(Mappings.Replace(0, 20, 10, Owner, 0));
	EXPECT_EQ(Mappings.GetRanges().size(), 3u);
	ASSERT_TRUE(Mappings.Replace(0, 0, 30, {}));
	EXPECT_EQ(Mappings.GetPrefixSize(1), 20u);
}

TEST(ArdaSparseMappings, TransactionsAndOverflowLeavePublishedOwnershipIntact)
{
	FArdaSparseMappings Published;
	auto Owner = eastl::make_shared<int>(1);
	ASSERT_TRUE(Published.Replace(0, 0, 16, Owner));
	auto Proposed = Published;
	ASSERT_TRUE(Proposed.Replace(0, 0, 16, {}));
	EXPECT_EQ(Published.GetPrefixSize(), 16u);
	EXPECT_FALSE(Published.Replace(0, UINT64_MAX - 1, 4, Owner));
	EXPECT_FALSE(Published.Replace(0, 0, 4, Owner, UINT64_MAX - 1));
	EXPECT_FALSE(Published.Replace(0, 0, 0, Owner));
	EXPECT_EQ(Published.GetPrefixSize(), 16u);
	Published = eastl::move(Proposed);
	EXPECT_TRUE(Published.GetRanges().empty());
}

TEST(ArdaSparseMappings, PrefixRoundingClampsBeforeAddition)
{
	constexpr uint64_t Tile = 65536;
	uint64_t Result = 1;
	ASSERT_TRUE(arda::CalculateArdaSparsePrefixSize(UINT64_MAX, Tile * 3, Tile, Result));
	EXPECT_EQ(Result, Tile * 3);
	ASSERT_TRUE(arda::CalculateArdaSparsePrefixSize(Tile + 1, Tile * 3, Tile, Result));
	EXPECT_EQ(Result, Tile * 2);
	ASSERT_TRUE(arda::CalculateArdaSparsePrefixSize(0, Tile * 3, Tile, Result));
	EXPECT_EQ(Result, 0u);
	EXPECT_FALSE(arda::CalculateArdaSparsePrefixSize(1, Tile * 3 - 1, Tile, Result));
	EXPECT_FALSE(arda::CalculateArdaSparsePrefixSize(1, Tile * 3, 0, Result));
}
