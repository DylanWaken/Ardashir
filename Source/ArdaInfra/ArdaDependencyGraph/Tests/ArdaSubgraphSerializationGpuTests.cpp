#include "ArdaBackend.h"
#include "ArdaDependencyGraph.h"
#include "ArdaTestBackend.h"

#include <gtest/gtest.h>

namespace arda
{
	namespace
	{
		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaArchivedSubgraphParameters)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mDepth, 0)
			ARDA_NODE_PARAMETER_WITH_DEFAULT(uint8_t, mValue, 0)
			ARDA_NODE_BUFFER(mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaArchivedSubgraphNode final : public TArdaNodeDefinition<FArdaArchivedSubgraphNode,
		                                            FArdaArchivedSubgraphParameters,
		                                            EArdaNodeType::Subgraph>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.subgraph.archived-gpu", 1};
			}

			static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
			{
				FArdaRHIBufferDesc Description;
				Description.mByteSize = 64;
				return Context.Buffer(Parameters.mOutput, "Destination", Description);
			}

			static FArdaRHIStatus Build(FArdaSubgraphContext& Context, const FArdaParameters& Parameters)
			{
				if (Parameters.mDepth)
				{
					const auto Nested =
					    Invoke(Context.GetEdit(), FArdaParameters{Parameters.mDepth - 1, Parameters.mValue}, "Nested");
					return Context.DeclareOutput("Destination", Nested.GetBuffer());
				}
				FArdaMemoryUploadBufferParameters Upload;
				Upload.mBytes.resize(64, Parameters.mValue);
				const auto Uploaded = UploadBuffer(Context.GetEdit(), Upload, "Upload");
				return Context.DeclareOutput("Destination", Uploaded.GetBuffer());
			}
		};

		ARDA_DECLARE_GRAPH_NODE(FArdaArchivedSubgraphNode, BuildArchivedSubgraph);
		ARDA_DEFINE_GRAPH_NODE(FArdaArchivedSubgraphNode, BuildArchivedSubgraph);

		ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaArchivedPassthroughParameters)
			ARDA_NODE_BUFFER(mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::CopySource)
			ARDA_NODE_BUFFER(mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest)
		ARDA_END_NODE_PARAMETER_STRUCT()

		class FArdaArchivedPassthroughNode final : public TArdaNodeDefinition<FArdaArchivedPassthroughNode,
		                                               FArdaArchivedPassthroughParameters,
		                                               EArdaNodeType::Subgraph>
		{
		public:
			static FArdaDependencyNodeMetadata GetMetadata()
			{
				return {"test.subgraph.archived-passthrough", 1};
			}

			static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
			{
				FArdaRHIBufferDesc Description;
				Description.mByteSize = 64;
				return Context.Buffer(Parameters.mOutput, "Destination", Description);
			}

			static FArdaRHIStatus Build(FArdaSubgraphContext& Context, const FArdaParameters&)
			{
				return Context.DeclareOutput("Destination", Context.RequestBufferInput("mSource"));
			}
		};

		ARDA_DECLARE_GRAPH_NODE(FArdaArchivedPassthroughNode, BuildArchivedPassthrough);
		ARDA_DEFINE_GRAPH_NODE(FArdaArchivedPassthroughNode, BuildArchivedPassthrough);

		class FArdaSubgraphSerializationGpu : public testing::TestWithParam<const char*>
		{
		protected:
			FArdaTestDiagnosticCallback mDiagnostics;

			void SetUp() override
			{
				ShutdownBackend();
				auto Configuration = MakeArdaTestBackendConfiguration();
				Configuration.mBackendName = GetParam();
				Configuration.mMessageCallback = &mDiagnostics;
				ASSERT_TRUE(ConfigureBackend(Configuration));
				ARDA_REQUIRE_BACKEND();
			}

			void TearDown() override
			{
				if (const auto Device = GetDevice())
				{
					EXPECT_TRUE(Device->WaitForIdle());
				}
				ShutdownBackend();
				EXPECT_TRUE(ConfigureBackend(MakeArdaTestBackendConfiguration()));
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
		};

		TEST_P(FArdaSubgraphSerializationGpu, RestoredNestedGraphsExecuteIndependentOutputs)
		{
			eastl::string Archive;
			{
				FArdaDependencyGraph Source;
				FArdaGraphEdit Edit(Source);
				const auto Producer = BuildArchivedSubgraph(Edit, FArdaArchivedSubgraphParameters{3, 42}, "Producer");
				const auto First =
				    BuildArchivedPassthrough(Edit, FArdaArchivedPassthroughParameters{Producer.GetBuffer()}, "First");
				const auto Second = BuildArchivedSubgraph(Edit, FArdaArchivedSubgraphParameters{2, 91}, "Second");
				ASSERT_TRUE(First);
				ASSERT_TRUE(Second);
				Edit.Keep(First.GetBuffer());
				Edit.Keep(Second.GetBuffer());
				ASSERT_TRUE(Edit.Commit());
				auto Encoded = FArdaGraphSerialization::Serialize(Source);
				ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
				Archive = Encoded.mValue;
				const auto Compiled = Source.CompileAndWait();
				ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
				Encoded = FArdaGraphSerialization::Serialize(Source);
				ASSERT_TRUE(Encoded) << Encoded.mStatus.mMessage.c_str();
				auto Processed = FArdaDependencyGraph::Deserialize(Encoded.mValue);
				ASSERT_TRUE(Processed) << Processed.mStatus.mMessage.c_str();
				EXPECT_TRUE(Processed.mValue->IsPreprocessed());
				EXPECT_EQ(Processed.mValue->GetTopology().GetNodeCount(), Source.GetTopology().GetNodeCount());
				const auto Reencoded = Processed.mValue->Serialize();
				ASSERT_TRUE(Reencoded) << Reencoded.mStatus.mMessage.c_str();
				EXPECT_EQ(Reencoded.mValue, Encoded.mValue);
			}

			auto Restored = FArdaGraphSerialization::Deserialize(Archive, GetDevice());
			ASSERT_TRUE(Restored) << Restored.mStatus.mMessage.c_str();
			auto& Graph = *Restored.mValue;
			ASSERT_EQ(Graph.GetTopology().GetNodes().size(), 3u);
			FArdaGraphEdit Edit(Graph);
			const auto First = Edit.UseBuffer(Graph.FindOutput(Graph.FindNode("First"), "Destination"));
			const auto Second = Edit.UseBuffer(Graph.FindOutput(Graph.FindNode("Second"), "Destination"));
			const auto FirstBytes = Readback(Edit, First);
			const auto SecondBytes = Readback(Edit, Second);
			ASSERT_TRUE(Edit.Commit()) << Edit.GetStatus().mMessage.c_str();
			const auto Compiled = Graph.CompileAndWait();
			ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			for (uint32_t Frame = 0; Frame < 2; ++Frame)
			{
				const auto Executed = Graph.ExecuteAndWait();
				ASSERT_TRUE(Executed.mStatus) << Executed.mStatus.mMessage.c_str();
				ASSERT_EQ(FirstBytes->size(), 64u);
				ASSERT_EQ(SecondBytes->size(), 64u);
				for (size_t Index = 0; Index < 64; ++Index)
				{
					EXPECT_EQ((*FirstBytes)[Index], 42u);
					EXPECT_EQ((*SecondBytes)[Index], 91u);
				}
			}
		}

#if defined(ARDA_TEST_NATIVE_D3D12) || defined(ARDA_TEST_NATIVE_VULKAN)
		const char* const ArdaSubgraphArchiveBackends[] = {
#if defined(ARDA_TEST_NATIVE_D3D12)
		    "native-d3d12",
#endif
#if defined(ARDA_TEST_NATIVE_VULKAN)
		    "native-vulkan",
#endif
		};
		INSTANTIATE_TEST_SUITE_P(Native, FArdaSubgraphSerializationGpu, testing::ValuesIn(ArdaSubgraphArchiveBackends));
#else
		GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(FArdaSubgraphSerializationGpu);
#endif
	}
}
