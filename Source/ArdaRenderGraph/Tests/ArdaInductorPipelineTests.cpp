#include "ArdaInductorPipeline.h"

#include <gtest/gtest.h>

namespace
{
	using namespace arda;

	template <typename Interface, EArdaRHIResourceType Kind>
	class TPipelineTestResource : public Interface
	{
	public:
		void AddRef() noexcept override
		{
			++mReferences;
		}

		void Release() noexcept override
		{
			if (--mReferences == 0)
			{
				delete this;
			}
		}

		EArdaRHIResourceType GetResourceType() const noexcept override
		{
			return Kind;
		}

		const char* GetDebugName() const noexcept override
		{
			return "Pipeline inference test resource";
		}

	private:
		uint32_t mReferences = 0;
	};

	class FPipelineTestShader final : public TPipelineTestResource<IArdaRHIShader, EArdaRHIResourceType::Shader>
	{
	public:
		FPipelineTestShader(EArdaRHIShaderStage Stage, uint64_t Key)
		    : mStage(Stage),
		      mKey(Key)
		{
		}

		EArdaRHIShaderStage GetStage() const noexcept override
		{
			return mStage;
		}

		uint64_t GetPersistentCacheHash() const noexcept override
		{
			return mKey;
		}

	private:
		EArdaRHIShaderStage mStage;
		uint64_t mKey;
	};

	class FPipelineTestLayout final
	    : public TPipelineTestResource<IArdaRHIBindingLayout, EArdaRHIResourceType::BindingLayout>
	{
	public:
		FPipelineTestLayout(uint32_t Space,
		    EArdaRHIBindingType Type,
		    EArdaRHIShaderStage Visibility = EArdaRHIShaderStage::All)
		{
			mDesc.mVisibility = Visibility;
			mDesc.mRegisterSpace = Space;
			mDesc.mbRegisterSpaceIsDescriptorSet = true;
			mDesc.mItems.push_back({0, 1, Type});
		}

		const FArdaRHIBindingLayoutDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

	private:
		FArdaRHIBindingLayoutDesc mDesc;
	};

	FArdaInductorPipelineContribution Stage(EArdaRHIShaderStage Type, uint64_t Key, const char* Group = "")
	{
		FArdaInductorPipelineContribution Result;
		Result.mShader = FArdaRHIShaderRef(new FPipelineTestShader(Type, Key));
		Result.mGroup = Group;
		return Result;
	}

	FArdaRHIBindingLayoutRef Layout(uint32_t Space, EArdaRHIBindingType Type)
	{
		return FArdaRHIBindingLayoutRef(new FPipelineTestLayout(Space, Type));
	}

	FArdaInductorPipelineRequest Request(uint64_t Terminal,
	    EArdaPipelineStateKind Kind = EArdaPipelineStateKind::Graphics)
	{
		FArdaInductorPipelineRequest Result;
		Result.mSlot = "main";
		Result.mTerminalNodeId = Terminal;
		Result.mKind = Kind;
		return Result;
	}
}

