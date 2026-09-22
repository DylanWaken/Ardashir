#include "ArdaBackend.h"
#include "ArdaMath.h"
#include "ArdaTestBackend.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "RHI/Shaders/ArdaGlobalShaderMap.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace
{
	struct alignas(16) FArdaMatrixShaderConstants
	{
		arda::FArdaShaderMatrix4f mModelToClip;
		arda::FArdaShaderMatrix3f mNormalToWorld;
		arda::FArdaShaderCameraMatrices mCamera;
		float mPosition[4];
		float mNormal[4];
		float mViewDepths[4];
	};

	static_assert(offsetof(FArdaMatrixShaderConstants, mNormalToWorld) == 64);
	static_assert(offsetof(FArdaMatrixShaderConstants, mCamera) == 112);
	static_assert(offsetof(FArdaMatrixShaderConstants, mPosition) == 240);
	static_assert(sizeof(FArdaMatrixShaderConstants) == 288);

	class FArdaMatrixDiagnosticCounter final : public arda::IArdaDiagnosticCallback
	{
	public:
		void Message(arda::EArdaDiagnosticSeverity Severity, const char* Text) override
		{
			if (Severity == arda::EArdaDiagnosticSeverity::Error || Severity == arda::EArdaDiagnosticSeverity::Fatal)
			{
				++mErrorCount;
				if (Text)
				{
					std::fprintf(stderr, "Arda matrix validation: %s\n", Text);
				}
			}
		}

		std::atomic<uint32_t> mErrorCount{0};
	};

	struct FArdaMatrixBackendCleanup
	{
		~FArdaMatrixBackendCleanup()
		{
			arda::ShutdownBackend();
			(void)arda::ConfigureBackend(arda::MakeArdaTestBackendConfiguration());
		}
	};

	void VerifyShaderMatrixPackingAndReversedDeviceZ(const char* BackendName)
	{
		using namespace arda;
		ShutdownBackend();
		FArdaMatrixDiagnosticCounter Diagnostics;
		FArdaMatrixBackendCleanup Cleanup;
		auto Configuration = MakeArdaTestBackendConfiguration();
		Configuration.mBackendName = BackendName;
		Configuration.mMessageCallback = &Diagnostics;
		Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
		ASSERT_TRUE(ConfigureBackend(Configuration));
		ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
		auto Device = GetDevice();
		ASSERT_TRUE(Device);

		const auto Artifact = std::filesystem::path(ARDA_BACKEND_TEST_SHADER_DIR) /
		    (eastl::string("ArdaMatrixInterop") + GetShaderArtifactExtension(BackendName)).c_str();
		const auto Bytecode = LoadShaderBytecode(Artifact);
		ASSERT_TRUE(Bytecode) << Bytecode.mDiagnostic.mMessage.c_str();
		FArdaRHIShaderDesc ShaderDesc;
		ShaderDesc.mStage = EArdaRHIShaderStage::Compute;
		ShaderDesc.mBytecode = Bytecode.mBytecode.data();
		ShaderDesc.mBytecodeSize = Bytecode.mBytecode.size();
		ShaderDesc.mEntryPoint = "MatrixInteropCS";
		auto Shader = Device->CreateShader(ShaderDesc);
		ASSERT_TRUE(Shader) << Shader.mStatus.mMessage.c_str();

		// Nonuniform scale, shear, translation, and an off-axis camera make accidental
		// transposes and tightly packed float3x3 uploads produce different answers.
		FArdaModelMatrix Model;
		Model << 2.f, 0.3f, -0.2f, 1.5f, -0.1f, 0.75f, 0.4f, -2.f, 0.2f, -0.35f, 1.5f, 4.f, 0.f, 0.f, 0.f, 1.f;
		FArdaCameraMatrices Camera;
		Camera.mWorldToView = MakeArdaViewMatrix({3.f, -2.f, -4.f}, {-0.2f, 0.1f, 1.f}, {0.f, 1.f, 0.f});
		constexpr float Near = 0.2f;
		constexpr float Far = 100.f;
		Camera.mViewToClip = MakeArdaPerspectiveProjection(0.9f, 1.7f, Near, Far);
		const FArdaMatrix4f ModelToClip = Camera.GetWorldToClip() * Model;
		const FArdaNormalMatrix NormalToWorld = MakeArdaNormalMatrix(Model);
		const FArdaVector4f Position{2.f, -1.f, 9.f, 1.f};
		const FArdaVector3f Normal{0.3f, 0.7f, -0.2f};
		FArdaMatrixShaderConstants Data{};
		Data.mModelToClip = PackArdaShaderMatrix(ModelToClip);
		Data.mNormalToWorld = PackArdaShaderMatrix(NormalToWorld);
		Data.mCamera = PackArdaShaderCameraMatrices(Camera);
		std::memcpy(Data.mPosition, Position.data(), sizeof(Data.mPosition));
		std::memcpy(Data.mNormal, Normal.data(), sizeof(float) * 3);
		Data.mViewDepths[0] = Near;
		Data.mViewDepths[1] = Far;
		Data.mViewDepths[2] = 5.f;
		Data.mViewDepths[3] = 29.f;

		FArdaRHIBufferDesc ConstantsDesc;
		ConstantsDesc.mByteSize = 512; // Includes D3D12's 256-byte CBV size alignment.
		ConstantsDesc.mUsage = EArdaRHIBufferUsage::Constant;
		auto Constants = Device->CreateBuffer(ConstantsDesc);
		ASSERT_TRUE(Constants) << Constants.mStatus.mMessage.c_str();
		FArdaRHIBufferDesc OutputDesc;
		OutputDesc.mByteSize = sizeof(float) * 16;
		OutputDesc.mStructureStride = sizeof(float) * 4;
		OutputDesc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
		auto Output = Device->CreateBuffer(OutputDesc);
		ASSERT_TRUE(Output) << Output.mStatus.mMessage.c_str();
		FArdaRHIBindingLayoutDesc LayoutDesc;
		LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
		LayoutDesc.mItems = {{0, 1, EArdaRHIBindingType::ConstantBuffer},
		    {0, 1, EArdaRHIBindingType::StructuredBufferUAV}};
		auto Layout = Device->CreateBindingLayout(LayoutDesc);
		ASSERT_TRUE(Layout) << Layout.mStatus.mMessage.c_str();
		FArdaRHIBindingSetDesc SetDesc;
		SetDesc.mLayout = Layout.mValue;
		SetDesc.mItems = {{0, 0, EArdaRHIBindingType::ConstantBuffer, FArdaRHIResourceRef(Constants.mValue.Get()), {}},
		    {0, 0, EArdaRHIBindingType::StructuredBufferUAV, FArdaRHIResourceRef(Output.mValue.Get()), {}}};
		auto Set = Device->CreateBindingSet(SetDesc);
		ASSERT_TRUE(Set) << Set.mStatus.mMessage.c_str();
		FArdaRHIComputePipelineDesc PipelineDesc;
		PipelineDesc.mComputeShader = Shader.mValue;
		PipelineDesc.mBindingLayouts = {Layout.mValue};
		auto Pipeline = Device->CreateComputePipeline(PipelineDesc);
		ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();
		auto Commands = Device->CreateCommandList(EArdaRHIQueueType::Compute);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		ASSERT_TRUE(Commands.mValue->WriteBuffer(*Constants.mValue, &Data, sizeof(Data)));
		ASSERT_TRUE(Commands.mValue->SetBufferState(*Constants.mValue, EArdaRHIResourceState::ConstantBuffer));
		ASSERT_TRUE(Commands.mValue->SetBufferState(*Output.mValue, EArdaRHIResourceState::UnorderedAccess));
		FArdaRHIComputeState State;
		State.mPipeline = Pipeline.mValue;
		State.mBindings = {Set.mValue};
		ASSERT_TRUE(Commands.mValue->SetComputeState(State));
		Commands.mValue->Dispatch(1, 1, 1);
		eastl::vector<uint8_t> Readback;
		ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Output.mValue, Readback, 0, OutputDesc.mByteSize));
		ASSERT_TRUE(Commands.mValue->Close());
		ASSERT_TRUE(Device->ExecuteCommandList(Commands.mValue));
		ASSERT_TRUE(Device->WaitForIdle());
		ASSERT_EQ(Readback.size(), OutputDesc.mByteSize);
		float Actual[4][4];
		std::memcpy(Actual, Readback.data(), sizeof(Actual));
		const FArdaVector4f ExpectedClip = ModelToClip * Position;
		const FArdaVector4f ExpectedCamera = Camera.GetWorldToClip() * Position;
		const FArdaVector3f ExpectedNormal = (NormalToWorld * Normal).normalized();
		for (int Index = 0; Index < 4; ++Index)
		{
			EXPECT_NEAR(Actual[0][Index], ExpectedClip[Index], 2e-5f);
			EXPECT_NEAR(Actual[2][Index], ExpectedCamera[Index], 2e-5f);
		}
		for (int Index = 0; Index < 3; ++Index)
		{
			EXPECT_NEAR(Actual[1][Index], ExpectedNormal[Index], 2e-6f);
			const FArdaVector4f Clip = Camera.mViewToClip * FArdaVector4f{0, 0, Data.mViewDepths[Index], 1};
			EXPECT_NEAR(Actual[3][Index], Clip.z() / Clip.w(), 1e-6f);
		}
		EXPECT_FLOAT_EQ(Actual[1][3], 0.f);
		EXPECT_NEAR(Actual[3][0], ArdaDeviceZNear, 1e-6f);
		EXPECT_NEAR(Actual[3][1], ArdaDeviceZFar, 1e-6f);
		EXPECT_GT(Actual[3][0], Actual[3][2]);
		EXPECT_GT(Actual[3][2], Actual[3][1]);
		EXPECT_FLOAT_EQ(Actual[3][3], 29.f);
		EXPECT_EQ(Diagnostics.mErrorCount.load(), 0u);
	}
}

#if defined(ARDA_TEST_NATIVE_D3D12)
TEST(ArdaMatrixGPU, D3D12PackedMatricesAndReversedDeviceZ)
{
	VerifyShaderMatrixPackingAndReversedDeviceZ("native-d3d12");
}
#endif

#if defined(ARDA_TEST_NATIVE_VULKAN)
TEST(ArdaMatrixGPU, VulkanPackedMatricesAndReversedDeviceZ)
{
	VerifyShaderMatrixPackingAndReversedDeviceZ("native-vulkan");
}
#endif
