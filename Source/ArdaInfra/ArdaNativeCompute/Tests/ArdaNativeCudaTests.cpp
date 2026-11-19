#include "ArdaBackend.h"
#include "ArdaTestBackend.h"
#include "CUDA/ArdaNativeCudaProvider.h"
#include "RHI/Providers/ArdaBackendProvider.h"

#include <EASTL/algorithm.h>
#include <cstring>
#include <gtest/gtest.h>

namespace arda
{
	namespace
	{
		FArdaNativeCudaSourceDesc MakeNativeCudaFill(bool bCudaCpp)
		{
			FArdaNativeCudaSourceDesc Desc;
			Desc.mModuleKernel.mEntryPoint = "ArdaNativeFill";
			Desc.mModuleKernel.mParameterSize = 16;
			Desc.mModuleKernel.mParameterAlignment = 8;
			Desc.mModuleKernel.mArguments = {{0, 8, 8, EArdaCudaModuleArgumentKind::Buffer},
			    {8, 4, 4, EArdaCudaModuleArgumentKind::Value}};
			Desc.mPatches = {{0, 0, alignof(uint32_t)}};
			if (bCudaCpp)
			{
				Desc.mCudaCpp = R"cuda(
extern "C" __global__ void ArdaNativeFill(unsigned int* Output, unsigned int Base)
{
	Output[threadIdx.x] = Base + threadIdx.x;
}
)cuda";
			}
			else
			{
				constexpr char Source[] = R"ptx(.version 8.0
.target sm_75
.address_size 64
.visible .entry ArdaNativeFill(.param .u64 output, .param .u32 base)
{
 .reg .b32 index, value;
 .reg .b64 pointer, offset;
 mov.u32 index, %tid.x;
 ld.param.u32 value, [base];
 add.u32 value, value, index;
 mul.wide.u32 offset, index, 4;
 ld.param.u64 pointer, [output];
 add.u64 pointer, pointer, offset;
 st.global.u32 [pointer], value;
 ret;
}
)ptx";
				Desc.mModule.mFormat = EArdaCudaModuleFormat::Ptx;
				Desc.mModule.mImage.assign(Source, Source + sizeof(Source) - 1);
				Desc.mModule.mBuild.mIdentity = "ArdaNativeFill.ptx";
				Desc.mModule.mBuild.mArchitectures.push_back({75, false});
			}
			return Desc;
		}

		class FArdaForeignPtxSource final : public IArdaNativeKernelSource
		{
		public:
			const char* GetProviderId() const noexcept override
			{
				return "foreign";
			}

			const char* GetFormat() const noexcept override
			{
				return "ptx";
			}
		};
	}

	TEST(ArdaNativeCuda, RejectsInvalidOrAmbiguousSourceDescriptions)
	{
		EXPECT_FALSE(CreateArdaNativeCudaSource({}));
		auto Desc = MakeNativeCudaFill(false);
		Desc.mCudaCpp = MakeNativeCudaFill(true).mCudaCpp;
		EXPECT_EQ(CreateArdaNativeCudaSource(Desc).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		Desc.mCudaCpp.clear();
		Desc.mModuleKernel.mArguments[0].mSize = 4;
		EXPECT_EQ(CreateArdaNativeCudaSource(Desc).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		Desc = MakeNativeCudaFill(true);
		auto CudaCpp = CreateArdaNativeCudaSource(Desc);
		ASSERT_TRUE(CudaCpp);
		FArdaNativeComputeRegistry Registry;
		FArdaNativeOperandDesc Operand;
		Operand.mId = "cuda-source-registration";
		Operand.mResources = {{"Output", EArdaRHIResourceType::Buffer, EArdaComputeAccess::Write}};
		Operand.mParameterSize = 16;
		Operand.mVariants.push_back({"cuda-cpp", eastl::move(CudaCpp.mValue), {}});
		EXPECT_TRUE(Registry.RegisterOperand(eastl::move(Operand)));
		Desc.mModuleKernel.mArguments[0].mSize = 4;
		EXPECT_EQ(CreateArdaNativeCudaSource(Desc).mStatus.mCode, EArdaRHIResult::InvalidArgument);
	}

	TEST(ArdaNativeCuda, AdvertisesExplicitRegisteredGraphicsNames)
	{
		const auto Provider = CreateArdaNativeCudaProvider({"custom-d3d12"});
		ASSERT_EQ(Provider->GetDesc().mGraphicsApis.size(), 1u);
		EXPECT_STREQ(Provider->GetDesc().mGraphicsApis.front().c_str(), "custom-d3d12");
		const auto CiG = CreateArdaNativeCudaInGraphicsProvider();
		EXPECT_STREQ(CiG->GetDesc().mId.c_str(), ArdaNativeCudaInGraphicsProviderId);
		EXPECT_EQ(CiG->GetDesc().mGraphicsApis, (eastl::vector<eastl::string>{"native-d3d12", "native-vulkan"}));
		EXPECT_EQ(CiG->GetDesc().mSourceFormats, Provider->GetDesc().mSourceFormats);
	}

	enum class EArdaNativeCudaTestMode
	{
		Ptx,
		CudaCpp,
		InvalidCudaCppWithPtxAlternative
	};

	class FArdaNativeCudaGpu : public testing::TestWithParam<const char*>
	{
	protected:
		virtual EArdaCudaExecutionMode GetExecutionMode() const
		{
			return EArdaCudaExecutionMode::ContextSwitch;
		}

		void SetUp() override
		{
			ShutdownBackend();
			auto Config = MakeArdaTestBackendConfiguration();
			Config.mBackendName = GetParam();
			Config.mCudaExecutionMode = GetExecutionMode();
			Config.mMessageCallback = &mDiagnostics;
			if (!FindBackendModule(Config.mBackendName.c_str()))
			{
				GTEST_SKIP() << "Backend not built";
			}
			ASSERT_TRUE(ConfigureBackend(Config));
			ARDA_REQUIRE_BACKEND();
			mDevice = GetDevice();
			if (!mDevice->GetCudaCapabilities())
			{
				GTEST_SKIP() << mDevice->GetCudaCapabilities().mUnavailableReason.c_str();
			}
			const auto Expected = GetExecutionMode() == EArdaCudaExecutionMode::ContextSwitch
			    ? EArdaCudaLaunchMode::ContextSwitch
			    : Config.mBackendName == "native-d3d12" ? EArdaCudaLaunchMode::D3D12CiG
			                                            : EArdaCudaLaunchMode::VulkanCiG;
			ASSERT_EQ(mDevice->GetCudaCapabilities().mLaunchMode, Expected);
		}

		void TearDown() override
		{
			const bool bValidationSkip = testing::Test::IsSkipped() && ArdaTestValidationEnabled &&
			    GetBackendInitializeResult() == EArdaInitializeResult::ValidationUnavailable;
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
			}
			mDevice.Reset();
			ShutdownBackend();
			if (!bValidationSkip)
			{
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
		}

		void RunFill(EArdaNativeCudaTestMode Mode)
		{
			static constexpr uint32_t Count = 16;
			constexpr uint32_t Base = 78241;
			constexpr uint32_t Guard = 0xdeadbeef;
			const bool bCiG = GetExecutionMode() == EArdaCudaExecutionMode::GraphicsQueue;
			auto Cuda = CreateArdaNativeCudaProvider();
			auto CiG = CreateArdaNativeCudaInGraphicsProvider();
			const auto& Provider = bCiG ? CiG : Cuda;
			const bool bCudaCpp = Mode == EArdaNativeCudaTestMode::CudaCpp;
			const bool bInvalidCudaCpp = Mode == EArdaNativeCudaTestMode::InvalidCudaCppWithPtxAlternative;
			auto Source = CreateArdaNativeCudaSource(MakeNativeCudaFill(bCudaCpp));
			ASSERT_TRUE(Source) << Source.mStatus.mMessage.c_str();
			const auto& Formats = Provider->GetDesc().mSourceFormats;
			if ((bCudaCpp || bInvalidCudaCpp) &&
			    eastl::find(Formats.begin(), Formats.end(), "cuda-cpp") == Formats.end())
			{
				GTEST_SKIP() << "NVRTC is unavailable";
			}
			FArdaNativeComputeRegistry Registry;
			ASSERT_TRUE(Registry.RegisterProvider(Cuda));
			ASSERT_TRUE(Registry.RegisterProvider(CiG));
			FArdaNativeOperandDesc Operand;
			Operand.mId = "test-native-cuda-fill";
			Operand.mResources = {{"Output", EArdaRHIResourceType::Buffer, EArdaComputeAccess::Write}};
			Operand.mParameterSize = 16;
			constexpr char BrokenId[] = "test-native-cuda-broken-only";
			if (bInvalidCudaCpp)
			{
				auto Broken = MakeNativeCudaFill(true);
				Broken.mCudaCpp = "extern \"C\" __global__ void ArdaNativeFill(";
				auto BrokenSource = CreateArdaNativeCudaSource(eastl::move(Broken));
				ASSERT_TRUE(BrokenSource);
				Operand.mVariants.push_back({"broken-cuda-cpp", eastl::move(BrokenSource.mValue), {}});
				auto BrokenOnly = Operand;
				BrokenOnly.mId = BrokenId;
				ASSERT_TRUE(Registry.RegisterOperand(eastl::move(BrokenOnly)));
			}
			AddArdaNativeCudaVariants(Operand, {"fill",
			    eastl::move(Source.mValue),
			    [](const FArdaNativeComputeContext&, const FArdaNativeInvocation&) -> TArdaRHIResult<FArdaNativeLaunch>
			    {
				    FArdaNativeLaunch Launch;
				    Launch.mBlockSize[0] = Count;
				    return {Launch, {}};
			    }});
			const auto Slot = Operand.mResources.front();
			const auto Id = Operand.mId;
			// A format string does not grant access to a different provider's internal source ABI.
			auto Foreign = Operand;
			Foreign.mId = "foreign-ptx";
			Foreign.mVariants = {
			    {"foreign", eastl::make_shared<FArdaForeignPtxSource>(), {}, ArdaNativeCudaInGraphicsProviderId}};
			ASSERT_TRUE(Registry.RegisterOperand(eastl::move(Foreign)));
			ASSERT_TRUE(Registry.RegisterOperand(eastl::move(Operand)));
			FArdaNativeComputeContext Context{mDevice,
			    EArdaRHIQueueType::Graphics,
			    EArdaNativeComputePreference::NativeOnly};
			Context.mRequiredProviderId = ArdaNativeCudaInGraphicsProviderId;
			const auto ForeignStatus = Registry.CheckSupport("foreign-ptx", Context);
			EXPECT_EQ(ForeignStatus.mCode, EArdaRHIResult::Unsupported);
			EXPECT_NE(ForeignStatus.mMessage.find("source metadata"), eastl::string::npos);
			if (!bCiG)
			{
				EXPECT_EQ(Registry.CheckSupport(Id.c_str(), Context).mCode, EArdaRHIResult::Unsupported);
				Context.mRequiredProviderId.clear();
			}
			const auto Support = Registry.CheckSupport(Id.c_str(), Context);
			ASSERT_TRUE(Support) << Support.mMessage.c_str();
			auto OtherQueue = Context;
			OtherQueue.mQueue = EArdaRHIQueueType::Copy;
			EXPECT_EQ(Registry.CheckSupport(Id.c_str(), OtherQueue).mCode, EArdaRHIResult::Unsupported);
			OtherQueue.mQueue = EArdaRHIQueueType::Compute;
			EXPECT_EQ(bool(Registry.CheckSupport(Id.c_str(), OtherQueue)),
			    mDevice->GetCudaCapabilities().mLaunchMode != EArdaCudaLaunchMode::D3D12CiG);

			FArdaRHIBufferDesc BufferDesc;
			BufferDesc.mByteSize = (Count + 2) * sizeof(uint32_t);
			BufferDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
			auto GraphicsBuffer = mDevice->CreateBuffer(BufferDesc);
			ASSERT_TRUE(GraphicsBuffer);
			FArdaNativeInvocation Invocation;
			Invocation.mResources.push_back({GraphicsBuffer.mValue, {sizeof(uint32_t), Count * sizeof(uint32_t)}});
			Invocation.mParameters.resize(16);
			std::memcpy(Invocation.mParameters.data() + 8, &Base, sizeof(Base));
			const auto Handoff = Provider->QueryHandoff(Context, Slot, Invocation.mResources.front());
			ASSERT_TRUE(Handoff);
			EXPECT_EQ(Handoff.mValue.mKind, EArdaNativeHandoff::ConversionRequired);
			EXPECT_EQ(Registry.Prepare(Id.c_str(), Context, Invocation).mStatus.mCode, EArdaRHIResult::Unsupported);

			BufferDesc.mbCudaInterop = true;
			auto SharedBuffer = mDevice->CreateBuffer(BufferDesc);
			ASSERT_TRUE(SharedBuffer) << SharedBuffer.mStatus.mMessage.c_str();
			Invocation.mResources.front().mResource = SharedBuffer.mValue;
			if (bInvalidCudaCpp)
			{
				const auto Failed = Registry.Prepare(BrokenId, Context, Invocation);
				ASSERT_EQ(Failed.mStatus.mCode, EArdaRHIResult::BackendFailure);
				EXPECT_NE(Failed.mStatus.mMessage.find("CUDA C++ compilation failed"), eastl::string::npos);
			}
			auto Plan = Registry.Prepare(Id.c_str(), Context, Invocation);
			ASSERT_TRUE(Plan) << Plan.mStatus.mMessage.c_str();
			ASSERT_EQ(Plan.mValue.GetRoute(), EArdaNativeComputeRoute::Native);
			EXPECT_STREQ(Plan.mValue.GetVariantName().c_str(), bCiG ? "fill.cig" : "fill");
			Invocation = {};

			auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Commands);
			ASSERT_TRUE(Commands.mValue->Open());
			const eastl::vector<uint32_t> Initial(Count + 2, Guard);
			ASSERT_TRUE(Commands.mValue->WriteBuffer(*SharedBuffer.mValue, Initial.data(), BufferDesc.mByteSize));
			const auto Recorded = Plan.mValue.Record(*Commands.mValue);
			ASSERT_TRUE(Recorded) << Recorded.mMessage.c_str();
			eastl::vector<uint8_t> Bytes;
			ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*SharedBuffer.mValue, Bytes));
			ASSERT_TRUE(Commands.mValue->Close());
			Plan = {};
			SharedBuffer.mValue.Reset();
			ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
			Commands.mValue.Reset();
			ASSERT_TRUE(mDevice->WaitForIdle());

			ASSERT_EQ(Bytes.size(), BufferDesc.mByteSize);
			for (uint32_t Index = 0; Index < Count + 2; ++Index)
			{
				uint32_t Actual = 0;
				std::memcpy(&Actual, Bytes.data() + Index * sizeof(Actual), sizeof(Actual));
				EXPECT_EQ(Actual, Index == 0 || Index == Count + 1 ? Guard : Base + Index - 1);
			}
		}

		FArdaTestDiagnosticCallback mDiagnostics;
		FArdaRHIDeviceRef mDevice;
	};

	TEST_P(FArdaNativeCudaGpu, ExecutesImportedPtxAndPreservesBufferRange)
	{
		RunFill(EArdaNativeCudaTestMode::Ptx);
	}

	TEST_P(FArdaNativeCudaGpu, CompilesAndExecutesCudaCpp)
	{
		RunFill(EArdaNativeCudaTestMode::CudaCpp);
	}

	TEST_P(FArdaNativeCudaGpu, TriesWorkingPtxVariantAfterNvrtcCompilationFails)
	{
		RunFill(EArdaNativeCudaTestMode::InvalidCudaCppWithPtxAlternative);
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaNativeCudaGpu, testing::Values("native-d3d12", "native-vulkan"));

	class FArdaNativeCudaInGraphicsGpu : public FArdaNativeCudaGpu
	{
	protected:
		EArdaCudaExecutionMode GetExecutionMode() const override
		{
			return EArdaCudaExecutionMode::GraphicsQueue;
		}
	};

	TEST_P(FArdaNativeCudaInGraphicsGpu, RecordsImportedPtxOnTheQualifiedGraphicsQueue)
	{
		RunFill(EArdaNativeCudaTestMode::Ptx);
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaNativeCudaInGraphicsGpu, testing::Values("native-d3d12", "native-vulkan"));
}
