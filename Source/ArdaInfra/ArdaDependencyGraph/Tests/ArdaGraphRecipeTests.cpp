#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include "Graph/Node/ArdaDependencyNode.h"
#include "Graph/Node/ArdaGraphEdit.h"
#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "RHI/Shaders/ArdaShaderCompiler.h"
#include "../../../../Docs/ArdaRDG/Examples/ArdaInductorRulePolicy.h"
#include <gtest/gtest.h>

namespace arda
{
	FArdaRHIStatus RunMeshRecipe(FArdaRHIDeviceRef Device);
	FArdaRHIStatus RunRasterRecipe(FArdaRHIDeviceRef Device);
	FArdaRHIStatus RunWorkRecipe(FArdaRHIDeviceRef Device);
	FArdaRHIStatus RunComputeRecipe(FArdaRHIDeviceRef Device);
}

namespace arda::graph_recipe_tests
{
	using namespace arda;

	struct FArdaRecipeCostParameters
	{
		uint32_t mCost = 1;
	};

	struct FArdaRecipeCostNode : TArdaGraphicsDependencyNode<FArdaRecipeCostNode, FArdaRecipeCostParameters>
	{
		static FArdaDependencyNodeMetadata GetMetadata()
		{
			return {"test.recipe.cost", 1};
		}

		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters)
		{
			return FArdaDependencyKeyBuilder{}.Value(Parameters.mCost).Build();
		}

		static FArdaDependencyNodeDesc Describe(const FArdaParameters& Parameters, const FArdaState&)
		{
			FArdaDependencyNodeDesc Desc;
			Desc.mEstimatedCost = Parameters.mCost;
			Desc.mbSideEffect = true;
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

	ARDA_DECLARE_GRAPH_NODE(FArdaRecipeCostNode, RecipeCost);
	ARDA_DEFINE_GRAPH_NODE(FArdaRecipeCostNode, RecipeCost);

	TEST(ArdaGraphRuleRecipe, PreprocessChangesOnlyCostsBelowItsConfiguredMinimum)
	{
		FArdaDependencyGraph Graph;
		const auto Record = [&](uint32_t Minimum)
		{
			FArdaGraphEdit Edit(Graph);
			FArdaInductorOptions Options;
			Options.mRules.mPreprocessRules.push_back(MakeArdaGraphRule<examples::FMinimumNodeCostRule>(Minimum));
			Edit.SetOptions(Options);
			RecipeCost(Edit, FArdaRecipeCostParameters{2}, "low");
			RecipeCost(Edit, FArdaRecipeCostParameters{12}, "high");
			return Edit.Commit();
		};
		ASSERT_TRUE(Record(5));
		ASSERT_TRUE(Graph.Compile());
		const auto& Topology = Graph.GetTopology();
		EXPECT_EQ(Topology.TryGetNode(Graph.FindNode("low"))->mPayload.mDesc.mEstimatedCost, 5u);
		EXPECT_EQ(Topology.TryGetNode(Graph.FindNode("high"))->mPayload.mDesc.mEstimatedCost, 12u);
		// A device-less execution attempt releases the revision for the invalid next construction.
		EXPECT_FALSE(Graph.Execute().mStatus);
		ASSERT_TRUE(Record(0));
		EXPECT_FALSE(Graph.Compile());
		EXPECT_TRUE(Graph.DiscardPendingEdits());
	}

	TEST(ArdaGraphRuleRecipe, OrdersReadyNodesByCostWithOpaqueIds)
	{
		eastl::vector<FArdaInductorOrderingNode> Nodes = {{47, "producer", 2.0, {}},
		    {12, "independent", 4.0, {}},
		    {89, "consumer", 10.0, {47}}};
		auto Options = examples::MakeCostFirstOptions();

		FArdaDependencyGraph::FArdaImpl State;
		const auto View = FArdaInductorGraphAccess::View(State);
		auto Order = examples::FCostFirstReadyPolicy{}.Order(View, Nodes);
		ASSERT_TRUE(Order) << Order.mStatus.mMessage.c_str();
		EXPECT_EQ(Order.mValue, (eastl::vector<uint32_t>{12, 47, 89}));
	}

	class FArdaGraphRecipes : public testing::TestWithParam<const char*>
	{
	protected:
		FArdaTestDiagnosticCallback mDiagnostics;
		FArdaShaderCompilerConfiguration mPreviousCompilerConfiguration;

		void SetUp() override
		{
			ShutdownBackend();

			// Recipe fixtures register shaders at runtime and deliberately exercise on-demand compilation in every build.
			mPreviousCompilerConfiguration = GetShaderCompilerConfiguration();
			auto CompilerConfiguration = mPreviousCompilerConfiguration;
			CompilerConfiguration.mbCompileMissingArtifacts = true;
			CompilerConfiguration.mbCompileOutdatedArtifacts = true;
			ConfigureShaderCompiler(CompilerConfiguration);

			auto C = MakeArdaTestBackendConfiguration();
			C.mBackendName = GetParam();
			C.mMessageCallback = &mDiagnostics;
			C.mShaderCompilationMode = EArdaShaderCompilationMode::OnDemand;
			C.mShaderCacheDirectory = ARDA_BACKEND_TEST_SHADER_DIR;
			ASSERT_TRUE(ConfigureBackend(C));
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
		}

		void TearDown() override
		{
			const bool MissingValidationSkip = testing::Test::IsSkipped() && ArdaTestValidationEnabled &&
			    GetBackendInitializeResult() == EArdaInitializeResult::ValidationUnavailable;
			if (auto Device = GetDevice())
			{
				EXPECT_TRUE(Device->WaitForIdle());
			}
			ShutdownBackend();
			ConfigureShaderCompiler(mPreviousCompilerConfiguration);

			// Feature skips after initialization still fail on native errors, including during shutdown.
			if (!MissingValidationSkip)
			{
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
		}
	};

	TEST_P(FArdaGraphRecipes, Compute)
	{
		const auto S = RunComputeRecipe(GetDevice());
		ASSERT_TRUE(S) << S.mMessage.c_str();
	}

	TEST_P(FArdaGraphRecipes, Raster)
	{
		const auto S = RunRasterRecipe(GetDevice());
		ASSERT_TRUE(S) << S.mMessage.c_str();
	}

	TEST_P(FArdaGraphRecipes, Mesh)
	{
		if (GetDevice()->GetCapabilities().mMeshShaderTier == EArdaRHIMeshShaderTier::None)
		{
			GTEST_SKIP() << "Mesh shaders unavailable";
		}
		const auto S = RunMeshRecipe(GetDevice());
		ASSERT_TRUE(S) << S.mMessage.c_str();
	}

	TEST_P(FArdaGraphRecipes, WorkGraph)
	{
		if (auto S = GetDevice()->QueryWorkGraphSupport(); !S)
		{
			GTEST_SKIP() << S.mMessage.c_str();
		}
		const auto S = RunWorkRecipe(GetDevice());
		ASSERT_TRUE(S) << S.mMessage.c_str();
	}

#if defined(ARDA_TEST_NATIVE_D3D12) || defined(ARDA_TEST_NATIVE_VULKAN)
	const char* const NativeBackends[] = {
#if defined(ARDA_TEST_NATIVE_D3D12)
	    "native-d3d12",
#endif
#if defined(ARDA_TEST_NATIVE_VULKAN)
	    "native-vulkan",
#endif
	};
	INSTANTIATE_TEST_SUITE_P(Native, FArdaGraphRecipes, testing::ValuesIn(NativeBackends));
#else
	GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(FArdaGraphRecipes);
#endif
}
