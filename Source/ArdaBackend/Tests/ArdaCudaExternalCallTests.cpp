#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ArdaTestComputeOperand.h"
#include "ArdaRenderGraph.h"
#include "Compute/ArdaCudaExternalCall.h"
#include <gtest/gtest.h>
#include <cstring>
#include <stdexcept>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace
{
	using namespace arda;

#define ARDA_EXTERNAL_SCALAR_FIELDS(V, B, S) V(uint32_t, mValue)
	ARDA_CUDA_PARAMETER_STRUCT(FExternalScalarParameters, ARDA_EXTERNAL_SCALAR_FIELDS)

	struct FPreparedNoop final : IArdaCudaPreparedCall
	{
		FArdaRHIStatus Enqueue(void*) override
		{
			return {};
		}
	};

	struct FExternalScalarCall : TArdaCudaExternalCall<FExternalScalarParameters>
	{
		mutable uint32_t mValue = 0;
		mutable bool mbAligned = false;
		mutable void* mContext = nullptr;

		TArdaRHIResult<eastl::unique_ptr<IArdaCudaPreparedCall>> PrepareCall(
		    const FArdaCudaExternalCallContext& Context,
		    const FExternalScalarParameters::FCuda& P) const override
		{
			mValue = P.mValue;
			mbAligned = reinterpret_cast<uintptr_t>(&P) % alignof(FExternalScalarParameters::FCuda) == 0;
			mContext = Context.mContext;
			return {eastl::unique_ptr<IArdaCudaPreparedCall>(new FPreparedNoop), {}};
		}
	};

	struct FThrowingSupportCall final : FExternalScalarCall
	{
		FArdaRHIStatus GetSupport(const FArdaCudaCapabilities&) const override
		{
			throw std::runtime_error("external support exception");
		}
	};

	struct FWrongExternalSignature final : IArdaCudaExternalCall
	{
		FArdaCudaKernelSignature GetSignature() const noexcept override
		{
			return {&typeid(uint32_t), sizeof(uint32_t), alignof(uint32_t), true};
		}

		TArdaRHIResult<eastl::unique_ptr<IArdaCudaPreparedCall>> Prepare(const FArdaCudaExternalCallContext&,
		    const void*,
		    size_t) const override
		{
			return {eastl::unique_ptr<IArdaCudaPreparedCall>(new FPreparedNoop), {}};
		}
	};

	struct FExternalTestEntry final : IArdaCudaKernelEntry
	{
		FArdaCudaBuildInfo mBuild{"external-test", "external-test-build", {{120, false}}, false};

		FArdaCudaKernelSignature GetSignature() const noexcept override
		{
			return FExternalScalarParameters::GetCudaMetadata().GetSignature();
		}

		const FArdaCudaBuildInfo& GetBuildInfo() const noexcept override
		{
			return mBuild;
		}

		TArdaRHIResult<FArdaCudaKernelLimits> GetLimits() const override
		{
			return {{256, 0, 32768}, {}};
		}

		FArdaRHIStatus Launch(void*, const FArdaCudaLaunchConfig&, const void*, size_t) const override
		{
			return {};
		}
	};

	FArdaCudaCapabilities ExternalCapabilities(EArdaCudaLaunchMode Mode = EArdaCudaLaunchMode::ContextSwitch)
	{
		FArdaCudaCapabilities C;
		C.mLaunchMode = Mode;
		C.mComputeCapability = 120;
		C.mMaxThreadsPerBlock = 1024;
		C.mMaxSharedMemoryBytes = 49152;
		for (uint32_t I = 0; I < 3; ++I)
		{
			C.mMaxBlockSize[I] = 1024;
			C.mMaxGridSize[I] = 65535;
		}
		return C;
	}

	TEST(ArdaCudaExternalCall, RegistrationRequiresExactSignatureAndFreezesBindings)
	{
		using FRegistry = TArdaCudaKernelRegistry<FExternalScalarParameters, uint32_t>;
		auto Call = eastl::make_shared<FExternalScalarCall>();
		FRegistry Registry;
		ASSERT_TRUE(Registry.Add(BindArdaCudaExternalCall<uint32_t>("scalar", 7, Call)));
		ASSERT_TRUE(Registry.Freeze());
		ASSERT_EQ(Registry.GetVariants().size(), 1u);
		EXPECT_EQ(Registry.GetVariants().front().mExternalCall.get(), Call.get());
		EXPECT_FALSE(Registry.GetVariants().front().mEntry);
		EXPECT_EQ(Registry.GetVariants().front().mPayload, 7u);
		EXPECT_FALSE(Registry.Add(BindArdaCudaExternalCall<uint32_t>("later", 0, Call)));
		FRegistry Wrong;
		EXPECT_FALSE(
		    Wrong.Add(BindArdaCudaExternalCall<uint32_t>("wrong", 0, eastl::make_shared<FWrongExternalSignature>())));
		EXPECT_FALSE(Wrong.Freeze());
		FRegistry Duplicate;
		ASSERT_TRUE(Duplicate.Add(BindArdaCudaExternalCall<uint32_t>("same", 0, Call)));
		EXPECT_FALSE(Duplicate.Add(BindArdaCudaExternalCall<uint32_t>("same", 1, Call)));
		FRegistry Missing;
		EXPECT_FALSE(Missing.Add(BindArdaCudaExternalCall<uint32_t>("missing", 0, {})));
	}

	TEST(ArdaCudaExternalCall, AdmissionUsesLibrarySupportAndExplicitRequirements)
	{
		TArdaCudaKernelRegistry<FExternalScalarParameters, uint32_t> Registry;
		auto Call = eastl::make_shared<FExternalScalarCall>();
		ASSERT_TRUE(Registry.Add(BindArdaCudaExternalCall<uint32_t>("scalar", 0, Call)));
		ASSERT_TRUE(Registry.Freeze());
		EXPECT_EQ(Registry.GetCompatibleVariants(ExternalCapabilities()).size(), 1u);
		EXPECT_EQ(Registry.GetCompatibleVariants(ExternalCapabilities(EArdaCudaLaunchMode::VulkanCiG)).size(), 1u);
		EXPECT_TRUE(Registry.GetCompatibleVariants(ExternalCapabilities(EArdaCudaLaunchMode::D3D12CiG)).empty());
		EXPECT_TRUE(Registry.GetCompatibleVariants(ExternalCapabilities(EArdaCudaLaunchMode::None)).empty());
		TArdaCudaKernelRegistry<FExternalScalarParameters, uint32_t> Restricted;
		FArdaCudaKernelRequirements Requirements;
		Requirements.mMinimumComputeCapability = 130;
		ASSERT_TRUE(Restricted.Add(BindArdaCudaExternalCall<uint32_t>("restricted", 0, Call, Requirements)));
		ASSERT_TRUE(Restricted.Freeze());
		EXPECT_TRUE(Restricted.GetCompatibleVariants(ExternalCapabilities()).empty());
	}

	TEST(ArdaCudaExternalCall, TypedPreparationCopiesUnalignedParametersAndRejectsWrongSize)
	{
		FExternalScalarCall Call;
		FExternalScalarParameters::FCuda P;
		P.mValue = 43;
		eastl::vector<uint8_t> Bytes(sizeof(P) + 1);
		std::memcpy(Bytes.data() + 1, &P, sizeof(P));
		FArdaCudaExternalCallContext Context;
		Context.mContext = reinterpret_cast<void*>(uintptr_t(1));
		Context.mStream = reinterpret_cast<void*>(uintptr_t(2));
		auto Prepared = Call.Prepare(Context, Bytes.data() + 1, sizeof(P));
		ASSERT_TRUE(Prepared);
		ASSERT_TRUE(Prepared.mValue);
		EXPECT_EQ(Call.mValue, 43u);
		EXPECT_TRUE(Call.mbAligned);
		EXPECT_EQ(Call.mContext, Context.mContext);
		EXPECT_FALSE(Call.Prepare(Context, Bytes.data() + 1, sizeof(P) - 1));
		EXPECT_FALSE(Call.Prepare(Context, nullptr, sizeof(P)));
	}

	TEST(ArdaCudaExternalCall, SupportExceptionsFilterVariantsAndReturnDescriptorErrors)
	{
		auto Call = eastl::make_shared<FThrowingSupportCall>();
		TArdaCudaKernelRegistry<FExternalScalarParameters, uint32_t> Registry;
		ASSERT_TRUE(Registry.Add(BindArdaCudaExternalCall<uint32_t>("throwing", 0, Call)));
		ASSERT_TRUE(Registry.Freeze());
		EXPECT_TRUE(Registry.GetCompatibleVariants(ExternalCapabilities()).empty());
		FArdaCudaKernel K;
		K.mExternalCall = Call;
		K.mParameters.resize(sizeof(FExternalScalarParameters::FCuda));
		auto Status = ValidateArdaCudaKernels({K}, 0, ExternalCapabilities());
		EXPECT_FALSE(Status);
		EXPECT_EQ(Status.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_NE(Status.mMessage.find("external support exception"), eastl::string::npos);
	}

	TEST(ArdaCudaExternalCall, DescriptorRequiresExactlyOneCallableAndValidResourcePatches)
	{
		FArdaCudaKernel K;
		K.mExternalCall = eastl::make_shared<FExternalScalarCall>();
		K.mParameters.resize(sizeof(FExternalScalarParameters::FCuda));
		EXPECT_TRUE(ValidateArdaCudaKernels({K}, 0, ExternalCapabilities()));
		EXPECT_TRUE(ValidateArdaCudaKernelBatch({K, K}, 0, ExternalCapabilities()));
		EXPECT_FALSE(ValidateArdaCudaKernels({K, K}, 0, ExternalCapabilities()));
		EXPECT_FALSE(ValidateArdaCudaKernels({K}, 0, ExternalCapabilities(EArdaCudaLaunchMode::D3D12CiG)));
		auto Invalid = K;
		Invalid.mEntry = eastl::make_shared<FExternalTestEntry>();
		EXPECT_FALSE(ValidateArdaCudaKernels({Invalid}, 0, ExternalCapabilities()));
		Invalid = K;
		Invalid.mExternalCall.reset();
		EXPECT_FALSE(ValidateArdaCudaKernels({Invalid}, 0, ExternalCapabilities()));
		Invalid = K;
		Invalid.mPatches.push_back({0, 0, 8});
		EXPECT_FALSE(ValidateArdaCudaKernels({Invalid}, 1, ExternalCapabilities()));
	}

	void* GetCurrentTestCudaContext()
	{
#if defined(_WIN32)
		using FGetCurrent = int(__stdcall*)(void**);
		const auto Module = GetModuleHandleW(L"nvcuda.dll");
		const auto GetCurrent =
		    Module ? reinterpret_cast<FGetCurrent>(GetProcAddress(Module, "cuCtxGetCurrent")) : nullptr;
		void* Context = nullptr;
		return GetCurrent && GetCurrent(&Context) == 0 ? Context : nullptr;
#else
		return nullptr;
#endif
	}

	struct FExternalTrace
	{
		uint32_t mPrepareCount = 0;
		uint32_t mDestroyCount = 0;
		uint32_t mStateCreateCount = 0;
		uint32_t mStateCreateAttemptCount = 0;
		uint32_t mStateDestroyCount = 0;
		eastl::vector<void*> mStreams;
		eastl::vector<void*> mInternalStreams;
		eastl::vector<void*> mContexts;
		eastl::vector<void*> mPreparedContexts;
		eastl::vector<void*> mEnqueueContexts;
		eastl::vector<void*> mDestroyedContexts;
		eastl::vector<uint32_t> mPrepareCountsAtEnqueue;
		eastl::vector<FArdaAddParameters::FCuda> mPatchedParameters;
		eastl::vector<void*> mStates;
		eastl::vector<void*> mStateDestroyedContexts;
		eastl::vector<uint32_t> mStateDestroyCountsAtPreparedDestruction;
	};

	struct FExternalCachedState final : IArdaCudaExternalCallState
	{
		eastl::shared_ptr<FExternalTrace> mTrace;

		~FExternalCachedState() override
		{
			++mTrace->mStateDestroyCount;
			mTrace->mStateDestroyedContexts.push_back(GetCurrentTestCudaContext());
		}
	};

	enum class EExternalFailure
	{
		None,
		PrepareStatus,
		PrepareException,
		PrepareNull,
		CreateStateStatus,
		CreateStateException,
		EnqueueException
	};

	struct FPreparedExternalAdd final : IArdaCudaPreparedCall
	{
		eastl::shared_ptr<const IArdaCudaKernelEntry> mEntry;
		eastl::shared_ptr<FExternalTrace> mTrace;
		FArdaAddParameters::FCuda mParameters;
		EExternalFailure mFailure = EExternalFailure::None;
		uint32_t mInternalLaunchCount = 1;

		~FPreparedExternalAdd() override
		{
			++mTrace->mDestroyCount;
			mTrace->mDestroyedContexts.push_back(GetCurrentTestCudaContext());
			mTrace->mStateDestroyCountsAtPreparedDestruction.push_back(mTrace->mStateDestroyCount);
		}

		FArdaRHIStatus Enqueue(void* Stream) override
		{
			mTrace->mStreams.push_back(Stream);
			mTrace->mEnqueueContexts.push_back(GetCurrentTestCudaContext());
			mTrace->mPrepareCountsAtEnqueue.push_back(mTrace->mPrepareCount);
			if (mFailure == EExternalFailure::EnqueueException)
			{
				throw std::runtime_error("external enqueue exception");
			}
			FArdaCudaLaunchConfig Launch;
			Launch.mBlockSize[0] = 128;
			Launch.mGridSize[0] = 1 + (mParameters.mCount - 1) / 128;
			for (uint32_t I = 0; I < mInternalLaunchCount; ++I)
			{
				mTrace->mInternalStreams.push_back(Stream);
				if (auto Status = mEntry->Launch(Stream, Launch, &mParameters, sizeof(mParameters)); !Status)
				{
					return Status;
				}
			}
			return {};
		}
	};

	struct FExternalAddCall final : TArdaCudaExternalCall<FArdaAddParameters>
	{
		eastl::shared_ptr<const IArdaCudaKernelEntry> mEntry;
		eastl::shared_ptr<FExternalTrace> mTrace;
		EExternalFailure mFailure = EExternalFailure::None;
		bool mbCacheState = false;
		bool mbGraphCaptureSafe = false;
		uint64_t mGraphRevision = 0;
		uint32_t mInternalLaunchCount = 1;

		bool SupportsGraphCapture(const FArdaCudaCapabilities&) const noexcept override
		{
			return mbGraphCaptureSafe;
		}

		uint64_t GetGraphCaptureRevision() const noexcept override
		{
			return mGraphRevision;
		}

		FArdaRHIStatus GetSupport(const FArdaCudaCapabilities& C) const override
		{
			// This adapter only invokes our capture-qualified test kernel. Real library adapters must qualify their own calls.
			return C.mLaunchMode == EArdaCudaLaunchMode::D3D12CiG ? FArdaRHIStatus{}
			                                                      : IArdaCudaExternalCall::GetSupport(C);
		}

		TArdaRHIResult<eastl::unique_ptr<IArdaCudaExternalCallState>> CreateContextState(
		    const FArdaCudaExternalCallContext&) const override
		{
			++mTrace->mStateCreateAttemptCount;
			if (mFailure == EExternalFailure::CreateStateStatus)
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "external state status")};
			}
			if (mFailure == EExternalFailure::CreateStateException)
			{
				throw std::runtime_error("external state exception");
			}
			if (!mbCacheState)
			{
				return {{}, {}};
			}
			++mTrace->mStateCreateCount;
			auto State = eastl::unique_ptr<FExternalCachedState>(new FExternalCachedState);
			State->mTrace = mTrace;
			return {eastl::move(State), {}};
		}

		TArdaRHIResult<eastl::unique_ptr<IArdaCudaPreparedCall>> PrepareCall(
		    const FArdaCudaExternalCallContext& Context,
		    const FArdaAddParameters::FCuda& P) const override
		{
			++mTrace->mPrepareCount;
			mTrace->mContexts.push_back(Context.mContext);
			mTrace->mPreparedContexts.push_back(GetCurrentTestCudaContext());
			mTrace->mPatchedParameters.push_back(P);
			mTrace->mStates.push_back(Context.mState);
			if (mFailure == EExternalFailure::PrepareStatus)
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "external prepare status")};
			}
			if (mFailure == EExternalFailure::PrepareException)
			{
				throw std::runtime_error("external prepare exception");
			}
			if (mFailure == EExternalFailure::PrepareNull)
			{
				return {{}, {}};
			}
			auto Prepared = eastl::unique_ptr<FPreparedExternalAdd>(new FPreparedExternalAdd);
			Prepared->mEntry = mEntry;
			Prepared->mTrace = mTrace;
			Prepared->mParameters = P;
			Prepared->mFailure = mFailure;
			Prepared->mInternalLaunchCount = mInternalLaunchCount;
			return {eastl::move(Prepared), {}};
		}
	};

	class FExternalAddOperand final : public TArdaComputeOperand<FArdaAddParameters, uint32_t>
	{
	public:
		FExternalAddOperand(FArdaRHIDeviceRef Device, eastl::shared_ptr<const IArdaCudaExternalCall> Call)
		    : TArdaComputeOperand(eastl::move(Device)),
		      mCall(eastl::move(Call))
		{
		}

		const char* GetName() const noexcept override
		{
			return "test.external.add";
		}

		void BindKernelVariants(FRegistry& Registry) const override
		{
			Registry.Add(BindArdaCudaExternalCall<uint32_t>("external-add", 0, mCall));
		}

		TArdaRHIResult<FArdaCudaKernelSelection> SelectKernel(const FParameters& P,
		    const FArdaCudaSelectionContext&,
		    const FVariants& Candidates) const override
		{
			if (!P.mCount || Candidates.empty() ||
			    P.mInput.mRange.Resolve(P.mInput.mBuffer->GetDesc()).mByteSize != uint64_t(P.mCount) * 4 ||
			    P.mOutput.mRange.Resolve(P.mOutput.mBuffer->GetDesc()).mByteSize != uint64_t(P.mCount) * 4)
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "External add count must match its views.")};
			}
			FArdaCudaKernelSelection Selection;
			Selection.mVariantId = Candidates.front().mId;
			return {Selection, {}};
		}

	private:
		eastl::shared_ptr<const IArdaCudaExternalCall> mCall;
	};

	class ArdaCudaExternalGpu : public testing::TestWithParam<const char*>
	{
	protected:
		FArdaRHIDeviceRef mDevice;
		eastl::shared_ptr<const IArdaCudaKernelEntry> mEntry;

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
			C.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
			ASSERT_TRUE(ConfigureBackend(C));
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
			mDevice = GetDevice();
			const auto Caps = mDevice->GetCudaCapabilities();
			if (!Caps)
			{
				GTEST_SKIP() << Caps.mUnavailableReason.c_str();
			}
			FArdaAddOperand Compiled(mDevice);
			auto Registry = Compiled.GetKernelVariants();
			if (!Registry)
			{
				GTEST_SKIP() << Registry.mStatus.mMessage.c_str();
			}
			for (const auto& V : Registry.mValue->GetCompatibleVariants(Caps))
			{
				if (V.mPayload.mBlockSize == 128 && !V.mEntry->GetBuildInfo().mbFastMath)
				{
					mEntry = V.mEntry;
					break;
				}
			}
			if (!mEntry)
			{
				GTEST_SKIP() << "No compatible test kernel";
			}
		}

		void TearDown() override
		{
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
			}
			mEntry.reset();
			mDevice.Reset();
			ShutdownBackend();
		}

		FArdaRHIBufferDesc BufferDesc(uint32_t Count)
		{
			FArdaRHIBufferDesc D;
			D.mByteSize = uint64_t(Count) * 4;
			D.mbCudaInterop = true;
			D.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
			D.mDebugName = "external CUDA test";
			return D;
		}

		eastl::shared_ptr<FExternalAddCall> MakeCall(const eastl::shared_ptr<FExternalTrace>& Trace,
		    EExternalFailure Failure = EExternalFailure::None)
		{
			auto Call = eastl::make_shared<FExternalAddCall>();
			Call->mEntry = mEntry;
			Call->mTrace = Trace;
			Call->mFailure = Failure;
			return Call;
		}

		void ExpectContexts(const FExternalTrace& Trace)
		{
			for (size_t I = 0; I < Trace.mContexts.size(); ++I)
			{
				EXPECT_NE(Trace.mContexts[I], nullptr);
#if defined(_WIN32)
				EXPECT_EQ(Trace.mPreparedContexts[I], Trace.mContexts[I]);
#endif
			}
#if defined(_WIN32)
			for (auto Context : Trace.mEnqueueContexts)
			{
				EXPECT_EQ(Context, Trace.mContexts.front());
			}
			for (auto Context : Trace.mDestroyedContexts)
			{
				EXPECT_EQ(Context, Trace.mContexts.front());
			}
#endif
		}
	};

	TEST_P(ArdaCudaExternalGpu, RetainedGraphRequiresOptInAndRetainsPreparedStateAcrossReplay)
	{
		constexpr uint32_t Count = 128;
		auto Buffer = mDevice->CreateBuffer(BufferDesc(Count));
		ASSERT_TRUE(Buffer);
		auto Upload = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Upload);
		ASSERT_TRUE(Upload.mValue->Open());
		eastl::vector<uint32_t> Values(Count, 5);
		ASSERT_TRUE(Upload.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Count * 4));
		ASSERT_TRUE(Upload.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Upload.mValue));
		auto Trace = eastl::make_shared<FExternalTrace>();
		auto Call = MakeCall(Trace);
		Call->mbCacheState = true;
		FExternalAddOperand Operand(mDevice, Call);
		FArdaAddParameters P;
		P.mCount = Count;
		P.mBias = 7;
		P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
		auto Cache = eastl::make_shared<FArdaCudaGraphCache>(EArdaCudaGraphMode::Require);
		{
			FArdaCudaSequence Sequence(mDevice, EArdaRHIQueueType::Graphics, Cache);
			ASSERT_TRUE(Sequence.Add(Operand, P));
			auto Rejected = Sequence.Dispatch();
			EXPECT_FALSE(Rejected);
			EXPECT_EQ(Rejected.mStatus.mCode, EArdaRHIResult::Unsupported);
			EXPECT_EQ(Trace->mPrepareCount, 0u);
		}
		uint32_t Expected = 5;
		for (const auto Mode : {EArdaCudaGraphMode::Prefer, EArdaCudaGraphMode::Disabled})
		{
			auto DirectCache = eastl::make_shared<FArdaCudaGraphCache>(Mode);
			FArdaCudaSequence Sequence(mDevice, EArdaRHIQueueType::Graphics, DirectCache);
			ASSERT_TRUE(Sequence.Add(Operand, P));
			ASSERT_TRUE(Sequence.Dispatch());
			ASSERT_TRUE(mDevice->WaitForIdle());
			Expected += 7;
			EXPECT_EQ(DirectCache->GetStats().mCaptureCount, 0u);
			EXPECT_EQ(DirectCache->GetStats().mFallbackCount, Mode == EArdaCudaGraphMode::Prefer ? 1u : 0u);
		}
		Call->mbGraphCaptureSafe = true;
		const auto PreparedBefore = Trace->mPrepareCount;
		const auto DestroyedBefore = Trace->mDestroyCount;
		for (uint32_t Frame = 0; Frame < 3; ++Frame)
		{
			if (Frame == 2)
			{
				++Call->mGraphRevision;
			}
			FArdaCudaSequence Sequence(mDevice, EArdaRHIQueueType::Graphics, Cache);
			ASSERT_TRUE(Sequence.Add(Operand, P));
			auto Submitted = Sequence.Dispatch();
			ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
			ASSERT_TRUE(mDevice->WaitForIdle());
			Expected += 7;
			EXPECT_EQ(Trace->mPrepareCount, PreparedBefore + (Frame < 2 ? 1 : 2));
			if (Frame < 2)
			{
				EXPECT_EQ(Trace->mDestroyCount, DestroyedBefore);
			}
		}
		EXPECT_EQ(Cache->GetStats().mCaptureCount, 2u);
		EXPECT_EQ(Cache->GetStats().mReplayCount, 1u);
		EXPECT_EQ(Cache->GetStats().mRebuildCount, 1u);
		EXPECT_EQ(Cache->GetStats().mCaptureFailureCount, 0u);
		auto Readback = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Readback);
		ASSERT_TRUE(Readback.mValue->Open());
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Readback.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
		ASSERT_TRUE(Readback.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Readback.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
		ASSERT_EQ(Bytes.size(), Count * 4);
		for (uint32_t I = 0; I < Count; ++I)
		{
			uint32_t Word;
			std::memcpy(&Word, Bytes.data() + I * 4, 4);
			EXPECT_EQ(Word, Expected);
		}
		eastl::weak_ptr<FArdaCudaGraphCache> WeakCache = Cache;
		Cache.reset(); // Natural teardown must not require Reset to break a native-state ownership cycle.
		EXPECT_TRUE(WeakCache.expired());
		EXPECT_EQ(Trace->mDestroyCount, Trace->mPrepareCount);
		ExpectContexts(*Trace);
	}

	TEST_P(ArdaCudaExternalGpu, TimingRegionIncludesAllLibraryLaunchesAndReplaysWithoutPreparation)
	{
		constexpr uint32_t Count = 4096;
		auto Buffer = mDevice->CreateBuffer(BufferDesc(Count));
		ASSERT_TRUE(Buffer);
		auto Upload = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Upload);
		ASSERT_TRUE(Upload.mValue->Open());
		eastl::vector<uint32_t> Values(Count, 5);
		ASSERT_TRUE(Upload.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Count * 4));
		ASSERT_TRUE(Upload.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Upload.mValue));
		auto Trace = eastl::make_shared<FExternalTrace>();
		auto Call = MakeCall(Trace);
		Call->mbGraphCaptureSafe = true;
		Call->mInternalLaunchCount = 3;
		FExternalAddOperand External(mDevice, Call);
		FArdaAddOperand Compiled(mDevice);
		FArdaAddParameters P;
		P.mCount = Count;
		P.mBias = 2;
		P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
		auto Cache = eastl::make_shared<FArdaCudaGraphCache>(EArdaCudaGraphMode::Require);
		auto Query = eastl::make_shared<FArdaCudaTimingQuery>();
		for (uint32_t Frame = 0; Frame < 2; ++Frame)
		{
			FArdaCudaSequence Sequence(mDevice, EArdaRHIQueueType::Graphics, Cache);
			ASSERT_TRUE(Sequence.BeginTimingRegion(Query, 101));
			ASSERT_TRUE(Sequence.Add(External, P));
			ASSERT_TRUE(Sequence.EndTimingRegion());
			ASSERT_TRUE(Sequence.BeginTimingRegion(Query, 202));
			ASSERT_TRUE(Sequence.Add(Compiled, P));
			ASSERT_TRUE(Sequence.EndTimingRegion());
			auto Submitted = Sequence.Dispatch();
			ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
			ASSERT_TRUE(mDevice->WaitForIdle());
			auto Timing = Query->Poll(true);
			ASSERT_TRUE(Timing) << Timing.mStatus.mMessage.c_str();
			ASSERT_TRUE(Timing.mValue.mbReady);
			ASSERT_EQ(Timing.mValue.mRegions.size(), 2u);
			EXPECT_EQ(Timing.mValue.mRegions[0].mRegionId, 101u);
			EXPECT_EQ(Timing.mValue.mRegions[1].mRegionId, 202u);
			for (const auto& Region : Timing.mValue.mRegions)
			{
				EXPECT_GT(Region.mGpuSeconds, 0.0);
			}
		}
		EXPECT_EQ(Cache->GetStats().mCaptureCount, 1u);
		EXPECT_EQ(Cache->GetStats().mReplayCount, 1u);
		EXPECT_EQ(Trace->mPrepareCount, 1u);
		EXPECT_EQ(Trace->mInternalStreams.size(), 3u);
		auto Readback = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Readback);
		ASSERT_TRUE(Readback.mValue->Open());
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Readback.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
		ASSERT_TRUE(Readback.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Readback.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
		ASSERT_EQ(Bytes.size(), Count * 4);
		for (uint32_t I = 0; I < Count; ++I)
		{
			uint32_t Value;
			std::memcpy(&Value, Bytes.data() + I * 4, 4);
			EXPECT_EQ(Value, 21u);
		}
	}

	TEST_P(ArdaCudaExternalGpu, CaptureOperationFailureEndsCaptureWithoutRetryingTheAdapter)
	{
		auto Buffer = mDevice->CreateBuffer(BufferDesc(128));
		ASSERT_TRUE(Buffer);
		auto Upload = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Upload);
		ASSERT_TRUE(Upload.mValue->Open());
		eastl::vector<uint32_t> Values(128, 5);
		ASSERT_TRUE(Upload.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Values.size() * 4));
		ASSERT_TRUE(Upload.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Upload.mValue));
		auto Cache = eastl::make_shared<FArdaCudaGraphCache>();
		FArdaAddParameters P;
		P.mCount = 128;
		P.mBias = 7;
		P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
		auto FailedTrace = eastl::make_shared<FExternalTrace>();
		{
			auto Call = MakeCall(FailedTrace, EExternalFailure::EnqueueException);
			Call->mbGraphCaptureSafe = true;
			FExternalAddOperand Operand(mDevice, Call);
			FArdaCudaSequence Sequence(mDevice, EArdaRHIQueueType::Graphics, Cache);
			ASSERT_TRUE(Sequence.Add(Operand, P));
			auto Rejected = Sequence.Dispatch();
			EXPECT_FALSE(Rejected);
			EXPECT_EQ(Rejected.mStatus.mCode, EArdaRHIResult::BackendFailure);
		}
		EXPECT_EQ(FailedTrace->mPrepareCount, 1u);
		EXPECT_EQ(FailedTrace->mStreams.size(), 1u);
		EXPECT_EQ(FailedTrace->mDestroyCount, 1u);
		EXPECT_EQ(Cache->GetStats().mCaptureFailureCount, 1u);
		EXPECT_EQ(Cache->GetStats().mFallbackCount, 0u);
		auto Trace = eastl::make_shared<FExternalTrace>();
		auto Call = MakeCall(Trace);
		Call->mbGraphCaptureSafe = true;
		FExternalAddOperand Operand(mDevice, Call);
		FArdaCudaSequence Sequence(mDevice, EArdaRHIQueueType::Graphics, Cache);
		ASSERT_TRUE(Sequence.Add(Operand, P));
		auto Submitted = Sequence.Dispatch();
		ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
		auto Readback = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Readback);
		ASSERT_TRUE(Readback.mValue->Open());
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Readback.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
		ASSERT_TRUE(Readback.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Readback.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
		EXPECT_EQ(Cache->GetStats().mCaptureCount, 1u);
		ASSERT_EQ(Bytes.size(), 128u * 4);
		for (uint32_t I = 0; I < 128; ++I)
		{
			uint32_t Word;
			std::memcpy(&Word, Bytes.data() + I * 4, 4);
			EXPECT_EQ(Word, 12u);
		}
	}

	TEST_P(ArdaCudaExternalGpu, StandaloneDispatchRetainsPatchedValuesUntilRetirement)
	{
		constexpr uint32_t Count = 257;
		auto Buffer = mDevice->CreateBuffer(BufferDesc(Count));
		ASSERT_TRUE(Buffer);
		auto Upload = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Upload);
		ASSERT_TRUE(Upload.mValue->Open());
		eastl::vector<uint32_t> Values(Count, 17);
		ASSERT_TRUE(Upload.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Values.size() * 4));
		ASSERT_TRUE(Upload.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Upload.mValue));
		Upload.mValue.Reset();
		auto Trace = eastl::make_shared<FExternalTrace>();
		{
			FExternalAddOperand Operand(mDevice, MakeCall(Trace));
			ASSERT_TRUE(Operand.GetOperandSupport());
			FArdaAddParameters P;
			P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
			P.mCount = Count;
			P.mBias = 23;
			auto Submitted = Operand.Dispatch(P);
			ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
			EXPECT_GT(Submitted.mValue.mInstance, 0u);
			P.mBias = 999;
		}
		auto Readback = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Readback);
		ASSERT_TRUE(Readback.mValue->Open());
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Readback.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
		ASSERT_TRUE(Readback.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Readback.mValue));
		Readback.mValue.Reset();
		Buffer.mValue.Reset();
		ASSERT_TRUE(mDevice->WaitForIdle());
		ASSERT_EQ(Bytes.size(), Count * 4u);
		for (uint32_t I = 0; I < Count; ++I)
		{
			uint32_t Value;
			std::memcpy(&Value, Bytes.data() + I * 4, 4);
			EXPECT_EQ(Value, 40u);
		}
		EXPECT_EQ(Trace->mPrepareCount, 1u);
		EXPECT_EQ(Trace->mDestroyCount, 1u);
		ASSERT_EQ(Trace->mStreams.size(), 1u);
		EXPECT_NE(Trace->mStreams.front(), nullptr);
		ASSERT_EQ(Trace->mPatchedParameters.size(), 1u);
		EXPECT_EQ(Trace->mPatchedParameters.front().mBias, 23u);
		EXPECT_EQ(Trace->mPatchedParameters.front().mInput, Trace->mPatchedParameters.front().mOutput);
		ExpectContexts(*Trace);
	}

	TEST_P(ArdaCudaExternalGpu, OneExternalOperationMayEnqueueMultipleInternalKernels)
	{
		auto Buffer = mDevice->CreateBuffer(BufferDesc(257));
		ASSERT_TRUE(Buffer);
		auto Upload = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Upload);
		ASSERT_TRUE(Upload.mValue->Open());
		eastl::vector<uint32_t> Values(257, 17);
		ASSERT_TRUE(Upload.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Values.size() * 4));
		ASSERT_TRUE(Upload.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Upload.mValue));
		Upload.mValue.Reset();
		auto Trace = eastl::make_shared<FExternalTrace>();
		{
			auto Call = MakeCall(Trace);
			Call->mInternalLaunchCount = 2;
			FExternalAddOperand Operand(mDevice, Call);
			FArdaAddParameters P;
			P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
			P.mCount = 257;
			P.mBias = 23;
			FArdaCudaSequence Sequence(mDevice);
			ASSERT_TRUE(Sequence.Add(Operand, P));
			EXPECT_EQ(Sequence.GetOperationCount(), 1u);
			EXPECT_EQ(Sequence.GetKernelCount(), 0u);
			auto Submitted = Sequence.Dispatch();
			ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
			EXPECT_GT(Submitted.mValue.mInstance, 0u);
			EXPECT_EQ(Submitted.mValue.mOperationCount, 1u);
			EXPECT_EQ(Submitted.mValue.mKernelCount, 0u);
		}
		auto Readback = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Readback);
		ASSERT_TRUE(Readback.mValue->Open());
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Readback.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
		ASSERT_TRUE(Readback.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Readback.mValue));
		Readback.mValue.Reset();
		ASSERT_TRUE(mDevice->WaitForIdle());
		ASSERT_EQ(Bytes.size(), Values.size() * 4);
		for (uint32_t I = 0; I < Values.size(); ++I)
		{
			uint32_t Value;
			std::memcpy(&Value, Bytes.data() + I * 4, 4);
			EXPECT_EQ(Value, 63u);
		}
		EXPECT_EQ(Trace->mPrepareCount, 1u);
		EXPECT_EQ(Trace->mDestroyCount, 1u);
		ASSERT_EQ(Trace->mStreams.size(), 1u);
		ASSERT_EQ(Trace->mInternalStreams.size(), 2u);
		EXPECT_NE(Trace->mStreams.front(), nullptr);
		EXPECT_EQ(Trace->mInternalStreams[0], Trace->mStreams.front());
		EXPECT_EQ(Trace->mInternalStreams[1], Trace->mStreams.front());
		ExpectContexts(*Trace);
	}

	TEST_P(ArdaCudaExternalGpu, CachedStateIsReusedAcrossSubmissionsAndDestroyedAfterPreparedCalls)
	{
		auto Trace = eastl::make_shared<FExternalTrace>();
		{
			auto Buffer = mDevice->CreateBuffer(BufferDesc(128));
			ASSERT_TRUE(Buffer);
			auto Upload = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Upload);
			ASSERT_TRUE(Upload.mValue->Open());
			eastl::vector<uint32_t> Values(128, 5);
			ASSERT_TRUE(Upload.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Values.size() * 4));
			ASSERT_TRUE(Upload.mValue->Close());
			ASSERT_TRUE(mDevice->ExecuteCommandList(Upload.mValue));
			Upload.mValue.Reset();
			auto Call = MakeCall(Trace);
			Call->mbCacheState = true;
			FExternalAddOperand Operand(mDevice, Call);
			FArdaAddParameters P;
			P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
			P.mCount = 128;
			P.mBias = 7;
			for (uint32_t I = 0; I < 2; ++I)
			{
				auto Submitted = Operand.Dispatch(P);
				ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
				ASSERT_TRUE(mDevice->WaitForIdle());
			}
			EXPECT_EQ(Trace->mPrepareCount, 2u);
			EXPECT_EQ(Trace->mDestroyCount, 2u);
			EXPECT_EQ(Trace->mStateCreateCount, 1u);
			EXPECT_EQ(Trace->mStateDestroyCount, 0u);
			ASSERT_EQ(Trace->mStates.size(), 2u);
			EXPECT_NE(Trace->mStates.front(), nullptr);
			EXPECT_EQ(Trace->mStates[0], Trace->mStates[1]);
		}
		// Context state is device-owned: retiring submissions releases each prepared lease,
		// and device shutdown finally releases the reusable state while its context is current.
		mEntry.reset();
		mDevice.Reset();
		ShutdownBackend();
		EXPECT_EQ(Trace->mStateDestroyCount, 1u);
		EXPECT_EQ(Trace->mStateDestroyCountsAtPreparedDestruction, (eastl::vector<uint32_t>{0, 0}));
		ExpectContexts(*Trace);
