#include "Inductor/Compiler/ArdaGraphRuleArchive.h"
#include <gtest/gtest.h>

namespace arda
{
	namespace
	{
		class FArdaPortableRule final : public TArdaGraphPreprocessRule<FArdaPortableRule>
		{
		public:
			static const char* GetRuleName() noexcept
			{
				return "test.archive.portable-rule";
			}

			FArdaRHIStatus Preprocess(FArdaInductorGraph&) const
			{
				return {};
			}

			eastl::string mLabel;
			eastl::vector<uint32_t> mSelection;
		};

		class FArdaUnregisteredArchiveRule final : public TArdaGraphCompileRule<FArdaUnregisteredArchiveRule>
		{
		public:
			static const char* GetRuleName() noexcept
			{
				return "test.archive.unregistered-rule";
			}

			FArdaRHIStatus Compile(const FArdaInductorGraph&, FArdaInductorCompileResult&) const
			{
				return {};
			}
		};

		FArdaRHIStatus RegisterPortableRule()
		{
			return RegisterArdaGraphRuleArchive<FArdaPortableRule>(
			    FArdaPortableRule::GetRuleName(),
			    2,
			    [](FArdaGraphArchiveWriter& Archive, const FArdaPortableRule& Rule)
			    {
				    Archive.Fields(Rule.mLabel, Rule.mSelection);
				    return Archive.GetStatus();
			    },
			    [](FArdaGraphArchiveReader& Archive) -> TArdaRHIResult<eastl::shared_ptr<const FArdaPortableRule>>
			    {
				    auto Rule = eastl::make_shared<FArdaPortableRule>();
				    Archive.Fields(Rule->mLabel, Rule->mSelection);
				    return {eastl::move(Rule), Archive.GetStatus()};
			    });
		}
	}

	TEST(ArdaGraphRuleArchive, RoundtripPreservesEveryOptionAndOrderedBuiltinConfiguration)
	{
		FArdaInductorOptions Original;
		Original.mObjective = EArdaInductorObjective::Memory;
		Original.mSearchMode = EArdaInductorSearchMode::Greedy;
		Original.mMaxVramBytes = 87654321;
		Original.mMinimumAsyncChain = 7;
		Original.mMinimumAsyncSlack = 9;
		Original.mMaxSearchStates = 567;
		Original.mFramesInFlight = 3;
		Original.mCudaGraphMode = EArdaCudaGraphMode::Require;
		Original.mCudaHandoffCost = 13;
		Original.mAliasSubmissionCost = 17;
		Original.mbEnableGpuTiming = true;
		Original.mGpuTimingEmaAlpha = 0.625;
		Original.mGpuTimingSampleInterval = 19;
		Original.mGpuTimingHistoryCapacity = 23;
		Original.mbEnableAdaptiveScheduling = true;
		Original.mAdaptiveSchedulingInterval = 29;
		Original.mAdaptiveSchedulingMinSamples = 31;
		Original.mAdaptiveSchedulingSearchBudget = 37;
		Original.mAdaptiveSchedulingMinImprovement = 0.125;
		Original.mbEnableCopyQueue = false;
		Original.mbEnableAsyncCompute = false;
		Original.mRules.mPreprocessRules.push_back(MakeArdaGraphRule<FArdaKeepAllNodesRule>());
		Original.mRules.mCompileRules = {MakeArdaGraphRule<FArdaMinimizeMemoryRule>(1234),
		    MakeArdaGraphRule<FArdaPlanMemoryRule>(true),
		    MakeArdaGraphRule<FArdaAssignQueuesRule>(false, true),
		    MakeArdaGraphRule<FArdaMinimizeMemoryRule>(5678),
		    MakeArdaGraphRule<FArdaGraphicsQueueRule>()};
		FArdaGraphArchiveWriter Writer;
		ASSERT_TRUE(WriteArdaGraphOptions(Writer, Original));
		FArdaGraphArchiveReader Reader(Writer.GetData());
		FArdaInductorOptions Loaded;
		ASSERT_TRUE(ReadArdaGraphOptions(Reader, Loaded));
		ASSERT_TRUE(Reader.Finish());
#define ARDA_EXPECT_OPTION(Field) EXPECT_EQ(Loaded.Field, Original.Field)
		ARDA_EXPECT_OPTION(mObjective);
		ARDA_EXPECT_OPTION(mSearchMode);
		ARDA_EXPECT_OPTION(mMaxVramBytes);
		ARDA_EXPECT_OPTION(mMinimumAsyncChain);
		ARDA_EXPECT_OPTION(mMinimumAsyncSlack);
		ARDA_EXPECT_OPTION(mMaxSearchStates);
		ARDA_EXPECT_OPTION(mFramesInFlight);
		ARDA_EXPECT_OPTION(mCudaGraphMode);
		ARDA_EXPECT_OPTION(mCudaHandoffCost);
		ARDA_EXPECT_OPTION(mAliasSubmissionCost);
		ARDA_EXPECT_OPTION(mbEnableGpuTiming);
		ARDA_EXPECT_OPTION(mGpuTimingEmaAlpha);
		ARDA_EXPECT_OPTION(mGpuTimingSampleInterval);
		ARDA_EXPECT_OPTION(mGpuTimingHistoryCapacity);
		ARDA_EXPECT_OPTION(mbEnableAdaptiveScheduling);
		ARDA_EXPECT_OPTION(mAdaptiveSchedulingInterval);
		ARDA_EXPECT_OPTION(mAdaptiveSchedulingMinSamples);
		ARDA_EXPECT_OPTION(mAdaptiveSchedulingSearchBudget);
		ARDA_EXPECT_OPTION(mAdaptiveSchedulingMinImprovement);
		ARDA_EXPECT_OPTION(mbEnableCopyQueue);
		ARDA_EXPECT_OPTION(mbEnableAsyncCompute);
#undef ARDA_EXPECT_OPTION
		ASSERT_EQ(Loaded.mRules.mPreprocessRules.size(), Original.mRules.mPreprocessRules.size());
		for (size_t I = 0; I < Loaded.mRules.mPreprocessRules.size(); ++I)
		{
			EXPECT_STREQ(Loaded.mRules.mPreprocessRules[I]->GetName(), Original.mRules.mPreprocessRules[I]->GetName());
		}
		ASSERT_EQ(Loaded.mRules.mCompileRules.size(), 5u);
		const auto* First = dynamic_cast<const FArdaMinimizeMemoryRule*>(Loaded.mRules.mCompileRules[0].get());
		const auto* Memory = dynamic_cast<const FArdaPlanMemoryRule*>(Loaded.mRules.mCompileRules[1].get());
		const auto* Queues = dynamic_cast<const FArdaAssignQueuesRule*>(Loaded.mRules.mCompileRules[2].get());
		const auto* Second = dynamic_cast<const FArdaMinimizeMemoryRule*>(Loaded.mRules.mCompileRules[3].get());
		ASSERT_TRUE(First && Memory && Queues && Second);
		EXPECT_EQ(First->mSearchBudget, 1234u);
		EXPECT_TRUE(Memory->mbAllowSerialization);
		EXPECT_FALSE(Queues->mbEnableCopy);
		EXPECT_TRUE(Queues->mbEnableCompute);
		EXPECT_EQ(Second->mSearchBudget, 5678u);
		EXPECT_NE(dynamic_cast<const FArdaGraphicsQueueRule*>(Loaded.mRules.mCompileRules[4].get()), nullptr);
	}

