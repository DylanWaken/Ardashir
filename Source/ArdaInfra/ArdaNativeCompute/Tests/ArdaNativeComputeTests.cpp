#include "ArdaNativeCompute.h"
#include "ArdaTestBackend.h"
#include <gtest/gtest.h>

namespace arda
{
	namespace
	{
		class FArdaTestNativeSource final : public IArdaNativeKernelSource
		{
		public:
			const char* GetProviderId() const noexcept override
			{
				return "example";
			}

			const char* GetFormat() const noexcept override
			{
				return "example-image";
			}
		};

		// A provider implemented entirely against the public API. Inject failures on either side
		// of the recording boundary to catch accidental retries of partially recorded work.
		class FArdaTestNativeProvider final : public IArdaNativeComputeProvider
		{
		public:
			FArdaNativeProviderDesc mDesc{"example", {"native-vulkan", "native-d3d12"}, {"example-image"}};
			FArdaRHIStatus mPrepareStatus;
			FArdaRHIStatus mRecordStatus;

			const FArdaNativeProviderDesc& GetDesc() const noexcept override
			{
				return mDesc;
			}

			FArdaRHIStatus CheckSupport(const FArdaNativeComputeContext&,
			    const FArdaNativeOperandDesc&,
			    const IArdaNativeKernelSource&) const override
			{
				return {};
			}

			TArdaRHIResult<FArdaNativeResourceHandoff> QueryHandoff(const FArdaNativeComputeContext&,
			    const FArdaNativeResourceSlot&,
			    const FArdaNativeResourceView&) const override
			{
				return {{EArdaNativeHandoff::ConversionRequired, EArdaRHIResourceState::UnorderedAccess}, {}};
			}

			TArdaRHIResult<FArdaNativeWorkRef> Prepare(const FArdaNativeComputeContext&,
			    const FArdaNativeOperandDesc&,
			    const IArdaNativeKernelSource&,
			    const FArdaNativeInvocation&,
			    const FArdaNativeLaunch&) const override
			{
				return {MakeArdaNativeWork(
				            [Status = mRecordStatus](IArdaRHICommandList&)
				            {
					            return Status;
				            }),
				    mPrepareStatus};
			}
		};

		FArdaNativeOperandDesc MakeOperand()
		{
			FArdaNativeOperandDesc Operand;
			Operand.mId = "example.fill";
			Operand.mVariants.push_back({"baseline", eastl::make_shared<FArdaTestNativeSource>(), {}});
			Operand.mFallback.mPrepare = [](const FArdaNativeComputeContext&,
			                                 const FArdaNativeInvocation&) -> TArdaRHIResult<FArdaNativeWorkRef>
			{
				return {MakeArdaNativeWork(
				            [](IArdaRHICommandList&)
				            {
					            return FArdaRHIStatus{};
				            }),
				    {}};
			};
			return Operand;
		}
	}

	TEST(ArdaNativeComputeRegistry, RejectsCollisionsWithoutReplacingDefinitions)
	{
		FArdaNativeComputeRegistry Registry;
		auto Provider = eastl::make_shared<FArdaTestNativeProvider>();
		ASSERT_TRUE(Registry.RegisterProvider(Provider));
		EXPECT_FALSE(Registry.RegisterProvider(eastl::make_shared<FArdaTestNativeProvider>()));
		EXPECT_EQ(Registry.FindProvider("example"), Provider);

		auto Operand = MakeOperand();
		Operand.mVariants.push_back(Operand.mVariants.front());
		EXPECT_FALSE(Registry.RegisterOperand(Operand));
		EXPECT_FALSE(Registry.FindOperand("example.fill"));
		Operand.mVariants.pop_back();
		ASSERT_TRUE(Registry.RegisterOperand(Operand));
		Operand.mParameterSize = 64;
		EXPECT_FALSE(Registry.RegisterOperand(Operand));
		ASSERT_TRUE(Registry.FindOperand("example.fill"));
		EXPECT_EQ(Registry.FindOperand("example.fill")->mParameterSize, 0u);
	}

	class FArdaNativeRegistryDevice : public FArdaTestBackendFixture
	{
	};

	TEST_P(FArdaNativeRegistryDevice, FiltersTheSelectedProviderWhenTwoVariantsShareASource)
	{
		FArdaNativeComputeRegistry Registry;
		auto Default = eastl::make_shared<FArdaTestNativeProvider>();
		auto Other = eastl::make_shared<FArdaTestNativeProvider>();
		Other->mDesc.mId = "other";
		ASSERT_TRUE(Registry.RegisterProvider(Default));
		ASSERT_TRUE(Registry.RegisterProvider(Other));
		auto Operand = MakeOperand();
		auto Variant = Operand.mVariants.front();
		Variant.mName = "other-route";
		Variant.mProviderId = "other";
		Operand.mVariants.insert(Operand.mVariants.begin(), eastl::move(Variant));
		ASSERT_TRUE(Registry.RegisterOperand(eastl::move(Operand)));

		FArdaNativeComputeContext Context{mDevice,
		    EArdaRHIQueueType::Graphics,
		    EArdaNativeComputePreference::NativeOnly};
		Context.mAllowedProviderIds = {"example", "other"};
		auto Either = Registry.Prepare("example.fill", Context, {});
		ASSERT_TRUE(Either);
		EXPECT_STREQ(Either.mValue.GetVariantName().c_str(), "other-route");
		Context.mAllowedProviderIds = {"example"};
		auto Allowed = Registry.Prepare("example.fill", Context, {});
		ASSERT_TRUE(Allowed);
		EXPECT_STREQ(Allowed.mValue.GetVariantName().c_str(), "baseline");
		Context.mRequiredProviderId = "other";
		EXPECT_EQ(Registry.CheckSupport("example.fill", Context).mCode, EArdaRHIResult::Unsupported);
		Context.mAllowedProviderIds.clear();
		auto Required = Registry.Prepare("example.fill", Context, {});
		ASSERT_TRUE(Required);
		EXPECT_STREQ(Required.mValue.GetVariantName().c_str(), "other-route");
	}

