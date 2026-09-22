#include "ArdaDependencyGraph.h"
#include "Graph/Node/ArdaRayTracingStageNode.h"
#include <EASTL/algorithm.h>
#include <gtest/gtest.h>

namespace arda::ray_stage_tests
{
	class FArdaRayTestShader final : public IArdaRHIShader
	{
	public:
		explicit FArdaRayTestShader(EArdaRHIShaderStage Stage)
		    : mStage(Stage)
		{
		}

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
			return EArdaRHIResourceType::Shader;
		}

		const char* GetDebugName() const noexcept override
		{
			return "Ray stage fixture";
		}

		EArdaRHIShaderStage GetStage() const noexcept override
		{
			return mStage;
		}

		uint64_t GetPersistentCacheHash() const noexcept override
		{
			return uint64_t(mStage) + 123;
		}

	private:
		EArdaRHIShaderStage mStage;
		uint32_t mReferences = 0;
	};

	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaRayWrongVisibility)
		ARDA_SHADER_BUFFER_SRV(mInput, 0, 0, EArdaRHIShaderStage::Compute)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	struct FArdaRayTestParameters
	{
		eastl::vector<FArdaGraphStageValue> mShaders;
		FArdaGraphBuffer mInput;
		bool mbWrongShader = false;
		bool mbWrongVisibility = false;
		uint32_t mIdentity = 0;
	};

	template <EArdaRHIShaderStage Stage>
	class TArdaRayTestNode final
	    : public TArdaRayTracingStageNode<TArdaRayTestNode<Stage>, FArdaRayTestParameters, Stage>
	{
	public:
		using FArdaParameters = FArdaRayTestParameters;
		using FArdaState = FArdaEmptyDependencyNodeState;
		using FArdaInstanceState = FArdaEmptyDependencyNodeState;

		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {Stage == EArdaRHIShaderStage::RayGeneration  ? "ray.stage.test.generation"
			        : Stage == EArdaRHIShaderStage::Miss         ? "ray.stage.test.miss"
			        : Stage == EArdaRHIShaderStage::ClosestHit   ? "ray.stage.test.closest"
			        : Stage == EArdaRHIShaderStage::AnyHit       ? "ray.stage.test.any"
			        : Stage == EArdaRHIShaderStage::Intersection ? "ray.stage.test.intersection"
			                                                     : "ray.stage.test.callable",
			    1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& P)
		{
			return FArdaDependencyKeyBuilder()
			    .Resource(P.mInput)
			    .Value(P.mbWrongShader)
			    .Value(P.mbWrongVisibility)
			    .Value(P.mIdentity)
			    .Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& P, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mbSideEffect = Stage == EArdaRHIShaderStage::RayGeneration;
			FArdaInductorPipelineContribution Contribution;
			Contribution.mShader =
			    FArdaRHIShaderRef(new FArdaRayTestShader(P.mbWrongShader ? EArdaRHIShaderStage::Compute : Stage));
			if constexpr (Stage == EArdaRHIShaderStage::ClosestHit || Stage == EArdaRHIShaderStage::AnyHit ||
			    Stage == EArdaRHIShaderStage::Intersection)
			{
				Contribution.mHitGroupName = "surface";
			}
			else
			{
				Contribution.mExportName = Stage == EArdaRHIShaderStage::RayGeneration ? "raygen"
				    : Stage == EArdaRHIShaderStage::Miss                               ? "miss"
				                                                                       : "callable";
			}
			Desc.mPipelineStages.push_back(eastl::move(Contribution));
			if (P.mInput)
			{
				Desc.mAccesses.push_back(
				    {P.mInput, EArdaDependencyAccess::Read, EArdaRHIResourceState::ShaderResource});
			}
			if (P.mbWrongVisibility)
			{
				Desc.BindShader<FArdaRayWrongVisibility>({{"mInput", P.mInput}});
			}
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

	using FArdaRayGenerationTestNode = TArdaRayTestNode<EArdaRHIShaderStage::RayGeneration>;
	using FArdaRayMissTestNode = TArdaRayTestNode<EArdaRHIShaderStage::Miss>;
	using FArdaRayClosestTestNode = TArdaRayTestNode<EArdaRHIShaderStage::ClosestHit>;
	using FArdaRayAnyTestNode = TArdaRayTestNode<EArdaRHIShaderStage::AnyHit>;
	using FArdaRayIntersectionTestNode = TArdaRayTestNode<EArdaRHIShaderStage::Intersection>;
	using FArdaRayCallableTestNode = TArdaRayTestNode<EArdaRHIShaderStage::Callable>;
	ARDA_DECLARE_GRAPH_NODE(FArdaRayGenerationTestNode, TestRayGenerate);
	ARDA_DEFINE_GRAPH_NODE(FArdaRayGenerationTestNode, TestRayGenerate);
	ARDA_DECLARE_GRAPH_NODE(FArdaRayMissTestNode, TestRayMiss);
	ARDA_DEFINE_GRAPH_NODE(FArdaRayMissTestNode, TestRayMiss);
	ARDA_DECLARE_GRAPH_NODE(FArdaRayClosestTestNode, TestRayClosest);
	ARDA_DEFINE_GRAPH_NODE(FArdaRayClosestTestNode, TestRayClosest);
	ARDA_DECLARE_GRAPH_NODE(FArdaRayAnyTestNode, TestRayAny);
	ARDA_DEFINE_GRAPH_NODE(FArdaRayAnyTestNode, TestRayAny);
	ARDA_DECLARE_GRAPH_NODE(FArdaRayIntersectionTestNode, TestRayIntersection);
	ARDA_DEFINE_GRAPH_NODE(FArdaRayIntersectionTestNode, TestRayIntersection);
	ARDA_DECLARE_GRAPH_NODE(FArdaRayCallableTestNode, TestRayCallable);
	ARDA_DEFINE_GRAPH_NODE(FArdaRayCallableTestNode, TestRayCallable);

	TEST(ArdaRayStages, SharedProvidersAssembleIndependentRayGenerationPipelines)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaRHIBufferDesc Buffer;
		Buffer.mByteSize = 16;
		Buffer.mStructureStride = 4;
		Buffer.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess |
		    EArdaRHIBufferUsage::ShaderResource;
		const auto Input = AllocateBuffer(Edit, Buffer);
		Clear(Edit, Input, uint32_t{0});
		const auto Miss = TestRayMiss(Edit, {{}, Input});
		const auto Closest = TestRayClosest(Edit, FArdaRayTestParameters{});
		const auto Any = TestRayAny(Edit, FArdaRayTestParameters{});
		const auto Intersection = TestRayIntersection(Edit, FArdaRayTestParameters{});
		const auto Callable = TestRayCallable(Edit, FArdaRayTestParameters{});
		const auto A = TestRayGenerate(Edit, {{Miss, Closest, Any, Intersection, Callable}});
		const auto B = TestRayGenerate(Edit, {{Miss, Callable}});
		ASSERT_TRUE(A && B);
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		const auto& First = Graph.GetTopology().TryGetNode(A.GetHandle())->mPayload;
		const auto& Second = Graph.GetTopology().TryGetNode(B.GetHandle())->mPayload;
		EXPECT_TRUE(First.mbRayTracingLowered);
		EXPECT_EQ(First.mDesc.mPipelineStages.size(), 6u);
		EXPECT_EQ(Second.mDesc.mPipelineStages.size(), 3u);
		ASSERT_EQ(First.mDesc.mPipelines.size(), 1u);
		EXPECT_EQ(First.mDesc.mPipelines.front().mKind, EArdaPipelineStateKind::RayTracing);
		EXPECT_EQ(First.mDesc.mAccesses.size(), 1u);
		EXPECT_EQ(Second.mDesc.mAccesses.size(), 1u);
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Miss.GetProducer(), A.GetHandle()));
		EXPECT_TRUE(Graph.GetTopology().IsReachable(Miss.GetProducer(), B.GetHandle()));
		EXPECT_TRUE(Graph.GetTopology().TryGetNode(Miss.GetProducer())->mPayload.mDesc.mbPipelineStageOnly);
		EXPECT_TRUE(Graph.GetTopology().TryGetNode(Miss.GetProducer())->mPayload.mDesc.mAccesses.empty());
	}

	TEST(ArdaRayStages, RejectsWrongShaderEvenOnUnconsumedProvider)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(TestRayMiss(Edit, {{}, {}, true}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("different pipeline stage"), eastl::string::npos);
	}

	TEST(ArdaRayStages, RejectsWrongInputKind)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		const auto Miss = TestRayMiss(Edit, FArdaRayTestParameters{});
		ASSERT_TRUE(
		    TestRayGenerate(Edit, {{FArdaGraphStageValue(Miss.GetProducer(), EArdaGraphStageValueKind::Fragments)}}));
		ASSERT_TRUE(Edit.Commit());
		EXPECT_FALSE(Graph.Compile());
	}

	TEST(ArdaRayStages, RejectsWrongProviderBindingVisibility)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		ASSERT_TRUE(TestRayMiss(Edit, {{}, {}, false, true}));
		ASSERT_TRUE(Edit.Commit());
		const auto Status = Graph.Compile();
		EXPECT_FALSE(Status);
		EXPECT_NE(Status.mMessage.find("visibility"), eastl::string::npos);
	}

	TEST(ArdaRayStages, ProviderSelectionParticipatesInConstructionCacheIdentity)
	{
		FArdaDependencyGraph Graph;
		const auto Construct = [&](bool Other)
		{
			FArdaGraphEdit Edit(Graph);
			const auto A = TestRayMiss(Edit, {{}, {}, false, false, 1});
			const auto B = TestRayMiss(Edit, {{}, {}, false, false, 2});
			EXPECT_TRUE(TestRayGenerate(Edit, {{Other ? B : A}}));
			EXPECT_TRUE(Edit.Commit());
			const auto Status = Graph.Compile();
			EXPECT_TRUE(Status) << Status.mMessage.c_str();
			(void)Graph.Execute();
		};
		Construct(false);
		const auto First = Graph.GetRevisionStats();
		Construct(false);
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, First.mCompileCount);
		EXPECT_EQ(Graph.GetRevisionStats().mCacheHitCount, First.mCacheHitCount + 1);
		Construct(true);
		EXPECT_EQ(Graph.GetRevisionStats().mCompileCount, First.mCompileCount + 1);
	}

	TEST(ArdaRayStages, MissingAssemblyRuleIsRejected)
	{
		FArdaDependencyGraph Graph;
		FArdaGraphEdit Edit(Graph);
		FArdaInductorOptions Options;
		auto& Rules = Options.mRules.mPreprocessRules;
		Rules.erase(eastl::remove_if(Rules.begin(),
		                Rules.end(),
		                [](const auto& Rule)
		                {
			                return eastl::string(Rule->GetName()) == "FArdaAssembleRayTracingStagesRule";
		                }),
		    Rules.end());
		ASSERT_TRUE(Edit.SetOptions(Options));
		ASSERT_TRUE(TestRayMiss(Edit, FArdaRayTestParameters{}));
		ASSERT_TRUE(Edit.Commit());
		EXPECT_FALSE(Graph.Compile());
	}
}