TEST(ArdaInductorPipeline, InfersAcrossIntermediateNodesAndIgnoresInstanceIdentity)
{
	eastl::vector<FArdaInductorPipelineNode> Nodes{{1, {}, {Stage(EArdaRHIShaderStage::Vertex, 100)}},
	    {2, {1}, {}},
	    {3, {2}, {Stage(EArdaRHIShaderStage::Pixel, 200)}},
	    {4, {3}, {}}};
	const auto First = InferArdaInductorPipeline(Nodes, Request(4));
	ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
	EXPECT_EQ(First.mValue.mContributingNodeIds, (eastl::vector<uint64_t>{1, 3}));
	EXPECT_EQ(First.mValue.mConfiguration.mGraphics.mDesc.mVertexShader->GetPersistentCacheHash(), 100u);
	EXPECT_EQ(First.mValue.mConfiguration.mGraphics.mDesc.mPixelShader->GetPersistentCacheHash(), 200u);
	EXPECT_NE(First.mValue.mStableKey, 0u);
	eastl::vector<FArdaInductorPipelineNode> Renumbered{{40, {30}, {}},
	    {30, {20}, {Stage(EArdaRHIShaderStage::Pixel, 200)}},
	    {20, {10}, {}},
	    {10, {}, {Stage(EArdaRHIShaderStage::Vertex, 100)}}};
	auto RenamedRequest = Request(40);
	RenamedRequest.mSlot = "another instance slot";
	const auto Same = InferArdaInductorPipeline(Renumbered, RenamedRequest);
	ASSERT_TRUE(Same);
	EXPECT_EQ(Same.mValue.mStableKey, First.mValue.mStableKey);
	Renumbered[1].mContributions[0] = Stage(EArdaRHIShaderStage::Pixel, 201);
	const auto Changed = InferArdaInductorPipeline(Renumbered, RenamedRequest);
	ASSERT_TRUE(Changed);
	EXPECT_NE(Changed.mValue.mStableKey, First.mValue.mStableKey);
}

TEST(ArdaInductorPipeline, SequentialComputeConsumersOwnIndependentPipelines)
{
	eastl::vector<FArdaInductorPipelineNode> Nodes{
	    {1, {}, {Stage(EArdaRHIShaderStage::Compute, 100)}, {EArdaPipelineStateKind::Compute}},
	    {2, {1}, {}},
	    {3, {2}, {Stage(EArdaRHIShaderStage::Compute, 200)}, {EArdaPipelineStateKind::Compute}}};
	const auto First = InferArdaInductorPipeline(Nodes, Request(1, EArdaPipelineStateKind::Compute));
	const auto Second = InferArdaInductorPipeline(Nodes, Request(3, EArdaPipelineStateKind::Compute));
	ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
	ASSERT_TRUE(Second) << Second.mStatus.mMessage.c_str();
	EXPECT_EQ(First.mValue.mConfiguration.mCompute.mDesc.mComputeShader->GetPersistentCacheHash(), 100u);
	EXPECT_EQ(Second.mValue.mConfiguration.mCompute.mDesc.mComputeShader->GetPersistentCacheHash(), 200u);
	EXPECT_EQ(Second.mValue.mContributingNodeIds, (eastl::vector<uint64_t>{3}));
	EXPECT_NE(First.mValue.mStableKey, Second.mValue.mStableKey);
	const eastl::vector<FArdaInductorPipelineNode> Isolated{
	    {40, {}, {Stage(EArdaRHIShaderStage::Compute, 200)}, {EArdaPipelineStateKind::Compute}}};
	const auto Same = InferArdaInductorPipeline(Isolated, Request(40, EArdaPipelineStateKind::Compute));
	ASSERT_TRUE(Same);
	EXPECT_EQ(Same.mValue.mStableKey, Second.mValue.mStableKey);
	// A previous consumer's invalid shader is outside this pattern; its own request
	// still reports the error. Topology errors remain errors even across the boundary.
	Nodes[0].mContributions[0] = Stage(EArdaRHIShaderStage::Compute, 0);
	EXPECT_TRUE(InferArdaInductorPipeline(Nodes, Request(3, EArdaPipelineStateKind::Compute)));
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, Request(1, EArdaPipelineStateKind::Compute)));
	Nodes[0].mDependencies = {99};
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, Request(3, EArdaPipelineStateKind::Compute)));
	Nodes[0].mDependencies = {3};
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, Request(3, EArdaPipelineStateKind::Compute)));
}

