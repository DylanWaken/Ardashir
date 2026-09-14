#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "ArdaTestComputeOperand.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"

#include <gtest/gtest.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#if defined(ARDA_TEST_NATIVE_D3D12)
namespace
{
	using namespace arda;

	class FArdaWorkGraphSubmissionDiagnostics final : public IArdaDiagnosticCallback
	{
	public:
		void Message(EArdaDiagnosticSeverity Severity, const char* Text) override
		{
			if (Severity >= EArdaDiagnosticSeverity::Warning)
			{
				++mWarnings;
				std::fprintf(stderr, "%s\n", Text ? Text : "");
			}
		}

		std::atomic<uint32_t> mWarnings{0};
	};

	class FArdaWorkGraphSubmissionTest : public testing::Test
	{
	protected:
		struct FArdaRecording
		{
			FArdaRHICommandListRef mCommands;
			FArdaRHIBufferRef mOutput;
		};

		void SetUp() override
		{
			ShutdownBackend();
			auto Configuration = MakeArdaTestBackendConfiguration();
			Configuration.mBackendName = "native-d3d12";
			Configuration.mCudaExecutionMode = mCudaExecutionMode;
			Configuration.mMessageCallback = &mDiagnostics;
			Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
			ASSERT_TRUE(ConfigureBackend(Configuration));
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
			mDevice = GetDevice();
			ASSERT_TRUE(mDevice);
			if (mDevice->GetCapabilities().mWorkGraphTier == EArdaRHIWorkGraphTier::None)
			{
				GTEST_SKIP() << "D3D12 work graphs are unavailable.";
			}

			// Every recording uses one cached pipeline and its backing memory, with independent outputs.
			const std::string Path = std::string(ARDA_BACKEND_TEST_SHADER_DIR "/ArdaWorkGraphTest") +
			    GetShaderArtifactExtension("native-d3d12");
			std::ifstream Stream(Path, std::ios::binary);
			ASSERT_TRUE(Stream) << Path;
			const std::vector<char> Bytes((std::istreambuf_iterator<char>(Stream)), {});
			FArdaRHIShaderDesc ShaderDesc;
			ShaderDesc.mStage = EArdaRHIShaderStage::WorkGraph;
			ShaderDesc.mEntryPoint = "WorkGraphMain";
			ShaderDesc.mBytecode = Bytes.data();
			ShaderDesc.mBytecodeSize = Bytes.size();
			auto Shader = mDevice->CreateShader(ShaderDesc);
			ASSERT_TRUE(Shader);
			FArdaRHIBindingLayoutDesc LayoutDesc;
			LayoutDesc.mVisibility = EArdaRHIShaderStage::WorkGraph;
			LayoutDesc.mItems = {{0, 1, EArdaRHIBindingType::StructuredBufferUAV}};
			auto Layout = mDevice->CreateBindingLayout(LayoutDesc);
			ASSERT_TRUE(Layout);
			mLayout = Layout.mValue;
			FArdaRHIWorkGraphPipelineDesc PipelineDesc;
			PipelineDesc.mProgramName = "ArdaWorkGraphSubmission";
			PipelineDesc.mEntryPoint = "WorkGraphMain";
			PipelineDesc.mShaders = {Shader.mValue};
			PipelineDesc.mGlobalBindingLayouts = {mLayout};
			PipelineDesc.mMaxInputRecords = 1;
			auto Pipeline = mDevice->CreateWorkGraphPipeline(PipelineDesc);
			ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();
			mPipeline = Pipeline.mValue;
		}

		void TearDown() override
		{
			const bool MissingValidationSkip = testing::Test::IsSkipped() && ArdaTestValidationEnabled &&
			    GetBackendInitializeResult() == EArdaInitializeResult::ValidationUnavailable;
			if (mDevice)
			{
				const auto Idle = mDevice->WaitForIdle();
				EXPECT_TRUE(Idle) << Idle.mMessage.c_str();
			}
			mPipeline = {};
			mLayout = {};
			mDevice = {};
			ShutdownBackend();
			if (!MissingValidationSkip)
			{
				EXPECT_EQ(mDiagnostics.mWarnings.load(), 0u);
			}
			static_cast<void>(ConfigureBackend(MakeArdaTestBackendConfiguration()));
		}