	TEST(ArdaGraphRuleArchive, ApplicationCodecPreservesConfigurationAndRejectsUnknownVersion)
	{
		ASSERT_TRUE(RegisterPortableRule());
		auto Rule = eastl::make_shared<FArdaPortableRule>();
		Rule->mLabel = "selected nodes";
		Rule->mSelection = {9, 2, 7};
		FArdaInductorOptions Original;
		Original.mRules.mPreprocessRules = {Rule};
		FArdaGraphArchiveWriter Writer;
		ASSERT_TRUE(WriteArdaGraphOptions(Writer, Original));
		FArdaGraphArchiveReader Reader(Writer.GetData());
		FArdaInductorOptions Loaded;
		ASSERT_TRUE(ReadArdaGraphOptions(Reader, Loaded));
		ASSERT_TRUE(Reader.Finish());
		ASSERT_EQ(Loaded.mRules.mPreprocessRules.size(), 1u);
		const auto* Typed = dynamic_cast<const FArdaPortableRule*>(Loaded.mRules.mPreprocessRules.front().get());
		ASSERT_NE(Typed, nullptr);
		EXPECT_EQ(Typed->mLabel, Rule->mLabel);
		EXPECT_EQ(Typed->mSelection, Rule->mSelection);
		auto Bytes = Writer.GetData();
		const eastl::string Name = FArdaPortableRule::GetRuleName();
		const auto NameOffset = Bytes.find(Name);
		ASSERT_NE(NameOffset, eastl::string::npos);
		Bytes[NameOffset + Name.size()] = 3;
		FArdaGraphArchiveReader Incompatible(Bytes);
		const auto Status = ReadArdaGraphOptions(Incompatible, Loaded);
		EXPECT_FALSE(Status);
		EXPECT_EQ(Status.mCode, EArdaRHIResult::Unsupported);
	}

	TEST(ArdaGraphRuleArchive, UnregisteredConfiguredRulesNeverFallBackToDefaults)
	{
		FArdaInductorOptions Options;
		Options.mRules.mCompileRules.push_back(MakeArdaGraphRule<FArdaUnregisteredArchiveRule>());
		FArdaGraphArchiveWriter Writer;
		const auto Status = WriteArdaGraphOptions(Writer, Options);
		EXPECT_FALSE(Status);
		EXPECT_EQ(Status.mCode, EArdaRHIResult::Unsupported);
		EXPECT_FALSE(Writer.GetStatus());
	}
}
