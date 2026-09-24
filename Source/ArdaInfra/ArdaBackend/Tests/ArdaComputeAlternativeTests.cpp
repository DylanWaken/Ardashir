#include "ArdaTestComputeOperand.h"
#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "RHI/Scheduling/ArdaCudaSequence.h"

#include <gtest/gtest.h>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace arda
{
	/** Host-only entry for testing native selection; no CUDA compiler or kernel execution is needed. */
	class FArdaAlternativeTestEntry final : public IArdaCudaKernelEntry
	{
	public:
		FArdaCudaBuildInfo mBuild{"alternative-test", "alternative-test-build", {{120, false}}, false};

		FArdaCudaKernelSignature GetSignature() const noexcept override
		{
			return FArdaAddParameters::GetCudaMetadata().GetSignature();
		}

		const FArdaCudaBuildInfo& GetBuildInfo() const noexcept override
		{
			return mBuild;
		}

		TArdaRHIResult<FArdaCudaKernelLimits> GetLimits() const override
		{
			return {{128, 0, 0}, {}};
		}

		FArdaRHIStatus Launch(void*, const FArdaCudaLaunchConfig&, const void*, size_t) const override
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Test entry must never execute.");
		}
	};

	void ArdaAlternativeTestKernel(FArdaAddParameters::FArdaCuda)
	{
	}

	class FArdaAlternativeSupportTestCall final : public IArdaCudaExternalCall
	{
	public:
		bool mbThrow = false;
		FArdaRHIStatus mStatus;

		FArdaCudaKernelSignature GetSignature() const noexcept override
		{
			return FArdaAddParameters::GetCudaMetadata().GetSignature();
		}

		FArdaRHIStatus GetSupport(const FArdaCudaCapabilities&) const override
		{
			if (mbThrow)
			{
				throw std::runtime_error("Intentional support error");
			}
			return mStatus;
		}

		TArdaRHIResult<eastl::unique_ptr<IArdaCudaPreparedCall>> Prepare(const FArdaCudaExternalCallContext&,
		    const void*,
		    size_t) const override
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Test support factory must never prepare.")};
		}
	};

	class FArdaAlternativeTestOperand : public TArdaComputeOperand<FArdaAddParameters, uint32_t>
	{
	public:
		using TArdaComputeOperand::TArdaComputeOperand;
		FArdaRHIComputePipelineRef mPipeline;
		FArdaRHIBindingLayoutRef mLayout;
		FArdaRHIStatus mAlternativeSupport;
		eastl::shared_ptr<FArdaAlternativeSupportTestCall> mExternalCall;
		bool mbHasAlternative = true;
		bool mbBindInvalid = false;
		bool mbBindNative = false;
		bool mbFailPreparation = false;
		mutable uint32_t mAlternativeChecks = 0;
		mutable uint32_t mAlternativePreparations = 0;

		const char* GetName() const noexcept override
		{
			return "test.compute-alternative";
		}

		void BindKernelVariants(FArdaRegistry& Registry) const override
		{
			if (mbBindInvalid)
			{
				// A supplied alternative must not hide a malformed native variant.
				Registry.Add(TArdaCudaExternalCallVariant<uint32_t>{{}, 0, "invalid", {}});
			}
			else if (mExternalCall)
			{
				Registry.Add(TArdaCudaExternalCallVariant<uint32_t>{mExternalCall, 0, "external", {}});
			}
			else if (mbBindNative)
			{
				auto Entry = eastl::make_shared<FArdaAlternativeTestEntry>();
				Entry->mBuild.mArchitectures = {{GetDevice()->GetCudaCapabilities().mComputeCapability, false}};
				Registry.Add(TArdaCudaKernelVariant<&ArdaAlternativeTestKernel, uint32_t>{Entry, 0, "native", {}});
			}
		}

		TArdaRHIResult<FArdaCudaKernelSelection> SelectKernel(const FArdaParameters&,
		    const FArdaCudaSelectionContext&,
		    const FArdaVariants&) const override
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Intentional native selection error.")};
		}

		bool HasComputeAlternative() const noexcept override
		{
			return mbHasAlternative;
		}

		FArdaRHIStatus CheckComputeAlternativeSupport() const override
		{
			++mAlternativeChecks;
			return mAlternativeSupport;
		}

		TArdaRHIResult<FArdaComputeAlternativePlan> PrepareComputeAlternative(const FArdaParameters& Parameters,
		    EArdaRHIQueueType) const override
		{
			++mAlternativePreparations;
			if (mbFailPreparation)
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Intentional preparation error.")};
			}
			FArdaComputeAlternativePlan Plan;
			Plan.mbNoWork = Parameters.mCount == 0;
			if (Plan.mbNoWork)
			{
				return {Plan, {}};
			}
			if (!Parameters.mInput.mBuffer || !Parameters.mOutput.mBuffer ||
			    Parameters.mInput.mBuffer->GetDesc().mByteSize < uint64_t(Parameters.mCount) * 4 ||
			    Parameters.mOutput.mBuffer->GetDesc().mByteSize < uint64_t(Parameters.mCount) * 4)
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Missing or undersized shader buffer.")};
			}
			FArdaRHIBindingSetDesc Bindings;
			Bindings.mLayout = mLayout;
			FArdaRHIBindingItem Input;
			Input.mType = EArdaRHIBindingType::StructuredBufferSRV;
			Input.mResource = Parameters.mInput.mBuffer;
			Input.mView.mBufferRange = {0, uint64_t(Parameters.mCount) * 4};
			Bindings.mItems.push_back(Input);
			FArdaRHIBindingItem Output;
			Output.mType = EArdaRHIBindingType::StructuredBufferUAV;
			Output.mResource = Parameters.mOutput.mBuffer;
			Output.mView.mBufferRange = {0, uint64_t(Parameters.mCount) * 4};
			Bindings.mItems.push_back(Output);
			auto Set = GetDevice()->CreateBindingSet(Bindings);
			if (!Set)
			{
				return {{}, Set.mStatus};
			}
			Plan.mState = {mPipeline, {Set.mValue}};
			Plan.mPushConstants.resize(sizeof(Parameters.mBias));
			std::memcpy(Plan.mPushConstants.data(), &Parameters.mBias, sizeof(Parameters.mBias));
			Plan.mGroupsX = (Parameters.mCount + 127) / 128;
			return {eastl::move(Plan), {}};
		}
	};

	TEST(ArdaComputeAlternative, EmptyRegistryRequiresExplicitAlternativeAndMissingDeviceStillFails)
	{
		FArdaAlternativeTestOperand ComputeOnly(FArdaRHIDeviceRef{});
		ASSERT_TRUE(ComputeOnly.GetKernelVariants());
		EXPECT_TRUE(ComputeOnly.GetKernelVariants().mValue->GetVariants().empty());
		EXPECT_EQ(ComputeOnly.CheckRequiredFeatures().mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(ComputeOnly.PrepareDispatch({}).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(ComputeOnly.mAlternativeChecks, 0u);
		FArdaAlternativeTestOperand NativeOnly(FArdaRHIDeviceRef{});
		NativeOnly.mbHasAlternative = false;
		EXPECT_EQ(NativeOnly.GetKernelVariants().mStatus.mCode, EArdaRHIResult::Unsupported);
		FArdaAlternativeTestOperand Invalid(FArdaRHIDeviceRef{});
		Invalid.mbBindInvalid = true;
		EXPECT_EQ(Invalid.GetKernelVariants().mStatus.mCode, EArdaRHIResult::InvalidArgument);
	}

	TEST(ArdaComputeAlternative, ExternalSupportFailuresAreNotCapabilityAbsence)
	{
		FArdaAlternativeTestOperand Operand(FArdaRHIDeviceRef{});
		Operand.mExternalCall = eastl::make_shared<FArdaAlternativeSupportTestCall>();
		auto Registry = Operand.GetKernelVariants();
		ASSERT_TRUE(Registry);
		FArdaCudaCapabilities Capabilities;
		Capabilities.mLaunchMode = EArdaCudaLaunchMode::ContextSwitch;
		FArdaRHIStatus Status;
		Operand.mExternalCall->mStatus = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Invalid library state");
		EXPECT_TRUE(Registry.mValue->GetCompatibleVariants(Capabilities, &Status).empty());
		EXPECT_EQ(Status.mCode, EArdaRHIResult::BackendFailure);
		Operand.mExternalCall->mbThrow = true;
		EXPECT_TRUE(Registry.mValue->GetCompatibleVariants(Capabilities, &Status).empty());
		EXPECT_EQ(Status.mCode, EArdaRHIResult::InvalidArgument);
		Operand.mExternalCall->mbThrow = false;
		Operand.mExternalCall->mStatus = FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Unavailable library");
		EXPECT_TRUE(Registry.mValue->GetCompatibleVariants(Capabilities, &Status).empty());
		EXPECT_TRUE(Status);
	}

	class FArdaComputeAlternativeGpu : public testing::TestWithParam<const char*>
	{
	protected:
		FArdaTestDiagnosticCallback mDiagnostics;
		FArdaRHIDeviceRef mDevice;
		FArdaRHIComputePipelineRef mPipeline;
		FArdaRHIBindingLayoutRef mLayout;

		void SetUp() override
		{
			ShutdownBackend();
			if (!FindBackendModule(GetParam()))
			{
				GTEST_SKIP() << "Backend not built";
			}
			auto Configuration = MakeArdaTestBackendConfiguration();
			Configuration.mBackendName = GetParam();
			Configuration.mCudaExecutionMode = EArdaCudaExecutionMode::ContextSwitch;
			Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
			Configuration.mMessageCallback = &mDiagnostics;
			ASSERT_TRUE(ConfigureBackend(Configuration));
			ARDA_REQUIRE_BACKEND();
			mDevice = GetDevice();
			ASSERT_TRUE(mDevice);
			const std::string Path = std::string(ARDA_BACKEND_TEST_SHADER_DIR) + "/ArdaCudaFallback" +
			    GetShaderArtifactExtension(GetParam());
			std::ifstream Stream(Path, std::ios::binary | std::ios::ate);
			ASSERT_TRUE(Stream) << Path;
			std::vector<uint8_t> Bytes(static_cast<size_t>(Stream.tellg()));
			Stream.seekg(0);
			Stream.read(reinterpret_cast<char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
			FArdaRHIShaderDesc ShaderDesc;
			ShaderDesc.mStage = EArdaRHIShaderStage::Compute;
			ShaderDesc.mBytecode = Bytes.data();
			ShaderDesc.mBytecodeSize = Bytes.size();
			ShaderDesc.mEntryPoint = "AddFallbackCS";
			auto Shader = mDevice->CreateShader(ShaderDesc);
			ASSERT_TRUE(Shader) << Shader.mStatus.mMessage.c_str();
			FArdaRHIBindingLayoutDesc Layout;
			Layout.mVisibility = EArdaRHIShaderStage::Compute;
			Layout.mItems = {{0, 1, EArdaRHIBindingType::StructuredBufferSRV},
			    {0, 1, EArdaRHIBindingType::StructuredBufferUAV},
			    {0, sizeof(uint32_t), EArdaRHIBindingType::PushConstants}};
			auto CreatedLayout = mDevice->CreateBindingLayout(Layout);
			ASSERT_TRUE(CreatedLayout) << CreatedLayout.mStatus.mMessage.c_str();
			mLayout = CreatedLayout.mValue;
			FArdaRHIComputePipelineDesc PipelineDesc;
			PipelineDesc.mComputeShader = Shader.mValue;
			PipelineDesc.mBindingLayouts.push_back(mLayout);
			auto Pipeline = mDevice->CreateComputePipeline(PipelineDesc);
			ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();
			mPipeline = Pipeline.mValue;
		}

		void TearDown() override
		{
			const bool bMissingValidation = IsSkipped() && ArdaTestValidationEnabled &&
			    GetBackendInitializeResult() == EArdaInitializeResult::ValidationUnavailable;
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
			}
			mPipeline.Reset();
			mLayout.Reset();
			mDevice.Reset();
			ShutdownBackend();
			EXPECT_TRUE(ConfigureBackend(MakeArdaTestBackendConfiguration()));
			if (!bMissingValidation)
			{
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
		}
	};

	class FArdaAlternativePlanTestOperand final : public FArdaAlternativeTestOperand
	{
	public:
		using FArdaAlternativeTestOperand::FArdaAlternativeTestOperand;
		FArdaComputeAlternativePlan mPlan;

		TArdaRHIResult<FArdaComputeAlternativePlan> PrepareComputeAlternative(const FArdaParameters&,
		    EArdaRHIQueueType) const override
		{
			++mAlternativePreparations;
			return {mPlan, {}};
		}
	};

	TEST_P(FArdaComputeAlternativeGpu, FreezesOrdinaryBuffersConstantsAndDispatchesWithoutNativeKernels)
	{
		constexpr uint32_t Count = 257;
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = Count * 4;
		Desc.mStructureStride = 4;
		Desc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::ShaderResource |
		    EArdaRHIBufferUsage::UnorderedAccess;
		auto Input = mDevice->CreateBuffer(Desc);
		auto Output = mDevice->CreateBuffer(Desc);
		ASSERT_TRUE(Input);
		ASSERT_TRUE(Output);
		EXPECT_FALSE(Input.mValue->GetCudaResourceInfo().mbSharingEnabled);
		FArdaAlternativeTestOperand Operand(mDevice);
		Operand.mPipeline = mPipeline;
		Operand.mLayout = mLayout;
		FArdaComputeOperand& Common = Operand;
		ASSERT_TRUE(Common.CheckRequiredFeatures());
		EXPECT_EQ(Operand.SelectDispatchRoute().mValue, EArdaComputeDispatchRoute::ComputeAlternative);
		EXPECT_EQ(Operand.mAlternativePreparations, 0u);
		FArdaAddParameters Parameters;
		Parameters.mInput.mBuffer = Input.mValue;
		Parameters.mOutput.mBuffer = Output.mValue;
		Parameters.mCount = Count;
		Parameters.mBias = 18;
		auto Plan = Operand.PrepareDispatch(Parameters);
		ASSERT_TRUE(Plan) << Plan.mStatus.mMessage.c_str();
		ASSERT_EQ(Plan.mValue->mRoute, EArdaComputeDispatchRoute::ComputeAlternative);
		EXPECT_TRUE(Plan.mValue->mDispatch.mKernels.empty());
		Parameters = {}; // Preparation owns both resources and constant bytes independently.
		auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		eastl::vector<uint32_t> Values(Count);
		for (uint32_t Index = 0; Index != Count; ++Index)
		{
			Values[Index] = Index * 3;
		}
		ASSERT_TRUE(Commands.mValue->WriteBuffer(*Input.mValue, Values.data(), Count * 4));
		Input.mValue.Reset();
		ASSERT_TRUE(Operand.RecordPlan(*Commands.mValue, Plan.mValue));
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Output.mValue, Bytes));
		ASSERT_TRUE(Commands.mValue->Close());
		Plan.mValue.reset();
		ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
		Commands.mValue.Reset();
		ASSERT_TRUE(mDevice->WaitForIdle());
		ASSERT_EQ(Bytes.size(), Count * 4u);
		for (uint32_t Index = 0; Index != Count; ++Index)
		{
			uint32_t Value = 0;
			std::memcpy(&Value, Bytes.data() + Index * 4, 4);
			ASSERT_EQ(Value, Index * 3 + 18);
		}
		auto SecondOutput = mDevice->CreateBuffer(Desc);
		ASSERT_TRUE(SecondOutput);
		Parameters.mInput.mBuffer = Output.mValue;
		Parameters.mOutput.mBuffer = SecondOutput.mValue;
		Parameters.mCount = Count;
		Parameters.mBias = 11;
		auto Submitted = Operand.Dispatch(Parameters);
		ASSERT_TRUE(Submitted) << Submitted.mStatus.mMessage.c_str();
		EXPECT_EQ(Submitted.mValue.mRoute, EArdaComputeDispatchRoute::ComputeAlternative);
		EXPECT_NE(Submitted.mValue.mInstance, 0u);
		Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*SecondOutput.mValue, Bytes));
		ASSERT_TRUE(Commands.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
		for (uint32_t Index = 0; Index != Count; ++Index)
		{
			uint32_t Value = 0;
			std::memcpy(&Value, Bytes.data() + Index * 4, 4);
			ASSERT_EQ(Value, Index * 3 + 29);
		}
		auto Empty = Operand.Dispatch({});
		ASSERT_TRUE(Empty);
		EXPECT_EQ(Empty.mValue.mRoute, EArdaComputeDispatchRoute::ComputeAlternative);
		EXPECT_EQ(Empty.mValue.mInstance, 0u);
	}

	TEST_P(FArdaComputeAlternativeGpu, PreservesUnsupportedBindingAndPreparationErrors)
	{
		FArdaAlternativeTestOperand Missing(mDevice);
		Missing.mbHasAlternative = false;
		EXPECT_EQ(Missing.CheckRequiredFeatures().mCode, EArdaRHIResult::Unsupported);
		FArdaAlternativeTestOperand Unsupported(mDevice);
		Unsupported.mAlternativeSupport = FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Feature unavailable");
		auto Status = Unsupported.CheckRequiredFeatures();
		EXPECT_EQ(Status.mCode, EArdaRHIResult::Unsupported);
		EXPECT_NE(Status.mMessage.find("compute alternative: Feature unavailable"), eastl::string::npos);
		EXPECT_EQ(Unsupported.mAlternativePreparations, 0u);
		FArdaAlternativeTestOperand Invalid(mDevice);
		Invalid.mbBindInvalid = true;
		EXPECT_EQ(Invalid.CheckRequiredFeatures().mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(Invalid.mAlternativeChecks, 0u);
		FArdaAlternativeTestOperand Failed(mDevice);
		Failed.mbFailPreparation = true;
		EXPECT_EQ(Failed.PrepareDispatch({}).mStatus.mCode, EArdaRHIResult::BackendFailure);
		EXPECT_EQ(Failed.mAlternativePreparations, 1u);
		EXPECT_EQ(Failed.SelectDispatchRoute(EArdaRHIQueueType::Copy).mStatus.mCode, EArdaRHIResult::Unsupported);
		FArdaAlternativePlanTestOperand InvalidPlan(mDevice);
		EXPECT_EQ(InvalidPlan.PrepareDispatch({}).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		InvalidPlan.mPlan.mState.mPipeline = mPipeline;
		InvalidPlan.mPlan.mGroupsX = 0;
		EXPECT_EQ(InvalidPlan.PrepareDispatch({}).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		InvalidPlan.mPlan.mGroupsX = 1;
		InvalidPlan.mPlan.mPushConstants.resize(5);
		EXPECT_EQ(InvalidPlan.PrepareDispatch({}).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		InvalidPlan.mPlan.mPushConstants.resize(4);
		auto Prepared = InvalidPlan.PrepareDispatch({});
		ASSERT_TRUE(Prepared);
		auto ClosedCommands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(ClosedCommands);
		const auto PreparedCount = InvalidPlan.mAlternativePreparations;
		EXPECT_EQ(InvalidPlan.RecordPlan(*ClosedCommands.mValue, Prepared.mValue).mCode, EArdaRHIResult::InvalidState);
		EXPECT_EQ(InvalidPlan.mAlternativePreparations, PreparedCount);
	}

	TEST_P(FArdaComputeAlternativeGpu, PrefersCompatibleCudaAndRejectsAlternativeSequencePlans)
	{
		if (!mDevice->GetCudaCapabilities())
		{
			GTEST_SKIP() << "Native CUDA preference requires a qualified CUDA device";
		}
		FArdaAlternativeTestOperand Native(mDevice);
		Native.mbBindNative = true;
		ASSERT_TRUE(Native.CheckRequiredFeatures());
		EXPECT_EQ(Native.SelectDispatchRoute().mValue, EArdaComputeDispatchRoute::Cuda);
		EXPECT_EQ(Native.mAlternativeChecks, 0u);
		// Resource validation fails after native selection; it must not silently change implementation.
		FArdaAddParameters Invalid;
		Invalid.mCount = 1;
		EXPECT_EQ(Native.PrepareDispatch(Invalid).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(Native.mAlternativePreparations, 0u);
		FArdaAlternativeTestOperand BrokenLibrary(mDevice);
		BrokenLibrary.mExternalCall = eastl::make_shared<FArdaAlternativeSupportTestCall>();
		BrokenLibrary.mExternalCall->mbThrow = true;
		EXPECT_EQ(BrokenLibrary.CheckRequiredFeatures().mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(BrokenLibrary.mAlternativeChecks, 0u);
		EXPECT_EQ(BrokenLibrary.mAlternativePreparations, 0u);
		FArdaAlternativeTestOperand Compute(mDevice);
		auto Plan = Compute.PrepareDispatch({});
		ASSERT_TRUE(Plan);
		FArdaCudaSequence Sequence(mDevice);
		ASSERT_TRUE(Sequence.GetStatus());
		EXPECT_EQ(Sequence.AddPlan(Plan.mValue).mCode, EArdaRHIResult::Unsupported);
		EXPECT_EQ(Sequence.GetOperationCount(), 0u);
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaComputeAlternativeGpu, testing::Values("native-vulkan", "native-d3d12"));
}
