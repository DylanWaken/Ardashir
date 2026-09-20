#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/Node/ArdaDependencyNode.h"
#include "Graph/DefaultNodes/ArdaDependencyGraphNodes.h"
#include <gtest/gtest.h>

namespace arda::dependencygraphnodes_tests
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

	ARDA_DECLARE_GRAPH_NODE(FArdaTypedClearNode, TypedClear);
	ARDA_DEFINE_GRAPH_NODE(FArdaTypedClearNode, TypedClear);

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

	ARDA_DECLARE_GRAPH_NODE(FArdaTypedCopyNode, TypedCopy);
	ARDA_DEFINE_GRAPH_NODE(FArdaTypedCopyNode, TypedCopy);

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

	ARDA_DECLARE_GRAPH_NODE(FArdaTypedFormatOutputNode, TypedFormatOutput);
	ARDA_DEFINE_GRAPH_NODE(FArdaTypedFormatOutputNode, TypedFormatOutput);

	TEST(ArdaDependencyGraphNodes, SuppliedTypedOutputMustMatchDeclaredFormat)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 512;
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		Desc.mFormat = EArdaRHIFormat::R32Float;
		const auto Mismatched = AllocateBuffer(Edit, Desc, "float output");
		ASSERT_TRUE(Mismatched);
		const auto Rejected =
		    TypedFormatOutput(Edit, FArdaTypedFormatOutputNode::FArdaParameters{Mismatched.GetHandle(), 7}, "supplied");
		EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_FALSE(Graph.FindNode("supplied"));
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());

		EXPECT_FALSE(Edit.Commit());
		ASSERT_TRUE(Edit.Cancel());
		FArdaGraphEdit ValidEdit(Graph);
		const auto Automatic = TypedFormatOutput(ValidEdit, FArdaTypedFormatOutputNode::FArdaParameters{}, "automatic");
		ASSERT_TRUE(Automatic);
		const auto AutomaticOutput = Graph.FindOutput(Automatic.GetHandle(), "Destination");
		ASSERT_NE(Graph.FindResource(AutomaticOutput), nullptr);
		EXPECT_EQ(Graph.FindResource(AutomaticOutput)->mBuffer.mFormat, EArdaRHIFormat::R32UInt);

		Desc.mFormat = EArdaRHIFormat::R32UInt;
		const auto Matching = AllocateBuffer(ValidEdit, Desc, "uint output");
		ASSERT_TRUE(Matching);
		const auto Supplied = TypedFormatOutput(ValidEdit,
		    FArdaTypedFormatOutputNode::FArdaParameters{Matching.GetHandle(), 7},
		    "supplied");
		ASSERT_TRUE(Supplied);
		EXPECT_EQ(Graph.FindOutput(Supplied.GetHandle(), "Destination"), Matching.GetHandle());
		ASSERT_TRUE(ValidEdit.Cancel());
	}

	TEST(ArdaDependencyGraphNodes, CopyAutomaticOutputUsesOrdinaryGpuStorage)
	{
		for (const bool TiledSource : {false, true})
		{
			FArdaDependencyGraph Graph;
			FArdaGraphEdit Edit(Graph);
			ASSERT_TRUE(Edit.GetStatus());
			FArdaRHIBufferDesc SourceDesc;
			SourceDesc.mByteSize = 1024;
			SourceDesc.mStructureStride = 16;
			SourceDesc.mFormat = EArdaRHIFormat::R32Float;
			SourceDesc.mUsage = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::Structured;
			SourceDesc.mCpuAccess = TiledSource ? EArdaRHICpuAccess::None : EArdaRHICpuAccess::Write;
			SourceDesc.mbTiled = TiledSource;
			const auto Source = AllocateBuffer(Edit, SourceDesc, "source");
			ASSERT_TRUE(Source);
			const auto Copy =
			    CopyNode(Edit, FArdaGraphCopyNode::FArdaParameters{Source.GetHandle(), {}, 256}, "automatic copy");
			ASSERT_TRUE(Copy);
			const auto Output = Graph.FindOutput(Copy.GetHandle(), "Destination");
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
			const auto Readback = AllocateBuffer(Edit, ReadbackDesc, "readback");
			ASSERT_TRUE(Readback);
			const auto Supplied = CopyNode(Edit,
			    FArdaGraphCopyNode::FArdaParameters{Source.GetHandle(), Readback.GetHandle(), 256},
			    "supplied copy");
			ASSERT_TRUE(Supplied);
			EXPECT_EQ(Graph.FindOutput(Supplied.GetHandle(), "Destination"), Readback.GetHandle());
			EXPECT_EQ(Graph.FindResource(Readback.GetHandle())->mBuffer.mCpuAccess, EArdaRHICpuAccess::Read);
			ASSERT_TRUE(Edit.Cancel());
		}
	}

	TEST(ArdaDependencyGraphNodes, TypedPreparationRequiresEditAndPropagatesFailure)
	{
		FArdaDependencyGraph Graph;
		FArdaTypedClearNode::mPreparations = 0;
		FArdaTypedClearNode::mbFailPreparation = true;
		FArdaGraphEdit ClosedEdit(Graph);
		ASSERT_TRUE(ClosedEdit.Cancel());
		auto Node = TypedClear(ClosedEdit, FArdaTypedClearNode::FArdaParameters{}, "clear");
		EXPECT_EQ(ClosedEdit.GetStatus().mCode, EArdaRHIResult::InvalidState);
		EXPECT_EQ(FArdaTypedClearNode::mPreparations, 0u);
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		Node = TypedClear(Edit, FArdaTypedClearNode::FArdaParameters{}, "clear");
		EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::Unsupported);
		EXPECT_EQ(Edit.GetStatus().mMessage, "Node setup unavailable.");
		EXPECT_EQ(FArdaTypedClearNode::mPreparations, 1u);
		EXPECT_FALSE(Graph.FindNode("clear"));
		FArdaTypedClearNode::mbFailPreparation = false;
		EXPECT_TRUE(Edit.Cancel());
	}

	TEST(ArdaDependencyGraphNodes, TypedNodesResolveConsumerFirstOperations)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 256;
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		auto Source = AllocateBuffer(Edit, Desc, "source");
		auto Destination = AllocateBuffer(Edit, Desc, "destination");
		ASSERT_TRUE(Source);
		ASSERT_TRUE(Destination);
		auto Copy = TypedCopy(Edit,
		    FArdaTypedCopyNode::FArdaParameters{Source.GetHandle(), Destination.GetHandle(), 256},
		    "copy");
		auto Clear = TypedClear(Edit, FArdaTypedClearNode::FArdaParameters{Source.GetHandle(), 42}, "clear");
		ASSERT_TRUE(Copy);
		ASSERT_TRUE(Clear);
		Edit.Keep(Destination.GetHandle());
		ASSERT_TRUE(Edit.GetStatus());
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{Clear.GetHandle(), Copy.GetHandle()}));
	}

	TEST(ArdaDependencyGraphNodes, BuiltinSynchronizationRetainsDependencyChain)
	{
		EXPECT_TRUE(FArdaNodeRegistry::Get().Find("arda.sync"));
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 256;
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		const auto Buffer = AllocateBuffer(Edit, Desc, "output");
		ASSERT_TRUE(Buffer);
		const auto Consumer = ClearNode(Edit, FArdaGraphClearParameters{Buffer.GetHandle(), 7}, "consumer");
		const auto Join = SyncNode(Edit, FArdaGraphSyncParameters{}, "join");
		const auto Producer = SyncNode(Edit, FArdaGraphSyncParameters{}, "producer");
		ASSERT_TRUE(Consumer);
		ASSERT_TRUE(Join);
		ASSERT_TRUE(Producer);
		ASSERT_TRUE(Edit.Order(Producer, Join));
		ASSERT_TRUE(Edit.Order(Join, Consumer));
		Edit.Keep(Buffer.GetHandle());
		ASSERT_TRUE(Edit.GetStatus());
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetCompileResult().mExecutionOrder,
		    (eastl::vector<FArdaGraphNodeHandle>{Producer.GetHandle(), Join.GetHandle(), Consumer.GetHandle()}));
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

		// Identify the semantic inputs for construction caching.
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

	ARDA_DECLARE_GRAPH_NODE(FArdaConfigurationTestNode, ConfigureTestPipeline);
	ARDA_DEFINE_GRAPH_NODE(FArdaConfigurationTestNode, ConfigureTestPipeline);

	TEST(ArdaDependencyGraphNodes, InvocationSnapshotsBothPipelineConfigurations)
	{
		ASSERT_TRUE(FArdaConfigurationTestNode::Register());
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(Edit.GetStatus());
		auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
		Configuration->mKind = EArdaPipelineStateKind::Compute;
		const auto Node = ConfigureTestPipeline(Edit, FArdaConfigurationParameters{Configuration}, "frozen");
		ASSERT_TRUE(Node);
		Configuration->mKind = EArdaPipelineStateKind::Graphics;
		const auto& Frozen = Graph.GetTopology().TryGetNode(Node.GetHandle())->mPayload.mDesc;
		ASSERT_EQ(Frozen.mPipelineStages.size(), 1u);
		ASSERT_EQ(Frozen.mPipelines.size(), 1u);
		EXPECT_EQ(Frozen.mPipelineStages[0].mConfiguration->mKind, EArdaPipelineStateKind::Compute);
		EXPECT_EQ(Frozen.mPipelines[0].mConfiguration->mKind, EArdaPipelineStateKind::Compute);
		EXPECT_NE(Frozen.mPipelineStages[0].mConfiguration.get(), Configuration.get());
		EXPECT_NE(Frozen.mPipelines[0].mConfiguration.get(), Configuration.get());
		const auto RetainedDefinition = Graph.GetTopology().TryGetNode(Node.GetHandle())->mPayload.mDefinition;
		EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister("inductor.test.frozen-configuration"));
		EXPECT_FALSE(FArdaNodeRegistry::Get().Find("inductor.test.frozen-configuration"));
		EXPECT_EQ(Graph.GetTopology().TryGetNode(Node.GetHandle())->mPayload.mDefinition, RetainedDefinition);
		ASSERT_TRUE(Edit.Cancel());
	}
}
