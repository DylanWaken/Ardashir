#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ArdaTestComputeOperand.h"
#include "Compute/ArdaCudaTextureBuffer.h"
#include "ArdaRenderGraph.h"
#include <gtest/gtest.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace
{
	using namespace arda;

	class FCudaDiagnostics : public IArdaDiagnosticCallback
	{
	public:
		std::atomic<uint32_t> mErrors{0};

		void Message(EArdaDiagnosticSeverity Severity, const char* Text) override
		{
			if (Severity == EArdaDiagnosticSeverity::Error || Severity == EArdaDiagnosticSeverity::Fatal)
			{
				++mErrors;
				std::fprintf(stderr, "CUDA validation: %s\n", Text);
			}
		}
	};

	class ArdaCudaGpu : public testing::TestWithParam<const char*>
	{
	protected:
		FCudaDiagnostics mDiagnostics;
		FArdaRHIDeviceRef mDevice;

		void SetUp() override
		{
			ShutdownBackend();
			FArdaBackendConfiguration C;
			const eastl::string Name = GetParam();
			C.mBackendName = Name.find("d3d12") != eastl::string::npos ? "native-d3d12" : "native-vulkan";
			C.mCudaExecutionMode = Name.find("context") != eastl::string::npos ? EArdaCudaExecutionMode::ContextSwitch
			                                                                   : EArdaCudaExecutionMode::GraphicsQueue;
			if (!FindBackendModule(C.mBackendName.c_str()))
			{
				GTEST_SKIP() << "Backend not built";
			}
			C.mbEnableValidation = true;
			C.mMessageCallback = &mDiagnostics;
			C.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
			ASSERT_TRUE(ConfigureBackend(C));
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
			mDevice = GetDevice();
			const auto Caps = mDevice->GetCudaCapabilities();
			if (!Caps)
			{
				GTEST_SKIP() << Caps.mUnavailableReason.c_str();
			}
			FArdaAddOperand Operand(mDevice);
			auto Support = Operand.GetOperandSupport();
			if (!Support)
			{
				GTEST_SKIP() << Support.mMessage.c_str();
			}
			std::printf("%s: CUDA mode=%u SM=%u\n", GetParam(), unsigned(Caps.mLaunchMode), Caps.mComputeCapability);
		}

		void TearDown() override
		{
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
			}
			mDevice.Reset();
			ShutdownBackend();
			EXPECT_EQ(mDiagnostics.mErrors.load(), 0u);
		}

		FArdaRHIBufferDesc BufferDesc(uint64_t Size, bool Cuda = true)
		{
			FArdaRHIBufferDesc D;
			D.mByteSize = Size;
			D.mbCudaInterop = Cuda;
			D.mDebugName = "CUDA test buffer";
			D.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
			return D;
		}

		void ExpectWords(const eastl::vector<uint8_t>& Bytes, uint32_t Count, uint32_t Bias)
		{
			ASSERT_EQ(Bytes.size(), size_t(Count) * 4);
			for (uint32_t I = 0; I < Count; ++I)
			{
				uint32_t V;
				std::memcpy(&V, Bytes.data() + I * 4, 4);
				ASSERT_EQ(V, I * 3 + Bias) << I;
			}
		}
	};

	struct FArdaCudaLaunchTrace
	{
		eastl::vector<void*> mStreams;
		size_t mLimitQueries = 0;
		size_t mQueriesBeforeLaunch = 0;
	};

	class FArdaTracingCudaEntry final : public IArdaCudaKernelEntry
	{
	public:
		eastl::shared_ptr<const IArdaCudaKernelEntry> mEntry;
		eastl::shared_ptr<FArdaCudaLaunchTrace> mTrace;
		bool mbRejectLimits = false;

		FArdaCudaKernelSignature GetSignature() const noexcept override
		{
			return mEntry->GetSignature();
		}

		const FArdaCudaBuildInfo& GetBuildInfo() const noexcept override
		{
			return mEntry->GetBuildInfo();
		}

		TArdaRHIResult<FArdaCudaKernelLimits> GetLimits() const override
		{
			++mTrace->mLimitQueries;
			return mbRejectLimits ? TArdaRHIResult<FArdaCudaKernelLimits>{{1, 0, 0}, {}} : mEntry->GetLimits();
		}

		FArdaRHIStatus Launch(void* Stream,
		    const FArdaCudaLaunchConfig& Config,
		    const void* Parameters,
		    size_t Size) const override
		{
			if (mTrace->mStreams.empty())
			{
				mTrace->mQueriesBeforeLaunch = mTrace->mLimitQueries;
			}
			mTrace->mStreams.push_back(Stream);
			return mEntry->Launch(Stream, Config, Parameters, Size);
		}
	};

	TEST_P(ArdaCudaGpu, SequenceUsesOneStreamAndRetainsPingPongViews)
	{
		constexpr uint32_t Count = 4097;
		auto Input = mDevice->CreateBuffer(BufferDesc(Count * 4));
		auto Scratch = mDevice->CreateBuffer(BufferDesc(Count * 4));
		auto Output = mDevice->CreateBuffer(BufferDesc((Count + 8) * 4));
		ASSERT_TRUE(Input);
		ASSERT_TRUE(Scratch);
		ASSERT_TRUE(Output);
		auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Cmd);
		ASSERT_TRUE(Cmd.mValue->Open());
		eastl::vector<uint32_t> Values(Count);
		for (uint32_t I = 0; I < Count; ++I)
		{
			Values[I] = I * 3;
		}
		ASSERT_TRUE(Cmd.mValue->WriteBuffer(*Input.mValue, Values.data(), Values.size() * 4));
		eastl::vector<uint32_t> Guards(Count + 8, 0xDEADBEEFu);
		ASSERT_TRUE(Cmd.mValue->WriteBuffer(*Output.mValue, Guards.data(), Guards.size() * 4));
		const auto Trace = eastl::make_shared<FArdaCudaLaunchTrace>();
		{
			FArdaAddOperand Operand(mDevice);
			FArdaCudaSequence Sequence(mDevice);
			FArdaAddParameters P;
			P.mCount = Count;
			for (uint32_t Step = 0; Step < 3; ++Step)
			{
				P.mInput.mBuffer = Step == 1 ? Scratch.mValue : Input.mValue;
				P.mOutput.mBuffer = Step == 0 ? Scratch.mValue : Step == 1 ? Input.mValue : Output.mValue;
				P.mOutput.mRange = {Step == 2 ? 16u : 0u, Count * 4};
				P.mBias = Step == 0 ? 7 : Step == 1 ? 11 : 13;
				auto Prepared = Operand.PrepareDispatch(P);
				ASSERT_TRUE(Prepared);
				auto Plan = eastl::make_shared<FArdaCudaDispatchPlan>(*Prepared.mValue);
				auto Entry = eastl::make_shared<FArdaTracingCudaEntry>();
				Entry->mEntry = Plan->mDispatch.mKernels.front().mEntry;
				Entry->mTrace = Trace;
				Plan->mDispatch.mKernels.front().mEntry = Entry;
				ASSERT_TRUE(Sequence.AddPlan(Plan));
				Plan->mDispatch = {}; // The sequence must own a copy, including its resources.
			}
			ASSERT_EQ(Sequence.GetKernelCount(), 3u);
			P.mBias = 999;
			auto Status = Sequence.DispatchDeferred(*Cmd.mValue);
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
		}
		Input.mValue.Reset();
		Scratch.mValue.Reset();
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*Output.mValue, Bytes));
		ASSERT_TRUE(Cmd.mValue->Close());
		Output.mValue.Reset();
		auto Submitted = mDevice->ExecuteCommandList(Cmd.mValue);
		ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
		Cmd.mValue.Reset();
		ASSERT_TRUE(mDevice->WaitForIdle());
		ASSERT_EQ(Trace->mStreams.size(), 3u);
		EXPECT_NE(Trace->mStreams[0], nullptr);
		EXPECT_EQ(Trace->mStreams[0], Trace->mStreams[1]);
		EXPECT_EQ(Trace->mStreams[0], Trace->mStreams[2]);
		EXPECT_EQ(Trace->mQueriesBeforeLaunch, 3u);
		ASSERT_EQ(Bytes.size(), Guards.size() * 4);
		for (uint32_t I = 0; I < Count + 8; ++I)
		{
			uint32_t Value;
			std::memcpy(&Value, Bytes.data() + I * 4, 4);
			EXPECT_EQ(Value, I < 4 || I >= Count + 4 ? 0xDEADBEEFu : (I - 4) * 3 + 31);
		}
	}

	TEST_P(ArdaCudaGpu, SequenceImmediateReplayNoWorkAndStickyErrors)
	{
		constexpr uint32_t Count = 65;
		auto Buffer = mDevice->CreateBuffer(BufferDesc(Count * 4));
		ASSERT_TRUE(Buffer);
		auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Cmd);
		ASSERT_TRUE(Cmd.mValue->Open());
		eastl::vector<uint32_t> Values(Count);
		for (uint32_t I = 0; I < Count; ++I)
		{
			Values[I] = I * 3;
		}
		ASSERT_TRUE(Cmd.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Values.size() * 4));
		ASSERT_TRUE(Cmd.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Cmd.mValue));
		FArdaAddOperand Operand(mDevice);
		FArdaAddParameters P;
		P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
		P.mCount = Count;
		P.mBias = 7;
		FArdaCudaSequence Sequence(mDevice);
		auto NoWork = eastl::make_shared<FArdaCudaDispatchPlan>();
		NoWork->mDevice = mDevice;
		NoWork->mSelection.mbNoWork = true;
		ASSERT_TRUE(Sequence.AddPlan(NoWork));
		auto Empty = Sequence.Dispatch();
		ASSERT_TRUE(Empty);
		EXPECT_EQ(Empty.mValue.mInstance, 0u);
		ASSERT_TRUE(Sequence.Add(Operand, P));
		P.mBias = 11;
		ASSERT_TRUE(Sequence.Add(Operand, P));
		P.mBias = 999;
		for (int Repeat = 0; Repeat < 2; ++Repeat)
		{
			auto Submitted = Sequence.Dispatch();
			ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
			EXPECT_NE(Submitted.mValue.mInstance, 0u);
			EXPECT_EQ(Submitted.mValue.mKernelCount, 2u);
		}
		FArdaCudaSequence Failed = Sequence;
		EXPECT_FALSE(Failed.AddPlan({}));
		EXPECT_FALSE(Failed.Add(Operand, P));
		EXPECT_FALSE(Failed.Dispatch());
		FArdaCudaSequence WrongQueue(mDevice);
		auto Mismatched = eastl::make_shared<FArdaCudaDispatchPlan>(*NoWork);
		Mismatched->mQueue = EArdaRHIQueueType::Compute;
		EXPECT_FALSE(WrongQueue.AddPlan(Mismatched));
		EXPECT_FALSE(FArdaCudaSequence(mDevice, EArdaRHIQueueType::Copy).GetStatus());
		FArdaCudaSequence WrongDevice(mDevice);
		Mismatched->mDevice.Reset();
		EXPECT_EQ(WrongDevice.AddPlan(Mismatched).mCode, EArdaRHIResult::WrongDevice);
		Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Cmd);
		EXPECT_FALSE(Sequence.DispatchDeferred(*Cmd.mValue)); // Closed lists do not accept sequences.
		ASSERT_TRUE(Cmd.mValue->Open());
		ASSERT_TRUE(Cmd.mValue->DispatchCudaSequence({}));
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
		ASSERT_TRUE(Cmd.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Cmd.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
		ExpectWords(Bytes, Count, 36);
	}

	TEST_P(ArdaCudaGpu, RetainedGraphReplaysAndRebuildsForArgumentsAndResources)
	{
		constexpr uint32_t Count = 257;
		auto Input = mDevice->CreateBuffer(BufferDesc(Count * 4));
		auto OutputA = mDevice->CreateBuffer(BufferDesc(Count * 4));
		auto OutputB = mDevice->CreateBuffer(BufferDesc(Count * 4));
		ASSERT_TRUE(Input);
		ASSERT_TRUE(OutputA);
		ASSERT_TRUE(OutputB);
		eastl::vector<uint32_t> Values(Count);
		for (uint32_t I = 0; I < Count; ++I)
		{
			Values[I] = I * 3;
		}
		auto Upload = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Upload);
		ASSERT_TRUE(Upload.mValue->Open());
		ASSERT_TRUE(Upload.mValue->WriteBuffer(*Input.mValue, Values.data(), Count * 4));
		ASSERT_TRUE(Upload.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Upload.mValue));
		FArdaAddOperand Operand(mDevice);
		for (const uint32_t Capacity : {2u, 4u})
		{
			auto Cache = eastl::make_shared<FArdaCudaGraphCache>(EArdaCudaGraphMode::Require, Capacity);
			auto Trace = eastl::make_shared<FArdaCudaLaunchTrace>();
			auto Entry = eastl::make_shared<FArdaTracingCudaEntry>();
			Entry->mTrace = Trace;
			for (uint32_t Frame = 0; Frame < 5; ++Frame)
			{
				const auto Output = Frame == 3 ? OutputB.mValue : OutputA.mValue;
				const uint32_t Bias = Frame < 2 || Frame == 4 ? 7 : 19;
				FArdaCudaSequence Sequence(mDevice, EArdaRHIQueueType::Graphics, Cache);
				FArdaAddParameters P;
				P.mCount = Count;
				P.mInput.mBuffer = Input.mValue;
				P.mOutput.mBuffer = Output;
				P.mBias = Bias;
				for (uint32_t Step = 0; Step < 2; ++Step)
				{
					auto Prepared = Operand.PrepareDispatch(P);
					ASSERT_TRUE(Prepared);
					auto Plan = eastl::make_shared<FArdaCudaDispatchPlan>(*Prepared.mValue);
					if (!Entry->mEntry)
					{
						Entry->mEntry = Plan->mDispatch.mKernels.front().mEntry;
					}
					Plan->mDispatch.mKernels.front().mEntry = Entry;
					ASSERT_TRUE(Sequence.AddPlan(Plan));
					P.mInput.mBuffer = Output;
					P.mBias = 11;
				}
				auto Submitted = Sequence.Dispatch();
				ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
				auto Readback = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
				ASSERT_TRUE(Readback);
				ASSERT_TRUE(Readback.mValue->Open());
				eastl::vector<uint8_t> Bytes;
				ASSERT_TRUE(Readback.mValue->CopyBufferDeviceToHost(*Output, Bytes));
				ASSERT_TRUE(Readback.mValue->Close());
				ASSERT_TRUE(mDevice->ExecuteCommandList(Readback.mValue));
				ASSERT_TRUE(mDevice->WaitForIdle());
				ExpectWords(Bytes, Count, Bias + 11);
			}
			const auto Stats = Cache->GetStats();
			const uint64_t Captures = Capacity == 2 ? 4 : 3;
			EXPECT_EQ(Stats.mCaptureCount, Captures);
			EXPECT_EQ(Stats.mRebuildCount, Captures - 1);
			EXPECT_EQ(Stats.mReplayCount, 5 - Captures);
			EXPECT_EQ(Stats.mCacheHitCount, 5 - Captures);
			EXPECT_EQ(Stats.mCachedVariantCount, Capacity == 2 ? 2u : 3u);
			EXPECT_EQ(Stats.mEvictionCount, Capacity == 2 ? 2u : 0u);
			EXPECT_EQ(Stats.mFallbackCount, 0u);
			EXPECT_EQ(Stats.mCaptureFailureCount, 0u);
			EXPECT_EQ(Trace->mStreams.size(), Captures * 2); // Only capture calls the two entry launchers.
			Cache->Reset();
			EXPECT_EQ(Cache->GetStats().mCaptureCount, Captures);
			EXPECT_EQ(Cache->GetStats().mCachedVariantCount, 0u);
		}
	}

	TEST_P(ArdaCudaGpu, TimingRegionsPreserveBatchReplayAndUnsampledVariants)
	{
		constexpr uint32_t Count = 4097;
		auto Buffer = mDevice->CreateBuffer(BufferDesc(Count * 4));
		ASSERT_TRUE(Buffer);
		eastl::vector<uint32_t> Values(Count);
		for (uint32_t I = 0; I < Count; ++I)
		{
			Values[I] = I * 3;
		}
		auto Upload = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Upload);
		ASSERT_TRUE(Upload.mValue->Open());
		ASSERT_TRUE(Upload.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Count * 4));
		ASSERT_TRUE(Upload.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Upload.mValue));
		FArdaAddOperand Operand(mDevice);
		FArdaAddParameters P;
		P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
		P.mCount = Count;
		P.mBias = 1;
		for (const auto Mode : {EArdaCudaGraphMode::Disabled, EArdaCudaGraphMode::Require})
		{
			auto Cache = eastl::make_shared<FArdaCudaGraphCache>(Mode);
			auto Query = eastl::make_shared<FArdaCudaTimingQuery>();
			for (uint32_t Frame = 0; Frame < 4; ++Frame)
			{
				const bool Sampled = Frame != 1;
				FArdaCudaSequence Sequence(mDevice, EArdaRHIQueueType::Graphics, Cache);
				if (Sampled)
				{
					ASSERT_TRUE(Sequence.BeginTimingRegion(Query, 11));
				}
				ASSERT_TRUE(Sequence.Add(Operand, P));
				if (Sampled)
				{
					ASSERT_TRUE(Sequence.EndTimingRegion());
					ASSERT_TRUE(Sequence.BeginTimingRegion(Query, 22));
				}
				for (uint32_t Step = 0; Step < 3; ++Step)
				{
					ASSERT_TRUE(Sequence.Add(Operand, P));
				}
				if (Sampled)
				{
					ASSERT_TRUE(Sequence.EndTimingRegion());
				}
				auto Submitted = Sequence.Dispatch();
				ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
				EXPECT_EQ(Submitted.mValue.mOperationCount, 4u);
				EXPECT_FALSE(Query->Poll().mValue.mbReady); // Never inspect a potentially stale capture event.
				ASSERT_TRUE(mDevice->WaitForIdle());
				auto Timing = Query->Poll(true);
				ASSERT_TRUE(Timing) << Timing.mStatus.mMessage.c_str();
				EXPECT_EQ(Timing.mValue.mbReady, Sampled);
				if (Sampled)
				{
					ASSERT_EQ(Timing.mValue.mRegions.size(), 2u);
					EXPECT_EQ(Timing.mValue.mRegions[0].mRegionId, 11u);
					EXPECT_EQ(Timing.mValue.mRegions[1].mRegionId, 22u);
					for (const auto& Region : Timing.mValue.mRegions)
					{
						EXPECT_GT(Region.mGpuSeconds, 0.0);
					}
				}
				EXPECT_FALSE(Query->Poll(true).mValue.mbReady); // A sample is consumed exactly once.
			}
			const auto Stats = Cache->GetStats();
			EXPECT_EQ(Stats.mCaptureCount, Mode == EArdaCudaGraphMode::Require ? 2u : 0u);
			EXPECT_EQ(Stats.mReplayCount, Mode == EArdaCudaGraphMode::Require ? 2u : 0u);
			EXPECT_EQ(Stats.mCachedVariantCount, Mode == EArdaCudaGraphMode::Require ? 2u : 0u);
		}
		auto Readback = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Readback);
		ASSERT_TRUE(Readback.mValue->Open());
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Readback.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
		ASSERT_TRUE(Readback.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Readback.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
		ExpectWords(Bytes, Count, 32);
	}

	TEST_P(ArdaCudaGpu, TimingQueryCancelsUnsubmittedRecordingsAndRejectsOverwritingASample)
	{
		auto Query = eastl::make_shared<FArdaCudaTimingQuery>();
		FArdaCudaSequence Unclosed(mDevice);
		ASSERT_TRUE(Unclosed.BeginTimingRegion(Query, 1));
		EXPECT_FALSE(Unclosed.Dispatch());
		EXPECT_FALSE(Unclosed.BeginTimingRegion(Query, 2));
		FArdaCudaSequence Unmatched(mDevice);
		EXPECT_FALSE(Unmatched.EndTimingRegion());
		FArdaCudaSequence Empty(mDevice);
		ASSERT_TRUE(Empty.BeginTimingRegion(Query, 1));
		ASSERT_TRUE(Empty.EndTimingRegion());
		ASSERT_TRUE(Empty.Dispatch());
		EXPECT_FALSE(Query->Poll(true).mValue.mbReady);

		auto Buffer = mDevice->CreateBuffer(BufferDesc(128));
		ASSERT_TRUE(Buffer);
		eastl::vector<uint32_t> Values(32);
		for (uint32_t I = 0; I < Values.size(); ++I)
		{
			Values[I] = I * 3;
		}
		auto Upload = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Upload);
		ASSERT_TRUE(Upload.mValue->Open());
		ASSERT_TRUE(Upload.mValue->WriteBuffer(*Buffer.mValue, Values.data(), 128));
		ASSERT_TRUE(Upload.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Upload.mValue));
		FArdaAddOperand Operand(mDevice);
		FArdaAddParameters P;
		P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
		P.mCount = 32;
		P.mBias = 1;
		for (const auto Mode : {EArdaCudaGraphMode::Disabled, EArdaCudaGraphMode::Require})
		{
			SCOPED_TRACE(static_cast<int>(Mode));
			auto Cache = eastl::make_shared<FArdaCudaGraphCache>(Mode);
			FArdaCudaSequence Sequence(mDevice, EArdaRHIQueueType::Graphics, Cache);
			ASSERT_TRUE(Sequence.BeginTimingRegion(Query, 3));
			ASSERT_TRUE(Sequence.Add(Operand, P));
			ASSERT_TRUE(Sequence.EndTimingRegion());
			{
				auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
				ASSERT_TRUE(Commands);
				ASSERT_TRUE(Commands.mValue->Open());
				ASSERT_TRUE(Sequence.DispatchDeferred(*Commands.mValue));
				ASSERT_TRUE(Commands.mValue->Close());
				EXPECT_FALSE(Query->Poll(true).mValue.mbReady);
			} // Cancel the sample and any executable containing an unexecuted CiG launch.
			EXPECT_FALSE(Query->Poll(true).mValue.mbReady);
			ASSERT_TRUE(Sequence.Dispatch());
			ASSERT_TRUE(mDevice->WaitForIdle());
			const auto Overwrite = Sequence.Dispatch();
			EXPECT_EQ(Overwrite.mStatus.mCode, EArdaRHIResult::InvalidState);
			auto Timing = Query->Poll(true);
			ASSERT_TRUE(Timing) << Timing.mStatus.mMessage.c_str();
			ASSERT_TRUE(Timing.mValue.mbReady);
			ASSERT_EQ(Timing.mValue.mRegions.size(), 1u);
			EXPECT_EQ(Timing.mValue.mRegions[0].mRegionId, 3u);
			ASSERT_TRUE(Sequence.Dispatch());
			ASSERT_TRUE(mDevice->WaitForIdle());
			EXPECT_TRUE(Query->Poll(true).mValue.mbReady);
			if (Mode == EArdaCudaGraphMode::Require)
			{
				const bool Cig = mDevice->GetCudaCapabilities().mLaunchMode == EArdaCudaLaunchMode::D3D12CiG;
				EXPECT_EQ(Cache->GetStats().mCaptureCount, Cig ? 2u : 1u);
				EXPECT_EQ(Cache->GetStats().mReplayCount, 1u);
				EXPECT_EQ(Cache->GetStats().mCachedVariantCount, 1u);
			}
		}
		auto Readback = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Readback);
		ASSERT_TRUE(Readback.mValue->Open());
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Readback.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
		ASSERT_TRUE(Readback.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Readback.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
		ExpectWords(Bytes, 32, 4); // Only the two accepted submissions per mode executed.
	}

	TEST_P(ArdaCudaGpu, SequenceRejectsLateInvalidStepBeforeAnyLaunch)
	{
		auto Buffer = mDevice->CreateBuffer(BufferDesc(32 * 4));
		ASSERT_TRUE(Buffer);
		FArdaAddOperand Operand(mDevice);
		FArdaAddParameters P;
		P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
		P.mCount = 32;
		P.mBias = 7;
		auto Prepared = Operand.PrepareDispatch(P);
		ASSERT_TRUE(Prepared);
		for (bool bNativeLimitFailure : {false, true})
		{
			const auto Trace = eastl::make_shared<FArdaCudaLaunchTrace>();
			eastl::vector<FArdaCudaDispatch> Steps;
			for (int I = 0; I < 2; ++I)
			{
				auto Step = Prepared.mValue->mDispatch;
				auto Entry = eastl::make_shared<FArdaTracingCudaEntry>();
				Entry->mEntry = Step.mKernels.front().mEntry;
				Entry->mTrace = Trace;
				Entry->mbRejectLimits = I == 1 && bNativeLimitFailure;
				Step.mKernels.front().mEntry = Entry;
				if (I == 1 && !bNativeLimitFailure)
				{
					Step.mBindings.front().mBufferRange.mByteOffset = 9999;
				}
				Steps.push_back(eastl::move(Step));
			}
			auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Cmd);
			ASSERT_TRUE(Cmd.mValue->Open());
			EXPECT_FALSE(Cmd.mValue->DispatchCudaSequence(Steps));
			EXPECT_TRUE(Trace->mStreams.empty());
			EXPECT_TRUE(Cmd.mValue->Close());
			ASSERT_TRUE(mDevice->ExecuteCommandList(Cmd.mValue));
			ASSERT_TRUE(mDevice->WaitForIdle());
			EXPECT_TRUE(Trace->mStreams.empty());
		}
	}

	TEST_P(ArdaCudaGpu, PrecompiledDeferredAndImmediateUseFrozenSingleKernelPlans)
	{
		for (uint32_t Count : {32u, 128u})
		{
			FArdaAddOperand Operand(mDevice);
			Operand.mbPreferFastMath = Count == 128;
			auto Input = mDevice->CreateBuffer(BufferDesc(Count * 4));
			auto Output = mDevice->CreateBuffer(BufferDesc(Count * 4));
			ASSERT_TRUE(Input);
			ASSERT_TRUE(Output);
			FArdaAddParameters P;
			P.mInput.mBuffer = Input.mValue;
			P.mOutput.mBuffer = Output.mValue;
			P.mCount = Count;
			P.mBias = 7;
			auto Plan = Operand.PrepareDispatch(P);
			ASSERT_TRUE(Plan) << Plan.mStatus.mMessage.c_str();
			ASSERT_EQ(Plan.mValue->mDispatch.mKernels.size(), 1u);
			if (mDevice->GetCudaCapabilities().mComputeCapability == 120)
			{
				EXPECT_EQ(Plan.mValue->mDispatch.mKernels.front().mEntry->GetBuildInfo().mbFastMath, Count == 128);
			}
			EXPECT_EQ(Plan.mValue->mSelection.mLaunch.mBlockSize[0], Count == 32 ? 32u : 128u);
			if (Count == 128)
			{
				const FArdaCudaSelectionContext Context{mDevice->GetCudaCapabilities()};
				auto Registry = Operand.GetKernelVariants();
				ASSERT_TRUE(Registry);
				FArdaAddOperand::FVariants Portable, Fast;
				for (const auto& V : Registry.mValue->GetCompatibleVariants(Context.mCapabilities))
				{
					(V.mEntry->GetBuildInfo().mbFastMath ? Fast : Portable).push_back(V);
				}
				Operand.mbPreferFastMath = false;
				EXPECT_FALSE(Operand.SelectKernel(P, Context, Fast));
				Operand.mbPreferFastMath = true;
				EXPECT_TRUE(Operand.SelectKernel(P, Context, Portable));
			}
			P.mBias = 999; // Recorded plan owns a snapshot, not this mutable host object.
			auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Cmd);
			ASSERT_TRUE(Cmd.mValue->Open());
			eastl::vector<uint32_t> Values(Count);
			for (uint32_t I = 0; I < Count; ++I)
			{
				Values[I] = I * 3;
			}
			ASSERT_TRUE(Cmd.mValue->WriteBuffer(*Input.mValue, Values.data(), Values.size() * 4));
			auto Status = Operand.RecordPlan(*Cmd.mValue, Plan.mValue);
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			eastl::vector<uint8_t> Bytes;
			ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*Output.mValue, Bytes));
			ASSERT_TRUE(Cmd.mValue->Close());
			auto Submitted = mDevice->ExecuteCommandList(Cmd.mValue);
			ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
			ASSERT_TRUE(mDevice->WaitForIdle());
			ExpectWords(Bytes, Count, 7);
			EXPECT_FALSE(mDevice->ExecuteCommandList(Cmd.mValue)); // CUDA lists are single-use.
			P.mInput.mBuffer = Output.mValue;
			P.mBias = 11;
			auto Immediate = Operand.Dispatch(P);
			ASSERT_TRUE(Immediate) << Immediate.mStatus.mMessage.c_str();
			EXPECT_GT(Immediate.mValue.mInstance, 0u);
			Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Cmd);
			ASSERT_TRUE(Cmd.mValue->Open());
			ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*Output.mValue, Bytes));
			ASSERT_TRUE(Cmd.mValue->Close());
			ASSERT_TRUE(mDevice->ExecuteCommandList(Cmd.mValue));
			ASSERT_TRUE(mDevice->WaitForIdle());
			ExpectWords(Bytes, Count, 18);
		}
	}

	TEST_P(ArdaCudaGpu, MultipleRecordedDispatchesRetainIndependentParameters)
	{
		constexpr uint32_t Count = 65;
		auto Buffer = mDevice->CreateBuffer(BufferDesc(Count * sizeof(uint32_t)));
		ASSERT_TRUE(Buffer);
		auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Cmd);
		ASSERT_TRUE(Cmd.mValue->Open());
		eastl::vector<uint32_t> Values(Count);
		for (uint32_t I = 0; I < Count; ++I)
		{
			Values[I] = I * 3;
		}
		ASSERT_TRUE(Cmd.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Values.size() * sizeof(uint32_t)));
		{
			FArdaAddOperand Operand(mDevice);
			FArdaAddParameters P;
			P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
			P.mCount = Count;
			for (uint32_t Bias : {7u, 11u})
			{
				P.mBias = Bias;
				auto Recorded = Operand.DispatchDeferred(*Cmd.mValue, P);
				ASSERT_TRUE(Recorded) << Recorded.mMessage.c_str();
			}
			P.mBias = 999;
		} // Recorded entries and arguments must survive their operand and host parameters.
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
		ASSERT_TRUE(Cmd.mValue->Close());
		auto Submitted = mDevice->ExecuteCommandList(Cmd.mValue);
		ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
		Cmd.mValue.Reset();
		Buffer.mValue.Reset();
		ASSERT_TRUE(mDevice->WaitForIdle());
		ExpectWords(Bytes, Count, 18);
	}

	TEST_P(ArdaCudaGpu, ResourceChecksAreRuntimeAndBufferOffsetsPreserveGuards)
	{
		FArdaAddOperand Operand(mDevice);
		auto Buffer = mDevice->CreateBuffer(BufferDesc(80 * 4));
		ASSERT_TRUE(Buffer);
		auto Normal = mDevice->CreateBuffer(BufferDesc(80 * 4, false));
		ASSERT_TRUE(Normal);
		FArdaAddParameters P;
		P.mCount = 32;
		P.mBias = 7;
		EXPECT_FALSE(Operand.PrepareDispatch(P));
		P.mInput = {Normal.mValue, {16, 128}};
		P.mOutput = {Buffer.mValue, {160, 128}};
		EXPECT_EQ(Operand.PrepareDispatch(P).mStatus.mCode, EArdaRHIResult::Unsupported);
		P.mInput = {Buffer.mValue, {17, 128}};
		EXPECT_FALSE(Operand.PrepareDispatch(P));
		P.mInput.mRange = {300, 128};
		EXPECT_FALSE(Operand.PrepareDispatch(P));
		P.mInput.mRange = {16, 128};
		P.mOutput.mRange = {20, 128};
		EXPECT_FALSE(Operand.PrepareDispatch(P));
		P.mOutput.mRange = {160, 128};
		auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Cmd);
		ASSERT_TRUE(Cmd.mValue->Open());
		eastl::vector<uint32_t> Values(80, 0xdeadbeefu);
		for (uint32_t I = 0; I < 32; ++I)
		{
			Values[I + 4] = I * 3;
		}
		ASSERT_TRUE(Cmd.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Values.size() * 4));
		auto S = Operand.DispatchDeferred(*Cmd.mValue, P);
		ASSERT_TRUE(S) << S.mMessage.c_str();
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
		ASSERT_TRUE(Cmd.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Cmd.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
		ASSERT_EQ(Bytes.size(), Values.size() * 4);
		for (uint32_t I = 0; I < Values.size(); ++I)
		{
			uint32_t V;
			std::memcpy(&V, Bytes.data() + I * 4, 4);
			EXPECT_EQ(V, I >= 40 && I < 72 ? (I - 40) * 3 + 7 : Values[I]) << I;
		}
	}

	TEST_P(ArdaCudaGpu, TextureBufferCopiesAcceptOffsetsAndMinimalFinalRow)
	{
		FArdaRHITextureDesc Desc;
		Desc.mWidth = 37;
		Desc.mHeight = 17;
		Desc.mFormat = EArdaRHIFormat::R32UInt;
		Desc.mUsage = EArdaRHITextureUsage::UnorderedAccess | EArdaRHITextureUsage::ShaderResource;
		auto Texture = mDevice->CreateTexture(Desc);
		ASSERT_TRUE(Texture);
		FArdaRHITextureSlice Crop;
		Crop.mX = 3;
		Crop.mY = 2;
		Crop.mWidth = 29;
		Crop.mHeight = 11;
		const FArdaRHITextureBufferLayout Layout{512, 256};
		// The allocation ends immediately after the last copied texel, not its padded row.
		const uint64_t ByteSize = Layout.mByteOffset + uint64_t(Crop.mHeight - 1) * Layout.mRowPitch + Crop.mWidth * 4;
		auto Source = mDevice->CreateBuffer(BufferDesc(ByteSize, false));
		auto Destination = mDevice->CreateBuffer(BufferDesc(ByteSize, false));
		ASSERT_TRUE(Source);
		ASSERT_TRUE(Destination);
		constexpr uint32_t SourceGuard = 0xc001d00du;
		constexpr uint32_t DestinationGuard = 0x7eca0001u;
		eastl::vector<uint32_t> SourceWords(static_cast<size_t>(ByteSize / 4), SourceGuard);
		eastl::vector<uint32_t> Expected(SourceWords.size(), DestinationGuard);
		for (uint32_t Y = 0; Y < Crop.mHeight; ++Y)
		{
			for (uint32_t X = 0; X < Crop.mWidth; ++X)
			{
				const size_t Index = static_cast<size_t>((Layout.mByteOffset + uint64_t(Y) * Layout.mRowPitch) / 4 + X);
				SourceWords[Index] = Expected[Index] = Y * 1000 + X * 3 + 7;
			}
		}
		eastl::vector<uint32_t> InitialDestination(Expected.size(), DestinationGuard);
		FArdaRHIStagingTextureDesc StagingDesc;
		StagingDesc.mTexture = Desc;
		StagingDesc.mCpuAccess = EArdaRHICpuAccess::Read;
		auto TextureReadback = mDevice->CreateStagingTexture(StagingDesc);
		ASSERT_TRUE(TextureReadback);
		auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Cmd);
		EXPECT_EQ(Cmd.mValue->CopyBufferToTexture(*Texture.mValue, Crop, *Source.mValue, Layout).mCode,
		    EArdaRHIResult::InvalidState);
		EXPECT_EQ(Cmd.mValue->CopyTextureToBuffer(*Destination.mValue, Layout, *Texture.mValue, Crop).mCode,
		    EArdaRHIResult::InvalidState);
		ASSERT_TRUE(Cmd.mValue->Open());
		ASSERT_TRUE(Cmd.mValue->ClearTextureUInt(*Texture.mValue, {}, 13));
		ASSERT_TRUE(Cmd.mValue->WriteBuffer(*Source.mValue, SourceWords.data(), ByteSize));
		ASSERT_TRUE(Cmd.mValue->WriteBuffer(*Destination.mValue, InitialDestination.data(), ByteSize));
		auto Copied = Cmd.mValue->CopyBufferToTexture(*Texture.mValue, Crop, *Source.mValue, Layout);
		ASSERT_TRUE(Copied) << Copied.mMessage.c_str();
		Copied = Cmd.mValue->CopyTextureToBuffer(*Destination.mValue, Layout, *Texture.mValue, Crop);
		ASSERT_TRUE(Copied) << Copied.mMessage.c_str();
		ASSERT_TRUE(Cmd.mValue->CopyTextureToStaging(*TextureReadback.mValue, {}, *Texture.mValue, {}));
		eastl::vector<uint8_t> ResultBytes;
		eastl::vector<uint8_t> SourceBytes;
		ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*Destination.mValue, ResultBytes));
		ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*Source.mValue, SourceBytes));
		ASSERT_TRUE(Cmd.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Cmd.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
		ASSERT_EQ(ResultBytes.size(), ByteSize);
		ASSERT_EQ(SourceBytes.size(), ByteSize);
		for (size_t Index = 0; Index < Expected.size(); ++Index)
		{
			uint32_t ResultWord = 0;
			uint32_t SourceWord = 0;
			std::memcpy(&ResultWord, ResultBytes.data() + Index * 4, 4);
			std::memcpy(&SourceWord, SourceBytes.data() + Index * 4, 4);
			EXPECT_EQ(ResultWord, Expected[Index]) << "destination word=" << Index;
			EXPECT_EQ(SourceWord, SourceWords[Index]) << "source word=" << Index;
		}
		auto Mapped = mDevice->MapStagingTexture(TextureReadback.mValue, {}, EArdaRHICpuAccess::Read);
		ASSERT_TRUE(Mapped);
		for (uint32_t Y = 0; Y < Desc.mHeight; ++Y)
		{
			for (uint32_t X = 0; X < Desc.mWidth; ++X)
			{
				uint32_t Value = 0;
				std::memcpy(&Value,
				    static_cast<const uint8_t*>(Mapped.mValue.mData) + Y * Mapped.mValue.mRowPitch + X * 4,
				    4);
				const bool Changed =
				    X >= Crop.mX && X < Crop.mX + Crop.mWidth && Y >= Crop.mY && Y < Crop.mY + Crop.mHeight;
				EXPECT_EQ(Value, Changed ? (Y - Crop.mY) * 1000 + (X - Crop.mX) * 3 + 7 : 13u)
				    << "texture xy=" << X << ',' << Y;
			}
		}
		ASSERT_TRUE(mDevice->UnmapStagingTexture(TextureReadback.mValue));
	}

	TEST_P(ArdaCudaGpu, TextureBufferSequencePreservesRegionsMipsAndLayers)
	{
		for (const auto Dimension : {EArdaRHITextureDimension::Texture2D,
		         EArdaRHITextureDimension::Texture2DArray,
		         EArdaRHITextureDimension::Texture3D})
		{
			SCOPED_TRACE(unsigned(Dimension));
			FArdaRHITextureDesc D;
			D.mDimension = Dimension;
			D.mWidth = 75;
			D.mHeight = 31;
			D.mDepth = Dimension == EArdaRHITextureDimension::Texture3D ? 9 : 1;
			D.mArraySize = Dimension == EArdaRHITextureDimension::Texture2DArray ? 2 : 1;
			D.mMipLevels = 2;
			D.mFormat = EArdaRHIFormat::R32UInt;
			D.mUsage = EArdaRHITextureUsage::UnorderedAccess | EArdaRHITextureUsage::ShaderResource;
			auto Texture = mDevice->CreateTexture(D); // No CUDA surface import is requested.
			ASSERT_TRUE(Texture) << Texture.mStatus.mMessage.c_str();
			FArdaRHITextureSlice Region;
			Region.mMipLevel = 1;
			Region.mArraySlice = D.mArraySize - 1;
			Region.mX = 3;
			Region.mY = 2;
			Region.mZ = D.mDepth > 1 ? 1 : 0;
			Region.mWidth = 29;
			Region.mHeight = 11;
			Region.mDepth = D.mDepth > 1 ? 2 : 1;
			auto Transfer = CreateArdaCudaTextureBuffer(*mDevice, D, Region);
			ASSERT_TRUE(Transfer) << Transfer.mStatus.mMessage.c_str();
			EXPECT_EQ(Transfer.mValue.mLayout.mRowPitch, 256u);
			EXPECT_EQ(Transfer.mValue.mExtent.mWidth, 29u);
			FArdaRHIStagingTextureDesc Staging;
			Staging.mTexture = D;
			Staging.mCpuAccess = EArdaRHICpuAccess::Read;
			auto Readback = mDevice->CreateStagingTexture(Staging);
			ASSERT_TRUE(Readback);
			auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Cmd);
			ASSERT_TRUE(Cmd.mValue->Open());
			ASSERT_TRUE(Cmd.mValue->ClearTextureUInt(*Texture.mValue, {}, 15));
			const auto Words = static_cast<uint32_t>(Transfer.mValue.mBuffer->GetDesc().mByteSize / 4);
			eastl::vector<uint32_t> Initial(Words, 0); // Initialize padding before the linear test kernels read it.
			ASSERT_TRUE(Cmd.mValue->WriteBuffer(*Transfer.mValue.mBuffer, Initial.data(), Words * 4));
			auto Copied = Cmd.mValue->CopyTextureToBuffer(*Transfer.mValue.mBuffer,
			    Transfer.mValue.mLayout,
			    *Texture.mValue,
			    Region);
			ASSERT_TRUE(Copied) << Copied.mMessage.c_str();
			{
				FArdaAddOperand Operand(mDevice);
				FArdaCudaSequence Sequence(mDevice);
				FArdaAddParameters P;
				P.mInput.mBuffer = P.mOutput.mBuffer = Transfer.mValue.mBuffer;
				P.mCount = Words;
				P.mBias = 17;
				ASSERT_TRUE(Sequence.Add(Operand, P));
				P.mBias = 23;
				ASSERT_TRUE(Sequence.Add(Operand, P));
				auto Status = Sequence.DispatchDeferred(*Cmd.mValue);
				ASSERT_TRUE(Status) << Status.mMessage.c_str();
			}
			Copied = Cmd.mValue->CopyBufferToTexture(*Texture.mValue,
			    Region,
			    *Transfer.mValue.mBuffer,
			    Transfer.mValue.mLayout);
			ASSERT_TRUE(Copied) << Copied.mMessage.c_str();
			for (uint32_t Layer = 0; Layer < D.mArraySize; ++Layer)
			{
				for (uint32_t Mip = 0; Mip < D.mMipLevels; ++Mip)
				{
					FArdaRHITextureSlice Slice;
					Slice.mMipLevel = Mip;
					Slice.mArraySlice = Layer;
					ASSERT_TRUE(Cmd.mValue->CopyTextureToStaging(*Readback.mValue, Slice, *Texture.mValue, Slice));
				}
			}
			ASSERT_TRUE(Cmd.mValue->Close());
			Transfer.mValue.mBuffer.Reset();
			Texture.mValue.Reset(); // Copy commands and the CUDA batch must retain both allocations.
			auto Submitted = mDevice->ExecuteCommandList(Cmd.mValue);
			ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
			Cmd.mValue.Reset();
			ASSERT_TRUE(mDevice->WaitForIdle());
			for (uint32_t Layer = 0; Layer < D.mArraySize; ++Layer)
			{
				for (uint32_t Mip = 0; Mip < D.mMipLevels; ++Mip)
				{
					FArdaRHITextureSlice Slice;
					Slice.mMipLevel = Mip;
					Slice.mArraySlice = Layer;
					auto Mapped = mDevice->MapStagingTexture(Readback.mValue, Slice, EArdaRHICpuAccess::Read);
					ASSERT_TRUE(Mapped);
					for (uint32_t Z = 0; Z < GetArdaRHITextureMipExtent(D.mDepth, Mip); ++Z)
					{
						for (uint32_t Y = 0; Y < GetArdaRHITextureMipExtent(D.mHeight, Mip); ++Y)
						{
							const auto* Row =
							    reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(Mapped.mValue.mData) +
							        Z * Mapped.mValue.mDepthPitch + Y * Mapped.mValue.mRowPitch);
							for (uint32_t X = 0; X < GetArdaRHITextureMipExtent(D.mWidth, Mip); ++X)
							{
								const bool Changed = Mip == Region.mMipLevel && Layer == Region.mArraySlice &&
								    X >= Region.mX && X < Region.mX + Region.mWidth && Y >= Region.mY &&
								    Y < Region.mY + Region.mHeight && Z >= Region.mZ && Z < Region.mZ + Region.mDepth;
								EXPECT_EQ(Row[X], Changed ? 55u : 15u)
								    << "mip=" << Mip << " layer=" << Layer << " xyz=" << X << ',' << Y << ',' << Z;
							}
						}
					}
					ASSERT_TRUE(mDevice->UnmapStagingTexture(Readback.mValue));
				}
			}
		}
	}

	TEST_P(ArdaCudaGpu, SurfaceConversionAndMipLifetime)
	{
		const auto Caps = mDevice->GetCudaCapabilities();
		if (!Caps.mbSurfaceAccess)
		{
			GTEST_SKIP() << Caps.mSurfaceUnavailableReason.c_str();
		}
		FArdaRHITextureDesc D;
		D.mbCudaInterop = true;
		D.mWidth = 54;
		D.mHeight = 38;
		D.mMipLevels = 2;
		D.mFormat = EArdaRHIFormat::R32UInt;
		D.mUsage = EArdaRHITextureUsage::ShaderResource | EArdaRHITextureUsage::UnorderedAccess;
		auto Texture = mDevice->CreateTexture(D);
		ASSERT_TRUE(Texture) << Texture.mStatus.mMessage.c_str();
		FArdaRHIStagingTextureDesc Staging;
		Staging.mTexture = D;
		Staging.mTexture.mbCudaInterop = false;
		Staging.mCpuAccess = EArdaRHICpuAccess::Read;
		auto Readback = mDevice->CreateStagingTexture(Staging);
		ASSERT_TRUE(Readback);
		auto Operand = eastl::make_shared<FArdaSurfaceOperand>(mDevice);
		FArdaSurfaceParameters P;
		P.mSurface.mTexture = Texture.mValue;
		P.mSurface.mRange.mBaseMipLevel = 1;
		P.mSurface.mRange.mMipLevelCount = 1;
		P.mWidth = 27;
		P.mHeight = 19;
		P.mValue = 101;
		P.mSurface.mRange.mMipLevelCount = 99;
		EXPECT_FALSE(Operand->PrepareDispatch(P));
		P.mSurface.mRange.mMipLevelCount = 1;
		auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Cmd);
		ASSERT_TRUE(Cmd.mValue->Open());
		auto S = Operand->DispatchDeferred(*Cmd.mValue, P);
		ASSERT_TRUE(S) << S.mMessage.c_str();
		FArdaRHITextureSlice Slice;
		Slice.mMipLevel = 1;
		ASSERT_TRUE(Cmd.mValue->CopyTextureToStaging(*Readback.mValue, Slice, *Texture.mValue, Slice));
		ASSERT_TRUE(Cmd.mValue->Close());
		P.mSurface.mTexture.Reset();
		Texture.mValue.Reset();
		Operand.reset();
		auto Submitted = mDevice->ExecuteCommandList(Cmd.mValue);
		ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
		Cmd.mValue.Reset();
		ASSERT_TRUE(mDevice->WaitForIdle());
		auto Mapped = mDevice->MapStagingTexture(Readback.mValue, Slice, EArdaRHICpuAccess::Read);
		ASSERT_TRUE(Mapped);
		for (uint32_t Y = 0; Y < 19; ++Y)
		{
			const auto* Row = reinterpret_cast<const uint32_t*>(
			    static_cast<const uint8_t*>(Mapped.mValue.mData) + Y * Mapped.mValue.mRowPitch);
			for (uint32_t X = 0; X < 27; ++X)
			{
				EXPECT_EQ(Row[X], 101u);
			}
		}
		mDevice->UnmapStagingTexture(Readback.mValue);
	}

	TEST_P(ArdaCudaGpu, SequenceMixesBufferAndSurfaceSchemas)
	{
		const auto Caps = mDevice->GetCudaCapabilities();
		if (!Caps.mbSurfaceAccess)
		{
			GTEST_SKIP() << Caps.mSurfaceUnavailableReason.c_str();
		}
		FArdaRHITextureDesc D;
		D.mbCudaInterop = true;
		D.mWidth = 8;
		D.mHeight = 8;
		D.mFormat = EArdaRHIFormat::R32UInt;
		D.mUsage = EArdaRHITextureUsage::UnorderedAccess;
		auto Texture = mDevice->CreateTexture(D);
		auto Buffer = mDevice->CreateBuffer(BufferDesc(4));
		ASSERT_TRUE(Texture);
		ASSERT_TRUE(Buffer);
		FArdaRHIStagingTextureDesc Staging;
		Staging.mTexture = D;
		Staging.mTexture.mbCudaInterop = false;
		Staging.mCpuAccess = EArdaRHICpuAccess::Read;
		auto Readback = mDevice->CreateStagingTexture(Staging);
		ASSERT_TRUE(Readback);
		auto Cmd = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Cmd);
		ASSERT_TRUE(Cmd.mValue->Open());
		uint32_t Initial = 42;
		ASSERT_TRUE(Cmd.mValue->WriteBuffer(*Buffer.mValue, &Initial, sizeof(Initial)));
		FArdaCudaSequence Sequence(mDevice);
		FArdaSurfaceOperand Surface(mDevice);
		FArdaSurfaceParameters P;
		P.mSurface.mTexture = Texture.mValue;
		P.mWidth = P.mHeight = 8;
		P.mValue = 17;
		ASSERT_TRUE(Sequence.Add(Surface, P));
		FArdaAddOperand Add(mDevice);
		FArdaAddParameters B;
		B.mInput.mBuffer = B.mOutput.mBuffer = Buffer.mValue;
		B.mCount = 1;
		B.mBias = 9;
		ASSERT_TRUE(Sequence.Add(Add, B));
		P.mValue = 101;
		ASSERT_TRUE(Sequence.Add(Surface, P));
		auto Status = Sequence.DispatchDeferred(*Cmd.mValue);
		ASSERT_TRUE(Status) << Status.mMessage.c_str();
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Cmd.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
		ASSERT_TRUE(Cmd.mValue->CopyTextureToStaging(*Readback.mValue, {}, *Texture.mValue, {}));
		ASSERT_TRUE(Cmd.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Cmd.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
		ASSERT_EQ(Bytes.size(), 4u);
		uint32_t Value;
		std::memcpy(&Value, Bytes.data(), 4);
		EXPECT_EQ(Value, 51u);
		auto Mapped = mDevice->MapStagingTexture(Readback.mValue, {}, EArdaRHICpuAccess::Read);
		ASSERT_TRUE(Mapped);
		for (uint32_t Y = 0; Y < 8; ++Y)
		{
			const auto* Row = reinterpret_cast<const uint32_t*>(
			    static_cast<const uint8_t*>(Mapped.mValue.mData) + Y * Mapped.mValue.mRowPitch);
			for (uint32_t X = 0; X < 8; ++X)
			{
				EXPECT_EQ(Row[X], 101u);
			}
		}
		mDevice->UnmapStagingTexture(Readback.mValue);
	}

	struct FScopedPersistentCudaRegistration
	{
		eastl::string mName;

		~FScopedPersistentCudaRegistration()
		{
			EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister(mName));
		}
	};

	FArdaGraphUploadParameters CudaGraphUpload(FArdaDependencyResourceHandle Destination, uint32_t Count)
	{
		FArdaGraphUploadParameters Upload;
		Upload.mDestination = Destination;
		Upload.mBytes.resize(Count * 4);
		for (uint32_t I = 0; I < Count; ++I)
		{
			const uint32_t Value = I * 3;
			std::memcpy(Upload.mBytes.data() + I * 4, &Value, 4);
		}
		return Upload;
	}

	TEST_P(ArdaCudaGpu, PersistentGraphDerivesCudaDependenciesBeforeExecution)
	{
		ASSERT_TRUE(RegisterArdaBuiltinNodes());
		auto Operand = eastl::make_shared<FArdaAddOperand>(mDevice);
		const eastl::string Definition = eastl::string("persistent.derived.") + GetParam();
		ASSERT_TRUE(RegisterArdaCudaOperandNode(Definition, Operand));
		FScopedPersistentCudaRegistration Registration{Definition};
		FArdaDependencyGraph Graph(mDevice);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		auto Input = Graph.CreateBuffer("input", BufferDesc(128 * 4));
		auto Middle = Graph.CreateBuffer("middle", BufferDesc(128 * 4));
		auto Output = Graph.CreateBuffer("output", BufferDesc(128 * 4));
		ASSERT_TRUE(Input);
		ASSERT_TRUE(Middle);
		ASSERT_TRUE(Output);
		ASSERT_TRUE(Graph.AttachOrFind("upload", "arda.upload", CudaGraphUpload(Input.mValue, 128)));
		TArdaDependencyCudaParameters<FArdaAddParameters> P;
		P.mInput.mResource = Input.mValue;
		P.mOutput.mResource = Middle.mValue;
		P.mCount = 128;
		P.mBias = 7;
		auto First = Graph.AttachOrFind("add seven", Definition, P);
		ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
		P.mInput.mResource = Middle.mValue;
		P.mOutput.mResource = Output.mValue;
		P.mBias = 11;
		auto Second = Graph.AttachOrFind("add eleven", Definition, P);
		ASSERT_TRUE(Second) << Second.mStatus.mMessage.c_str();
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(
		    Graph.AttachOrFind("readback", "arda.readback", FArdaGraphReadbackParameters{Output.mValue, Bytes}));
		auto Compiled = Graph.EndGraphEdit();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		ASSERT_EQ(Graph.GetCompileResult().mCudaBatches.size(), 1u);
		EXPECT_EQ(Graph.GetCompileResult().mCudaBatches.front(),
		    (eastl::vector<FArdaGraphNodeHandle>{First.mValue, Second.mValue}));
		const auto Result = Graph.Execute();
		ASSERT_TRUE(Result.mStatus) << Result.mStatus.mMessage.c_str();
		EXPECT_GT(Result.mSubmittedCommandListCount, 0u);
		ExpectWords(*Bytes, 128, 18);
	}

	TEST_P(ArdaCudaGpu, PersistentGraphCoalescesCudaNodesAndCullsUnusedWork)
	{
		ASSERT_TRUE(RegisterArdaBuiltinNodes());
		auto Operand = eastl::make_shared<FArdaAddOperand>(mDevice);
		const eastl::string Definition = eastl::string("persistent.coalesced.") + GetParam();
		ASSERT_TRUE(RegisterArdaCudaOperandNode(Definition, Operand));
		FScopedPersistentCudaRegistration Registration{Definition};
		FArdaDependencyGraph Graph(mDevice);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		auto Input = Graph.CreateBuffer("input", BufferDesc(128 * 4));
		auto Scratch = Graph.CreateBuffer("scratch", BufferDesc(128 * 4));
		auto Output = Graph.CreateBuffer("output", BufferDesc(128 * 4));
		auto DeadOutput = Graph.CreateBuffer("unused", BufferDesc(128 * 4));
		ASSERT_TRUE(Input);
		ASSERT_TRUE(Scratch);
		ASSERT_TRUE(Output);
		ASSERT_TRUE(DeadOutput);
		ASSERT_TRUE(Graph.AttachOrFind("upload", "arda.upload", CudaGraphUpload(Input.mValue, 128)));
		TArdaDependencyCudaParameters<FArdaAddParameters> P;
		P.mInput.mResource = Input.mValue;
		P.mOutput.mResource = Scratch.mValue;
		P.mCount = 128;
		P.mBias = 7;
		auto First = Graph.AttachOrFind("first CUDA kernel", Definition, P);
		ASSERT_TRUE(First);
		P.mInput.mResource = Scratch.mValue;
		P.mOutput.mResource = Output.mValue;
		P.mBias = 11;
		auto Second = Graph.AttachOrFind("second CUDA kernel", Definition, P);
		ASSERT_TRUE(Second);
		P.mBias = 999; // Mutating the source must not change either stored node snapshot.
		P.mInput.mResource = Input.mValue;
		P.mOutput.mResource = DeadOutput.mValue;
		auto Dead = Graph.AttachOrFind("unused CUDA work", Definition, P);
		ASSERT_TRUE(Dead);
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(
		    Graph.AttachOrFind("readback", "arda.readback", FArdaGraphReadbackParameters{Output.mValue, Bytes}));
		auto Compiled = Graph.EndGraphEdit();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		ASSERT_EQ(Graph.GetCompileResult().mCudaBatches.size(), 1u);
		EXPECT_EQ(Graph.GetCompileResult().mCudaBatches.front(),
		    (eastl::vector<FArdaGraphNodeHandle>{First.mValue, Second.mValue}));
		EXPECT_NE(eastl::find(Graph.GetCompileResult().mCulledNodes.begin(),
		              Graph.GetCompileResult().mCulledNodes.end(),
		              Dead.mValue),
		    Graph.GetCompileResult().mCulledNodes.end());
		Operand.reset();
		for (uint32_t Frame = 0; Frame < 2; ++Frame)
		{
			const auto Result = Graph.Execute();
			ASSERT_TRUE(Result.mStatus) << Result.mStatus.mMessage.c_str();
			ExpectWords(*Bytes, 128, 18);
		}
	}

	TEST_P(ArdaCudaGpu, PersistentGraphTextureCudaNodesPreserveCropAndPadding)
	{
		FArdaRHITextureDesc Desc;
		Desc.mWidth = 75;
		Desc.mHeight = 31;
		Desc.mFormat = EArdaRHIFormat::R32UInt;
		Desc.mUsage = EArdaRHITextureUsage::UnorderedAccess | EArdaRHITextureUsage::ShaderResource;
		Desc.mDebugName = "Persistent texture transferred through CUDA buffers";
		auto NativeTexture = mDevice->CreateTexture(Desc);
		auto NativeOutputTexture = mDevice->CreateTexture(Desc);
		ASSERT_TRUE(NativeOutputTexture);
		ASSERT_TRUE(NativeTexture) << NativeTexture.mStatus.mMessage.c_str();
		FArdaRHITextureSlice Crop;
		Crop.mX = 3;
		Crop.mY = 2;
		Crop.mWidth = 29;
		Crop.mHeight = 11;
		auto Transfer = CreateArdaCudaTextureBuffer(*mDevice, Desc, Crop);
		auto Readback = CreateArdaCudaTextureBuffer(*mDevice, Desc);
		ASSERT_TRUE(Transfer) << Transfer.mStatus.mMessage.c_str();
		ASSERT_TRUE(Readback) << Readback.mStatus.mMessage.c_str();
		const auto TransferLayout = Transfer.mValue.mLayout;
		const auto ReadbackLayout = Readback.mValue.mLayout;
		const auto TransferWords = static_cast<uint32_t>(Transfer.mValue.mBuffer->GetDesc().mByteSize / 4);
		const auto ReadbackWords = static_cast<uint32_t>(Readback.mValue.mBuffer->GetDesc().mByteSize / 4);
		constexpr uint32_t PaddingSentinel = 0xabad1deau;
		eastl::vector<uint32_t> InitialTransfer(TransferWords, 0);
		eastl::vector<uint32_t> InitialReadback(ReadbackWords, PaddingSentinel);
		auto Initialize = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Initialize);
		ASSERT_TRUE(Initialize.mValue->Open());
		ASSERT_TRUE(Initialize.mValue->ClearTextureUInt(*NativeTexture.mValue, {}, 15));
		ASSERT_TRUE(Initialize.mValue->ClearTextureUInt(*NativeOutputTexture.mValue, {}, 15));
		ASSERT_TRUE(Initialize.mValue->SetTextureState(*NativeOutputTexture.mValue, {}, EArdaRHIResourceState::Common));
		ASSERT_TRUE(Initialize.mValue->SetTextureState(*NativeTexture.mValue, {}, EArdaRHIResourceState::Common));
		// The add kernels process every uint, including padding; initialize it before they read it.
		ASSERT_TRUE(
		    Initialize.mValue->WriteBuffer(*Transfer.mValue.mBuffer, InitialTransfer.data(), TransferWords * 4));
		ASSERT_TRUE(
		    Initialize.mValue->WriteBuffer(*Readback.mValue.mBuffer, InitialReadback.data(), ReadbackWords * 4));
		ASSERT_TRUE(Initialize.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Initialize.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
		Initialize.mValue.Reset();

		ASSERT_TRUE(RegisterArdaBuiltinNodes());
		const eastl::string Definition = eastl::string("persistent.texture.") + GetParam();
		ASSERT_TRUE(RegisterArdaCudaOperandNode(Definition, eastl::make_shared<FArdaAddOperand>(mDevice)));
		FScopedPersistentCudaRegistration Registration{Definition};
		FArdaDependencyGraph Graph(mDevice);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		auto Texture = Graph.ImportTexture("source texture", NativeTexture.mValue);
		auto UpdatedTexture = Graph.ImportTexture("updated texture", NativeOutputTexture.mValue);
		auto Intermediate = Graph.ImportBuffer("download", Transfer.mValue.mBuffer);
		auto FirstOutput = Graph.CreateBuffer("CUDA intermediate", Transfer.mValue.mBuffer->GetDesc());
		auto SecondOutput = Graph.CreateBuffer("CUDA result", Transfer.mValue.mBuffer->GetDesc());
		auto Output = Graph.ImportBuffer("padded readback", Readback.mValue.mBuffer);
		ASSERT_TRUE(Texture);
		ASSERT_TRUE(UpdatedTexture);
		ASSERT_TRUE(Intermediate);
		ASSERT_TRUE(FirstOutput);
		ASSERT_TRUE(SecondOutput);
		ASSERT_TRUE(Output);
		auto Download = AttachArdaTextureToBuffer(Graph,
		    "download crop",
		    Texture.mValue,
		    Crop,
		    Intermediate.mValue,
		    TransferLayout);
		ASSERT_TRUE(Download) << Download.mStatus.mMessage.c_str();
		TArdaDependencyCudaParameters<FArdaAddParameters> P;
		P.mInput.mResource = Intermediate.mValue;
		P.mOutput.mResource = FirstOutput.mValue;
		P.mCount = TransferWords;
		P.mBias = 17;
		auto First = Graph.AttachOrFind("add seventeen", Definition, P);
		ASSERT_TRUE(First);
		P.mInput.mResource = FirstOutput.mValue;
		P.mOutput.mResource = SecondOutput.mValue;
		P.mBias = 23;
		auto Second = Graph.AttachOrFind("add twenty-three", Definition, P);
		ASSERT_TRUE(Second);
		auto Upload = AttachArdaBufferToTexture(Graph,
		    "upload crop",
		    UpdatedTexture.mValue,
		    Crop,
		    SecondOutput.mValue,
		    TransferLayout);
		ASSERT_TRUE(Upload) << Upload.mStatus.mMessage.c_str();
		auto CopyForReadback = AttachArdaTextureToBuffer(Graph,
		    "copy for readback",
		    UpdatedTexture.mValue,
		    {},
		    Output.mValue,
		    ReadbackLayout);
		ASSERT_TRUE(CopyForReadback) << CopyForReadback.mStatus.mMessage.c_str();
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(
		    Graph.AttachOrFind("readback", "arda.readback", FArdaGraphReadbackParameters{Output.mValue, Bytes}));
		auto Compiled = Graph.EndGraphEdit();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		ASSERT_EQ(Graph.GetCompileResult().mCudaBatches.size(), 1u);
		EXPECT_EQ(Graph.GetCompileResult().mCudaBatches.front(),
		    (eastl::vector<FArdaGraphNodeHandle>{First.mValue, Second.mValue}));
		NativeTexture.mValue.Reset();
		NativeOutputTexture.mValue.Reset();
		Transfer.mValue.mBuffer.Reset();
		Readback.mValue.mBuffer.Reset();
		for (uint32_t Frame = 0; Frame < 2; ++Frame)
		{
			const auto Result = Graph.Execute();
			ASSERT_TRUE(Result.mStatus) << Result.mStatus.mMessage.c_str();
			ASSERT_EQ(Bytes->size(), ReadbackWords * 4u);
			for (uint32_t Y = 0; Y < Desc.mHeight; ++Y)
			{
				for (uint32_t X = 0; X < ReadbackLayout.mRowPitch / 4; ++X)
				{
					uint32_t Value = 0;
					std::memcpy(&Value, Bytes->data() + Y * ReadbackLayout.mRowPitch + X * 4, 4);
					const bool Changed =
					    X >= Crop.mX && X < Crop.mX + Crop.mWidth && Y >= Crop.mY && Y < Crop.mY + Crop.mHeight;
					const uint32_t Expected = X >= Desc.mWidth ? PaddingSentinel : Changed ? 55u : 15u;
					EXPECT_EQ(Value, Expected) << "xy=" << X << ',' << Y;
				}
			}
		}
	}

	TEST_P(ArdaCudaGpu, PersistentGraphRejectsForeignResourcesAndLatePreparationFailure)
	{
		ASSERT_TRUE(RegisterArdaBuiltinNodes());
		const eastl::string Definition = eastl::string("persistent.invalid.") + GetParam();
		ASSERT_TRUE(RegisterArdaCudaOperandNode(Definition, eastl::make_shared<FArdaAddOperand>(mDevice)));
		FScopedPersistentCudaRegistration Registration{Definition};
		for (const bool Shared : {true, false})
		{
			FArdaDependencyGraph Graph(mDevice), Other(mDevice);
			ASSERT_TRUE(Graph.BeginGraphEdit());
			ASSERT_TRUE(Other.BeginGraphEdit());
			auto NativeInput = mDevice->CreateBuffer(BufferDesc(128 * 4, Shared));
			ASSERT_TRUE(NativeInput);
			auto Input = Graph.ImportBuffer("input", NativeInput.mValue);
			auto Middle = Graph.CreateBuffer("middle", BufferDesc(128 * 4));
			auto Output = Graph.CreateBuffer("output", BufferDesc(128 * 4));
			auto Foreign = Other.CreateBuffer("foreign", BufferDesc(128 * 4));
			ASSERT_TRUE(Input);
			ASSERT_TRUE(Middle);
			ASSERT_TRUE(Output);
			ASSERT_TRUE(Foreign);
			TArdaDependencyCudaParameters<FArdaAddParameters> P;
			P.mInput.mResource = Foreign.mValue;
			P.mOutput.mResource = Middle.mValue;
			P.mCount = 128;
			P.mBias = 7;
			EXPECT_FALSE(Graph.AttachOrFind("foreign resource", Definition, P));
			P.mInput.mResource = Input.mValue;
			ASSERT_TRUE(Graph.AttachOrFind("first CUDA node", Definition, P));
			P.mInput.mResource = Middle.mValue;
			P.mOutput.mResource = Output.mValue;
			P.mCount = Shared
			    ? 127
			    : 128; // Selection fails after the first plan prepares, or the first input is not shareable.
			ASSERT_TRUE(Graph.AttachOrFind("invalid CUDA node", Definition, P));
			auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
			ASSERT_TRUE(
			    Graph.AttachOrFind("readback", "arda.readback", FArdaGraphReadbackParameters{Output.mValue, Bytes}));
			auto Compiled = Graph.EndGraphEdit();
			ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
			const auto Result = Graph.Execute();
			EXPECT_FALSE(Result.mStatus);
			EXPECT_EQ(Result.mSubmittedCommandListCount, 0u);
			EXPECT_TRUE(Bytes->empty());
		}
	}

	TEST_P(ArdaCudaGpu, PersistentInductorCompilesReorderedCudaNodesAndReusesFrameStorage)
	{
		ASSERT_TRUE(RegisterArdaBuiltinNodes());
		const eastl::string Definition = eastl::string("inductor.add.") + GetParam();
		ASSERT_TRUE(RegisterArdaCudaOperandNode(Definition, eastl::make_shared<FArdaAddOperand>(mDevice)));

		struct FRegistrationScope
		{
			eastl::string mName;

			~FRegistrationScope()
			{
				EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister(mName));
			}
		} RegistrationScope{Definition};

		constexpr uint32_t Count = 128;
		auto Desc = BufferDesc(Count * 4);
		auto Requirements = mDevice->QueryBufferMemoryRequirements(Desc);
		ASSERT_TRUE(Requirements) << Requirements.mStatus.mMessage.c_str();
		FArdaDependencyGraph Graph(mDevice);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mObjective = EArdaInductorObjective::Memory;
		Options.mCudaGraphMode = EArdaCudaGraphMode::Require;
		Options.mbEnableGpuTiming = true;
		Options.mGpuTimingSampleInterval = 2;
		Options.mMaxVramBytes = Requirements.mValue.mSize * 2;
		ASSERT_TRUE(Graph.SetOptions(Options));
		auto Input = Graph.CreateBuffer("input", Desc);
		auto Middle = Graph.CreateBuffer("middle", Desc);
		auto Output = Graph.CreateBuffer("output", Desc);
		ASSERT_TRUE(Input);
		ASSERT_TRUE(Middle);
		ASSERT_TRUE(Output);
		auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
		ASSERT_TRUE(
		    Graph.AttachOrFind("readback", "arda.readback", FArdaGraphReadbackParameters{Output.mValue, Bytes}));
		TArdaDependencyCudaParameters<FArdaAddParameters> P;
		P.mInput.mResource = Middle.mValue;
		P.mOutput.mResource = Output.mValue;
		P.mCount = Count;
		P.mBias = 11;
		auto Last = Graph.AttachOrFind("add eleven", Definition, P);
		ASSERT_TRUE(Last);
		P.mInput.mResource = Input.mValue;
		P.mOutput.mResource = Middle.mValue;
		P.mBias = 7;
		auto First = Graph.AttachOrFind("add seven", Definition, P);
		ASSERT_TRUE(First);
		const auto Sync = Graph.AttachOrFind("CUDA dependency join", "arda.sync", FArdaGraphSyncParameters{});
		ASSERT_TRUE(Sync);
		ASSERT_TRUE(Graph.AddDependency(First.mValue, Sync.mValue));
		ASSERT_TRUE(Graph.AddDependency(Sync.mValue, Last.mValue));
		FArdaGraphUploadParameters Upload;
		Upload.mDestination = Input.mValue;
		Upload.mBytes.resize(Count * 4);
		for (uint32_t I = 0; I < Count; ++I)
		{
			const uint32_t Value = I * 3;
			std::memcpy(Upload.mBytes.data() + I * 4, &Value, 4);
		}
		ASSERT_TRUE(Graph.AttachOrFind("upload", "arda.upload", Upload));
		auto Compiled = Graph.EndGraphEdit();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		EXPECT_LE(Graph.GetCompileResult().mAllocatedBytes, Options.mMaxVramBytes);
		EXPECT_GT(Graph.GetCompileResult().mAliasedBytes, 0u);
		ASSERT_EQ(Graph.GetCompileResult().mCudaBatches.size(), 1u);
		EXPECT_EQ(Graph.GetCompileResult().mCudaBatches[0],
		    (eastl::vector<FArdaGraphNodeHandle>{First.mValue, Last.mValue}));
		const auto Revision = Graph.GetCompileResult().mRevision;
		for (uint32_t Frame = 0; Frame < 5; ++Frame)
		{
			auto Result = Graph.Execute();
			ASSERT_TRUE(Result.mStatus) << Result.mStatus.mMessage.c_str();
			ASSERT_TRUE(mDevice->WaitForIdle());
			ExpectWords(*Bytes, Count, 18);
			EXPECT_EQ(Graph.GetCompileResult().mRevision, Revision);
			EXPECT_EQ(Graph.GetCompileResult().mCudaBatches[0],
			    (eastl::vector<FArdaGraphNodeHandle>{First.mValue, Last.mValue}));
			const auto CaptureStats = Graph.GetCudaGraphStats();
			EXPECT_EQ(CaptureStats.mCaptureCount, Frame == 0 ? 1u : 2u);
			EXPECT_EQ(CaptureStats.mReplayCount, Frame < 2 ? 0u : Frame - 1);
			EXPECT_EQ(CaptureStats.mCacheHitCount, Frame < 2 ? 0u : Frame - 1);
			EXPECT_EQ(CaptureStats.mCachedVariantCount, Frame == 0 ? 1u : 2u);
			EXPECT_EQ(CaptureStats.mRebuildCount, Frame == 0 ? 0u : 1u);
			EXPECT_EQ(CaptureStats.mFallbackCount, 0u);

			const auto Profile = Graph.GetTimingProfile();
			const auto History = Graph.GetTimingHistory();
			for (const auto Node : {First.mValue, Last.mValue})
			{
				const auto Sample = eastl::find_if(Profile.begin(),
				    Profile.end(),
				    [&](const auto& Entry)
				    {
					    return eastl::find(Entry.mNodes.begin(), Entry.mNodes.end(), Node) != Entry.mNodes.end();
				    });
				ASSERT_NE(Sample, Profile.end());
				EXPECT_EQ(Sample->mNodes, (eastl::vector<FArdaGraphNodeHandle>{Node}));
				EXPECT_EQ(Sample->mSampleCount, Frame / 2 + 1);
				EXPECT_GT(Sample->mGpuSeconds, 0.0);
				EXPECT_GT(Sample->mLastGpuSeconds, 0.0);
				EXPECT_EQ(Sample->mLastFrameSequence, uint64_t((Frame / 2) * 2 + 1));
				eastl::vector<uint64_t> Sequences;
				for (const auto& Entry : History)
				{
					if (Entry.mNode == Node)
					{
						Sequences.push_back(Entry.mFrameSequence);
						EXPECT_GT(Entry.mGpuSeconds, 0.0);
					}
				}
				ASSERT_EQ(Sequences.size(), Frame / 2 + 1);
				for (size_t I = 0; I < Sequences.size(); ++I)
				{
					EXPECT_EQ(Sequences[I], I * 2 + 1);
				}
			}
		}
		ASSERT_TRUE(Graph.BeginGraphEdit());
		Options.mMaxVramBytes = Requirements.mValue.mSize;
		ASSERT_TRUE(Graph.SetOptions(Options));
		EXPECT_FALSE(Graph.EndGraphEdit());
		ASSERT_TRUE(Graph.CancelGraphEdit());
		auto Result = Graph.Execute();
		ASSERT_TRUE(Result.mStatus) << Result.mStatus.mMessage.c_str();
		ASSERT_TRUE(mDevice->WaitForIdle());
		ExpectWords(*Bytes, Count, 18);
	}

	INSTANTIATE_TEST_SUITE_P(Native,
	    ArdaCudaGpu,
	    testing::Values("d3d12-context", "vulkan-context", "d3d12-cig", "vulkan-cig"));
}
