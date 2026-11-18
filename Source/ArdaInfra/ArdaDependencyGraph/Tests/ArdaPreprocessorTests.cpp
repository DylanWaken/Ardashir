#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/DefaultNodes/ArdaMemoryNodes.h"
#include "Inductor/Preprocess/ArdaPreprocessor.h"
#include "Inductor/Preprocess/ArdaPreprocessGraph.h"
#include "Inductor/Preprocess/ArdaGraphPreprocessRule.h"
#include "Inductor/Compiler/ArdaInductor.h"
#include "Inductor/Runtime/ArdaGraphExecutor.h"

#include <gtest/gtest.h>
#include <stdexcept>

namespace arda
{
	namespace
	{
		class FArdaCapturedSourceCompileTestRule final
		    : public TArdaGraphCompileRule<FArdaCapturedSourceCompileTestRule>
		{
		public:
			FArdaCapturedSourceCompileTestRule(FArdaDependencyGraph*& Source, eastl::vector<FArdaRHIStatus>& Results)
			    : mSource(Source),
			      mResults(Results)
			{
			}

			static const char* GetRuleName() noexcept
			{
				return "CapturedSourceCompileTest";
			}

			FArdaRHIStatus Compile(const FArdaInductorGraph&, FArdaInductorCompileResult&) const
			{
				FArdaGraphEdit Edit(*mSource);
				mResults.push_back(Edit.GetStatus());
				if (Edit.IsOpen())
				{
					(void)Edit.Cancel();
				}
				mResults.push_back(mSource->Preprocess().mStatus);
				mResults.push_back(FArdaInductor::Compile(*mSource).mStatus);
				return {};
			}

		private:
			FArdaDependencyGraph*& mSource;
			eastl::vector<FArdaRHIStatus>& mResults;
		};

		class FArdaInsertPreprocessTestRule final : public TArdaGraphPreprocessRule<FArdaInsertPreprocessTestRule>
		{
		public:
			explicit FArdaInsertPreprocessTestRule(FArdaGraphNodeHandle Removed = {}, bool bFail = false)
			    : mRemoved(Removed),
			      mbFail(bFail)
			{
			}

			static const char* GetRuleName() noexcept
			{
				return "InsertPreprocessTest";
			}

			FArdaRHIStatus Preprocess(FArdaPreprocessGraph& Graph) const
			{
				if (mRemoved)
				{
					if (auto Status = Graph.RemoveNode(mRemoved); !Status)
					{
						return Status;
					}
				}
				if (auto Status = Graph.Edit(
				        [](FArdaGraphEdit& Edit)
				        {
					        FArdaMemoryUploadBufferParameters Parameters;
					        Parameters.mBytes.resize(16, 7);
					        Edit.Keep(UploadBuffer(Edit, Parameters, "Inserted").GetBuffer());
					        return Edit.GetStatus();
				        });
				    !Status)
				{
					return Status;
				}
				if (mbFail)
				{
					throw std::runtime_error("preprocessing test failure");
				}
				return {};
			}

		private:
			FArdaGraphNodeHandle mRemoved;
			bool mbFail;
		};
	}

	TEST(ArdaPreprocessor, ReturnsNormalGraphWithCullingAndNoExecutionPlan)
	{
		FArdaDependencyGraph Source;
		FArdaGraphEdit Edit(Source);
		FArdaMemoryUploadBufferParameters Parameters;
		Parameters.mBytes.resize(16, 42);
		const auto Kept = UploadBuffer(Edit, Parameters, "Kept");
		const auto Dead = UploadBuffer(Edit, Parameters, "Dead");
		Edit.Keep(Kept.GetBuffer());
		ASSERT_TRUE(Edit.Commit());
		const auto Result = Source.Preprocess();
		ASSERT_TRUE(Result) << Result.mStatus.mMessage.c_str();
		EXPECT_FALSE(Source.IsPreprocessed());
		EXPECT_FALSE(Source.GetTopology().TryGetNode(Dead.GetHandle())->mPayload.mbCulled);
		EXPECT_TRUE(Result.mValue->IsPreprocessed());
		EXPECT_FALSE(Result.mValue->GetTopology().TryGetNode(Kept.GetHandle())->mPayload.mbCulled);
		EXPECT_TRUE(Result.mValue->GetTopology().TryGetNode(Dead.GetHandle())->mPayload.mbCulled);
		EXPECT_TRUE(Result.mValue->GetCompileResult().mExecutionOrder.empty());
		EXPECT_TRUE(Result.mValue->GetCompileResult().mNodes.empty());
		EXPECT_EQ(Result.mValue->GetCompileResult().mRevision, 0u);
		FArdaGraphEdit Extension(*Result.mValue);
		ASSERT_TRUE(UploadBuffer(Extension, Parameters, "Later"));
		ASSERT_TRUE(Extension.Commit());
		EXPECT_EQ(Result.mValue->GetTopology().GetNodes().size(), 3u);
		EXPECT_FALSE(Result.mValue->IsPreprocessed());
	}