TEST(ArdaInductorPipeline, SharedProvidersTraverseOtherFamiliesAndIndependentEdges)
{
	eastl::vector<FArdaInductorPipelineNode> Nodes{{1, {}, {Stage(EArdaRHIShaderStage::Vertex, 100)}},
	    {2, {1}, {Stage(EArdaRHIShaderStage::Pixel, 200)}, {EArdaPipelineStateKind::Graphics}},
	    {3, {2, 1}, {Stage(EArdaRHIShaderStage::Compute, 300)}, {EArdaPipelineStateKind::Compute}},
	    {4, {3}, {Stage(EArdaRHIShaderStage::Pixel, 400)}, {EArdaPipelineStateKind::Graphics}}};
	const auto First = InferArdaInductorPipeline(Nodes, Request(2));
	const auto Second = InferArdaInductorPipeline(Nodes, Request(4));
	ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
	ASSERT_TRUE(Second) << Second.mStatus.mMessage.c_str();
	EXPECT_EQ(Second.mValue.mConfiguration.mGraphics.mDesc.mVertexShader->GetPersistentCacheHash(), 100u);
	EXPECT_EQ(Second.mValue.mConfiguration.mGraphics.mDesc.mPixelShader->GetPersistentCacheHash(), 400u);
	EXPECT_EQ(Second.mValue.mContributingNodeIds, (eastl::vector<uint64_t>{1, 4}));
	Nodes[2].mDependencies = {2};
	// Sharing through a completed graphics consumer alone does not propagate its stages.
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, Request(4)));
}

TEST(ArdaInductorPipeline, AmbiguousBranchesRequireExplicitGroups)
{
	eastl::vector<FArdaInductorPipelineNode> Nodes{{1, {}, {Stage(EArdaRHIShaderStage::Vertex, 100, "left")}},
	    {2, {}, {Stage(EArdaRHIShaderStage::Vertex, 200, "right")}},
	    {3, {1, 2}, {Stage(EArdaRHIShaderStage::Pixel, 300)}}};
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, Request(3)));
	auto Left = Request(3);
	Left.mGroup = "left";
	const auto Selected = InferArdaInductorPipeline(Nodes, Left);
	ASSERT_TRUE(Selected) << Selected.mStatus.mMessage.c_str();
	EXPECT_EQ(Selected.mValue.mConfiguration.mGraphics.mDesc.mVertexShader->GetPersistentCacheHash(), 100u);
	EXPECT_EQ(Selected.mValue.mContributingNodeIds, (eastl::vector<uint64_t>{1, 3}));
	Nodes[0].mContributions[0].mGroup.clear();
	Nodes[1].mContributions[0].mGroup.clear();
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, Request(3)));
}

TEST(ArdaInductorPipeline, RejectsInvalidTopologyAndUnidentifiedShaders)
{
	eastl::vector<FArdaInductorPipelineNode> Nodes{{1, {2}, {Stage(EArdaRHIShaderStage::Compute, 1)}}};
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, Request(1, EArdaPipelineStateKind::Compute)));
	Nodes.push_back({2, {1}, {}});
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, Request(1, EArdaPipelineStateKind::Compute)));
	Nodes[1].mDependencies.clear();
	Nodes[0].mContributions[0] = Stage(EArdaRHIShaderStage::Compute, 0);
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, Request(1, EArdaPipelineStateKind::Compute)));
	Nodes[0].mContributions[0] = Stage(EArdaRHIShaderStage::Compute, 1);
	ASSERT_TRUE(InferArdaInductorPipeline(Nodes, Request(1, EArdaPipelineStateKind::Compute)));
	Nodes.push_back(Nodes[1]);
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, Request(1, EArdaPipelineStateKind::Compute)));
}

