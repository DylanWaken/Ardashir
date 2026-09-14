#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ShaderStructs/ArdaShaderCompiler.h"
#include <gtest/gtest.h>

namespace arda
{
	FArdaRHIStatus RunMeshRecipe(FArdaRHIDeviceRef Device);
	FArdaRHIStatus RunRasterRecipe(FArdaRHIDeviceRef Device);
	FArdaRHIStatus RunWorkRecipe(FArdaRHIDeviceRef Device);
	FArdaRHIStatus RunComputeRecipe(FArdaRHIDeviceRef Device);
}

namespace
{
	using namespace arda;

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

	INSTANTIATE_TEST_SUITE_P(Native, FArdaGraphRecipes, testing::Values("native-d3d12", "native-vulkan"));
}
