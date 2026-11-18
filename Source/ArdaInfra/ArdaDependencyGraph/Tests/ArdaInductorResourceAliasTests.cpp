#include "ArdaDependencyGraph.h"
#include <gtest/gtest.h>

namespace arda::resource_alias_tests
{
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaResourceAliasParameters)
		ARDA_NODE_BUFFER(mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::CopySource)
		ARDA_NODE_BUFFER(mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaResourceAliasNode final
	    : public TArdaNodeDefinition<FArdaResourceAliasNode, FArdaResourceAliasParameters, EArdaNodeType::Subgraph>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.rules.resource-alias", 1};
		}

		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
		{
			FArdaRHIBufferDesc Description;
			Description.mByteSize = 16;
			return Context.Buffer(Parameters.mOutput, "Destination", Description);
		}

		static FArdaRHIStatus Build(FArdaSubgraphContext& Context, const FArdaParameters&)
		{
			return Context.DeclareOutput("Destination", Context.RequestBufferInput("mSource"));
		}
	};

	class FArdaPersistAliasedResourceRule final : public FArdaGraphPreprocessRule
	{
	public:
		explicit FArdaPersistAliasedResourceRule(FArdaDependencyResourceHandle Resource)
		    : mResource(Resource)
		{
		}

		const char* GetName() const noexcept override
		{
			return "PersistAliasedResource";
		}

		FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const override
		{
			auto* Resource = Graph.EditResource(mResource);
			if (!Resource)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Expected a live aliased resource.");
			}
			Resource->mbPersistent = true;
			return {};
		}

	private:
		FArdaDependencyResourceHandle mResource;
	};

	TEST(ArdaInductorRules, PreprocessingAnAliasedOutputEditsItsResolvedStorage)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaMemoryUploadBufferParameters Upload;
		FArdaRHIBufferDesc Description;
		Description.mByteSize = 16;
		Upload.mDestination = AllocateBuffer(Edit, Description);
		Upload.mBytes.resize(16, 7);
		const auto Source = UploadBuffer(Edit, Upload).GetBuffer();
		const auto Output = FArdaResourceAliasNode::Invoke(Edit, FArdaResourceAliasParameters{Source}).GetBuffer();
		ASSERT_TRUE(Output);
		ASSERT_NE(Source.GetHandle().mIndex, Output.GetHandle().mIndex);
		FArdaInductorOptions Options;
		Options.mRules.mPreprocessRules.push_back(MakeArdaGraphRule<FArdaPersistAliasedResourceRule>(Output));
		ASSERT_TRUE(Edit.SetOptions(Options));
		Edit.Keep(Output);
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.CompileAndWait();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		ASSERT_NE(Graph.FindResource(Source), nullptr);
		EXPECT_EQ(Graph.FindResource(Source), Graph.FindResource(Output));
		EXPECT_TRUE(Graph.FindResource(Source)->mbPersistent);
	}
}
