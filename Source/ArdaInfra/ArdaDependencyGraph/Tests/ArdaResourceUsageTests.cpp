#include "ArdaDependencyGraph.h"
#include "Graph/DefaultNodes/ArdaGraphOperations.h"
#include "Graph/Node/ArdaNodeDefinition.h"
#include <gtest/gtest.h>
#include <EASTL/algorithm.h>

namespace arda::resource_usage_tests
{
	struct FArdaUsageAccessParameters
	{
		FArdaDependencyResourceHandle mResource;
		EArdaDependencyAccess mAccess = EArdaDependencyAccess::Write;
		EArdaRHIResourceState mState = EArdaRHIResourceState::UnorderedAccess;
		bool mbAttachment = false;
	};

	class FArdaUsageAccessNode final
	    : public TArdaNodeDefinition<FArdaUsageAccessNode, FArdaUsageAccessParameters, EArdaNodeType::Graphics>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.resource-usage.access", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}
			    .Resource(P.mResource)
			    .Value(P.mAccess)
			    .Value(P.mState)
			    .Value(P.mbAttachment)
			    .Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc D;
			D.mbSideEffect = true;
			D.mAccesses = {{P.mResource, P.mAccess, P.mState}};
			if (P.mbAttachment)
			{
				if (P.mState == EArdaRHIResourceState::RenderTarget)
				{
					D.mColorTargets = {P.mResource};
				}
				else
				{
					D.mDepthTarget = P.mResource;
				}
			}
			return D;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaUsageAccessNode, ResourceUsageAccess);
	ARDA_DEFINE_GRAPH_NODE(FArdaUsageAccessNode, ResourceUsageAccess);

	FArdaRHIBufferDesc BufferDescription()
	{
		FArdaRHIBufferDesc D;
		D.mByteSize = 256;
		return D;
	}

	FArdaRHITextureDesc TextureDescription(EArdaRHIFormat Format = EArdaRHIFormat::RGBA8UNorm)
	{
		FArdaRHITextureDesc D;
		D.mWidth = D.mHeight = 4;
		D.mFormat = Format;
		return D;
	}

	TEST(ArdaResourceUsage, DescriptorsDefaultToAutomaticDeduction)
	{
		EXPECT_EQ(FArdaRHIBufferDesc{}.mUsage, EArdaRHIBufferUsage::AutoDeduction);
		EXPECT_EQ(FArdaRHITextureDesc{}.mUsage, EArdaRHITextureUsage::AutoDeduction);
	}

	TEST(ArdaResourceUsage, UnionsBufferRequirementsAcrossWriterAndReader)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = AllocateBuffer(Edit, BufferDescription());
		ASSERT_TRUE(Buffer);
		ASSERT_TRUE(
		    ResourceUsageAccess(Edit, {Buffer, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess}));
		ASSERT_TRUE(
		    ResourceUsageAccess(Edit, {Buffer, EArdaDependencyAccess::Read, EArdaRHIResourceState::ShaderResource}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		ASSERT_NE(Graph.FindResource(Buffer), nullptr);
		EXPECT_EQ(Graph.FindResource(Buffer)->mBuffer.mUsage,
		    EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource);
		EXPECT_EQ(Graph.FindResource(Buffer)->mTexture.mUsage, EArdaRHITextureUsage::None);
	}

	TEST(ArdaResourceUsage, PreservesExplicitHintsWhenAutomaticDeductionIsEnabled)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		auto Description = BufferDescription();
		Description.mUsage |= EArdaRHIBufferUsage::Raw;
		const auto Buffer = AllocateBuffer(Edit, Description);
		ASSERT_TRUE(Buffer);
		ASSERT_TRUE(ResourceUsageAccess(Edit, {Buffer}));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.FindResource(Buffer)->mBuffer.mUsage,
		    EArdaRHIBufferUsage::Raw | EArdaRHIBufferUsage::UnorderedAccess);
	}

	TEST(ArdaResourceUsage, InitialStateContributesItsRequiredCapability)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		auto Description = BufferDescription();
		Description.mInitialState = EArdaRHIResourceState::VertexBuffer;
		const auto Buffer = AllocateBuffer(Edit, Description);
		ASSERT_TRUE(Buffer);
		ASSERT_TRUE(ResourceUsageAccess(Edit, {Buffer, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest}));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.FindResource(Buffer)->mBuffer.mUsage, EArdaRHIBufferUsage::Vertex);
	}

	TEST(ArdaResourceUsage, ColorAttachmentAndLaterSamplingInferBothCapabilities)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Texture = AllocateTexture(Edit, TextureDescription());
		ASSERT_TRUE(Texture);
		ASSERT_TRUE(ResourceUsageAccess(Edit,
		    {Texture, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget, true}));
		ASSERT_TRUE(
		    ResourceUsageAccess(Edit, {Texture, EArdaDependencyAccess::Read, EArdaRHIResourceState::ShaderResource}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(Graph.FindResource(Texture)->mTexture.mUsage,
		    EArdaRHITextureUsage::RenderTarget | EArdaRHITextureUsage::ShaderResource);
		EXPECT_EQ(Graph.FindResource(Texture)->mBuffer.mUsage, EArdaRHIBufferUsage::None);
	}

	TEST(ArdaResourceUsage, DepthAttachmentAndLaterSamplingInferBothCapabilities)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Depth = AllocateTexture(Edit, TextureDescription(EArdaRHIFormat::D32));
		ASSERT_TRUE(Depth) << Edit.GetStatus().mMessage.c_str();
		ASSERT_TRUE(
		    ResourceUsageAccess(Edit, {Depth, EArdaDependencyAccess::Write, EArdaRHIResourceState::DepthWrite, true}));
		ASSERT_TRUE(
		    ResourceUsageAccess(Edit, {Depth, EArdaDependencyAccess::Read, EArdaRHIResourceState::ShaderResource}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(Graph.FindResource(Depth)->mTexture.mUsage,
		    EArdaRHITextureUsage::DepthStencil | EArdaRHITextureUsage::ShaderResource);
	}

	TEST(ArdaResourceUsage, CopyOnlyAndUnusedResourcesResolveToConcreteNone)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Source = AllocateBuffer(Edit, BufferDescription());
		const auto Destination = AllocateBuffer(Edit, BufferDescription());
		const auto UnusedBuffer = AllocateBuffer(Edit, BufferDescription());
		const auto UnusedTexture = AllocateTexture(Edit, TextureDescription());
		ASSERT_TRUE(Upload(Edit, Source, eastl::vector<uint8_t>(256, 7)));
		ASSERT_TRUE(CopyTo(Edit, Source, Destination));
		Edit.Keep(Destination);
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		for (const auto Buffer : {Source, Destination, UnusedBuffer})
		{
			ASSERT_NE(Graph.FindResource(Buffer), nullptr);
			EXPECT_EQ(Graph.FindResource(Buffer)->mBuffer.mUsage, EArdaRHIBufferUsage::None);
			EXPECT_EQ(Graph.FindResource(Buffer)->mTexture.mUsage, EArdaRHITextureUsage::None);
		}
		EXPECT_EQ(Graph.FindResource(UnusedTexture)->mTexture.mUsage, EArdaRHITextureUsage::None);
		EXPECT_EQ(Graph.FindResource(UnusedTexture)->mBuffer.mUsage, EArdaRHIBufferUsage::None);
	}

	TEST(ArdaResourceUsage, ExplicitSupersetRemainsUnchanged)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		auto Description = BufferDescription();
		Description.mUsage =
		    EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::Indirect;
		const auto Buffer = AllocateBuffer(Edit, Description);
		ASSERT_TRUE(ResourceUsageAccess(Edit, {Buffer}));
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.FindResource(Buffer)->mBuffer.mUsage, Description.mUsage);
	}

	TEST(ArdaResourceUsage, ExplicitBufferCapabilitiesCannotBeSilentlyExpanded)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		auto Description = BufferDescription();
		Description.mUsage = EArdaRHIBufferUsage::ShaderResource;
		const auto Buffer = AllocateBuffer(Edit, Description);
		ASSERT_TRUE(Buffer);
		ASSERT_TRUE(ResourceUsageAccess(Edit, {Buffer}));
		ASSERT_TRUE(Edit.Commit());
		EXPECT_FALSE(Graph.Compile());
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 0u);
	}

	TEST(ArdaResourceUsage, ExplicitTextureCapabilitiesCannotBeSilentlyExpanded)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		auto Description = TextureDescription();
		Description.mUsage = EArdaRHITextureUsage::ShaderResource;
		const auto Texture = AllocateTexture(Edit, Description);
		ASSERT_TRUE(
		    ResourceUsageAccess(Edit, {Texture, EArdaDependencyAccess::Write, EArdaRHIResourceState::RenderTarget}));
		ASSERT_TRUE(Edit.Commit());
		EXPECT_FALSE(Graph.Compile());
	}

	TEST(ArdaResourceUsage, OmittingDeductionRuleCannotPublishAutomaticDescriptions)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = AllocateBuffer(Edit, BufferDescription());
		ASSERT_TRUE(Buffer);
		FArdaInductorOptions Options;
		auto& Rules = Options.mRules.mPreprocessRules;
		const auto Rule = eastl::find_if(Rules.begin(),
		    Rules.end(),
		    [](const auto& Candidate)
		    {
			    return dynamic_cast<const FArdaDeduceResourceUsageRule*>(Candidate.get()) != nullptr;
		    });
		ASSERT_NE(Rule, Rules.end());
		Rules.erase(Rule);
		ASSERT_TRUE(Edit.SetOptions(Options));
		ASSERT_TRUE(Edit.Commit());
		EXPECT_FALSE(Graph.Compile());
	}

	class FArdaRestoreAutomaticUsageRule final : public FArdaGraphPreprocessRule
	{
	public:
		explicit FArdaRestoreAutomaticUsageRule(FArdaDependencyResourceHandle Resource)
		    : mResource(Resource)
		{
		}

		const char* GetName() const noexcept override
		{
			return "TestRestoreAutomaticUsage";
		}

		FArdaRHIStatus Apply(FArdaInductorGraph& Graph) const override
		{
			Graph.EditResource(mResource)->mBuffer.mUsage = EArdaRHIBufferUsage::AutoDeduction;
			return {};
		}

	private:
		FArdaDependencyResourceHandle mResource;
	};

	TEST(ArdaResourceUsage, LaterCustomRuleCannotRestoreUnresolvedUsage)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = AllocateBuffer(Edit, BufferDescription());
		ASSERT_TRUE(Buffer);
		FArdaInductorOptions Options;
		Options.mRules.mPreprocessRules.push_back(MakeArdaGraphRule<FArdaRestoreAutomaticUsageRule>(Buffer));
		ASSERT_TRUE(Edit.SetOptions(Options));
		ASSERT_TRUE(Edit.Commit());
		EXPECT_FALSE(Graph.Compile());
	}

	TEST(ArdaResourceUsage, RepeatedConstructionKeepsAutomaticRequestAndReusesResolvedRevision)
	{
		FArdaDependencyGraph Graph;
		uint64_t FirstHash = 0;
		for (uint32_t Frame = 0; Frame < 2; ++Frame)
		{
			FArdaGraphEdit Edit(Graph);
			const auto Buffer = AllocateBuffer(Edit, BufferDescription(), "same-request");
			ASSERT_TRUE(Buffer);
			EXPECT_EQ(Graph.FindResource(Buffer)->mBuffer.mUsage, EArdaRHIBufferUsage::AutoDeduction);
			ASSERT_TRUE(ResourceUsageAccess(Edit, {Buffer}));
			ASSERT_TRUE(Edit.Commit());
			ASSERT_TRUE(Graph.Compile());
			EXPECT_EQ(Graph.FindResource(Buffer)->mBuffer.mUsage, EArdaRHIBufferUsage::UnorderedAccess);
			const auto Stats = Graph.GetRevisionStats();
			if (!Frame)
			{
				FirstHash = Stats.mConstructionHash;
			}
			else
			{
				EXPECT_EQ(Stats.mConstructionHash, FirstHash);
				EXPECT_EQ(Stats.mCompileCount, 1u);
				EXPECT_EQ(Stats.mCacheHitCount, 1u);
			}
			EXPECT_FALSE(Graph.Execute().mStatus); // A device-less execution releases the edit lock.
		}
	}

	struct FArdaUsageOutputParameters
	{
		FArdaDependencyResourceHandle mOutput;
		bool mbReject = false;
	};

	class FArdaUsageOutputNode final
	    : public TArdaNodeDefinition<FArdaUsageOutputNode, FArdaUsageOutputParameters, EArdaNodeType::Copy>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.resource-usage.output", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Resource(P.mOutput).Value(P.mbReject).Build();
		}

		static FArdaRHIStatus DeclareResources(FArdaDependencyResourceContext& Context, FArdaParameters& P)
		{
			auto Description = BufferDescription();
			Description.mUsage = EArdaRHIBufferUsage::ShaderResource;
			return Context.Buffer(P.mOutput, "Output", Description);
		}

		static FArdaRHIStatus Validate(const FArdaParameters& P)
		{
			return P.mbReject
			    ? FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Rejected after resource declaration.")
			    : FArdaRHIStatus{};
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc D;
			D.mbSideEffect = true;
			D.mAccesses = {{P.mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::CopyDest}};
			return D;
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaUsageOutputNode, ResourceUsageOutput);
	ARDA_DEFINE_GRAPH_NODE(FArdaUsageOutputNode, ResourceUsageOutput);

	TEST(ArdaResourceUsage, SuppliedAutomaticOutputRetainsNodeDeclarationRequirementsUntilCompile)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = AllocateBuffer(Edit, BufferDescription());
		ASSERT_TRUE(ResourceUsageOutput(Edit, {Buffer}));
		EXPECT_EQ(Graph.FindResource(Buffer)->mBuffer.mUsage, EArdaRHIBufferUsage::AutoDeduction);
		ASSERT_TRUE(Edit.Commit());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.FindResource(Buffer)->mBuffer.mUsage, EArdaRHIBufferUsage::ShaderResource);
	}

	TEST(ArdaResourceUsage, FailedOutputDeclarationDoesNotMutateSuppliedAutomaticResource)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Declaration(Graph);
		const auto Buffer = AllocateBuffer(Declaration, BufferDescription());
		ASSERT_TRUE(Declaration.Commit());
		FArdaGraphEdit Edit(Graph);
		EXPECT_FALSE(ResourceUsageOutput(Edit, {Buffer, true}));
		EXPECT_EQ(Graph.FindResource(Buffer)->mBuffer.mUsage, EArdaRHIBufferUsage::AutoDeduction);
		ASSERT_TRUE(Edit.Cancel());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.FindResource(Buffer)->mBuffer.mUsage, EArdaRHIBufferUsage::None);
	}

	TEST(ArdaResourceUsage, CancelledOutputDeclarationDoesNotRetainUsageRequirements)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Declaration(Graph);
		const auto Buffer = AllocateBuffer(Declaration, BufferDescription());
		ASSERT_TRUE(Declaration.Commit());
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(ResourceUsageOutput(Edit, {Buffer}));
		ASSERT_TRUE(Edit.Cancel());
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.FindResource(Buffer)->mBuffer.mUsage, EArdaRHIBufferUsage::None);
	}
}
