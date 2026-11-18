#include "ArdaDependencyGraph.h"
#include "Inductor/Compiler/ArdaInductor.h"
#include "Inductor/Preprocess/ArdaPreprocessor.h"
#include <gtest/gtest.h>

namespace arda
{
	namespace
	{
		uint32_t ExecutionInstances = 0;
		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaPreparationBoundaryParameters)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(bool, mbRejectInstance, false)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaPreparationBoundaryNode final : public TArdaNodeDefinition<FArdaPreparationBoundaryNode,
		                                               FArdaPreparationBoundaryParameters,
		                                               EArdaNodeType::Compute>
		{
		public:
			struct FArdaInstanceState
			{
				uint32_t mIdentity = 0;
			};

			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.prepare.execution-boundary", 1};
			}

			static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
			{
				FArdaDependencyNodeDesc Desc;
				Desc.mbSideEffect = true;
				return Desc;
			}

			static TArdaRHIResult<eastl::shared_ptr<FArdaInstanceState>> CreateInstance(FArdaRHIDeviceRef,
			    const FArdaParameters& Parameters,
			    const FArdaState&)
			{
				if (Parameters.mbRejectInstance)
				{
					return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Rejected execution instance.")};
				}
				auto Instance = eastl::make_shared<FArdaInstanceState>();
				Instance->mIdentity = ++ExecutionInstances;
				return {eastl::move(Instance), {}};
			}

			static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
			    const FArdaParameters&,
			    const FArdaState&,
			    FArdaInstanceState&)
			{
				return {};
			}
		};

		class FArdaPreparationCostRule final : public TArdaGraphPreprocessRule<FArdaPreparationCostRule>
		{
		public:
			static const char* GetRuleName() noexcept
			{
				return "test.prepare.cost-rule";
			}

			FArdaRHIStatus Preprocess(FArdaInductorGraph& Graph) const
			{
				for (const auto Node : Graph.GetTopology().GetNodes())
				{
					Graph.GetNodeDescription(Node).mEstimatedCost = 27;
				}
				return {};
			}
		};
	}

	TEST(ArdaGraphNodePreparation, PreprocessingOwnsNoExecutionInstancesAndConstCompilesAreIndependent)
	{
		ExecutionInstances = 0;
		FArdaDependencyGraph Source;
		FArdaGraphEdit Edit(Source);
		FArdaInductorOptions Options;
		Options.mRules.mPreprocessRules.insert(Options.mRules.mPreprocessRules.begin(),
		    MakeArdaGraphRule<FArdaPreparationCostRule>());
		ASSERT_TRUE(Edit.SetOptions(Options));
		const auto Operation = FArdaPreparationBoundaryNode::Invoke(Edit, FArdaPreparationBoundaryParameters{}, "work");
		ASSERT_TRUE(Operation);
		const auto Handle = Operation.GetHandle();
		ASSERT_TRUE(Edit.Commit());
		EXPECT_EQ(ExecutionInstances, 0u);
		EXPECT_TRUE(Source.GetCompileResult().mNodes.empty());
		auto Preprocessed = FArdaPreprocessor::Preprocess(Source);
		ASSERT_TRUE(Preprocessed) << Preprocessed.mStatus.mMessage.c_str();
		EXPECT_EQ(ExecutionInstances, 0u);
		EXPECT_TRUE(Preprocessed.mValue->GetCompileResult().mNodes.empty());
		EXPECT_EQ(Source.GetNodeDescription(Handle).mEstimatedCost, 1u);
		EXPECT_EQ(Preprocessed.mValue->GetNodeDescription(Handle).mEstimatedCost, 27u);
		const auto First = FArdaInductor::Compile(*Preprocessed.mValue);
		ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
		const auto Second = FArdaInductor::Compile(*Preprocessed.mValue);
		ASSERT_TRUE(Second) << Second.mStatus.mMessage.c_str();
		EXPECT_EQ(ExecutionInstances, 2u);
		EXPECT_NE(First.mValue.mNodes.at(Handle).mParameters, Second.mValue.mNodes.at(Handle).mParameters);
		EXPECT_EQ(First.mValue.mNodes.at(Handle).mDesc.mEstimatedCost, 27u);
		EXPECT_EQ(Second.mValue.mNodes.at(Handle).mDesc.mEstimatedCost, 27u);
		EXPECT_TRUE(Preprocessed.mValue->GetCompileResult().mNodes.empty());
		EXPECT_EQ(Preprocessed.mValue->GetNodeDescription(Handle).mEstimatedCost, 27u);
	}

	TEST(ArdaGraphNodePreparation, FailedExecutionPreparationDoesNotPreventGraphPreprocessing)
	{
		FArdaDependencyGraph Source;
		FArdaGraphEdit Edit(Source);
		ASSERT_TRUE(FArdaPreparationBoundaryNode::Invoke(Edit, FArdaPreparationBoundaryParameters{true}, "work"));
		ASSERT_TRUE(Edit.Commit());
		auto Preprocessed = FArdaPreprocessor::Preprocess(Source);
		ASSERT_TRUE(Preprocessed) << Preprocessed.mStatus.mMessage.c_str();
		const auto Result = FArdaInductor::Compile(*Preprocessed.mValue);
		EXPECT_FALSE(Result);
		EXPECT_NE(Result.mStatus.mMessage.find("Rejected execution instance"), eastl::string::npos);
		EXPECT_TRUE(Preprocessed.mValue->GetCompileResult().mNodes.empty());
		EXPECT_EQ(Preprocessed.mValue->GetTopology().GetNodes().size(), 1u);
	}

	TEST(ArdaGraphNodePreparation, EditingAPreprocessedCompiledGraphKeepsNewDeclarations)
	{
		FArdaDependencyGraph Source;
		{
			FArdaGraphEdit Edit(Source);
			ASSERT_TRUE(FArdaPreparationBoundaryNode::Invoke(Edit, FArdaPreparationBoundaryParameters{}, "original"));
			ASSERT_TRUE(Edit.Commit());
		}
		ASSERT_TRUE(Source.CompileAndWait());
		auto Processed = Source.Preprocess();
		ASSERT_TRUE(Processed) << Processed.mStatus.mMessage.c_str();
		{
			FArdaGraphEdit Edit(*Processed.mValue);
			ASSERT_TRUE(FArdaPreparationBoundaryNode::Invoke(Edit, FArdaPreparationBoundaryParameters{}, "appended"));
			ASSERT_TRUE(Edit.Commit());
		}
		EXPECT_FALSE(Processed.mValue->IsPreprocessed());
		ASSERT_TRUE(Processed.mValue->CompileAndWait());
		EXPECT_TRUE(Processed.mValue->FindNode("original"));
		EXPECT_TRUE(Processed.mValue->FindNode("appended"));
		EXPECT_EQ(Processed.mValue->GetCompileResult().mExecutionOrder.size(), 2u);
		EXPECT_EQ(Source.GetTopology().GetNodes().size(), 1u);
	}
}
