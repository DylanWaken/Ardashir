#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "RHI/Shaders/ArdaGlobalShaderMap.h"

#include <gtest/gtest.h>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace
{
	using namespace arda;

	class FArdaCommandValidationTest : public testing::TestWithParam<const char*>, public IArdaDiagnosticCallback
	{
	protected:
		void Message(EArdaDiagnosticSeverity Severity, const char* Text) override
		{
			if (Severity >= EArdaDiagnosticSeverity::Error)
			{
				++mErrors;
				std::fprintf(stderr, "%s\n", Text ? Text : "");
			}
		}

		void SetUp() override
		{
			ShutdownBackend();
			auto Configuration = MakeArdaTestBackendConfiguration();
			Configuration.mBackendName = GetParam();
			Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
			Configuration.mMessageCallback = this;
			ASSERT_TRUE(ConfigureBackend(Configuration));
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
			mDevice = GetDevice();
			ASSERT_TRUE(mDevice);
		}

		void TearDown() override
		{
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
			}
			mDevice = {};
			ShutdownBackend();
			static_cast<void>(ConfigureBackend(MakeArdaTestBackendConfiguration()));
			if (!testing::Test::IsSkipped())
			{
				EXPECT_EQ(mErrors.load(), 0u);
			}
		}

		FArdaRHIShaderRef Shader(const char* Artifact, const char* Entry, EArdaRHIShaderStage Stage)
		{
			const std::string Path =
			    std::string(ARDA_BACKEND_TEST_SHADER_DIR "/") + Artifact + GetShaderArtifactExtension(GetParam());
			std::ifstream Stream(Path, std::ios::binary);
			EXPECT_TRUE(Stream) << Path;
			const std::vector<char> Bytes((std::istreambuf_iterator<char>(Stream)), {});
			FArdaRHIShaderDesc Desc;
			Desc.mStage = Stage;
			Desc.mEntryPoint = Entry;
			Desc.mBytecode = Bytes.data();
			Desc.mBytecodeSize = Bytes.size();
			auto Result = mDevice->CreateShader(Desc);
			EXPECT_TRUE(Result) << Result.mStatus.mMessage.c_str();
			return Result.mValue;
		}

		FArdaRHIGraphicsState GraphicsState(bool bVertexInput = false)
		{
			FArdaRHIGraphicsPipelineDesc Pipeline;
			Pipeline.mVertexShader = Shader("ArdaRasterStageVS", "RasterStageVS", EArdaRHIShaderStage::Vertex);
			Pipeline.mPixelShader = Shader("ArdaRasterStagePS", "RasterStagePS", EArdaRHIShaderStage::Pixel);
			Pipeline.mColorFormats = {EArdaRHIFormat::RGBA8UNorm};
			Pipeline.mDepthStencilState.mbDepthTest = false;
			Pipeline.mDepthStencilState.mbDepthWrite = false;
			Pipeline.mRasterState.mCullMode = EArdaRHICullMode::None;
			if (bVertexInput)
			{
				FArdaRHIVertexAttributeDesc Attribute;
				Attribute.mSemanticName = "POSITION";
				Attribute.mFormat = EArdaRHIFormat::RGB32Float;
				Attribute.mElementStride = 12;
				Pipeline.mInputLayout = mDevice->CreateInputLayout({Attribute}).mValue;
				EXPECT_TRUE(Pipeline.mInputLayout);
			}
			auto CreatedPipeline = mDevice->CreateGraphicsPipeline(Pipeline);
			EXPECT_TRUE(CreatedPipeline) << CreatedPipeline.mStatus.mMessage.c_str();
			FArdaRHITextureDesc Target;
			Target.mWidth = Target.mHeight = 4;
			Target.mFormat = EArdaRHIFormat::RGBA8UNorm;
			Target.mUsage = EArdaRHITextureUsage::RenderTarget;
			Target.mInitialState = EArdaRHIResourceState::RenderTarget;
			auto Texture = mDevice->CreateTexture(Target);
			EXPECT_TRUE(Texture);
			FArdaRHIFramebufferDesc Framebuffer;
			Framebuffer.mColorAttachments = {{Texture.mValue, {}}};
			auto CreatedFramebuffer = mDevice->CreateFramebuffer(Framebuffer);
			EXPECT_TRUE(CreatedFramebuffer);
			FArdaRHIGraphicsState State;
			State.mPipeline = CreatedPipeline.mValue;
			State.mFramebuffer = CreatedFramebuffer.mValue;
			State.mViewports = {{0, 4, 0, 4, 0, 1}};
			State.mScissors = {{0, 4, 0, 4}};
			return State;
		}

		FArdaRHIComputeState ComputeState()
		{
			FArdaRHIBindingLayoutDesc Layout;
			Layout.mVisibility = EArdaRHIShaderStage::Compute;
			Layout.mItems = {{0, 1, EArdaRHIBindingType::StructuredBufferUAV}};
			auto CreatedLayout = mDevice->CreateBindingLayout(Layout);
			EXPECT_TRUE(CreatedLayout);
			FArdaRHIComputePipelineDesc Pipeline;
			Pipeline.mComputeShader =
			    Shader("ArdaShaderStructTest", "ShaderStructTestCS", EArdaRHIShaderStage::Compute);
			Pipeline.mBindingLayouts = {CreatedLayout.mValue};
			auto CreatedPipeline = mDevice->CreateComputePipeline(Pipeline);
			EXPECT_TRUE(CreatedPipeline) << CreatedPipeline.mStatus.mMessage.c_str();
			FArdaRHIBufferDesc Buffer;
			Buffer.mByteSize = 16;
			Buffer.mStructureStride = 4;
			Buffer.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
			auto CreatedBuffer = mDevice->CreateBuffer(Buffer);
			EXPECT_TRUE(CreatedBuffer);
			FArdaRHIBindingSetDesc Set;
			Set.mLayout = CreatedLayout.mValue;
			FArdaRHIBindingItem Item;
			Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
			Item.mResource = FArdaRHIResourceRef(CreatedBuffer.mValue.Get());
			Set.mItems = {Item};
			auto CreatedSet = mDevice->CreateBindingSet(Set);
			EXPECT_TRUE(CreatedSet);
			return {CreatedPipeline.mValue, {CreatedSet.mValue}};
		}

		FArdaRHIDeviceRef mDevice;
		std::atomic<uint32_t> mErrors{0};
	};

	TEST_P(FArdaCommandValidationTest, InvalidVoidCallsLatchUntilResetAndNeverSubmit)
	{
		for (const auto Queue : {EArdaRHIQueueType::Graphics, EArdaRHIQueueType::Compute, EArdaRHIQueueType::Copy})
		{
			if (!mDevice->GetCapabilities().IsQueueSupported(Queue))
			{
				continue;
			}
			for (uint32_t Operation = 0; Operation < 3; ++Operation)
			{
				auto Commands = mDevice->CreateCommandList(Queue);
				ASSERT_TRUE(Commands);
				ASSERT_TRUE(Commands.mValue->Open());
				if (Operation == 0)
				{
					Commands.mValue->Draw({3});
				}
				if (Operation == 1)
				{
					Commands.mValue->DrawIndexed({3});
				}
				if (Operation == 2)
				{
					Commands.mValue->Dispatch(1, 1, 1);
				}
				EXPECT_EQ(Commands.mValue->Close().mCode, EArdaRHIResult::InvalidState);
				EXPECT_EQ(mDevice->ExecuteCommandList(Commands.mValue).mStatus.mCode, EArdaRHIResult::InvalidState);
				ASSERT_TRUE(Commands.mValue->Reset());
				ASSERT_TRUE(Commands.mValue->Close());
				auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
				ASSERT_TRUE(Submitted);
				ASSERT_TRUE(mDevice->WaitForSubmission(Submitted.mValue));
			}
		}
	}

	TEST_P(FArdaCommandValidationTest, DispatchLimitsZeroWorkAndClosedRecording)
	{
		auto State = ComputeState();
		ASSERT_TRUE(State.mPipeline);
		auto Commands = mDevice->CreateCommandList();
		ASSERT_TRUE(Commands);
		EXPECT_EQ(Commands.mValue->SetComputeState(State).mCode, EArdaRHIResult::InvalidState);
		ASSERT_TRUE(Commands.mValue->Open());
		ASSERT_TRUE(Commands.mValue->SetComputeState(State));
		Commands.mValue->Dispatch(0, 1, 1);
		ASSERT_TRUE(Commands.mValue->Close());
		auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
		ASSERT_TRUE(Submitted);
		ASSERT_TRUE(mDevice->WaitForSubmission(Submitted.mValue));
		Commands.mValue->Dispatch(1, 1, 1);
		EXPECT_EQ(mDevice->ExecuteCommandList(Commands.mValue).mStatus.mCode, EArdaRHIResult::InvalidState);
		for (uint32_t Axis = 0; Axis < 3; ++Axis)
		{
			const uint32_t Limit = mDevice->GetCapabilities().mLimits.mMaxComputeWorkGroupCount[Axis];
			ASSERT_GT(Limit, 0u);
			ASSERT_LT(Limit, UINT32_MAX);
			ASSERT_TRUE(Commands.mValue->Reset());
			ASSERT_TRUE(Commands.mValue->SetComputeState(State));
			uint32_t Groups[] = {1, 1, 1};
			Groups[Axis] = Limit;
			Groups[(Axis + 1) % 3] = 0;
			Commands.mValue->Dispatch(Groups[0], Groups[1], Groups[2]);
			ASSERT_TRUE(Commands.mValue->Close());
			ASSERT_TRUE(Commands.mValue->Reset());
			ASSERT_TRUE(Commands.mValue->SetComputeState(State));
			Groups[(Axis + 1) % 3] = 1;
			Groups[Axis] = Limit + 1;
			Commands.mValue->Dispatch(Groups[0], Groups[1], Groups[2]);
			EXPECT_EQ(Commands.mValue->Close().mCode, EArdaRHIResult::InvalidArgument);
		}
	}

	TEST_P(FArdaCommandValidationTest, PipelineKindsAndQueueClassesCannotBeMixedImplicitly)
	{
		auto Graphics = GraphicsState();
		auto Compute = ComputeState();
		ASSERT_TRUE(Graphics.mPipeline);
		ASSERT_TRUE(Compute.mPipeline);
		auto Commands = mDevice->CreateCommandList();
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		ASSERT_TRUE(Commands.mValue->SetGraphicsState(Graphics));
		Commands.mValue->Dispatch(1, 1, 1);
		EXPECT_EQ(Commands.mValue->Close().mCode, EArdaRHIResult::InvalidState);
		ASSERT_TRUE(Commands.mValue->Reset());
		ASSERT_TRUE(Commands.mValue->SetComputeState(Compute));
		Commands.mValue->Draw({3});
		EXPECT_EQ(Commands.mValue->Close().mCode, EArdaRHIResult::InvalidState);
		for (const auto Queue : {EArdaRHIQueueType::Compute, EArdaRHIQueueType::Copy})
		{
			if (!mDevice->GetCapabilities().IsQueueSupported(Queue))
			{
				continue;
			}
			auto Other = mDevice->CreateCommandList(Queue);
			ASSERT_TRUE(Other);
			ASSERT_TRUE(Other.mValue->Open());
			EXPECT_EQ(Other.mValue->SetGraphicsState(Graphics).mCode, EArdaRHIResult::InvalidState);
			if (Queue == EArdaRHIQueueType::Copy)
			{
				EXPECT_EQ(Other.mValue->SetComputeState(Compute).mCode, EArdaRHIResult::InvalidState);
			}
			ASSERT_TRUE(Other.mValue->Close());
		}
	}

	TEST_P(FArdaCommandValidationTest, RequiredBindingsAreCheckedBeforeChangingNativeState)
	{
		auto State = ComputeState();
		ASSERT_TRUE(State.mPipeline);
		auto Commands = mDevice->CreateCommandList();
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		ASSERT_TRUE(Commands.mValue->SetComputeState(State));
		auto InvalidState = State;
		InvalidState.mBindings.clear();
		EXPECT_EQ(Commands.mValue->SetComputeState(InvalidState).mCode, EArdaRHIResult::InvalidArgument);
		InvalidState = State;
		InvalidState.mBindings.push_back(State.mBindings.front());
		EXPECT_EQ(Commands.mValue->SetComputeState(InvalidState).mCode, EArdaRHIResult::InvalidArgument);
		// Rejected facade input preserves the preceding complete binding and remains submit-safe.
		Commands.mValue->Dispatch(1, 1, 1);
		ASSERT_TRUE(Commands.mValue->Close());
		auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
		ASSERT_TRUE(Submitted);
		ASSERT_TRUE(mDevice->WaitForSubmission(Submitted.mValue));
	}

	TEST_P(FArdaCommandValidationTest, OmittedPushOnlyBindingsAreReusedWithinOneRecordingGeneration)
	{
		auto State = ComputeState();
		ASSERT_TRUE(State.mPipeline);
		FArdaRHIBindingLayoutDesc PushLayoutDesc;
		PushLayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
		PushLayoutDesc.mRegisterSpace = 1;
		PushLayoutDesc.mItems = {{0, 4, EArdaRHIBindingType::PushConstants}};
		auto PushLayout = mDevice->CreateBindingLayout(PushLayoutDesc);
		ASSERT_TRUE(PushLayout);
		auto PipelineDesc = State.mPipeline->GetDesc();
		// Omit the push-only set before a required descriptor set to exercise native positional ordering too.
		PipelineDesc.mBindingLayouts.insert(PipelineDesc.mBindingLayouts.begin(), PushLayout.mValue);
		auto Pipeline = mDevice->CreateComputePipeline(PipelineDesc);
		ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();
		State.mPipeline = Pipeline.mValue;
		auto Commands = mDevice->CreateCommandList();
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		const auto Baseline = mDevice->GetResourceLifetimeStats();
		ASSERT_TRUE(Commands.mValue->SetComputeState(State));
		const auto FirstBind = mDevice->GetResourceLifetimeStats();
		EXPECT_EQ(FirstBind.GetLiveResourceCount(EArdaRHIResourceType::BindingSet),
		    Baseline.GetLiveResourceCount(EArdaRHIResourceType::BindingSet) + 1);
		// Exceed the Vulkan descriptor pool's 8192-set capacity: repeated binds must reuse the empty set.
		for (uint32_t Index = 0; Index < 8200; ++Index)
		{
			const auto Status = Commands.mValue->SetComputeState(State);
			ASSERT_TRUE(Status) << "bind " << Index << ": " << Status.mMessage.c_str();
		}
		const auto Repeated = mDevice->GetResourceLifetimeStats();
		EXPECT_EQ(Repeated.GetLiveResourceCount(EArdaRHIResourceType::BindingSet),
		    FirstBind.GetLiveResourceCount(EArdaRHIResourceType::BindingSet));
		EXPECT_EQ(Repeated.mDescriptorSets, FirstBind.mDescriptorSets);
		ASSERT_TRUE(Commands.mValue->Close());
		ASSERT_TRUE(Commands.mValue->Reset());
		const auto Reset = mDevice->GetResourceLifetimeStats();
		EXPECT_EQ(Reset.GetLiveResourceCount(EArdaRHIResourceType::BindingSet),
		    Baseline.GetLiveResourceCount(EArdaRHIResourceType::BindingSet));
		EXPECT_EQ(Reset.mDescriptorSets, Baseline.mDescriptorSets);
		ASSERT_TRUE(Commands.mValue->Close());
	}

	TEST_P(FArdaCommandValidationTest, GraphicsStateRejectsIncompatibleTargetsAndInvalidViewports)
	{
		auto State = GraphicsState();
		ASSERT_TRUE(State.mPipeline);
		auto Commands = mDevice->CreateCommandList();
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		for (const float InvalidValue :
		    {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.f})
		{
			auto InvalidState = State;
			InvalidState.mViewports[0].mMaxX = InvalidValue;
			EXPECT_EQ(Commands.mValue->SetGraphicsState(InvalidState).mCode, EArdaRHIResult::InvalidArgument);
		}
		auto InvalidState = State;
		InvalidState.mScissors[0].mMinX = -1;
		EXPECT_EQ(Commands.mValue->SetGraphicsState(InvalidState).mCode, EArdaRHIResult::InvalidArgument);
		InvalidState = State;
		InvalidState.mViewports.push_back(State.mViewports[0]);
		EXPECT_EQ(Commands.mValue->SetGraphicsState(InvalidState).mCode, EArdaRHIResult::InvalidArgument);
		auto PipelineDesc = State.mPipeline->GetDesc();
		PipelineDesc.mColorFormats = {EArdaRHIFormat::RGBA16Float};
		auto Pipeline = mDevice->CreateGraphicsPipeline(PipelineDesc);
		ASSERT_TRUE(Pipeline);
		InvalidState = State;
		InvalidState.mPipeline = Pipeline.mValue;
		EXPECT_EQ(Commands.mValue->SetGraphicsState(InvalidState).mCode, EArdaRHIResult::InvalidArgument);
		if (mDevice->QueryFormatSupport(EArdaRHIFormat::RGBA8UNorm).mSampleCounts & 4u)
		{
			PipelineDesc = State.mPipeline->GetDesc();
			PipelineDesc.mSampleCount = 4;
			Pipeline = mDevice->CreateGraphicsPipeline(PipelineDesc);
			ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();
			InvalidState.mPipeline = Pipeline.mValue;
			EXPECT_EQ(Commands.mValue->SetGraphicsState(InvalidState).mCode, EArdaRHIResult::InvalidArgument);
		}
		ASSERT_TRUE(Commands.mValue->SetGraphicsState(State));
		Commands.mValue->Draw({0});
		ASSERT_TRUE(Commands.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
	}

	TEST_P(FArdaCommandValidationTest, IndexBindingsAndDrawRangesAreChecked)
	{
		auto State = GraphicsState();
		ASSERT_TRUE(State.mPipeline);
		auto Commands = mDevice->CreateCommandList();
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		ASSERT_TRUE(Commands.mValue->SetGraphicsState(State));
		Commands.mValue->DrawIndexed({3});
		EXPECT_EQ(Commands.mValue->Close().mCode, EArdaRHIResult::InvalidState);
		ASSERT_TRUE(Commands.mValue->Reset());
		FArdaRHIBufferDesc Buffer;
		Buffer.mByteSize = 12;
		Buffer.mUsage = EArdaRHIBufferUsage::Index;
		auto Indices = mDevice->CreateBuffer(Buffer);
		ASSERT_TRUE(Indices);
		State.mIndexBuffer = Indices.mValue;
		State.mIndexFormat = EArdaRHIFormat::R32UInt;
		for (const uint32_t Offset : {1u, 12u, UINT32_MAX})
		{
			State.mIndexOffset = Offset;
			EXPECT_EQ(Commands.mValue->SetGraphicsState(State).mCode, EArdaRHIResult::InvalidArgument);
		}
		State.mIndexOffset = 0;
		ASSERT_TRUE(Commands.mValue->SetGraphicsState(State));
		Commands.mValue->DrawIndexed({4});
		EXPECT_EQ(Commands.mValue->Close().mCode, EArdaRHIResult::InvalidArgument);
	}

	TEST_P(FArdaCommandValidationTest, VertexBindingsAndDirectDrawRangesAreChecked)
	{
		auto State = GraphicsState(true);
		ASSERT_TRUE(State.mPipeline);
		auto Commands = mDevice->CreateCommandList();
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		EXPECT_EQ(Commands.mValue->SetGraphicsState(State).mCode, EArdaRHIResult::InvalidArgument);
		FArdaRHIBufferDesc Buffer;
		Buffer.mByteSize = 36;
		Buffer.mUsage = EArdaRHIBufferUsage::Vertex;
		auto Vertices = mDevice->CreateBuffer(Buffer);
		ASSERT_TRUE(Vertices);
		State.mVertexBuffers = {{Vertices.mValue, 0, 0}};
		auto InvalidState = State;
		InvalidState.mVertexBuffers.push_back(State.mVertexBuffers[0]);
		EXPECT_EQ(Commands.mValue->SetGraphicsState(InvalidState).mCode, EArdaRHIResult::InvalidArgument);
		State.mVertexBuffers[0].mOffset = 36;
		EXPECT_EQ(Commands.mValue->SetGraphicsState(State).mCode, EArdaRHIResult::InvalidArgument);
		State.mVertexBuffers[0].mOffset = 1;
		EXPECT_EQ(Commands.mValue->SetGraphicsState(State).mCode, EArdaRHIResult::InvalidArgument);
		State.mVertexBuffers[0].mOffset = 0;
		ASSERT_TRUE(Commands.mValue->SetGraphicsState(State));
		Commands.mValue->Draw({4});
		EXPECT_EQ(Commands.mValue->Close().mCode, EArdaRHIResult::InvalidArgument);
	}

	TEST_P(FArdaCommandValidationTest, DiagnosticsAreOwnedBoundedAndAvailableWithoutSubmitting)
	{
		auto Snapshot = mDevice->CaptureDiagnosticSnapshot();
		ASSERT_TRUE(Snapshot) << Snapshot.mStatus.mMessage.c_str();
		EXPECT_FALSE(Snapshot.mValue.mBackendName.empty());
		EXPECT_FALSE(Snapshot.mValue.mAdapterName.empty());
		EXPECT_LE(Snapshot.mValue.mBreadcrumbs.size(), ArdaRHIMaxDiagnosticEntries);
		EXPECT_LE(Snapshot.mValue.mFaults.size(), ArdaRHIMaxDiagnosticEntries);
		EXPECT_LE(Snapshot.mValue.mMessages.size(), ArdaRHIMaxDiagnosticEntries);
		auto Commands = mDevice->CreateCommandList();
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		Commands.mValue->BeginMarker("audit snapshot marker");
		Commands.mValue->EndMarker();
		ASSERT_TRUE(Commands.mValue->Close());
		auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
		ASSERT_TRUE(Submitted);
		ASSERT_TRUE(mDevice->WaitForSubmission(Submitted.mValue));
		Snapshot = mDevice->CaptureDiagnosticSnapshot();
		ASSERT_TRUE(Snapshot);
		bool bFoundMarker = false;
		for (const auto& Marker : Snapshot.mValue.mBreadcrumbs)
		{
			bFoundMarker |= Marker.find("audit snapshot marker") != eastl::string::npos;
		}
		EXPECT_TRUE(bFoundMarker);
		Commands.mValue = {};
		mDevice = {};
		ShutdownBackend();
		EXPECT_FALSE(Snapshot.mValue.mAdapterName.empty());
	}

	const char* const NativeBackends[] = {
#if defined(ARDA_TEST_NATIVE_D3D12)
	    "native-d3d12",
#endif
#if defined(ARDA_TEST_NATIVE_VULKAN)
	    "native-vulkan",
#endif
	};
	INSTANTIATE_TEST_SUITE_P(NativeProviders, FArdaCommandValidationTest, testing::ValuesIn(NativeBackends));
}