TEST(ArdaInductorPipeline, CombinesCompatibleLayoutsAndRejectsConflictingSettings)
{
	auto Vertex = Stage(EArdaRHIShaderStage::Vertex, 1);
	auto Pixel = Stage(EArdaRHIShaderStage::Pixel, 2);
	Vertex.mBindingLayouts = {Layout(1, EArdaRHIBindingType::TextureSRV)};
	Pixel.mBindingLayouts = {Layout(0, EArdaRHIBindingType::ConstantBuffer)};
	eastl::vector<FArdaInductorPipelineNode> Nodes{{1, {}, {Vertex}}, {2, {1}, {Pixel}}};
	const auto First = InferArdaInductorPipeline(Nodes, Request(2));
	ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
	const auto& Layouts = First.mValue.mConfiguration.mGraphics.mDesc.mBindingLayouts;
	ASSERT_EQ(Layouts.size(), 2u);
	EXPECT_EQ(Layouts[0]->GetDesc().mRegisterSpace, 0u);
	EXPECT_EQ(Layouts[1]->GetDesc().mRegisterSpace, 1u);
	Nodes[0].mContributions[0].mBindingLayouts = {Layout(1, EArdaRHIBindingType::TextureSRV)};
	EXPECT_EQ(InferArdaInductorPipeline(Nodes, Request(2)).mValue.mStableKey, First.mValue.mStableKey);
	Nodes[1].mContributions[0].mBindingLayouts = {Layout(1, EArdaRHIBindingType::ConstantBuffer)};
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, Request(2)));
	Nodes[1].mContributions[0].mBindingLayouts.clear();
	auto A = eastl::make_shared<FArdaInductorPipelineConfiguration>();
	auto B = eastl::make_shared<FArdaInductorPipelineConfiguration>();
	B->mGraphics.mDesc.mRasterState.mCullMode = EArdaRHICullMode::Front;
	Nodes[0].mContributions[0].mConfiguration = A;
	Nodes[1].mContributions[0].mConfiguration = B;
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, Request(2)));
}

TEST(ArdaInductorPipeline, StageDisjointLayoutsShareRegisterSpaceWithStableIdentity)
{
	auto Vertex = Stage(EArdaRHIShaderStage::Vertex, 301);
	auto Pixel = Stage(EArdaRHIShaderStage::Pixel, 302);
	Vertex.mBindingLayouts = {FArdaRHIBindingLayoutRef(
	    new FPipelineTestLayout(0, EArdaRHIBindingType::ConstantBuffer, EArdaRHIShaderStage::Vertex))};
	Pixel.mBindingLayouts = {FArdaRHIBindingLayoutRef(
	    new FPipelineTestLayout(0, EArdaRHIBindingType::TextureSRV, EArdaRHIShaderStage::Pixel))};
	eastl::vector<FArdaInductorPipelineNode> Nodes{{1, {}, {Vertex, Pixel}}};
	const auto First = InferArdaInductorPipeline(Nodes, Request(1));
	ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
	ASSERT_EQ(First.mValue.mConfiguration.mGraphics.mDesc.mBindingLayouts.size(), 2u);
	Nodes[0].mContributions = {Pixel, Vertex, Pixel};
	const auto Reordered = InferArdaInductorPipeline(Nodes, Request(1));
	ASSERT_TRUE(Reordered) << Reordered.mStatus.mMessage.c_str();
	EXPECT_EQ(Reordered.mValue.mStableKey, First.mValue.mStableKey);
	EXPECT_EQ(Reordered.mValue.mConfiguration.mGraphics.mDesc.mBindingLayouts.size(), 2u);
	Vertex.mBindingLayouts = {FArdaRHIBindingLayoutRef(
	    new FPipelineTestLayout(0, EArdaRHIBindingType::ConstantBuffer, EArdaRHIShaderStage::All))};
	Nodes[0].mContributions = {Vertex, Pixel};
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, Request(1)));
}