	TEST(ArdaPreprocessor, CustomRulesInsertRegisteredNodesAndRemoveExistingNodes)
	{
		FArdaDependencyGraph Source;
		FArdaGraphEdit Edit(Source);
		FArdaMemoryUploadBufferParameters Parameters;
		Parameters.mBytes.resize(16, 42);
		const auto Removed = UploadBuffer(Edit, Parameters, "Removed");
		FArdaInductorOptions Options;
		Options.mRules.mPreprocessRules.insert(Options.mRules.mPreprocessRules.begin(),
		    MakeArdaGraphRule<FArdaInsertPreprocessTestRule>(Removed.GetHandle()));
		ASSERT_TRUE(Edit.SetOptions(Options));
		ASSERT_TRUE(Edit.Commit());
		const auto Result = FArdaPreprocessor::Preprocess(Source);
		ASSERT_TRUE(Result) << Result.mStatus.mMessage.c_str();
		EXPECT_TRUE(Source.GetTopology().ContainsNode(Removed.GetHandle()));
		EXPECT_FALSE(Source.FindNode("Inserted"));
		EXPECT_FALSE(Result.mValue->GetTopology().ContainsNode(Removed.GetHandle()));
		const auto Added = Result.mValue->FindNode("Inserted");
		ASSERT_TRUE(Added);
		EXPECT_EQ(Result.mValue->GetTopology().GetNodes().size(), 1u);
		EXPECT_FALSE(Result.mValue->GetTopology().TryGetNode(Added)->mPayload.mbCulled);
	}

	TEST(ArdaPreprocessor, CallbackExceptionsDiscardTheCopyAndReleaseSourceGuards)
	{
		FArdaDependencyGraph Source;
		FArdaGraphEdit Edit(Source);
		FArdaInductorOptions Options;
		Options.mRules.mPreprocessRules.insert(Options.mRules.mPreprocessRules.begin(),
		    MakeArdaGraphRule<FArdaInsertPreprocessTestRule>(FArdaGraphNodeHandle{}, true));
		ASSERT_TRUE(Edit.SetOptions(Options));
		ASSERT_TRUE(Edit.Commit());
		const auto Result = Source.Preprocess();
		EXPECT_FALSE(Result);
		EXPECT_NE(Result.mStatus.mMessage.find("InsertPreprocessTest: preprocessing test failure"),
		    eastl::string::npos);
		EXPECT_TRUE(Source.GetTopology().GetNodes().empty());
		EXPECT_FALSE(Source.IsPreprocessed());
		FArdaGraphEdit Retry(Source);
		EXPECT_TRUE(Retry.IsOpen());
		EXPECT_TRUE(Retry.Cancel());
	}