#if defined(_WIN32)
		ASSERT_EQ(Trace->mStateDestroyedContexts.size(), 1u);
		EXPECT_EQ(Trace->mStateDestroyedContexts.front(), Trace->mContexts.front());
#endif
	}

	TEST_P(ArdaCudaExternalGpu, OutstandingRecordingsShareContextStateAndRetainSeparatePreparedCalls)
	{
		auto Buffer = mDevice->CreateBuffer(BufferDesc(128));
		ASSERT_TRUE(Buffer);
		auto Upload = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Upload);
		ASSERT_TRUE(Upload.mValue->Open());
		eastl::vector<uint32_t> Values(128, 5);
		ASSERT_TRUE(Upload.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Values.size() * 4));
		ASSERT_TRUE(Upload.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Upload.mValue));
		Upload.mValue.Reset();
		auto Trace = eastl::make_shared<FExternalTrace>();
		auto Call = MakeCall(Trace);
		Call->mbCacheState = true;
		FExternalAddOperand Operand(mDevice, Call);
		FArdaAddParameters P;
		P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
		P.mCount = 128;
		P.mBias = 7;
		auto First = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		auto Second = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(First);
		ASSERT_TRUE(Second);
		ASSERT_TRUE(First.mValue->Open());
		ASSERT_TRUE(Operand.DispatchDeferred(*First.mValue, P));
		ASSERT_TRUE(First.mValue->Close());
		const bool IsCapture = mDevice->GetCudaCapabilities().mLaunchMode == EArdaCudaLaunchMode::D3D12CiG;
		if (IsCapture)
		{
			// CiG permits only one unsubmitted capture. Keep its command list alive so both prepared leases overlap.
			ASSERT_TRUE(mDevice->ExecuteCommandList(First.mValue));
		}
		ASSERT_TRUE(Second.mValue->Open());
		P.mBias = 11;
		ASSERT_TRUE(Operand.DispatchDeferred(*Second.mValue, P));
		ASSERT_TRUE(Second.mValue->Close());
		EXPECT_EQ(Trace->mPrepareCount, 2u);
		EXPECT_EQ(Trace->mStateCreateCount, 1u);
		EXPECT_EQ(Trace->mStateCreateAttemptCount, 1u);
		EXPECT_EQ(Trace->mDestroyCount, 0u);
		ASSERT_EQ(Trace->mStates.size(), 2u);
		EXPECT_NE(Trace->mStates.front(), nullptr);
		EXPECT_EQ(Trace->mStates[0], Trace->mStates[1]);
		if (!IsCapture)
		{
			ASSERT_TRUE(mDevice->ExecuteCommandList(First.mValue));
		}
		ASSERT_TRUE(mDevice->ExecuteCommandList(Second.mValue));
		First.mValue.Reset();
		Second.mValue.Reset();
		auto Readback = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Readback);
		ASSERT_TRUE(Readback.mValue->Open());
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Readback.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
		ASSERT_TRUE(Readback.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Readback.mValue));
		Readback.mValue.Reset();
		ASSERT_TRUE(mDevice->WaitForIdle());
		EXPECT_EQ(Trace->mDestroyCount, 2u);
		ASSERT_EQ(Bytes.size(), Values.size() * 4);
		for (uint32_t I = 0; I < Values.size(); ++I)
		{
			uint32_t Value;
			std::memcpy(&Value, Bytes.data() + I * 4, 4);
			EXPECT_EQ(Value, 23u);
		}
		ExpectContexts(*Trace);
	}

	TEST_P(ArdaCudaExternalGpu, AbandonedRecordingReleasesPreparedCallsWithoutExecutingWork)
	{
		auto Buffer = mDevice->CreateBuffer(BufferDesc(128));
		ASSERT_TRUE(Buffer);
		auto Upload = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Upload);
		ASSERT_TRUE(Upload.mValue->Open());
		eastl::vector<uint32_t> Values(128, 17);
		ASSERT_TRUE(Upload.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Values.size() * 4));
		ASSERT_TRUE(Upload.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Upload.mValue));
		Upload.mValue.Reset();
		auto Trace = eastl::make_shared<FExternalTrace>();
		{
			FExternalAddOperand Operand(mDevice, MakeCall(Trace));
			FArdaAddParameters P;
			P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
			P.mCount = 128;
			P.mBias = 23;
			auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Commands);
			ASSERT_TRUE(Commands.mValue->Open());
			ASSERT_TRUE(Operand.DispatchDeferred(*Commands.mValue, P));
			ASSERT_TRUE(Commands.mValue->Close());
			EXPECT_EQ(Trace->mDestroyCount, 0u);
		}
		EXPECT_EQ(Trace->mPrepareCount, 1u);
		EXPECT_EQ(Trace->mDestroyCount, 1u);
		auto Readback = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Readback);
		ASSERT_TRUE(Readback.mValue->Open());
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Readback.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
		ASSERT_TRUE(Readback.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Readback.mValue));
		Readback.mValue.Reset();
		ASSERT_TRUE(mDevice->WaitForIdle());
		ASSERT_EQ(Bytes.size(), Values.size() * 4);
		EXPECT_EQ(std::memcmp(Bytes.data(), Values.data(), Bytes.size()), 0);
		ExpectContexts(*Trace);
	}

	TEST_P(ArdaCudaExternalGpu, ContextStateFailuresLeaveLaterRecordingUsable)
	{
		auto Buffer = mDevice->CreateBuffer(BufferDesc(128));
		ASSERT_TRUE(Buffer);
		auto Upload = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Upload);
		ASSERT_TRUE(Upload.mValue->Open());
		eastl::vector<uint32_t> Values(128, 17);
		ASSERT_TRUE(Upload.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Values.size() * 4));
		ASSERT_TRUE(Upload.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Upload.mValue));
		Upload.mValue.Reset();
		FArdaAddParameters P;
		P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
		P.mCount = 128;
		P.mBias = 23;
		for (auto Failure : {EExternalFailure::CreateStateStatus, EExternalFailure::CreateStateException})
		{
			auto GoodTrace = eastl::make_shared<FExternalTrace>();
			auto BadTrace = eastl::make_shared<FExternalTrace>();
			FExternalAddOperand Good(mDevice, MakeCall(GoodTrace));
			{
				FExternalAddOperand Bad(mDevice, MakeCall(BadTrace, Failure));
				FArdaCudaSequence Sequence(mDevice);
				ASSERT_TRUE(Sequence.Add(Good, P));
				ASSERT_TRUE(Sequence.Add(Bad, P));
				auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
				ASSERT_TRUE(Commands);
				ASSERT_TRUE(Commands.mValue->Open());
				EXPECT_FALSE(Sequence.DispatchDeferred(*Commands.mValue));
			}
			EXPECT_EQ(BadTrace->mStateCreateAttemptCount, 1u);
			EXPECT_EQ(BadTrace->mPrepareCount, 0u);
			EXPECT_TRUE(GoodTrace->mStreams.empty());
			EXPECT_TRUE(BadTrace->mStreams.empty());
			EXPECT_EQ(GoodTrace->mDestroyCount, 1u);
			auto Submitted = Good.Dispatch(P);
			ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
			ASSERT_TRUE(mDevice->WaitForIdle());
			EXPECT_EQ(GoodTrace->mPrepareCount, 2u);
			EXPECT_EQ(GoodTrace->mStreams.size(), 1u);
			EXPECT_EQ(GoodTrace->mDestroyCount, 2u);
			ExpectContexts(*GoodTrace);
		}
	}

	TEST_P(ArdaCudaExternalGpu, MixedSequencePreservesOrderingOffsetsAndOneStream)
	{
		constexpr uint32_t Count = 257;
		auto Buffer = mDevice->CreateBuffer(BufferDesc(Count * 2 + 8));
		ASSERT_TRUE(Buffer);
		auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		eastl::vector<uint32_t> Values(Count * 2 + 8, 0xDEADBEEFu);
		for (uint32_t I = 0; I < Count; ++I)
		{
			Values[I + 4] = I * 3;
		}
		ASSERT_TRUE(Commands.mValue->WriteBuffer(*Buffer.mValue, Values.data(), Values.size() * 4));
		auto Trace = eastl::make_shared<FExternalTrace>();
		{
			FExternalAddOperand External(mDevice, MakeCall(Trace));
			FArdaAddOperand Compiled(mDevice);
			FArdaCudaSequence Sequence(mDevice);
			FArdaAddParameters P;
			P.mInput = {Buffer.mValue, {16, Count * 4}};
			P.mOutput = {Buffer.mValue, {(Count + 4) * 4, Count * 4}};
			P.mCount = Count;
			P.mBias = 7;
			ASSERT_TRUE(Sequence.Add(External, P));
			P.mInput.mRange = P.mOutput.mRange;
			P.mBias = 11;
			ASSERT_TRUE(Sequence.Add(Compiled, P));
			P.mBias = 13;
			ASSERT_TRUE(Sequence.Add(External, P));
			EXPECT_EQ(Sequence.GetOperationCount(), 3u);
			EXPECT_EQ(Sequence.GetKernelCount(), 1u);
			auto Status = Sequence.DispatchDeferred(*Commands.mValue);
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
		}
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
		ASSERT_TRUE(Commands.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
		Commands.mValue.Reset();
		Buffer.mValue.Reset();
		ASSERT_TRUE(mDevice->WaitForIdle());
		ASSERT_EQ(Bytes.size(), Values.size() * 4);
		for (uint32_t I = 0; I < Values.size(); ++I)
		{
			uint32_t Value;
			std::memcpy(&Value, Bytes.data() + I * 4, 4);
			EXPECT_EQ(Value, I >= Count + 4 && I < Count * 2 + 4 ? (I - Count - 4) * 3 + 31 : Values[I]);
		}
		EXPECT_EQ(Trace->mPrepareCount, 2u);
		EXPECT_EQ(Trace->mDestroyCount, 2u);
		ASSERT_EQ(Trace->mStreams.size(), 2u);
		EXPECT_NE(Trace->mStreams.front(), nullptr);
		EXPECT_EQ(Trace->mStreams[0], Trace->mStreams[1]);
		EXPECT_EQ(Trace->mPrepareCountsAtEnqueue, (eastl::vector<uint32_t>{2, 2}));
		ASSERT_EQ(Trace->mPatchedParameters.size(), 2u);
		EXPECT_EQ(reinterpret_cast<uintptr_t>(Trace->mPatchedParameters[0].mOutput) -
		        reinterpret_cast<uintptr_t>(Trace->mPatchedParameters[0].mInput),
		    Count * 4u);
		EXPECT_EQ(Trace->mPatchedParameters[0].mOutput, Trace->mPatchedParameters[1].mInput);
		ExpectContexts(*Trace);
	}

	TEST_P(ArdaCudaExternalGpu, LatePreparationFailuresNeverEnqueueAndReleaseInContext)
	{
		auto Buffer = mDevice->CreateBuffer(BufferDesc(128));
		ASSERT_TRUE(Buffer);
		for (auto Failure :
		    {EExternalFailure::PrepareStatus, EExternalFailure::PrepareException, EExternalFailure::PrepareNull})
		{
			auto Trace = eastl::make_shared<FExternalTrace>();
			{
				FExternalAddOperand Good(mDevice, MakeCall(Trace));
				FExternalAddOperand Bad(mDevice, MakeCall(Trace, Failure));
				FArdaAddParameters P;
				P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
				P.mCount = 128;
				P.mBias = 7;
				FArdaCudaSequence Sequence(mDevice);
				ASSERT_TRUE(Sequence.Add(Good, P));
				ASSERT_TRUE(Sequence.Add(Bad, P));
				auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
				ASSERT_TRUE(Commands);
				ASSERT_TRUE(Commands.mValue->Open());
				auto Status = Sequence.DispatchDeferred(*Commands.mValue);
				EXPECT_FALSE(Status);
			}
			EXPECT_EQ(Trace->mPrepareCount, 2u);
			EXPECT_TRUE(Trace->mStreams.empty());
			EXPECT_EQ(Trace->mDestroyCount, 1u);
			ExpectContexts(*Trace);
		}
	}

	TEST_P(ArdaCudaExternalGpu, EnqueueExceptionsReturnStatusAndReleaseInContext)
	{
		auto Buffer = mDevice->CreateBuffer(BufferDesc(128));
		ASSERT_TRUE(Buffer);
		auto Trace = eastl::make_shared<FExternalTrace>();
		{
			FExternalAddOperand Operand(mDevice, MakeCall(Trace, EExternalFailure::EnqueueException));
			FArdaAddParameters P;
			P.mInput.mBuffer = P.mOutput.mBuffer = Buffer.mValue;
			P.mCount = 128;
			P.mBias = 7;
			auto Submitted = Operand.Dispatch(P);
			EXPECT_FALSE(Submitted);
			EXPECT_NE(Submitted.mStatus.mMessage.find("external enqueue exception"), eastl::string::npos);
		}
		ASSERT_TRUE(mDevice->WaitForIdle());
		EXPECT_EQ(Trace->mPrepareCount, 1u);
		EXPECT_EQ(Trace->mStreams.size(), 1u);
		EXPECT_EQ(Trace->mDestroyCount, 1u);
		ExpectContexts(*Trace);
	}

	TEST_P(ArdaCudaExternalGpu, PersistentGraphMixesExternalAndCompiledCudaNodes)
	{
		ASSERT_TRUE(RegisterArdaBuiltinNodes());
		auto Trace = eastl::make_shared<FExternalTrace>();
		{
			auto External = eastl::make_shared<FExternalAddOperand>(mDevice, MakeCall(Trace));
			auto Compiled = eastl::make_shared<FArdaAddOperand>(mDevice);
			const eastl::string ExternalDefinition = eastl::string("persistent.external.") + GetParam();
			const eastl::string CompiledDefinition = eastl::string("persistent.compiled.") + GetParam();
			ASSERT_TRUE(RegisterArdaCudaOperandNode(ExternalDefinition, External));

			struct FRegistrationScope
			{
				eastl::string mName;

				~FRegistrationScope()
				{
					EXPECT_TRUE(FArdaNodeRegistry::Get().Unregister(mName));
				}
			} ExternalRegistration{ExternalDefinition};

			ASSERT_TRUE(RegisterArdaCudaOperandNode(CompiledDefinition, Compiled));
			FRegistrationScope CompiledRegistration{CompiledDefinition};
			FArdaDependencyGraph Graph(mDevice);
			ASSERT_TRUE(Graph.BeginGraphEdit());
			auto Input = Graph.CreateBuffer("input", BufferDesc(128));
			auto Middle = Graph.CreateBuffer("external result", BufferDesc(128));
			auto CompiledOutput = Graph.CreateBuffer("compiled result", BufferDesc(128));
			auto Output = Graph.CreateBuffer("output", BufferDesc(128));
			ASSERT_TRUE(Input);
			ASSERT_TRUE(Middle);
			ASSERT_TRUE(CompiledOutput);
			ASSERT_TRUE(Output);
			FArdaGraphUploadParameters Upload;
			Upload.mDestination = Input.mValue;
			Upload.mBytes.resize(128 * 4);
			for (uint32_t I = 0; I < 128; ++I)
			{
				const uint32_t Value = 19;
				std::memcpy(Upload.mBytes.data() + I * 4, &Value, 4);
			}
			ASSERT_TRUE(Graph.AttachOrFind("upload", "arda.upload", Upload));
			TArdaDependencyCudaParameters<FArdaAddParameters> P;
			P.mInput.mResource = Input.mValue;
			P.mOutput.mResource = Middle.mValue;
			P.mCount = 128;
			P.mBias = 7;
			auto First = Graph.AttachOrFind("external first", ExternalDefinition, P);
			ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
			P.mInput.mResource = Middle.mValue;
			P.mOutput.mResource = CompiledOutput.mValue;
			P.mBias = 11;
			auto Second = Graph.AttachOrFind("compiled middle", CompiledDefinition, P);
			ASSERT_TRUE(Second);
			P.mInput.mResource = CompiledOutput.mValue;
			P.mOutput.mResource = Output.mValue;
			P.mBias = 13;
			auto Third = Graph.AttachOrFind("external last", ExternalDefinition, P);
			ASSERT_TRUE(Third);
			auto Bytes = eastl::make_shared<eastl::vector<uint8_t>>();
			ASSERT_TRUE(
			    Graph.AttachOrFind("readback", "arda.readback", FArdaGraphReadbackParameters{Output.mValue, Bytes}));
			auto Status = Graph.EndGraphEdit();
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			ASSERT_EQ(Graph.GetCompileResult().mCudaBatches.size(), 1u);
			EXPECT_EQ(Graph.GetCompileResult().mCudaBatches.front(),
			    (eastl::vector<FArdaGraphNodeHandle>{First.mValue, Second.mValue, Third.mValue}));
			External.reset();
			Compiled.reset();
			const auto Result = Graph.Execute();
			ASSERT_TRUE(Result.mStatus) << Result.mStatus.mMessage.c_str();
			ASSERT_EQ(Bytes->size(), Upload.mBytes.size());
			for (uint32_t I = 0; I < 128; ++I)
			{
				uint32_t Value;
				std::memcpy(&Value, Bytes->data() + I * 4, 4);
				EXPECT_EQ(Value, 50u);
			}
			EXPECT_EQ(Trace->mStreams.size(), 2u);
			EXPECT_EQ(Trace->mStreams[0], Trace->mStreams[1]);
		}
		ASSERT_TRUE(mDevice->WaitForIdle());
		EXPECT_EQ(Trace->mPrepareCount, 2u);
		EXPECT_EQ(Trace->mStreams.size(), 2u);
		EXPECT_EQ(Trace->mDestroyCount, 2u);
		ExpectContexts(*Trace);
	}

	INSTANTIATE_TEST_SUITE_P(Native,
	    ArdaCudaExternalGpu,
	    testing::Values("d3d12-context", "vulkan-context", "d3d12-cig", "vulkan-cig"));
}
