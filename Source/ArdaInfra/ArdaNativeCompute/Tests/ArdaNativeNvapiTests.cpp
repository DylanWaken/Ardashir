#include "ArdaBackend.h"
#include "ArdaTestBackend.h"
#include "NVAPI/ArdaNativeNvapiProvider.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include <cstring>
#include <fstream>
#include <gtest/gtest.h>

namespace arda
{
	TEST(ArdaNativeNvapi, RejectsMissingShaderAndAdvertisesOnlyD3D12)
	{
		EXPECT_FALSE(CreateArdaNativeNvapiSource({}));
		const auto Provider = CreateArdaNativeNvapiProvider();
		ASSERT_EQ(Provider->GetDesc().mGraphicsApis.size(), 1u);
		EXPECT_EQ(Provider->GetDesc().mGraphicsApis.front(), "native-d3d12");
		EXPECT_EQ(Provider->GetDesc().mId, "nvapi");
	}

#ifdef ARDA_NVAPI_TEST_SHADER
	namespace
	{
		FArdaNativeNvapiSourceDesc LoadNvapiTestSource()
		{
			FArdaNativeNvapiSourceDesc Source;
			std::ifstream File(ARDA_NVAPI_TEST_SHADER, std::ios::binary | std::ios::ate);
			if (!File) return Source;
			Source.mDxil.resize(static_cast<size_t>(File.tellg()));
			File.seekg(0);
			File.read(reinterpret_cast<char*>(Source.mDxil.data()), static_cast<std::streamsize>(Source.mDxil.size()));
			Source.mRequiredOpcodes = {8}; // NV_EXTN_OP_GET_LANE_ID
			return Source;
		}
	}

	TEST(ArdaNativeNvapi, RejectsMisdeclaredExtensionSlotAndTruncatedDxil)
	{
		auto Source = LoadNvapiTestSource();
		ASSERT_TRUE(CreateArdaNativeNvapiSource(Source));
		Source.mExtensionRegisterSpace = 2;
		EXPECT_FALSE(CreateArdaNativeNvapiSource(Source));
		Source.mExtensionRegisterSpace = 1;
		Source.mDxil.resize(32);
		EXPECT_FALSE(CreateArdaNativeNvapiSource(eastl::move(Source)));
	}

