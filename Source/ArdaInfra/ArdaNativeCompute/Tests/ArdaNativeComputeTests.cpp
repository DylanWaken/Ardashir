#include "NativeCompute/ArdaNativeCompute.h"
#include "ArdaTestBackend.h"
#include <gtest/gtest.h>
#include "FileOperations/ArdaFileOperations.h"
#include <fstream>

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
			mutable unsigned mProbeCount = 0;
			mutable eastl::vector<size_t> mBatchSizes;
			FArdaNativeProviderSupport mSupport;
			FArdaRHIStatus mProbeStatus;
			eastl::function<void()> mOnRecord;
			FArdaRHIStatus mPrepareStatus;

			TArdaRHIResult<FArdaNativeProviderSupport> ProbeDevice(const IArdaRHIDevice&) const override
			{
				++mProbeCount;
				return {mSupport, mProbeStatus};
			}

			FArdaRHIStatus RecordBatch(IArdaRHICommandList& Commands, const eastl::vector<FArdaNativeWorkRef>& Work) const override
			{
				mBatchSizes.push_back(Work.size());
				for (const auto& Item : Work)
				{
					if (auto Status = Item->Record(Commands); !Status) return Status;
				}
				return {};
			}

			FArdaRHIStatus mRecordStatus;

			const FArdaNativeProviderDesc& GetDesc() const noexcept override
			{
				return mDesc;
			}

			FArdaRHIStatus CheckSupport(const FArdaNativeComputeContext&,
			    const FArdaNativeOperand&,
			    const IArdaNativeKernelSource&) const override
			{
				return {};
			}

			TArdaRHIResult<FArdaNativeResourceHandoff> QueryHandoff(const FArdaNativeComputeContext&,
			    const FArdaNativeResourceSlot& Slot,
			    const FArdaNativeResourceView& View) const override
			{
				if (Slot.mType == EArdaRHIResourceType::Count && View.mNativeResource)
					return {{EArdaNativeHandoff::Direct, EArdaRHIResourceState::Common}, {}};
				return {{EArdaNativeHandoff::ConversionRequired, EArdaRHIResourceState::UnorderedAccess}, {}};
			}

			TArdaRHIResult<FArdaNativeWorkRef> Prepare(const FArdaNativeComputeContext&,
			    const FArdaNativeOperand&,
			    const IArdaNativeKernelSource&,
			    const FArdaNativeInvocation&,
			    const FArdaNativeLaunch&) const override
			{
				return {MakeArdaNativeWork(
				            [Status = mRecordStatus, OnRecord = mOnRecord](IArdaRHICommandList&)
				            {
					            if (OnRecord) OnRecord();
					            return Status;
				            }),
				    mPrepareStatus};
			}
		};

		FArdaNativeOperand MakeOperand()
		{
			FArdaNativeOperand Operand;
			UseArdaNativeDirectResources(Operand);
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

	TEST(ArdaNativeComputeRegistry, FinalizesResourceSlotsInBindingOrderAndCanStartAnotherList)
	{
		FArdaNativeResourceSlotsCreator Slots;
		Slots.AddBuffer("Input", EArdaComputeAccess::Read)
		    .AddTexture("Output", EArdaComputeAccess::Write, EArdaRHIFormat::RGBA8UInt, "surface");
		FArdaNativeResourceSlot Scratch;
		Scratch.mName = "Scratch";
		Scratch.mType = EArdaRHIResourceType::Count;
		Scratch.mAccess = EArdaComputeAccess::ReadWrite;
		Scratch.mCategory = "opaque";
		Slots.AddSlot(Scratch);

		const auto First = Slots.Finalize();
		EXPECT_TRUE(Slots.Finalize().empty());
		Slots.AddBuffer("Next", EArdaComputeAccess::ReadWrite, "device-buffer");
		const auto Second = Slots.Finalize();

		ASSERT_EQ(First.size(), 3u);
		EXPECT_STREQ(First[0].mName.c_str(), "Input");
		EXPECT_EQ(First[0].mType, EArdaRHIResourceType::Buffer);
		EXPECT_EQ(First[0].mAccess, EArdaComputeAccess::Read);
		EXPECT_EQ(First[0].mFormat, EArdaRHIFormat::Unknown);
		EXPECT_TRUE(First[0].mCategory.empty());
		EXPECT_STREQ(First[1].mName.c_str(), "Output");
		EXPECT_EQ(First[1].mType, EArdaRHIResourceType::Texture);
		EXPECT_EQ(First[1].mAccess, EArdaComputeAccess::Write);
		EXPECT_EQ(First[1].mFormat, EArdaRHIFormat::RGBA8UInt);
		EXPECT_STREQ(First[1].mCategory.c_str(), "surface");
		EXPECT_STREQ(First[2].mName.c_str(), "Scratch");
		EXPECT_EQ(First[2].mType, EArdaRHIResourceType::Count);
		EXPECT_STREQ(First[2].mCategory.c_str(), "opaque");
		ASSERT_EQ(Second.size(), 1u);
		EXPECT_STREQ(Second[0].mName.c_str(), "Next");
		EXPECT_STREQ(Second[0].mCategory.c_str(), "device-buffer");
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

	TEST(ArdaNativeComputeRegistry, RequiresBothBoundaryHooksAndScopesResourceCategoriesByProvider)
	{
		FArdaNativeComputeRegistry Registry;
		auto Operand = MakeOperand();
		Operand.mNativeToGraphics = {};
		EXPECT_FALSE(Registry.RegisterOperand(Operand));
		Operand.mGraphicsToNative = {};
		EXPECT_FALSE(Registry.RegisterOperand(Operand));
		UseArdaNativeDirectResources(Operand);
		EXPECT_FALSE(HasArdaNativeResourceConversions(Operand));
		ASSERT_TRUE(Registry.RegisterOperand(eastl::move(Operand)));
		auto Provider = eastl::make_shared<FArdaTestNativeProvider>();
		Provider->mDesc.mResourceCategories = {{"linear", EArdaRHIResourceType::Buffer}};
		Provider->mDesc.mResourceCategories.push_back(Provider->mDesc.mResourceCategories.front());
		EXPECT_FALSE(Registry.RegisterProvider(Provider));
		Provider->mDesc.mResourceCategories.pop_back();
		ASSERT_TRUE(Registry.RegisterProvider(Provider));
		auto Category = Registry.FindResourceCategory("example", "linear");
		ASSERT_TRUE(Category);
		EXPECT_EQ(Category.mValue.mType, EArdaRHIResourceType::Buffer);
		EXPECT_FALSE(Registry.FindResourceCategory("absent", "linear"));
	}

	TEST_P(FArdaNativeRegistryDevice, ConversionCanBindProviderOwnedStorage)
	{
		struct FArdaTestNativeTile final : IArdaNativeResource
		{
			const char* GetProviderId() const noexcept override { return "example"; }
			const char* GetCategoryId() const noexcept override { return "tile"; }
		};
		FArdaNativeComputeRegistry Registry;
		auto Provider = eastl::make_shared<FArdaTestNativeProvider>();
		Provider->mDesc.mResourceCategories = {{"tile", EArdaRHIResourceType::Count}};
		ASSERT_TRUE(Registry.RegisterProvider(Provider));
		auto Operand = MakeOperand();
		Operand.mResources = {{"input", EArdaRHIResourceType::Buffer, EArdaComputeAccess::Read}};
		Operand.mVariants.front().mNativeResources = {{"input", EArdaRHIResourceType::Count,
		    EArdaComputeAccess::Read, EArdaRHIFormat::Unknown, "tile"}};
		Operand.mVariants.front().mGraphicsStates = {EArdaRHIResourceState::Common};
		Operand.mGraphicsToNative = [](const auto&, FArdaNativeConversion& Conversion)
		{
			auto& View = Conversion.mNative.mResources.front();
			View.mResource.Reset();
			View.mNativeResource = eastl::make_shared<FArdaTestNativeTile>();
			return FArdaRHIStatus{};
		};
		ASSERT_TRUE(Registry.RegisterOperand(Operand));
		FArdaRHIBufferDesc Desc;
		Desc.mByteSize = 16;
		Desc.mUsage = EArdaRHIBufferUsage::ShaderResource;
		auto Buffer = mDevice->CreateBuffer(Desc);
		ASSERT_TRUE(Buffer);
		FArdaNativeInvocation Invocation;
		Invocation.mResources.push_back({Buffer.mValue});
		FArdaNativeComputeContext Context{mDevice};
		Context.mPreference = EArdaNativeComputePreference::NativeOnly;
		auto Plan = Registry.Prepare("example.fill", Context, Invocation);
		ASSERT_TRUE(Plan) << Plan.mStatus.mMessage.c_str();
		EXPECT_EQ(Plan.mValue.GetRoute(), EArdaNativeComputeRoute::Native);
	}

	TEST_P(FArdaNativeRegistryDevice, ConversionWorkSurroundsTheNativeBatchAndFallbackDiscardsIt)
	{
		FArdaNativeComputeRegistry Registry;
		eastl::vector<int> Recorded;
		auto Provider = eastl::make_shared<FArdaTestNativeProvider>();
		Provider->mSupport.mMode = EArdaNativeExecutionMode::ContextSwitching;
		Provider->mOnRecord = [&] { Recorded.push_back(2); };
		ASSERT_TRUE(Registry.RegisterProvider(Provider));
		auto Operand = MakeOperand();
		auto Mark = [&](int Value) { return MakeArdaNativeWork([&, Value](IArdaRHICommandList&)
		{
			Recorded.push_back(Value);
			return FArdaRHIStatus{};
		}); };
		Operand.mGraphicsToNative = [&](const FArdaNativeConversionContext&, FArdaNativeConversion& Conversion)
		{
			Conversion.mBeforeNative.push_back(Mark(1));
			return FArdaRHIStatus{};
		};
		Operand.mNativeToGraphics = [&](const FArdaNativeConversionContext&, FArdaNativeConversion& Conversion)
		{
			Conversion.mAfterNative.push_back(Mark(3));
			return FArdaRHIStatus{};
		};
		ASSERT_TRUE(HasArdaNativeResourceConversions(Operand));
		ASSERT_TRUE(Registry.RegisterOperand(Operand));
		auto Context = FArdaNativeComputeContext{mDevice};
		auto Plan = Registry.Prepare("example.fill", Context, {});
		ASSERT_TRUE(Plan);
		EXPECT_TRUE(Recorded.empty());
		auto Commands = mDevice->CreateCommandList(Context.mQueue);
		ASSERT_TRUE(Commands && Commands.mValue->Open());
		FArdaNativeComputeSession Session;
		ASSERT_TRUE(Session.Append(Plan.mValue, *Commands.mValue));
		ASSERT_TRUE(Session.Flush(*Commands.mValue));
		EXPECT_EQ(Recorded, (eastl::vector<int>{1, 2, 3}));
		EXPECT_EQ(Provider->mBatchSizes, (eastl::vector<size_t>{1}));
		Operand.mId = "conversion-fallback";
		Operand.mNativeToGraphics = [](const auto&, auto&)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Cannot restore the destination format.");
		};
		ASSERT_TRUE(Registry.RegisterOperand(Operand));
		auto Shader = Registry.Prepare("conversion-fallback", Context, {});
		ASSERT_TRUE(Shader);
		EXPECT_EQ(Shader.mValue.GetRoute(), EArdaNativeComputeRoute::ComputeShader);
		ASSERT_TRUE(Session.Append(Shader.mValue, *Commands.mValue));
		EXPECT_EQ(Recorded, (eastl::vector<int>{1, 2, 3}));
		ASSERT_TRUE(Commands.mValue->Close());
	}

	TEST_P(FArdaNativeRegistryDevice, OperandSelectsFirstEligibleShaderFallback)
	{
		FArdaNativeComputeRegistry Registry;
		auto Operand = MakeOperand();
		auto Compute = eastl::move(Operand.mFallback);
		auto Graphics = Compute;
		Graphics.mRoute = EArdaNativeComputeRoute::GraphicsShader;
		Operand.mShaderFallbacks = {Graphics, Compute};
		Operand.mFallback = {};
		ASSERT_TRUE(Registry.RegisterOperand(eastl::move(Operand)));
		FArdaNativeComputeContext Context{mDevice};
		auto OnGraphics = Registry.Prepare("example.fill", Context, {});
		ASSERT_TRUE(OnGraphics);
		EXPECT_EQ(OnGraphics.mValue.GetRoute(), EArdaNativeComputeRoute::GraphicsShader);
		if (mDevice->GetFeatures()->Supports("queue.compute"))
		{
			Context.mQueue = EArdaRHIQueueType::Compute;
			auto OnCompute = Registry.Prepare("example.fill", Context, {});
			ASSERT_TRUE(OnCompute);
			EXPECT_EQ(OnCompute.mValue.GetRoute(), EArdaNativeComputeRoute::ComputeShader);
		}
	}

	TEST_P(FArdaNativeRegistryDevice, FailedProbeDisablesOnlyThatProviderOnTheDevice)
	{
		FArdaNativeComputeRegistry Registry;
		auto Disabled = eastl::make_shared<FArdaTestNativeProvider>();
		Disabled->mProbeStatus = FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Driver is unavailable.");
		ASSERT_TRUE(Registry.RegisterProvider(Disabled));
		ASSERT_TRUE(Registry.RegisterOperand(MakeOperand()));
		FArdaNativeComputeContext Context{mDevice};
		Registry.ProbeDeviceProviders(mDevice);
		EXPECT_FALSE(Registry.QueryProviderSupport("example", Context));
		auto Plan = Registry.Prepare("example.fill", Context, {});
		ASSERT_TRUE(Plan);
		EXPECT_EQ(Plan.mValue.GetRoute(), EArdaNativeComputeRoute::ComputeShader);
		EXPECT_EQ(Disabled->mProbeCount, 1u);

		FArdaNativeComputeRegistry Independent;
		auto Enabled = eastl::make_shared<FArdaTestNativeProvider>();
		Enabled->mSupport.mbRuntimeCompilation = true;
		ASSERT_TRUE(Independent.RegisterProvider(Enabled));
		auto Support = Independent.QueryProviderSupport("example", Context);
		ASSERT_TRUE(Support);
		EXPECT_TRUE(Support.mValue.mbRuntimeCompilation);
		EXPECT_EQ(Enabled->mProbeCount, 1u);
	}

	TEST_P(FArdaNativeRegistryDevice, SessionFlushesAtShaderAndCommandListBoundaries)
	{
		FArdaNativeComputeRegistry Registry;
		auto Provider = eastl::make_shared<FArdaTestNativeProvider>();
		Provider->mSupport.mMode = EArdaNativeExecutionMode::ContextSwitching;
		ASSERT_TRUE(Registry.RegisterProvider(Provider));
		ASSERT_TRUE(Registry.RegisterOperand(MakeOperand()));
		FArdaNativeComputeContext Context{mDevice};
		auto Plan = Registry.Prepare("example.fill", Context, {});
		ASSERT_TRUE(Plan);
		EXPECT_EQ(Plan.mValue.GetExecutionMode(), EArdaNativeExecutionMode::ContextSwitching);
		auto Commands = mDevice->CreateCommandList(Context.mQueue);
		auto Other = mDevice->CreateCommandList(Context.mQueue);
		ASSERT_TRUE(Commands && Other);
		ASSERT_TRUE(Commands.mValue->Open());
		FArdaNativeComputeSession Session;
		ASSERT_TRUE(Session.Append(Plan.mValue, *Commands.mValue));
		EXPECT_FALSE(Session.Flush(*Other.mValue));
		ASSERT_TRUE(Session.Append(Plan.mValue, *Commands.mValue));
		EXPECT_TRUE(Provider->mBatchSizes.empty());

		Context.mPreference = EArdaNativeComputePreference::ShaderOnly;
		auto Shader = Registry.Prepare("example.fill", Context, {});
		ASSERT_TRUE(Shader);
		ASSERT_TRUE(Session.Append(Shader.mValue, *Commands.mValue));
		EXPECT_EQ(Provider->mBatchSizes, (eastl::vector<size_t>{2}));
		ASSERT_TRUE(Session.Append(Plan.mValue, *Commands.mValue));
		ASSERT_TRUE(Session.Flush(*Commands.mValue));
		ASSERT_TRUE(Session.Flush(*Commands.mValue));
		EXPECT_EQ(Provider->mBatchSizes, (eastl::vector<size_t>{2, 1}));
		ASSERT_TRUE(Commands.mValue->Close());
	}

	TEST(ArdaNativeKernelLibrary, ValidatesTheWholeLibraryBeforePublishingEntries)
	{
		const FArdaNativeKernelKey Key{"arda-atomic-a", "sample", "operand", "v1"};
		const FArdaNativeKernelBinary Binary{"sample-format", "entry", {1, 2}, {81, 82, 83}};
		ASSERT_TRUE(RegisterArdaNativeKernelBinary(Key, Binary));
		ASSERT_TRUE(RegisterArdaNativeKernelBinary(Key, Binary));
		auto Different = Binary;
		Different.mBytes.pop_back();
		EXPECT_FALSE(RegisterArdaNativeKernelBinary(Key, Different));
		const auto Directory = std::filesystem::temp_directory_path();
		const auto TestDirectory = fileops::TemporaryPath(Directory / "arda-catalog", "test");
		const auto Original = TestDirectory / "arda-atomic-a.ardakernels";
		const auto Changed = TestDirectory / "arda-atomic-b.ardakernels";
		struct FArdaCleanup
		{
			std::filesystem::path mDirectory, mA, mB;
			~FArdaCleanup()
			{
				std::error_code Error;
				for (const auto& Path : {mDirectory / "save-as" / mB.filename(), mDirectory / "save-as", mA, mB, mDirectory}) std::filesystem::remove(Path, Error);
			}
		} Cleanup{TestDirectory, Original, Changed};
		ASSERT_TRUE(SaveArdaNativeKernelLibrary(Key.mLibrary, TestDirectory));
		ASSERT_TRUE(SaveArdaNativeKernelLibrary(Key.mLibrary, TestDirectory));
		eastl::string Bytes;
		ASSERT_EQ(fileops::ReadBinaryFile(Original, Bytes, 4096), fileops::EArdaFileReadResult::Success);
		const auto Name = Bytes.find("arda-atomic-a");
		ASSERT_NE(Name, eastl::string::npos);
		Bytes[Name + Key.mLibrary.size() - 1] = 'b';
		auto OtherKey = Key;
		OtherKey.mLibrary = "arda-atomic-b";
		ASSERT_TRUE(fileops::AtomicWrite(Changed, Bytes + "trailing"));
		EXPECT_FALSE(LoadArdaNativeKernelLibrary(Changed));
		EXPECT_FALSE(FindArdaNativeKernelBinary(OtherKey));
		ASSERT_TRUE(fileops::AtomicWrite(Changed, Bytes));
		ASSERT_TRUE(LoadArdaNativeKernelLibrary(Changed));
		auto Found = FindArdaNativeKernelBinary(OtherKey);
		ASSERT_TRUE(Found);
		EXPECT_EQ(Found.mValue->mBytes, Binary.mBytes);
		const auto Export = TestDirectory / "save-as";
		ASSERT_TRUE(StageArdaNativeKernelLibrary(OtherKey.mLibrary, Export));
		eastl::string Restored;
		ASSERT_EQ(fileops::ReadBinaryFile(Export / Changed.filename(), Restored, 4096), fileops::EArdaFileReadResult::Success);
		EXPECT_EQ(Restored, Bytes);
		EXPECT_TRUE(StageArdaNativeKernelLibrary("absent-deployment", Export));
		EXPECT_FALSE(std::filesystem::exists(Export / "absent-deployment.ardakernels"));
		OtherKey.mSignature = "v2";
		EXPECT_FALSE(FindArdaNativeKernelBinary(OtherKey));
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaNativeRegistryDevice, testing::Values("native-d3d12", "native-vulkan"));
}
