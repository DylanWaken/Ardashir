#include "ArdaDependencyGraphNodes.h"
#include <gtest/gtest.h>

namespace
{
	using namespace arda;

	struct FTypedClearNode : TArdaComputeDependencyNode<FTypedClearNode, FArdaGraphClearParameters>
	{
		inline static uint32_t mPreparations = 0;
		inline static bool mbFailPreparation = false;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.typed-clear", 1};
		}

		static eastl::string GetCanonicalKey(const FParameters& P)
		{
			return FArdaGraphClearNode::GetCanonicalKey(P);
		}

		static FArdaDependencyNodeDesc Describe(const FParameters& P, const FState& S)
		{
			return FArdaGraphClearNode::Describe(P, S);
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FParameters& P,
		    const FState& S,
		    FInstanceState& I)
		{
			return FArdaGraphClearNode::Record(C, P, S, I);
		}

		static TArdaRHIResult<eastl::shared_ptr<const FState>> Prepare(FArdaRHIDeviceRef)
		{
			++mPreparations;
			if (mbFailPreparation)
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Node setup unavailable.")};
			}
			return {eastl::make_shared<const FState>(), {}};
		}
	};

	struct FTypedCopyNode : TArdaCopyDependencyNode<FTypedCopyNode, FArdaGraphCopyParameters>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.typed-copy", 1};
		}

		static eastl::string GetCanonicalKey(const FParameters& P)
		{
			return FArdaGraphCopyNode::GetCanonicalKey(P);
		}

		static FArdaDependencyNodeDesc Describe(const FParameters& P, const FState& S)
		{
			return FArdaGraphCopyNode::Describe(P, S);
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FParameters& P,
		    const FState& S,
		    FInstanceState& I)
		{
			return FArdaGraphCopyNode::Record(C, P, S, I);
		}
	};

	TEST(ArdaDependencyGraphNodes, TypedPreparationRequiresEditAndPropagatesFailure)
	{
		FArdaDependencyGraph Graph;
		FTypedClearNode::mPreparations = 0;
		FTypedClearNode::mbFailPreparation = true;
		auto Node = Graph.AttachOrFind<FTypedClearNode>("clear", {});
		EXPECT_EQ(Node.mStatus.mCode, EArdaRHIResult::InvalidState);
		EXPECT_EQ(FTypedClearNode::mPreparations, 0u);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		Node = Graph.AttachOrFind<FTypedClearNode>("clear", {});
		EXPECT_EQ(Node.mStatus.mCode, EArdaRHIResult::Unsupported);
		EXPECT_EQ(Node.mStatus.mMessage, "Node setup unavailable.");
		EXPECT_EQ(FTypedClearNode::mPreparations, 1u);
		EXPECT_FALSE(Graph.FindNode("clear"));
		FTypedClearNode::mbFailPreparation = false;
		EXPECT_TRUE(Graph.CancelGraphEdit());
	}

	TEST(ArdaDependencyGraphNodes, TypedNodesDeduplicateAndResolveConsumerFirstAttachment)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 256;
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		auto Source = Graph.CreateBuffer("source", Desc);
		auto Destination = Graph.CreateBuffer("destination", Desc);
		ASSERT_TRUE(Source);
		ASSERT_TRUE(Destination);
		auto Copy = Graph.AttachOrFind<FTypedCopyNode>("copy", {Source.mValue, Destination.mValue, 256});
		auto Clear = Graph.AttachOrFind<FTypedClearNode>("clear", {Source.mValue, 42});
		ASSERT_TRUE(Copy);
		ASSERT_TRUE(Clear);
		auto Again = Graph.AttachOrFind<FTypedClearNode>("clear", {Source.mValue, 42});
		ASSERT_TRUE(Again);
		EXPECT_EQ(Clear.mValue, Again.mValue);
		ASSERT_TRUE(Graph.MarkOutput(Destination.mValue));
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{Clear.mValue, Copy.mValue}));
	}

	TEST(ArdaDependencyGraphNodes, BuiltinSynchronizationRetainsDependencyChain)
	{
		EXPECT_TRUE(FArdaNodeRegistry::Get().Find("arda.sync"));
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 256;
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		const auto Buffer = Graph.CreateBuffer("output", Desc);
		ASSERT_TRUE(Buffer);
		const auto Consumer =
		    Graph.AttachOrFind("consumer", "arda.clear-buffer", FArdaGraphClearParameters{Buffer.mValue, 7});
		const auto Join = Graph.AttachOrFind("join", "arda.sync", FArdaGraphSyncParameters{});
		const auto Producer = Graph.AttachOrFind("producer", "arda.sync", FArdaGraphSyncParameters{});
		ASSERT_TRUE(Consumer);
		ASSERT_TRUE(Join);
		ASSERT_TRUE(Producer);
		ASSERT_TRUE(Graph.AddDependency(Producer.mValue, Join.mValue));
		ASSERT_TRUE(Graph.AddDependency(Join.mValue, Consumer.mValue));
		ASSERT_TRUE(Graph.MarkOutput(Buffer.mValue));
		ASSERT_TRUE(Graph.EndGraphEdit());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{Producer.mValue, Join.mValue, Consumer.mValue}));
	}

	struct FConfigurationParameters
	{
		eastl::shared_ptr<FArdaInductorPipelineConfiguration> mConfiguration;
	};

	TEST(ArdaDependencyGraphNodes, AttachmentSnapshotsBothPipelineConfigurations)
	{
		TArdaDependencyNodeDefinition<FConfigurationParameters> Definition;
		Definition.mName = "inductor.test.frozen-configuration";
		Definition.mCanonicalKey = [](const FConfigurationParameters& P)
		{
			return eastl::string(1, char(P.mConfiguration->mKind));
		};
		Definition.mDescribe = [](const FConfigurationParameters& P)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mbPipelineStageOnly = true;
			FArdaInductorPipelineContribution Stage;
			Stage.mConfiguration = P.mConfiguration;
			Desc.mPipelineStages.push_back(Stage);
			FArdaInductorPipelineRequest Request;
			Request.mConfiguration = P.mConfiguration;
			Desc.mPipelines.push_back(Request);
			return Desc;
		};
		ASSERT_TRUE(FArdaNodeRegistry::Get().Register(eastl::move(Definition)));
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
		Configuration->mKind = EArdaPipelineStateKind::Compute;
		const auto Node =
		    Graph.AttachOrFind("frozen", "inductor.test.frozen-configuration", FConfigurationParameters{Configuration});
		ASSERT_TRUE(Node);
		Configuration->mKind = EArdaPipelineStateKind::Graphics;
		const auto& Frozen = Graph.GetTopology().TryGetNode(Node.mValue)->mPayload.mDesc;
		ASSERT_EQ(Frozen.mPipelineStages.size(), 1u);
		ASSERT_EQ(Frozen.mPipelines.size(), 1u);
		EXPECT_EQ(Frozen.mPipelineStages[0].mConfiguration->mKind, EArdaPipelineStateKind::Compute);
		EXPECT_EQ(Frozen.mPipelines[0].mConfiguration->mKind, EArdaPipelineStateKind::Compute);
		EXPECT_NE(Frozen.mPipelineStages[0].mConfiguration.get(), Configuration.get());
		EXPECT_NE(Frozen.mPipelines[0].mConfiguration.get(), Configuration.get());
		const auto RetainedDefinition = Graph.GetTopology().TryGetNode(Node.mValue)->mPayload.mDefinition;
		EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister("inductor.test.frozen-configuration"));
		EXPECT_FALSE(FArdaNodeRegistry::Get().Find("inductor.test.frozen-configuration"));
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Node.mValue)->mPayload.mDefinition, RetainedDefinition);
		ASSERT_TRUE(Graph.CancelGraphEdit());
	}
}