	class FArdaNativeNvapiGpu : public testing::TestWithParam<const char*>
	{
	protected:
		void SetUp() override
		{
			ShutdownBackend();
			if (!FindBackendModule(GetParam())) GTEST_SKIP() << "Backend was not built";
			auto Config = MakeArdaTestBackendConfiguration();
			Config.mBackendName = GetParam();
			Config.mMessageCallback = &mDiagnostics;
			ASSERT_TRUE(ConfigureBackend(Config));
			ARDA_REQUIRE_BACKEND();
			mDevice = GetDevice();
		}
		void TearDown() override
		{
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
				mDevice.Reset();
				EXPECT_EQ(mDiagnostics.GetErrorCount(), 0u);
			}
			ShutdownBackend();
		}
		FArdaTestDiagnosticCallback mDiagnostics;
		FArdaRHIDeviceRef mDevice;
	};

	TEST_P(FArdaNativeNvapiGpu, QualifiesOpcodesAndExecutesVendorLaneIntrinsic)
	{
		auto SourceDesc = LoadNvapiTestSource();
		ASSERT_FALSE(SourceDesc.mDxil.empty());
		const auto Source = CreateArdaNativeNvapiSource(SourceDesc);
		ASSERT_TRUE(Source);
		FArdaNativeComputeRegistry Registry;
		const auto Provider = CreateArdaNativeNvapiProvider();
		ASSERT_TRUE(Registry.RegisterProvider(Provider));
		FArdaNativeOperandDesc Operand;
		Operand.mId = "nvapi-lane-id";
		Operand.mResources = {{"Output", EArdaRHIResourceType::Buffer, EArdaComputeAccess::Write}};
		Operand.mParameterSize = 4;
		Operand.mVariants.push_back({"vendor-lane-id", Source.mValue, {}});
		ASSERT_TRUE(Registry.RegisterOperand(Operand));
		const FArdaNativeComputeContext Context{mDevice, EArdaRHIQueueType::Graphics, EArdaNativeComputePreference::NativeOnly};
		auto Large = Operand;
		Large.mParameterSize = 252;
		EXPECT_EQ(Provider->CheckSupport(Context, Large, *Source.mValue).mCode, EArdaRHIResult::Unsupported);
		const auto Supported = Registry.CheckSupport(Operand.mId.c_str(), Context);
		if (std::strcmp(GetParam(), "native-vulkan") == 0)
		{
			EXPECT_EQ(Supported.mCode, EArdaRHIResult::Unsupported);
			return;
		}
		if (!Supported) GTEST_SKIP() << Supported.mMessage.c_str();

		constexpr uint32_t Base = 4331, Guard = 93711, Count = 32;
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = (Count + 8) * sizeof(uint32_t);
		Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::Raw;
		auto Buffer = mDevice->CreateBuffer(Desc);
		ASSERT_TRUE(Buffer);
		FArdaNativeInvocation Invocation;
		Invocation.mResources.push_back({Buffer.mValue, {16, Count * sizeof(uint32_t)}});
		auto Misaligned = Invocation.mResources.front();
		Misaligned.mBufferRange.mByteOffset = 1;
		const auto Handoff = Provider->QueryHandoff(Context, Operand.mResources.front(), Misaligned);
		ASSERT_TRUE(Handoff);
		EXPECT_EQ(Handoff.mValue.mKind, EArdaNativeHandoff::ConversionRequired);
		Invocation.mParameters.resize(sizeof(Base));
		std::memcpy(Invocation.mParameters.data(), &Base, sizeof(Base));
		SourceDesc.mRequiredOpcodes = {0xffffffffu};
		const auto UnknownOpcode = CreateArdaNativeNvapiSource(eastl::move(SourceDesc));
		ASSERT_TRUE(UnknownOpcode);
		Operand.mId = "unsupported-nvapi";
		Operand.mVariants.front().mSource = UnknownOpcode.mValue;
		Operand.mFallback.mPrepare = [](const FArdaNativeComputeContext&, const FArdaNativeInvocation&)
		    -> TArdaRHIResult<FArdaNativeWorkRef>
		{
			return {MakeArdaNativeWork([](IArdaRHICommandList&) { return FArdaRHIStatus{}; }), {}};
		};
		ASSERT_TRUE(Registry.RegisterOperand(Operand));
		EXPECT_EQ(Registry.CheckSupport("unsupported-nvapi", Context).mCode, EArdaRHIResult::Unsupported);
		const auto Fallback = Registry.Prepare("unsupported-nvapi", {mDevice}, Invocation);
		ASSERT_TRUE(Fallback) << Fallback.mStatus.mMessage.c_str();
		EXPECT_EQ(Fallback.mValue.GetRoute(), EArdaNativeComputeRoute::ComputeShader);

		for (const auto Queue : {EArdaRHIQueueType::Graphics, EArdaRHIQueueType::Compute})
		{
			auto QueueContext = Context;
			QueueContext.mQueue = Queue;
			auto Plan = Registry.Prepare("nvapi-lane-id", QueueContext, Invocation);
			ASSERT_TRUE(Plan) << Plan.mStatus.mMessage.c_str();
			EXPECT_EQ(Plan.mValue.GetRoute(), EArdaNativeComputeRoute::Native);
			auto Commands = mDevice->CreateCommandList(Queue);
			ASSERT_TRUE(Commands);
			ASSERT_TRUE(Commands.mValue->Open());
			eastl::vector<uint32_t> Initial(Count + 8, Guard);
			ASSERT_TRUE(Commands.mValue->CopyBufferHostToDevice(*Buffer.mValue, Initial.data(), Desc.mByteSize));
			ASSERT_TRUE(Commands.mValue->SetBufferState(*Buffer.mValue, EArdaRHIResourceState::UnorderedAccess));
			Commands.mValue->CommitBarriers();
			const auto Recorded = Plan.mValue.Record(*Commands.mValue);
			ASSERT_TRUE(Recorded) << Recorded.mMessage.c_str();
			eastl::vector<uint8_t> Bytes;
			ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
			ASSERT_TRUE(Commands.mValue->Close());
			Plan = {};
			ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
			ASSERT_TRUE(mDevice->WaitForIdle());
			ASSERT_EQ(Bytes.size(), Desc.mByteSize);
			for (uint32_t Index = 0; Index < Count + 8; ++Index)
			{
				uint32_t Word;
				std::memcpy(&Word, Bytes.data() + Index * sizeof(Word), sizeof(Word));
				EXPECT_EQ(Word, Index < 4 || Index >= Count + 4 ? Guard : Base + Index - 4);
			}
		}
	}
	INSTANTIATE_TEST_SUITE_P(Native, FArdaNativeNvapiGpu, testing::Values("native-d3d12", "native-vulkan"));
#endif
}