		void Record(FArdaRecording& Recording,
		    uint32_t Input,
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics,
		    bool bAppendCuda = false)
		{
			FArdaRHIBufferDesc BufferDesc;
			BufferDesc.mByteSize = sizeof(uint32_t);
			BufferDesc.mStructureStride = sizeof(uint32_t);
			BufferDesc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess |
			    EArdaRHIBufferUsage::ShaderResource;
			BufferDesc.mInitialState = EArdaRHIResourceState::UnorderedAccess;
			BufferDesc.mbCudaInterop = bAppendCuda;
			auto Buffer = mDevice->CreateBuffer(BufferDesc);
			ASSERT_TRUE(Buffer) << Buffer.mStatus.mMessage.c_str();
			Recording.mOutput = Buffer.mValue;
			FArdaRHIBindingSetDesc SetDesc;
			SetDesc.mLayout = mLayout;
			FArdaRHIBindingItem Item;
			Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
			Item.mResource = FArdaRHIResourceRef(Buffer.mValue.Get());
			SetDesc.mItems = {Item};
			auto Set = mDevice->CreateBindingSet(SetDesc);
			ASSERT_TRUE(Set);

			// Keep dispatch recordings asynchronous: readbacks are submitted separately after every dispatch.
			auto Commands = mDevice->CreateCommandList(Queue);
			ASSERT_TRUE(Commands);
			Recording.mCommands = Commands.mValue;
			ASSERT_TRUE(Recording.mCommands->Open());
			const auto Dispatch =
			    Recording.mCommands->DispatchWorkGraph(*mPipeline, &Input, 1, sizeof(Input), {Set.mValue});
			ASSERT_TRUE(Dispatch) << Dispatch.mMessage.c_str();
			if (bAppendCuda)
			{
				// This moves the preceding workgraph commands into the graphics-to-CUDA segment.
				FArdaAddOperand Operand(mDevice);
				FArdaAddParameters Parameters;
				Parameters.mInput.mBuffer = Parameters.mOutput.mBuffer = Buffer.mValue;
				Parameters.mCount = 1;
				Parameters.mBias = 11;
				const auto Cuda = Operand.DispatchDeferred(*Recording.mCommands, Parameters);
				ASSERT_TRUE(Cuda) << Cuda.mMessage.c_str();
			}
			ASSERT_TRUE(Recording.mCommands->Close());
		}

		void ExpectResult(const FArdaRecording& Recording, uint32_t Expected)
		{
			// Readback cannot accidentally serialize the dispatch submissions being tested.
			auto Commands = mDevice->CreateCommandList();
			ASSERT_TRUE(Commands) << Commands.mStatus.mMessage.c_str();
			ASSERT_TRUE(Commands.mValue->Open());
			eastl::vector<uint8_t> Readback;
			const auto Copy =
			    Commands.mValue->CopyBufferDeviceToHost(*Recording.mOutput, Readback, 0, sizeof(Expected));
			ASSERT_TRUE(Copy) << Copy.mMessage.c_str();
			ASSERT_TRUE(Commands.mValue->Close());
			const auto Submission = mDevice->ExecuteCommandList(Commands.mValue);
			ASSERT_TRUE(Submission) << Submission.mStatus.mMessage.c_str();
			ASSERT_EQ(Readback.size(), sizeof(Expected));
			uint32_t Actual = 0;
			std::memcpy(&Actual, Readback.data(), sizeof(Actual));
			EXPECT_EQ(Actual, Expected);
		}

		FArdaWorkGraphSubmissionDiagnostics mDiagnostics;
		FArdaRHIDeviceRef mDevice;
		FArdaRHIBindingLayoutRef mLayout;
		FArdaRHIWorkGraphPipelineRef mPipeline;
		EArdaCudaExecutionMode mCudaExecutionMode = EArdaCudaExecutionMode::Automatic;
	};

	class FArdaWorkGraphCudaSubmissionTest : public FArdaWorkGraphSubmissionTest
	{
	public:
		FArdaWorkGraphCudaSubmissionTest()
		{
			mCudaExecutionMode = EArdaCudaExecutionMode::ContextSwitch;
		}
	};

	TEST_F(FArdaWorkGraphCudaSubmissionTest, SegmentedCudaRetainsWorkGraphAndOrdersNextQueue)
	{
		const auto Capabilities = mDevice->GetCudaCapabilities();
		if (!Capabilities)
		{
			GTEST_SKIP() << Capabilities.mUnavailableReason.c_str();
		}
		ASSERT_EQ(Capabilities.mLaunchMode, EArdaCudaLaunchMode::ContextSwitch);
		FArdaAddOperand Operand(mDevice);
		const auto Support = Operand.GetOperandSupport();
		if (!Support && Support.mCode == EArdaRHIResult::Unsupported)
		{
			GTEST_SKIP() << Support.mMessage.c_str();
		}
		ASSERT_TRUE(Support) << Support.mMessage.c_str();

		FArdaRecording Mixed, Compute;
		ASSERT_NO_FATAL_FAILURE(Record(Mixed, 0x12345678u, EArdaRHIQueueType::Graphics, true));
		ASSERT_NO_FATAL_FAILURE(Record(Compute, 0x87654321u, EArdaRHIQueueType::Compute));
		const auto MixedSubmission = mDevice->ExecuteCommandList(Mixed.mCommands);
		ASSERT_TRUE(MixedSubmission) << MixedSubmission.mStatus.mMessage.c_str();
		Mixed.mCommands.Reset();
		const auto ComputeSubmission = mDevice->ExecuteCommandList(Compute.mCommands);
		ASSERT_TRUE(ComputeSubmission) << ComputeSubmission.mStatus.mMessage.c_str();
		Compute.mCommands.Reset();

		const auto Idle = mDevice->WaitForIdle();
		ASSERT_TRUE(Idle) << Idle.mMessage.c_str();
		ExpectResult(Mixed, 0x12345678u + 11u);
		ExpectResult(Compute, 0x87654321u);
	}