TEST(ArdaInductorPipeline, SupportsMultipleStagesPerNodeAndChecksTessellationPairs)
{
	eastl::vector<FArdaInductorPipelineNode> Mesh{{1,
	    {},
	    {Stage(EArdaRHIShaderStage::Amplification, 1),
	        Stage(EArdaRHIShaderStage::Mesh, 2),
	        Stage(EArdaRHIShaderStage::Pixel, 3)}}};
	const auto MeshResult = InferArdaInductorPipeline(Mesh, Request(1, EArdaPipelineStateKind::Meshlet));
	ASSERT_TRUE(MeshResult) << MeshResult.mStatus.mMessage.c_str();
	EXPECT_TRUE(MeshResult.mValue.mConfiguration.mMeshlet.mDesc.mAmplificationShader);
	EXPECT_TRUE(MeshResult.mValue.mConfiguration.mMeshlet.mDesc.mMeshShader);
	EXPECT_TRUE(MeshResult.mValue.mConfiguration.mMeshlet.mDesc.mPixelShader);
	eastl::vector<FArdaInductorPipelineNode> Tessellation{
	    {1, {}, {Stage(EArdaRHIShaderStage::Vertex, 1), Stage(EArdaRHIShaderStage::Hull, 2)}}};
	EXPECT_FALSE(InferArdaInductorPipeline(Tessellation, Request(1)));
	Tessellation[0].mContributions.push_back(Stage(EArdaRHIShaderStage::Domain, 3));
	EXPECT_FALSE(InferArdaInductorPipeline(Tessellation, Request(1)));
	auto Config = eastl::make_shared<FArdaInductorPipelineConfiguration>();
	Config->mGraphics.mDesc.mTopology = EArdaRHIPrimitiveTopology::PatchList;
	Config->mGraphics.mDesc.mPatchControlPoints = 3;
	auto TessRequest = Request(1);
	TessRequest.mConfiguration = Config;
	EXPECT_TRUE(InferArdaInductorPipeline(Tessellation, TessRequest));
}

TEST(ArdaInductorPipeline, InfersRayExportsAndHitGroupsInCanonicalOrder)
{
	auto RayGen = Stage(EArdaRHIShaderStage::RayGeneration, 100);
	RayGen.mExportName = "raygen";
	auto Miss = Stage(EArdaRHIShaderStage::Miss, 200);
	Miss.mExportName = "miss";
	auto Closest = Stage(EArdaRHIShaderStage::ClosestHit, 300);
	Closest.mHitGroupName = "surface";
	auto Any = Stage(EArdaRHIShaderStage::AnyHit, 400);
	Any.mHitGroupName = "surface";
	eastl::vector<FArdaInductorPipelineNode> Nodes{{1, {}, {RayGen, Closest}}, {2, {1}, {Miss, Any}}};
	const auto First = InferArdaInductorPipeline(Nodes, Request(2, EArdaPipelineStateKind::RayTracing));
	ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
	const auto& Desc = First.mValue.mConfiguration.mRayTracing.mDesc;
	ASSERT_EQ(Desc.mHitGroups.size(), 1u);
	EXPECT_TRUE(Desc.mHitGroups[0].mClosestHitShader);
	EXPECT_TRUE(Desc.mHitGroups[0].mAnyHitShader);
	ASSERT_EQ(Desc.mShaders.size(), 2u);
	EXPECT_EQ(Desc.mShaders[0].mExportName, "miss");
	Nodes[0].mContributions = {Any, Miss};
	Nodes[1].mContributions = {Closest, RayGen};
	const auto Reordered = InferArdaInductorPipeline(Nodes, Request(2, EArdaPipelineStateKind::RayTracing));
	ASSERT_TRUE(Reordered);
	EXPECT_EQ(Reordered.mValue.mStableKey, First.mValue.mStableKey);
	Nodes[1].mContributions[0].mHitGroupName.clear();
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, Request(2, EArdaPipelineStateKind::RayTracing)));
}

