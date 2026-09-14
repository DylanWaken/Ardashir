#include "ArdaDependencyNode.h"
#include "ArdaDependencyGraphNodes.h"
#include <gtest/gtest.h>

namespace
{
	using namespace arda;

	struct FArdaTypedClearNode : TArdaComputeDependencyNode<FArdaTypedClearNode, FArdaGraphClearParameters>
	{
		inline static uint32_t mPreparations = 0;
		inline static bool mbFailPreparation = false;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.typed-clear", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaGraphClearNode::GetCanonicalKey(P);
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& S)
		{
			return FArdaGraphClearNode::Describe(P, S);
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState& S,
		    FArdaInstanceState& I)
		{
			return FArdaGraphClearNode::Record(C, P, S, I);
		}

		static TArdaRHIResult<eastl::shared_ptr<const FArdaState>> Prepare(FArdaRHIDeviceRef)
		{
			++mPreparations;
			if (mbFailPreparation)
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Node setup unavailable.")};
			}
			return {eastl::make_shared<const FArdaState>(), {}};
		}
	};

	struct FArdaTypedCopyNode : TArdaCopyDependencyNode<FArdaTypedCopyNode, FArdaGraphCopyParameters>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.typed-copy", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaGraphCopyNode::GetCanonicalKey(P);
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState& S)
		{
			return FArdaGraphCopyNode::Describe(P, S);
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& C,
		    const FArdaParameters& P,
		    const FArdaState& S,
		    FArdaInstanceState& I)
		{
			return FArdaGraphCopyNode::Record(C, P, S, I);
		}
	};

	struct FArdaTypedFormatOutputNode
	    : TArdaComputeDependencyNode<FArdaTypedFormatOutputNode, FArdaGraphClearParameters>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.typed-format-output", 1};
		}

		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
		{
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 256;
			Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			Desc.mFormat = EArdaRHIFormat::R32UInt;
			return Context.Buffer(Parameters.mDestination, "Destination", Desc);
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters)
		{
			return FArdaGraphClearNode::GetCanonicalKey(Parameters);
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState& State)
		{
			return FArdaGraphClearNode::Describe(Parameters, State);
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance)
		{
			return FArdaGraphClearNode::Record(Context, Parameters, State, Instance);
		}
	};

	TEST(ArdaDependencyGraphNodes, SuppliedTypedOutputMustMatchDeclaredFormat)
	{
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 512;
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		Desc.mFormat = EArdaRHIFormat::R32Float;
		const auto Mismatched = Graph.CreateBuffer("float output", Desc);
		ASSERT_TRUE(Mismatched);
		const auto Rejected = Graph.AttachOrFind<FArdaTypedFormatOutputNode>("supplied", {Mismatched.mValue, 7});
		EXPECT_EQ(Rejected.mStatus.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_FALSE(Graph.FindNode("supplied"));
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());

		const auto Automatic = Graph.AttachOrFind<FArdaTypedFormatOutputNode>("automatic", {});
		ASSERT_TRUE(Automatic);
		const auto AutomaticOutput = Graph.FindOutput(Automatic.mValue, "Destination");
		ASSERT_NE(Graph.FindResource(AutomaticOutput), nullptr);
		EXPECT_EQ(Graph.FindResource(AutomaticOutput)->mBuffer.mFormat, EArdaRHIFormat::R32UInt);

		Desc.mFormat = EArdaRHIFormat::R32UInt;
		const auto Matching = Graph.CreateBuffer("uint output", Desc);
		ASSERT_TRUE(Matching);
		const auto Supplied = Graph.AttachOrFind<FArdaTypedFormatOutputNode>("supplied", {Matching.mValue, 7});
		ASSERT_TRUE(Supplied);
		EXPECT_EQ(Graph.FindOutput(Supplied.mValue, "Destination"), Matching.mValue);
		ASSERT_TRUE(Graph.CancelGraphEdit());
	}

	TEST(ArdaDependencyGraphNodes, CopyAutomaticOutputUsesOrdinaryGpuStorage)
	{
		for (const bool TiledSource : {false, true})
		{
			FArdaDependencyGraph Graph;
			ASSERT_TRUE(Graph.BeginGraphEdit());
			FArdaRHIBufferDesc SourceDesc;
			SourceDesc.mByteSize = 1024;
			SourceDesc.mStructureStride = 16;
			SourceDesc.mFormat = EArdaRHIFormat::R32Float;
			SourceDesc.mUsage = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::Structured;
			SourceDesc.mCpuAccess = TiledSource ? EArdaRHICpuAccess::None : EArdaRHICpuAccess::Write;
			SourceDesc.mbTiled = TiledSource;
			const auto Source = Graph.CreateBuffer("source", SourceDesc);
			ASSERT_TRUE(Source);
			const auto Copy = Graph.AttachOrFind<FArdaGraphCopyNode>("automatic copy", {Source.mValue, {}, 256});
			ASSERT_TRUE(Copy);
			const auto Output = Graph.FindOutput(Copy.mValue, "Destination");
			ASSERT_NE(Graph.FindResource(Output), nullptr);
			const auto& OutputDesc = Graph.FindResource(Output)->mBuffer;
			EXPECT_EQ(OutputDesc.mByteSize, 256u);
			EXPECT_EQ(OutputDesc.mStructureStride, SourceDesc.mStructureStride);
			EXPECT_EQ(OutputDesc.mFormat, SourceDesc.mFormat);
			EXPECT_EQ(OutputDesc.mUsage, SourceDesc.mUsage);
			EXPECT_EQ(OutputDesc.mCpuAccess, EArdaRHICpuAccess::None);
			EXPECT_FALSE(OutputDesc.mbTiled);

			FArdaRHIBufferDesc ReadbackDesc;
			ReadbackDesc.mByteSize = 256;
			ReadbackDesc.mCpuAccess = EArdaRHICpuAccess::Read;
			const auto Readback = Graph.CreateBuffer("readback", ReadbackDesc);
			ASSERT_TRUE(Readback);
			const auto Supplied =
			    Graph.AttachOrFind<FArdaGraphCopyNode>("supplied copy", {Source.mValue, Readback.mValue, 256});
			ASSERT_TRUE(Supplied);
			EXPECT_EQ(Graph.FindOutput(Supplied.mValue, "Destination"), Readback.mValue);
			EXPECT_EQ(Graph.FindResource(Readback.mValue)->mBuffer.mCpuAccess, EArdaRHICpuAccess::Read);
			ASSERT_TRUE(Graph.CancelGraphEdit());
		}
	}

	TEST(ArdaDependencyGraphNodes, TypedPreparationRequiresEditAndPropagatesFailure)
	{
		FArdaDependencyGraph Graph;
		FArdaTypedClearNode::mPreparations = 0;
		FArdaTypedClearNode::mbFailPreparation = true;
		auto Node = Graph.AttachOrFind<FArdaTypedClearNode>("clear", {});
		EXPECT_EQ(Node.mStatus.mCode, EArdaRHIResult::InvalidState);
		EXPECT_EQ(FArdaTypedClearNode::mPreparations, 0u);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		Node = Graph.AttachOrFind<FArdaTypedClearNode>("clear", {});
		EXPECT_EQ(Node.mStatus.mCode, EArdaRHIResult::Unsupported);
		EXPECT_EQ(Node.mStatus.mMessage, "Node setup unavailable.");
		EXPECT_EQ(FArdaTypedClearNode::mPreparations, 1u);
		EXPECT_FALSE(Graph.FindNode("clear"));
		FArdaTypedClearNode::mbFailPreparation = false;
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
		auto Copy = Graph.AttachOrFind<FArdaTypedCopyNode>("copy", {Source.mValue, Destination.mValue, 256});
		auto Clear = Graph.AttachOrFind<FArdaTypedClearNode>("clear", {Source.mValue, 42});
		ASSERT_TRUE(Copy);
		ASSERT_TRUE(Clear);
		auto Again = Graph.AttachOrFind<FArdaTypedClearNode>("clear", {Source.mValue, 42});
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

	struct FArdaConfigurationParameters
	{
		eastl::shared_ptr<FArdaInductorPipelineConfiguration> mConfiguration;
	};

	struct FArdaConfigurationTestNode : TArdaDependencyNode<FArdaConfigurationTestNode,
	                                        FArdaConfigurationParameters,
	                                        EArdaDependencyNodeKind::Compute>
	{
		using FArdaParameters = FArdaConfigurationParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"inductor.test.frozen-configuration", 1};
		}

		// Identify the semantic inputs for node deduplication.
		static eastl::string GetCanonicalKey(const FArdaConfigurationParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Value(P.mConfiguration->mKind).Build();
		}

		// Expose resource effects and pipeline needs to the compiler.
		static FArdaDependencyNodeDesc Describe(const FArdaConfigurationParameters& P, const FArdaState&)
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
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	TEST(ArdaDependencyGraphNodes, AttachmentSnapshotsBothPipelineConfigurations)
	{
		ASSERT_TRUE(FArdaConfigurationTestNode::Register());
		FArdaDependencyGraph Graph;
		ASSERT_TRUE(Graph.BeginGraphEdit());
		auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
		Configuration->mKind = EArdaPipelineStateKind::Compute;
		const auto Node = Graph.AttachOrFind("frozen",
		    "inductor.test.frozen-configuration",
		    FArdaConfigurationParameters{Configuration});
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