	TEST_F(FArdaWorkGraphSubmissionTest, DiscardedFirstRecordingDoesNotConsumeInitialization)
	{
		{
			FArdaRecording Discarded;
			ASSERT_NO_FATAL_FAILURE(Record(Discarded, 0xDEADu));
		}

		FArdaRecording Executed;
		ASSERT_NO_FATAL_FAILURE(Record(Executed, 0x10203040u));
		const auto ExecutedSubmission = mDevice->ExecuteCommandList(Executed.mCommands);
		ASSERT_TRUE(ExecutedSubmission) << ExecutedSubmission.mStatus.mMessage.c_str();
		const auto Idle = mDevice->WaitForIdle();
		ASSERT_TRUE(Idle) << Idle.mMessage.c_str();
		ExpectResult(Executed, 0x10203040u);
	}

	TEST_F(FArdaWorkGraphSubmissionTest, ResetFirstRecordingDoesNotConsumeInitialization)
	{
		FArdaRecording ResetRecording;
		ASSERT_NO_FATAL_FAILURE(Record(ResetRecording, 0xDEADu));
		ASSERT_TRUE(ResetRecording.mCommands->Reset());
		ASSERT_TRUE(ResetRecording.mCommands->Close());
		const auto ResetRecordingSubmission = mDevice->ExecuteCommandList(ResetRecording.mCommands);
		ASSERT_TRUE(ResetRecordingSubmission) << ResetRecordingSubmission.mStatus.mMessage.c_str();

		FArdaRecording Executed;
		ASSERT_NO_FATAL_FAILURE(Record(Executed, 0x50607080u));
		const auto ExecutedSubmission = mDevice->ExecuteCommandList(Executed.mCommands);
		ASSERT_TRUE(ExecutedSubmission) << ExecutedSubmission.mStatus.mMessage.c_str();
		const auto Idle = mDevice->WaitForIdle();
		ASSERT_TRUE(Idle) << Idle.mMessage.c_str();
		ExpectResult(Executed, 0x50607080u);
	}

	TEST_F(FArdaWorkGraphSubmissionTest, SubmissionOrderDeterminesFirstUse)
	{
		FArdaRecording FirstRecorded, SecondRecorded;
		ASSERT_NO_FATAL_FAILURE(Record(FirstRecorded, 0x12345678u));
		ASSERT_NO_FATAL_FAILURE(Record(SecondRecorded, 0x87654321u));
		const auto SecondRecordedSubmission = mDevice->ExecuteCommandList(SecondRecorded.mCommands);
		ASSERT_TRUE(SecondRecordedSubmission) << SecondRecordedSubmission.mStatus.mMessage.c_str();
		const auto FirstRecordedSubmission = mDevice->ExecuteCommandList(FirstRecorded.mCommands);
		ASSERT_TRUE(FirstRecordedSubmission) << FirstRecordedSubmission.mStatus.mMessage.c_str();
		const auto Idle = mDevice->WaitForIdle();
		ASSERT_TRUE(Idle) << Idle.mMessage.c_str();
		ExpectResult(FirstRecorded, 0x12345678u);
		ExpectResult(SecondRecorded, 0x87654321u);
	}

	TEST_F(FArdaWorkGraphSubmissionTest, BackingMemoryReuseOrdersIndependentQueues)
	{
		ASSERT_TRUE(mDevice->GetCapabilities().mQueues.mbCompute);
		FArdaRecording Graphics, Compute, ComputeAgain;
		ASSERT_NO_FATAL_FAILURE(Record(Graphics, 0x12345678u));
		ASSERT_NO_FATAL_FAILURE(Record(Compute, 0x87654321u, EArdaRHIQueueType::Compute));
		ASSERT_NO_FATAL_FAILURE(Record(ComputeAgain, 0xABCDEF01u, EArdaRHIQueueType::Compute));
		const auto ComputeSubmission = mDevice->ExecuteCommandList(Compute.mCommands);
		ASSERT_TRUE(ComputeSubmission) << ComputeSubmission.mStatus.mMessage.c_str();
		const auto GraphicsSubmission = mDevice->ExecuteCommandList(Graphics.mCommands);
		ASSERT_TRUE(GraphicsSubmission) << GraphicsSubmission.mStatus.mMessage.c_str();
		const auto ComputeAgainSubmission = mDevice->ExecuteCommandList(ComputeAgain.mCommands);
		ASSERT_TRUE(ComputeAgainSubmission) << ComputeAgainSubmission.mStatus.mMessage.c_str();
		const auto Idle = mDevice->WaitForIdle();
		ASSERT_TRUE(Idle) << Idle.mMessage.c_str();
		ExpectResult(Graphics, 0x12345678u);
		ExpectResult(Compute, 0x87654321u);
		ExpectResult(ComputeAgain, 0xABCDEF01u);
	}
}
#endif