	TEST(ArdaPreprocessor, StandaloneCompilationPreservesGraphAndResultOutlivesItsSources)
	{
		FArdaInductorCompileResult Plan;
		{
			FArdaDependencyGraph Source;
			FArdaGraphEdit Edit(Source);
			FArdaMemoryUploadBufferParameters Parameters;
			Parameters.mBytes.resize(16, 42);
			const auto Kept = UploadBuffer(Edit, Parameters, "Kept");
			const auto Dead = UploadBuffer(Edit, Parameters, "Dead");
			Edit.Keep(Kept.GetBuffer());
			ASSERT_TRUE(Edit.Commit());
			const auto Preprocessed = Source.Preprocess();
			ASSERT_TRUE(Preprocessed) << Preprocessed.mStatus.mMessage.c_str();
			const FArdaDependencyGraph& Input = *Preprocessed.mValue;
			const auto Before = Input.Serialize();
			ASSERT_TRUE(Before) << Before.mStatus.mMessage.c_str();
			const auto Nodes = Input.GetTopology().GetNodes();
			auto Compiled = FArdaInductor::Compile(Input);
			ASSERT_TRUE(Compiled) << Compiled.mStatus.mMessage.c_str();
			EXPECT_EQ(Compiled.mValue.mExecutionOrder, (eastl::vector<FArdaGraphNodeHandle>{Kept.GetHandle()}));
			EXPECT_EQ(Compiled.mValue.mCulledNodes, (eastl::vector<FArdaGraphNodeHandle>{Dead.GetHandle()}));
			EXPECT_EQ(Input.GetTopology().GetNodes(), Nodes);
			EXPECT_FALSE(Input.GetTopology().TryGetNode(Kept.GetHandle())->mPayload.mbCulled);
			EXPECT_TRUE(Input.GetTopology().TryGetNode(Dead.GetHandle())->mPayload.mbCulled);
			EXPECT_TRUE(Input.GetCompileResult().mNodes.empty());
			EXPECT_TRUE(Input.GetCompileResult().mExecutionOrder.empty());
			const auto After = Input.Serialize();
			ASSERT_TRUE(After) << After.mStatus.mMessage.c_str();
			EXPECT_EQ(Before.mValue, After.mValue);
			Plan = eastl::move(Compiled.mValue);
		}
		const auto Duplicate = Plan;
		FArdaGraphExecutor Executor(eastl::move(Plan));
		ASSERT_TRUE(Executor.GetStatus()) << Executor.GetStatus().mMessage.c_str();
		FArdaGraphExecutor Second(Duplicate);
		EXPECT_FALSE(Second.GetStatus());
		EXPECT_EQ(Second.GetStatus().mCode, EArdaRHIResult::InvalidState);
		const auto Result = Executor.Execute();
		EXPECT_FALSE(Result.mStatus);
		EXPECT_NE(Result.mStatus.mMessage.find("device"), eastl::string::npos);
	}

	TEST(ArdaPreprocessor, StandaloneCompilerRejectsCapturedSourceMutationAndRecursion)
	{
		FArdaDependencyGraph* Captured = nullptr;
		eastl::vector<FArdaRHIStatus> Attempts;
		FArdaDependencyGraph Source;
		FArdaGraphEdit Edit(Source);
		FArdaMemoryUploadBufferParameters Parameters;
		Parameters.mBytes.resize(16, 42);
		Edit.Keep(UploadBuffer(Edit, Parameters).GetBuffer());
		FArdaInductorOptions Options;
		Options.mRules.mCompileRules.insert(Options.mRules.mCompileRules.begin(),
		    MakeArdaGraphRule<FArdaCapturedSourceCompileTestRule>(Captured, Attempts));
		ASSERT_TRUE(Edit.SetOptions(Options));
		ASSERT_TRUE(Edit.Commit());
		const auto Preprocessed = Source.Preprocess();
		ASSERT_TRUE(Preprocessed) << Preprocessed.mStatus.mMessage.c_str();
		Captured = Preprocessed.mValue.get();
		const auto Result = FArdaInductor::Compile(*Captured);
		ASSERT_TRUE(Result) << Result.mStatus.mMessage.c_str();
		ASSERT_EQ(Attempts.size(), 3u);
		for (const auto& Attempt : Attempts)
		{
			EXPECT_FALSE(Attempt);
		}
		EXPECT_TRUE(Captured->IsPreprocessed());
		EXPECT_EQ(Captured->GetTopology().GetNodes().size(), 1u);
		FArdaGraphEdit Retry(*Captured);
		EXPECT_TRUE(Retry.IsOpen());
		EXPECT_TRUE(Retry.Cancel());
	}
}