	TEST_P(FArdaNativeRegistryDevice, TriesVariantsInOrderAndFreezesBeforeRecording)
	{
		FArdaNativeComputeRegistry Registry;
		auto Provider = eastl::make_shared<FArdaTestNativeProvider>();
		Provider->mRecordStatus = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Recording failed.");
		ASSERT_TRUE(Registry.RegisterProvider(Provider));
		auto Operand = MakeOperand();
		auto Tuned = Operand.mVariants.front();
		Tuned.mName = "wide";
		Tuned.mSelect = [](const FArdaNativeComputeContext&,
		                    const FArdaNativeInvocation& Invocation) -> TArdaRHIResult<FArdaNativeLaunch>
		{
			if (Invocation.mLaunch.mBlockSize[0] < 128)
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Use the baseline for short inputs.")};
			}
			return {Invocation.mLaunch, {}};
		};
		Operand.mVariants.insert(Operand.mVariants.begin(), eastl::move(Tuned));
		ASSERT_TRUE(Registry.RegisterOperand(eastl::move(Operand)));
		FArdaNativeComputeContext Context{mDevice};
		FArdaNativeInvocation Invocation;
		ASSERT_TRUE(Registry.CheckSupport("example.fill", Context));
		auto Short = Registry.Prepare("example.fill", Context, Invocation);
		ASSERT_TRUE(Short);
		EXPECT_STREQ(Short.mValue.GetVariantName().c_str(), "baseline");
		Invocation.mLaunch.mBlockSize[0] = 256;
		auto Wide = Registry.Prepare("example.fill", Context, Invocation);
		ASSERT_TRUE(Wide);
		EXPECT_STREQ(Wide.mValue.GetVariantName().c_str(), "wide");

		auto Commands = mDevice->CreateCommandList(Context.mQueue);
		ASSERT_TRUE(Commands);
		EXPECT_EQ(Commands.mValue->RecordNativeCompute(Wide.mValue).mCode, EArdaRHIResult::InvalidState);
		ASSERT_TRUE(Commands.mValue->Open());
		EXPECT_EQ(Commands.mValue->RecordNativeCompute(Wide.mValue).mCode, EArdaRHIResult::BackendFailure);
		EXPECT_FALSE(Commands.mValue->Close());
	}

	TEST_P(FArdaNativeRegistryDevice, ShaderFallbackHandlesProviderAbsenceAndPreparationFailures)
	{
		FArdaNativeComputeRegistry Registry;
		auto Operand = MakeOperand();
		ASSERT_TRUE(Registry.RegisterOperand(Operand));
		FArdaNativeComputeContext Context{mDevice};
		ASSERT_TRUE(Registry.CheckSupport("example.fill", Context));
		auto Plan = Registry.Prepare("example.fill", Context, {});
		ASSERT_TRUE(Plan);
		EXPECT_EQ(Plan.mValue.GetRoute(), EArdaNativeComputeRoute::ComputeShader);
		auto Provider = eastl::make_shared<FArdaTestNativeProvider>();
		Provider->mPrepareStatus = FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Compiler failed.");
		ASSERT_TRUE(Registry.RegisterProvider(Provider));
		auto Next = Registry.Prepare("example.fill", Context, {});
		ASSERT_TRUE(Next);
		EXPECT_EQ(Next.mValue.GetRoute(), EArdaNativeComputeRoute::ComputeShader);
		Context.mPreference = EArdaNativeComputePreference::NativeOnly;
		EXPECT_EQ(Registry.Prepare("example.fill", Context, {}).mStatus.mCode, EArdaRHIResult::BackendFailure);

		Operand.mId = "example.store";
		Operand.mResources = {{"output", EArdaRHIResourceType::Buffer, EArdaComputeAccess::Write}};
		ASSERT_TRUE(Registry.RegisterOperand(Operand));
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = 16;
		BufferDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		auto Buffer = mDevice->CreateBuffer(BufferDesc);
		ASSERT_TRUE(Buffer);
		FArdaNativeInvocation Invocation;
		Invocation.mResources.push_back({Buffer.mValue});
		EXPECT_EQ(Registry.Prepare("example.store", Context, Invocation).mStatus.mCode, EArdaRHIResult::Unsupported);
		Context.mPreference = EArdaNativeComputePreference::PreferNative;
		auto Converted = Registry.Prepare("example.store", Context, Invocation);
		ASSERT_TRUE(Converted);
		EXPECT_EQ(Converted.mValue.GetRoute(), EArdaNativeComputeRoute::ComputeShader);
		Invocation.mResources[0].mBufferRange = {12, 8};
		EXPECT_EQ(Registry.Prepare("example.store", Context, Invocation).mStatus.mCode,
		    EArdaRHIResult::InvalidArgument);
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaNativeRegistryDevice, testing::Values("native-d3d12", "native-vulkan"));
}
