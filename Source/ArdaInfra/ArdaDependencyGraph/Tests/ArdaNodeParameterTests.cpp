#include "ArdaDependencyGraph.h"
#include "Graph/Node/ArdaNodeDefinition.h"
#include "Graph/Node/ArdaNodeParameters.h"
#include <gtest/gtest.h>
#include <type_traits>

namespace arda::node_parameter_tests
{
	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDeclaredComputeParameters)
		ARDA_NODE_BUFFER(mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::ShaderResource)
		ARDA_NODE_BUFFER(mDestination, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mCount, 7u)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaDeclaredComputeNode final
	    : public TArdaNodeDefinition<FArdaDeclaredComputeNode, FArdaDeclaredComputeParameters>
	{
	public:
		inline static uint32_t mRecordedCount = 0;
		inline static FArdaDependencyResourceHandle mRecordedDestination;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-parameters.compute", 1};
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters& P,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			mRecordedCount = P.mCount;
			mRecordedDestination = P.mDestination;
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaDeclaredComputeNode, DeclaredCompute);
	ARDA_DEFINE_GRAPH_NODE(FArdaDeclaredComputeNode, DeclaredCompute);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDeclaredAttachments)
		ARDA_NODE_COLOR_TARGET(mRGB)
		ARDA_NODE_COLOR_TARGET(mNormal)
		ARDA_NODE_DEPTH_TARGET(mDepth, EArdaDependencyAccess::ReadWrite)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaDeclaredAttachmentNode final
	    : public TArdaNodeDefinition<FArdaDeclaredAttachmentNode, FArdaDeclaredAttachments, EArdaNodeType::Graphics>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-parameters.attachments", 1};
		}

		// Optional custom descriptions add non-parameter requirements to the generated declaration.
		static FArdaDependencyNodeDesc Describe(const FArdaParameters&, const FArdaState&)
		{
			FArdaDependencyNodeDesc D;
			D.mbSideEffect = true;
			D.mEstimatedCost = 23;
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

	ARDA_DECLARE_GRAPH_NODE(FArdaDeclaredAttachmentNode, DeclaredAttachments);
	ARDA_DEFINE_GRAPH_NODE(FArdaDeclaredAttachmentNode, DeclaredAttachments);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaReadOnlyDepthParameters)
		ARDA_NODE_DEPTH_TARGET(mDepth, EArdaDependencyAccess::Read)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDepthInitializationParameters)
		ARDA_NODE_TEXTURE(mDepth, EArdaDependencyAccess::Write, EArdaRHIResourceState::DepthWrite)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaDepthInitializationNode final : public TArdaNodeDefinition<FArdaDepthInitializationNode,
	                                               FArdaDepthInitializationParameters,
	                                               EArdaNodeType::Graphics>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-parameters.depth-initialization", 1};
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaDepthInitializationNode, DeclaredDepthInitialization);
	ARDA_DEFINE_GRAPH_NODE(FArdaDepthInitializationNode, DeclaredDepthInitialization);

	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaDeclaredShaderParameters)
		ARDA_SHADER_BUFFER_SRV(mInput, 0, 0, EArdaRHIShaderStage::Compute)
		ARDA_SHADER_BUFFER_UAV(mOutput, 0, 0, EArdaRHIShaderStage::Compute)
		ARDA_SHADER_TEXTURE_SRV(mImage, 1, 0, EArdaRHIShaderStage::Compute)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDeclaredShaderNodeParameters)
		ARDA_NODE_SHADER_BUFFER(mSource, FArdaDeclaredShaderParameters, mInput)
		ARDA_NODE_SHADER_BUFFER_ACCESS(mDestination,
		    FArdaDeclaredShaderParameters,
		    mOutput,
		    EArdaDependencyAccess::ReadWrite)
		ARDA_NODE_SHADER_TEXTURE(mTexture, FArdaDeclaredShaderParameters, mImage)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaArrayShaderParameters)
		ARDA_SHADER_BUFFER_SRV_ARRAY(mBuffers, 0, 0, 2, EArdaRHIShaderStage::Compute)
		ARDA_SHADER_TEXTURE_SRV_ARRAY(mTextures, 2, 0, 2, EArdaRHIShaderStage::Compute)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaArrayParameters)
		ARDA_NODE_SHADER_BUFFER_ARRAY(mSources, FArdaArrayShaderParameters, mBuffers, 2, EArdaDependencyAccess::Read)
		ARDA_NODE_SHADER_TEXTURE_ARRAY(mImages, FArdaArrayShaderParameters, mTextures, 2, EArdaDependencyAccess::Read)
	ARDA_END_NODE_PARAMETER_STRUCT()

	FArdaRHIViewDesc RestrictedBufferView()
	{
		FArdaRHIViewDesc View;
		View.mBufferRange = {16, 32};
		return View;
	}

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaSlotParameters)
		ARDA_NODE_SHADER_BUFFER_BINDING(mFirst,
		    FArdaArrayShaderParameters,
		    mBuffers,
		    "first",
		    EArdaDependencyAccess::Read,
		    1,
		    RestrictedBufferView())
		ARDA_NODE_SHADER_BUFFER_BINDING(mSecond,
		    FArdaArrayShaderParameters,
		    mBuffers,
		    "second",
		    EArdaDependencyAccess::Read,
		    0,
		    FArdaRHIViewDesc{})
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDeclaredVertexParameters)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mCount, 3u)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaDeclaredVertexNode final
	    : public TArdaNodeDefinition<FArdaDeclaredVertexNode, FArdaDeclaredVertexParameters, EArdaNodeType::Vertex>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-parameters.vertex", 1};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaDeclaredVertexNode, DeclaredVertices);
	ARDA_DEFINE_GRAPH_NODE(FArdaDeclaredVertexNode, DeclaredVertices);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDeclaredPixelParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphFragments, mFragments)
		ARDA_NODE_COLOR_TARGET(mColor)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaDeclaredPixelNode final
	    : public TArdaNodeDefinition<FArdaDeclaredPixelNode, FArdaDeclaredPixelParameters, EArdaNodeType::Pixel>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-parameters.pixel", 1};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaDeclaredPixelNode, DeclaredPixels);
	ARDA_DEFINE_GRAPH_NODE(FArdaDeclaredPixelNode, DeclaredPixels);

	struct FArdaComplexSetting
	{
		float mScale = 1;
		uint32_t mIterations = 2;
	};

	struct FArdaResourceWithOffset
	{
		FArdaDependencyResourceHandle mResource;
		uint64_t mOffset = 0;

		operator FArdaDependencyResourceHandle() const
		{
			return mResource;
		}
	};

	struct FArdaStageWithPayload : FArdaGraphStageValue
	{
		uint32_t mPayload = 0;
	};

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaComplexParameters)
		ARDA_NODE_PARAMETER(FArdaComplexSetting, mSettings)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaComplexParameterNode final
	    : public TArdaNodeDefinition<FArdaComplexParameterNode, FArdaComplexParameters, EArdaNodeType::Synchronization>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-parameters.complex", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder{}.Value(P.mSettings.mScale).Value(P.mSettings.mIterations).Build();
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaComplexParameterNode, DeclaredComplex);
	ARDA_DEFINE_GRAPH_NODE(FArdaComplexParameterNode, DeclaredComplex);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDeclaredCudaParameters)
		ARDA_NODE_BUFFER(mDestination, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess)
		ARDA_NODE_PARAMETER_WITH_DEFAULT(uint32_t, mCount, 4u)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaDeclaredCudaNode final
	    : public TArdaNodeDefinition<FArdaDeclaredCudaNode, FArdaDeclaredCudaParameters, EArdaNodeType::Cuda>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-parameters.cuda", 1};
		}

		static FArdaRHIStatus PrepareCuda(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&,
		    FArdaCudaSequence&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaDeclaredCudaNode, DeclaredCuda);
	ARDA_DEFINE_GRAPH_NODE(FArdaDeclaredCudaNode, DeclaredCuda);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDeclaredMissParameters)
		ARDA_NODE_PARAMETER(uint32_t, mIdentity)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaDeclaredMissNode final
	    : public TArdaNodeDefinition<FArdaDeclaredMissNode, FArdaDeclaredMissParameters, EArdaNodeType::RayMiss>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-parameters.ray-miss", 1};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaDeclaredMissNode, DeclaredMiss);
	ARDA_DEFINE_GRAPH_NODE(FArdaDeclaredMissNode, DeclaredMiss);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDeclaredRayGenerationParameters)
		ARDA_NODE_STAGE_INPUTS(FArdaGraphRayShader, mProviders)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaDeclaredRayGenerationNode final : public TArdaNodeDefinition<FArdaDeclaredRayGenerationNode,
	                                                 FArdaDeclaredRayGenerationParameters,
	                                                 EArdaNodeType::RayGeneration>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-parameters.ray-generation", 1};
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaDeclaredRayGenerationNode, DeclaredRayGeneration);
	ARDA_DEFINE_GRAPH_NODE(FArdaDeclaredRayGenerationNode, DeclaredRayGeneration);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDuplicateDepthParameters)
		ARDA_NODE_DEPTH_TARGET(mFirst, EArdaDependencyAccess::Read)
		ARDA_NODE_DEPTH_TARGET(mSecond, EArdaDependencyAccess::Read)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaDuplicateStageParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphFragments, mFirst)
		ARDA_NODE_STAGE_INPUT(FArdaGraphFragments, mSecond)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaDuplicateStageNode final
	    : public TArdaNodeDefinition<FArdaDuplicateStageNode, FArdaDuplicateStageParameters, EArdaNodeType::Pixel>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-parameters.invalid-stage", 1};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaDuplicateStageNode, DeclaredInvalidStage);
	ARDA_DEFINE_GRAPH_NODE(FArdaDuplicateStageNode, DeclaredInvalidStage);

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaNonStageInputParameters)
		ARDA_NODE_STAGE_INPUT(FArdaGraphFragments, mFragments)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaNonStageInputNode final
	    : public TArdaNodeDefinition<FArdaNonStageInputNode, FArdaNonStageInputParameters>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.node-parameters.invalid-compute-stage", 1};
		}

		static FArdaRHIStatus Record(FArdaDependencyExecutionContext&,
		    const FArdaParameters&,
		    const FArdaState&,
		    FArdaInstanceState&)
		{
			return {};
		}
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaNonStageInputNode, DeclaredInvalidComputeStage);
	ARDA_DEFINE_GRAPH_NODE(FArdaNonStageInputNode, DeclaredInvalidComputeStage);

	static_assert(std::is_aggregate_v<FArdaDeclaredComputeParameters>);
	static_assert(FArdaDeclaredComputeParameters::SupportsNodeParameterKey());
	static_assert(TArdaDependencyNodeContract<FArdaDeclaredComputeNode>::value);
	static_assert(TArdaDependencyNodeContract<FArdaDeclaredVertexNode>::value);
	static_assert(TArdaDependencyNodeContract<FArdaDeclaredPixelNode>::value);
	static_assert(!FArdaComplexParameters::SupportsNodeParameterKey());
	static_assert(!TArdaNodeParameterKey<FArdaResourceWithOffset>::mSupported);
	static_assert(!TArdaNodeParameterKey<FArdaStageWithPayload>::mSupported);
	static_assert(TArdaDependencyNodeContract<FArdaComplexParameterNode>::value);
	static_assert(TArdaDependencyNodeContract<FArdaDeclaredCudaNode>::value);
	static_assert(TArdaDependencyNodeContract<FArdaDeclaredMissNode>::value);
	static_assert(TArdaDependencyNodeContract<FArdaDeclaredRayGenerationNode>::value);

	FArdaRHIBufferDesc BufferDescription()
	{
		FArdaRHIBufferDesc D;
		D.mByteSize = 64;
		D.mStructureStride = 4;
		return D;
	}

	FArdaRHITextureDesc TextureDescription(EArdaRHIFormat Format = EArdaRHIFormat::RGBA8UNorm)
	{
		FArdaRHITextureDesc D;
		D.mWidth = D.mHeight = 4;
		D.mFormat = Format;
		return D;
	}

	TEST(ArdaNodeParameters, AggregateAndPositionalCallsGenerateDependenciesWithoutDescribe)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Source = AllocateBuffer(Edit, BufferDescription());
		const auto Destination = AllocateBuffer(Edit, BufferDescription());
		ASSERT_TRUE(Upload(Edit, Source, eastl::vector<uint8_t>(64, 0)));
		const auto Structured = DeclaredCompute(Edit, FArdaDeclaredComputeParameters{Source, Destination});
		const auto Positional = DeclaredCompute(Edit, Source, Destination);
		ASSERT_TRUE(Structured && Positional) << Edit.GetStatus().mMessage.c_str();
		const auto& First = Graph.GetTopology().TryGetNode(Structured.GetHandle())->mPayload;
		const auto& Second = Graph.GetTopology().TryGetNode(Positional.GetHandle())->mPayload;
		EXPECT_EQ(First.mCanonicalKey, Second.mCanonicalKey);
		ASSERT_EQ(First.mDesc.mAccesses.size(), 2u);
		EXPECT_EQ(First.mDesc.mAccesses[0].mResource, Source.GetHandle());
		EXPECT_EQ(First.mDesc.mAccesses[0].mAccess, EArdaDependencyAccess::Read);
		EXPECT_EQ(First.mDesc.mAccesses[0].mState, EArdaRHIResourceState::ShaderResource);
		EXPECT_EQ(First.mDesc.mAccesses[1].mResource, Destination.GetHandle());
		EXPECT_EQ(First.mDesc.mAccesses[1].mAccess, EArdaDependencyAccess::Write);
		EXPECT_EQ(First.mDesc.mAccesses[1].mState, EArdaRHIResourceState::UnorderedAccess);
		FArdaDependencyExecutionContext Context;
		ASSERT_TRUE(Second.mDefinition->mRecord(Context, Second.mParameters.get()));
		EXPECT_EQ(FArdaDeclaredComputeNode::mRecordedCount, 7u);
		Edit.Keep(Destination);
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(Graph.FindResource(Source)->mBuffer.mUsage, EArdaRHIBufferUsage::ShaderResource);
		EXPECT_EQ(Graph.FindResource(Destination)->mBuffer.mUsage, EArdaRHIBufferUsage::UnorderedAccess);
	}

	TEST(ArdaNodeParameters, InvocationFreezesParametersAndCanonicalKeysIncludeEveryDeclaredField)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Source = AllocateBuffer(Edit, BufferDescription());
		const auto Original = AllocateBuffer(Edit, BufferDescription());
		const auto Replacement = AllocateBuffer(Edit, BufferDescription());
		FArdaDeclaredComputeParameters P{Source, Original, 11};
		const auto First = DeclaredCompute(Edit, P);
		ASSERT_TRUE(First);
		P.mCount = 19;
		const auto ChangedCount = DeclaredCompute(Edit, P);
		P.mDestination = Replacement;
		const auto ChangedResource = DeclaredCompute(Edit, P);
		ASSERT_TRUE(ChangedCount && ChangedResource);
		const auto& Frozen = Graph.GetTopology().TryGetNode(First.GetHandle())->mPayload;
		const auto& Count = Graph.GetTopology().TryGetNode(ChangedCount.GetHandle())->mPayload;
		const auto& Resource = Graph.GetTopology().TryGetNode(ChangedResource.GetHandle())->mPayload;
		EXPECT_NE(Frozen.mCanonicalKey, Count.mCanonicalKey);
		EXPECT_NE(Count.mCanonicalKey, Resource.mCanonicalKey);
		ASSERT_EQ(Frozen.mDesc.mAccesses.size(), 2u);
		EXPECT_EQ(Frozen.mDesc.mAccesses[1].mResource, Original.GetHandle());
		FArdaDependencyExecutionContext Context;
		ASSERT_TRUE(Frozen.mDefinition->mRecord(Context, Frozen.mParameters.get()));
		EXPECT_EQ(FArdaDeclaredComputeNode::mRecordedCount, 11u);
		EXPECT_EQ(FArdaDeclaredComputeNode::mRecordedDestination, Original.GetHandle());
	}

	TEST(ArdaNodeParameters, GeneratedRequirementsRejectInsufficientExplicitUsage)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Source = AllocateBuffer(Edit, BufferDescription());
		auto Description = BufferDescription();
		Description.mUsage = EArdaRHIBufferUsage::ShaderResource;
		const auto Destination = AllocateBuffer(Edit, Description);
		ASSERT_TRUE(Upload(Edit, Source, eastl::vector<uint8_t>(64, 0)));
		ASSERT_TRUE(DeclaredCompute(Edit, Source, Destination));
		Edit.Keep(Destination);
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("Explicit usage"), eastl::string::npos);
	}

	TEST(ArdaNodeParameters, AttachmentsPreserveDeclarationOrderAndExtendCustomDescription)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto RGB = AllocateTexture(Edit, TextureDescription());
		const auto Normal = AllocateTexture(Edit, TextureDescription());
		const auto Depth = AllocateTexture(Edit, TextureDescription(EArdaRHIFormat::D32));
		ASSERT_TRUE(DeclaredDepthInitialization(Edit, Depth));
		const auto Operation = DeclaredAttachments(Edit, RGB, Normal, Depth);
		ASSERT_TRUE(Operation) << Edit.GetStatus().mMessage.c_str();
		const auto& Description = Graph.GetTopology().TryGetNode(Operation.GetHandle())->mPayload.mDesc;
		EXPECT_TRUE(Description.mbSideEffect);
		EXPECT_EQ(Description.mEstimatedCost, 23u);
		ASSERT_EQ(Description.mColorTargets.size(), 2u);
		EXPECT_EQ(Description.mColorTargets[0], RGB.GetHandle());
		EXPECT_EQ(Description.mColorTargets[1], Normal.GetHandle());
		EXPECT_EQ(Description.mDepthTarget, Depth.GetHandle());
		ASSERT_EQ(Description.mAccesses.size(), 3u);
		EXPECT_EQ(Description.mAccesses[2].mAccess, EArdaDependencyAccess::ReadWrite);
		EXPECT_EQ(Description.mAccesses[2].mState, EArdaRHIResourceState::DepthWrite);
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		EXPECT_EQ(Graph.FindResource(RGB)->mTexture.mUsage, EArdaRHITextureUsage::RenderTarget);
		EXPECT_EQ(Graph.FindResource(Normal)->mTexture.mUsage, EArdaRHITextureUsage::RenderTarget);
		EXPECT_EQ(Graph.FindResource(Depth)->mTexture.mUsage, EArdaRHITextureUsage::DepthStencil);
	}

	TEST(ArdaNodeParameters, ReadOnlyDepthDeclarationUsesDepthReadState)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Depth = AllocateTexture(Edit, TextureDescription(EArdaRHIFormat::D32));
		FArdaReadOnlyDepthParameters P{Depth};
		FArdaDependencyNodeDesc Description;
		ASSERT_TRUE(FArdaReadOnlyDepthParameters::GetNodeParameterMetadata().Apply(P, Description));
		EXPECT_EQ(Description.mDepthTarget, Depth.GetHandle());
		ASSERT_EQ(Description.mAccesses.size(), 1u);
		EXPECT_EQ(Description.mAccesses[0].mAccess, EArdaDependencyAccess::Read);
		EXPECT_EQ(Description.mAccesses[0].mState, EArdaRHIResourceState::DepthRead);
	}

	TEST(ArdaNodeParameters, ShaderMappingsShareSchemaAndPreserveUavAccess)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Source = AllocateBuffer(Edit, BufferDescription());
		const auto Destination = AllocateBuffer(Edit, BufferDescription());
		const auto Texture = AllocateTexture(Edit, TextureDescription());
		FArdaDependencyNodeDesc Description;
		ASSERT_TRUE(FArdaDeclaredShaderNodeParameters::GetNodeParameterMetadata().Apply({Source, Destination, Texture},
		    Description));
		ASSERT_EQ(Description.mShaderBindings.size(), 1u);
		const auto& Bindings = Description.mShaderBindings[0];
		EXPECT_EQ(Bindings.mMetadata, &FArdaDeclaredShaderParameters::GetStaticMetadata());
		ASSERT_EQ(Bindings.mResources.size(), 3u);
		EXPECT_EQ(Bindings.mResources[0].mMember, "mInput");
		EXPECT_EQ(Bindings.mResources[0].mResource, Source.GetHandle());
		EXPECT_EQ(Bindings.mResources[1].mMember, "mOutput");
		EXPECT_EQ(Bindings.mResources[1].mAccess, EArdaDependencyAccess::ReadWrite);
		EXPECT_EQ(Bindings.mResources[2].mMember, "mImage");
		EXPECT_EQ(Bindings.mResources[2].mResource, Texture.GetHandle());
	}

	TEST(ArdaNodeParameters, StageInputsDoNotDependOnMemberNamingConvention)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Color = AllocateTexture(Edit, TextureDescription());
		const auto Vertices = DeclaredVertices(Edit);
		ASSERT_TRUE(Vertices);
		FArdaRasterConfig Config;
		Config.mViewport = {0, 4, 0, 4, 0, 1};
		const auto Fragments = Rasterize(Edit, Vertices, Config);
		ASSERT_TRUE(Fragments) << Edit.GetStatus().mMessage.c_str();
		const auto Pixels = DeclaredPixels(Edit, Fragments, Color);
		ASSERT_TRUE(Pixels) << Edit.GetStatus().mMessage.c_str();
		const auto& Description = Graph.GetTopology().TryGetNode(Pixels.GetProducer())->mPayload.mDesc;
		EXPECT_EQ(Description.mGraphicsInput.GetProducer(), Fragments.GetProducer());
		EXPECT_EQ(Description.mGraphicsInput.GetKind(), EArdaGraphStageValueKind::Fragments);
		ASSERT_EQ(Description.mColorTargets.size(), 1u);
		EXPECT_EQ(Description.mColorTargets[0], Color.GetHandle());
		// Shader-less facade fixtures stop before pipeline compilation.
	}

	TEST(ArdaNodeParameters, ShaderMappingsMergeWithEmptyDefaultSlotFromCustomDescription)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Source = AllocateBuffer(Edit, BufferDescription());
		const auto Destination = AllocateBuffer(Edit, BufferDescription());
		const auto Texture = AllocateTexture(Edit, TextureDescription());
		FArdaDependencyNodeDesc Description;
		Description.BindShader<FArdaDeclaredShaderParameters>({}, "");
		ASSERT_TRUE(FArdaDeclaredShaderNodeParameters::GetNodeParameterMetadata().Apply({Source, Destination, Texture},
		    Description));
		ASSERT_EQ(Description.mShaderBindings.size(), 1u);
		const auto& Bindings = Description.mShaderBindings[0];
		EXPECT_EQ(Bindings.mMetadata, &FArdaDeclaredShaderParameters::GetStaticMetadata());
		ASSERT_EQ(Bindings.mResources.size(), 3u);
		EXPECT_EQ(Bindings.mResources[0].mResource, Source.GetHandle());
		EXPECT_EQ(Bindings.mResources[1].mResource, Destination.GetHandle());
		EXPECT_EQ(Bindings.mResources[2].mResource, Texture.GetHandle());
	}

	TEST(ArdaNodeParameters, ShaderArraysPreserveElementMappingAndKeyIdentity)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto FirstBuffer = AllocateBuffer(Edit, BufferDescription());
		const auto SecondBuffer = AllocateBuffer(Edit, BufferDescription());
		const auto FirstTexture = AllocateTexture(Edit, TextureDescription());
		const auto SecondTexture = AllocateTexture(Edit, TextureDescription());
		FArdaArrayParameters P{{FirstBuffer, SecondBuffer}, {FirstTexture, SecondTexture}};
		FArdaDependencyNodeDesc Description;
		ASSERT_TRUE(FArdaArrayParameters::GetNodeParameterMetadata().Apply(P, Description));
		ASSERT_EQ(Description.mShaderBindings.size(), 1u);
		const auto& Resources = Description.mShaderBindings[0].mResources;
		ASSERT_EQ(Resources.size(), 4u);
		EXPECT_EQ(Resources[0].mMember, "mBuffers");
		EXPECT_EQ(Resources[0].mResource, FirstBuffer.GetHandle());
		EXPECT_EQ(Resources[1].mResource, SecondBuffer.GetHandle());
		EXPECT_EQ(Resources[2].mMember, "mTextures");
		EXPECT_EQ(Resources[2].mResource, FirstTexture.GetHandle());
		EXPECT_EQ(Resources[3].mResource, SecondTexture.GetHandle());
		EXPECT_EQ(Resources[0].mArrayElement, 0u);
		EXPECT_EQ(Resources[1].mArrayElement, 1u);
		EXPECT_EQ(Resources[2].mArrayElement, 0u);
		EXPECT_EQ(Resources[3].mArrayElement, 1u);
		const auto OriginalKey = FArdaArrayParameters::GetNodeParameterKey(P);
		P.mSources[1] = FirstBuffer;
		EXPECT_NE(OriginalKey, FArdaArrayParameters::GetNodeParameterKey(P));
	}

	TEST(ArdaNodeParameters, ShaderMappingsKeepPipelineSlotsArrayElementsAndViews)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Buffer = AllocateBuffer(Edit, BufferDescription());
		FArdaDependencyNodeDesc Description;
		ASSERT_TRUE(FArdaSlotParameters::GetNodeParameterMetadata().Apply({Buffer, Buffer}, Description));
		ASSERT_EQ(Description.mShaderBindings.size(), 2u);
		const auto& First = Description.mShaderBindings[0];
		const auto& Second = Description.mShaderBindings[1];
		EXPECT_EQ(First.mPipelineSlot, "first");
		EXPECT_EQ(Second.mPipelineSlot, "second");
		ASSERT_EQ(First.mResources.size(), 1u);
		ASSERT_EQ(Second.mResources.size(), 1u);
		EXPECT_EQ(First.mResources[0].mArrayElement, 1u);
		EXPECT_EQ(Second.mResources[0].mArrayElement, 0u);
		EXPECT_EQ(First.mResources[0].mView.mBufferRange.mByteOffset, 16u);
		EXPECT_EQ(First.mResources[0].mView.mBufferRange.mByteSize, 32u);
	}

	TEST(ArdaNodeParameters, GeneratedKeysReuseEquivalentFreshFrameConstructions)
	{
		FArdaDependencyGraph Graph;
		const auto Build = [&](uint32_t Count)
		{
			FArdaGraphEdit Edit(Graph);
			const auto Source = AllocateBuffer(Edit, BufferDescription());
			const auto Destination = AllocateBuffer(Edit, BufferDescription());
			if (!Upload(Edit, Source, eastl::vector<uint8_t>(64, 0)))
			{
				return Edit.GetStatus();
			}
			if (!DeclaredCompute(Edit, Source, Destination, Count))
			{
				return Edit.GetStatus();
			}
			Edit.Keep(Destination);
			return Edit.Commit();
		};
		ASSERT_TRUE(Build(7));
		ASSERT_TRUE(Graph.Compile());
		EXPECT_FALSE(Graph.Execute().mStatus);
		ASSERT_TRUE(Build(7));
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 1u);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, 1u);
		EXPECT_FALSE(Graph.Execute().mStatus);
		ASSERT_TRUE(Build(8));
		ASSERT_TRUE(Graph.Compile());
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, 2u);
	}

	TEST(ArdaNodeParameters, ComplexValuesUseCustomSemanticKeysWithoutRawByteEncoding)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto First = DeclaredComplex(Edit, FArdaComplexSetting{2.0f, 5});
		const auto Same = DeclaredComplex(Edit, FArdaComplexParameters{{2.0f, 5}});
		const auto Changed = DeclaredComplex(Edit, FArdaComplexSetting{2.0f, 6});
		ASSERT_TRUE(First && Same && Changed) << Edit.GetStatus().mMessage.c_str();
		const auto& FirstKey = Graph.GetTopology().TryGetNode(First.GetHandle())->mPayload.mCanonicalKey;
		EXPECT_EQ(FirstKey, Graph.GetTopology().TryGetNode(Same.GetHandle())->mPayload.mCanonicalKey);
		EXPECT_NE(FirstKey, Graph.GetTopology().TryGetNode(Changed.GetHandle())->mPayload.mCanonicalKey);
	}

	TEST(ArdaNodeParameters, CudaUsesSameDeclarativeResourceContract)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Destination = AllocateBuffer(Edit, BufferDescription());
		const auto Operation = DeclaredCuda(Edit, Destination);
		ASSERT_TRUE(Operation) << Edit.GetStatus().mMessage.c_str();
		const auto& Node = Graph.GetTopology().TryGetNode(Operation.GetHandle())->mPayload;
		EXPECT_TRUE(bool(Node.mDefinition->mPrepareCuda));
		EXPECT_FALSE(bool(Node.mDefinition->mRecord));
		ASSERT_EQ(Node.mDesc.mAccesses.size(), 1u);
		EXPECT_EQ(Node.mDesc.mAccesses[0].mResource, Destination.GetHandle());
		EXPECT_EQ(Node.mDesc.mAccesses[0].mAccess, EArdaDependencyAccess::Write);
		EXPECT_EQ(Node.mDesc.mAccesses[0].mState, EArdaRHIResourceState::UnorderedAccess);
	}

	TEST(ArdaNodeParameters, MetadataRejectsAmbiguousDepthAndGraphicsStageInputs)
	{
		EXPECT_FALSE(FArdaDuplicateDepthParameters::GetNodeParameterMetadata().GetStatus());
		EXPECT_TRUE(FArdaDeclaredAttachments::GetNodeParameterMetadata().GetStatus());
		EXPECT_TRUE(FArdaDeclaredPixelParameters::GetNodeParameterMetadata().GetStatus());
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		EXPECT_FALSE(DeclaredInvalidStage(Edit));
		EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
	}

	TEST(ArdaNodeParameters, ConflictingManualDepthLeavesDescriptionUnchanged)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto ManualDepth = AllocateTexture(Edit, TextureDescription(EArdaRHIFormat::D32));
		const auto DeclaredDepth = AllocateTexture(Edit, TextureDescription(EArdaRHIFormat::D32));
		FArdaDependencyNodeDesc Description;
		Description.mDepthTarget = ManualDepth;
		Description.mEstimatedCost = 17;
		EXPECT_FALSE(FArdaReadOnlyDepthParameters::GetNodeParameterMetadata().Apply({DeclaredDepth}, Description));
		EXPECT_EQ(Description.mDepthTarget, ManualDepth.GetHandle());
		EXPECT_EQ(Description.mEstimatedCost, 17u);
		EXPECT_TRUE(Description.mAccesses.empty());
	}

	TEST(ArdaNodeParameters, NonStageOperationsRejectStageInputDependenciesEvenWhenEmpty)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		EXPECT_FALSE(DeclaredInvalidComputeStage(Edit));
		EXPECT_EQ(Edit.GetStatus().mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_TRUE(Graph.GetTopology().GetNodes().empty());
	}

	TEST(ArdaNodeParameters, RayStageInputListsPreserveProviderOrderAndIdentity)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto FirstMiss = DeclaredMiss(Edit, 1u);
		const auto SecondMiss = DeclaredMiss(Edit, 2u);
		ASSERT_TRUE(FirstMiss && SecondMiss);
		FArdaDeclaredRayGenerationParameters P{{FirstMiss, SecondMiss}};
		const auto First = DeclaredRayGeneration(Edit, P);
		P.mProviders = {SecondMiss, FirstMiss};
		const auto Reversed = DeclaredRayGeneration(Edit, P);
		ASSERT_TRUE(First && Reversed) << Edit.GetStatus().mMessage.c_str();
		const auto& Frozen = Graph.GetTopology().TryGetNode(First.GetHandle())->mPayload;
		ASSERT_EQ(Frozen.mDesc.mRayInputs.size(), 2u);
		EXPECT_EQ(Frozen.mDesc.mRayInputs[0].GetProducer(), FirstMiss.GetProducer());
		EXPECT_EQ(Frozen.mDesc.mRayInputs[1].GetProducer(), SecondMiss.GetProducer());
		EXPECT_EQ(Frozen.mDesc.mRayInputs[0].GetKind(), EArdaGraphStageValueKind::RayShader);
		EXPECT_NE(Frozen.mCanonicalKey, Graph.GetTopology().TryGetNode(Reversed.GetHandle())->mPayload.mCanonicalKey);
	}
}