TEST(ArdaInductorPipeline, WorkGraphProgramSettingsParticipateInIdentity)
{
	eastl::vector<FArdaInductorPipelineNode> Nodes{
	    {1, {}, {Stage(EArdaRHIShaderStage::WorkGraph, 2), Stage(EArdaRHIShaderStage::WorkGraph, 1)}}};
	auto WorkRequest = Request(1, EArdaPipelineStateKind::WorkGraph);
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, WorkRequest));
	auto Config = eastl::make_shared<FArdaInductorPipelineConfiguration>();
	Config->mKind = EArdaPipelineStateKind::WorkGraph;
	Config->mWorkGraph.mDesc.mProgramName = "program";
	Config->mWorkGraph.mDesc.mEntryPoint = "entry";
	WorkRequest.mConfiguration = Config;
	const auto First = InferArdaInductorPipeline(Nodes, WorkRequest);
	ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
	EXPECT_EQ(First.mValue.mConfiguration.mWorkGraph.mDesc.mShaders.front()->GetPersistentCacheHash(), 1u);
	Config->mWorkGraph.mDesc.mMaxInputRecords = 2;
	const auto Changed = InferArdaInductorPipeline(Nodes, WorkRequest);
	ASSERT_TRUE(Changed);
	EXPECT_NE(Changed.mValue.mStableKey, First.mValue.mStableKey);
}

TEST(ArdaInductorPipeline, ResolvesThroughDeviceCacheAndRejectsModifiedPatterns)
{
	eastl::vector<FArdaInductorPipelineNode> Nodes{{1, {}, {Stage(EArdaRHIShaderStage::Compute, 1)}}};
	auto Pattern = InferArdaInductorPipeline(Nodes, Request(1, EArdaPipelineStateKind::Compute));
	ASSERT_TRUE(Pattern);
	FArdaPipelineStateCache Cache({});
	const auto MissingDevice = ResolveArdaInductorPipeline(Cache, Pattern.mValue);
	EXPECT_EQ(MissingDevice.mStatus.mCode, EArdaRHIResult::InvalidState);
	Pattern.mValue.mConfiguration.mCompute.mDesc.mComputeShader = Stage(EArdaRHIShaderStage::Compute, 2).mShader;
	const auto Changed = ResolveArdaInductorPipeline(Cache, Pattern.mValue);
	EXPECT_EQ(Changed.mStatus.mCode, EArdaRHIResult::InvalidArgument);
}

TEST(ArdaInductorPipeline, DoesNotDropInvalidConfiguredStages)
{
	eastl::vector<FArdaInductorPipelineNode> Nodes{{1, {}, {}}};
	auto RayConfig = eastl::make_shared<FArdaInductorPipelineConfiguration>();
	RayConfig->mKind = EArdaPipelineStateKind::RayTracing;
	RayConfig->mRayTracing.mDesc.mShaders.push_back(
	    {"raygen", Stage(EArdaRHIShaderStage::RayGeneration, 1).mShader, {}});
	FArdaRHIRayTracingHitGroupDesc EmptyHitGroup;
	EmptyHitGroup.mExportName = "empty";
	RayConfig->mRayTracing.mDesc.mHitGroups.push_back(EmptyHitGroup);
	auto RayRequest = Request(1, EArdaPipelineStateKind::RayTracing);
	RayRequest.mConfiguration = RayConfig;
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, RayRequest));
	RayConfig->mRayTracing.mDesc.mHitGroups.clear();
	EXPECT_TRUE(InferArdaInductorPipeline(Nodes, RayRequest));

	auto WorkConfig = eastl::make_shared<FArdaInductorPipelineConfiguration>();
	WorkConfig->mKind = EArdaPipelineStateKind::WorkGraph;
	WorkConfig->mWorkGraph.mDesc.mProgramName = "program";
	WorkConfig->mWorkGraph.mDesc.mEntryPoint = "entry";
	WorkConfig->mWorkGraph.mDesc.mShaders = {Stage(EArdaRHIShaderStage::WorkGraph, 2).mShader, {}};
	auto WorkRequest = Request(1, EArdaPipelineStateKind::WorkGraph);
	WorkRequest.mConfiguration = WorkConfig;
	EXPECT_FALSE(InferArdaInductorPipeline(Nodes, WorkRequest));
	WorkConfig->mWorkGraph.mDesc.mShaders.pop_back();
	EXPECT_TRUE(InferArdaInductorPipeline(Nodes, WorkRequest));
}
