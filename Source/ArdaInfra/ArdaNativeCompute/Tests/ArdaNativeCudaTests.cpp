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
		UseArdaNativeDirectResources(Operand);
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
		FArdaNativeComputeRegistry Registry;
		ASSERT_TRUE(Registry.RegisterProvider(Provider));
		ASSERT_TRUE(Registry.RegisterProvider(CiG));
		for (const char* Id : {ArdaNativeCudaProviderId, ArdaNativeCudaInGraphicsProviderId})
		{
			const auto Buffer = Registry.FindResourceCategory(Id, ArdaNativeCudaBufferCategory);
			const auto Surface = Registry.FindResourceCategory(Id, ArdaNativeCudaSurfaceCategory);
			ASSERT_TRUE(Buffer);
			ASSERT_TRUE(Surface);
			EXPECT_EQ(Buffer.mValue.mType, EArdaRHIResourceType::Buffer);
			EXPECT_EQ(Surface.mValue.mType, EArdaRHIResourceType::Texture);
		}
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
			constexpr uint32_t SecondBase = Base + 100;
			constexpr uint32_t Guard = 0xdeadbeef;
			const bool bCiG = GetExecutionMode() == EArdaCudaExecutionMode::GraphicsQueue;
			const bool bCudaCpp = Mode == EArdaNativeCudaTestMode::CudaCpp;
			const bool bInvalidCudaCpp = Mode == EArdaNativeCudaTestMode::InvalidCudaCppWithPtxAlternative;
			const char* ProviderId = bCiG ? ArdaNativeCudaInGraphicsProviderId : ArdaNativeCudaProviderId;
			FArdaNativeComputeRegistry Registry;
			ASSERT_TRUE(RegisterArdaCudaNativeComputeProvider(Registry));
			ASSERT_TRUE(RegisterArdaCiGNativeComputeProvider(Registry));
			const auto Provider = Registry.FindProvider(ProviderId);
			ASSERT_TRUE(Provider);
			auto DeviceSupport = Registry.QueryProviderSupport(ProviderId, {mDevice});
			ASSERT_TRUE(DeviceSupport) << DeviceSupport.mStatus.mMessage.c_str();
			const auto ExpectedMode = mDevice->GetCudaCapabilities().mLaunchMode == EArdaCudaLaunchMode::D3D12CiG
			    ? EArdaNativeExecutionMode::Direct
			    : EArdaNativeExecutionMode::ContextSwitching;
			EXPECT_EQ(DeviceSupport.mValue.mMode, ExpectedMode);
			const auto& Formats = Provider->GetDesc().mSourceFormats;
			EXPECT_EQ(DeviceSupport.mValue.mbRuntimeCompilation,
			    eastl::find(Formats.begin(), Formats.end(), "cuda-cpp") != Formats.end());
			if ((bCudaCpp || bInvalidCudaCpp) && !DeviceSupport.mValue.mbRuntimeCompilation)
			{
				GTEST_SKIP() << "NVRTC is unavailable";
			}
			FArdaNativeOperandDesc Operand;
			Operand.mId = "test-native-cuda-fill";
			Operand.mResources = {{"Output", EArdaRHIResourceType::Buffer, EArdaComputeAccess::Write}};
			Operand.mParameterSize = 16;
			UseArdaNativeCudaResources(Operand);
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
			if (bCudaCpp)
			{
				auto Source = CreateArdaNativeCudaSource(MakeNativeCudaFill(true));
				ASSERT_TRUE(Source) << Source.mStatus.mMessage.c_str();
				AddArdaNativeCudaVariants(Operand, {"fill", eastl::move(Source.mValue), {}});
			}
			else
			{
				auto Loaded = LoadArdaNativeKernelLibrary(
				    std::filesystem::path(ARDA_NATIVE_TEST_KERNEL_DIR) / "arda-native-test.ardakernels");
				ASSERT_TRUE(Loaded) << Loaded.mMessage.c_str();
				const FArdaNativeKernelLayout Layout{16,
				    8,
				    {{0, 8, 8, EArdaNativeArgumentKind::Buffer}, {8, 4, 4, EArdaNativeArgumentKind::Value}}};
				auto CiG = MakeArdaPackagedNativeOperand(
				    {"arda-native-test", ArdaNativeCudaInGraphicsProviderId, "fill", "fill-v1"},
				    Layout,
				    eastl::move(Operand));
				ASSERT_TRUE(CiG) << CiG.mStatus.mMessage.c_str();
				CiG.mValue.mVariants.back().mName = "fill.cig";
				auto Cuda =
				    MakeArdaPackagedNativeOperand({"arda-native-test", ArdaNativeCudaProviderId, "fill", "fill-v1"},
				        Layout,
				        eastl::move(CiG.mValue));
				ASSERT_TRUE(Cuda) << Cuda.mStatus.mMessage.c_str();
				Operand = eastl::move(Cuda.mValue);
			}
			for (auto& Variant : Operand.mVariants)
			{
				Variant.mNativeResources = Operand.mResources;
				Variant.mNativeResources.front().mCategory = ArdaNativeCudaBufferCategory;
				Variant.mGraphicsStates = {EArdaRHIResourceState::UnorderedAccess};
			}
			const auto Slot = Operand.mResources.front();
			const auto Id = Operand.mId;
			auto DirectOnly = Operand;
			DirectOnly.mId = "cuda-direct-only";
			UseArdaNativeDirectResources(DirectOnly);
			ASSERT_TRUE(Registry.RegisterOperand(eastl::move(DirectOnly)));
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
			EXPECT_NE(ForeignStatus.mMessage.find(bCiG ? "source metadata" : "qualified CiG context"),
			    eastl::string::npos);
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
			BufferDesc.mByteSize = (2 * Count + 3) * sizeof(uint32_t);
			BufferDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
			auto GraphicsBuffer = mDevice->CreateBuffer(BufferDesc);
			ASSERT_TRUE(GraphicsBuffer);
			FArdaNativeInvocation Invocation;
			Invocation.mResources.push_back({GraphicsBuffer.mValue, {sizeof(uint32_t), Count * sizeof(uint32_t)}});
			Invocation.mParameters.resize(16);
			Invocation.mLaunch.mBlockSize[0] = Count;
			std::memcpy(Invocation.mParameters.data() + 8, &Base, sizeof(Base));
			const auto Handoff = Provider->QueryHandoff(Context, Slot, Invocation.mResources.front());
			ASSERT_TRUE(Handoff);
			EXPECT_EQ(Handoff.mValue.mKind, EArdaNativeHandoff::ConversionRequired);
			EXPECT_EQ(Registry.Prepare("cuda-direct-only", Context, Invocation).mStatus.mCode,
			    EArdaRHIResult::Unsupported);
			auto Staged = Registry.Prepare(Id.c_str(), Context, Invocation);
			ASSERT_TRUE(Staged) << Staged.mStatus.mMessage.c_str();

			Provider->ConfigureBuffer(BufferDesc);
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
			EXPECT_EQ(Plan.mValue.GetExecutionMode(), ExpectedMode);
			Invocation.mResources.front().mBufferRange.mByteOffset = (Count + 2) * sizeof(uint32_t);
			std::memcpy(Invocation.mParameters.data() + 8, &SecondBase, sizeof(SecondBase));
			auto Second = Registry.Prepare(Id.c_str(), Context, Invocation);
			ASSERT_TRUE(Second) << Second.mStatus.mMessage.c_str();
			Invocation = {};

			auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Commands);
			ASSERT_TRUE(Commands.mValue->Open());
			const eastl::vector<uint32_t> Initial(2 * Count + 3, Guard);
			ASSERT_TRUE(Commands.mValue->WriteBuffer(*GraphicsBuffer.mValue, Initial.data(), BufferDesc.mByteSize));
			ASSERT_TRUE(
			    Commands.mValue->SetBufferState(*GraphicsBuffer.mValue, EArdaRHIResourceState::UnorderedAccess));
			Commands.mValue->CommitBarriers();
			ASSERT_TRUE(Commands.mValue->WriteBuffer(*SharedBuffer.mValue, Initial.data(), BufferDesc.mByteSize));
			FArdaNativeComputeSession Session;
			ASSERT_TRUE(Session.Append(eastl::move(Staged.mValue), *Commands.mValue));
			ASSERT_TRUE(Session.Append(eastl::move(Plan.mValue), *Commands.mValue));
			ASSERT_TRUE(Session.Append(eastl::move(Second.mValue), *Commands.mValue));
			const auto Flushed = Session.Flush(*Commands.mValue);
			ASSERT_TRUE(Flushed) << Flushed.mMessage.c_str();
			eastl::vector<uint8_t> Bytes;
			eastl::vector<uint8_t> StagedBytes;
			ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*GraphicsBuffer.mValue, StagedBytes));
			ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*SharedBuffer.mValue, Bytes));
			ASSERT_TRUE(Commands.mValue->Close());
			SharedBuffer.mValue.Reset();
			ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
			Commands.mValue.Reset();
			ASSERT_TRUE(mDevice->WaitForIdle());

			ASSERT_EQ(Bytes.size(), BufferDesc.mByteSize);
			ASSERT_EQ(StagedBytes.size(), BufferDesc.mByteSize);
			for (uint32_t Index = 0; Index < 2 * Count + 3; ++Index)
			{
				uint32_t Actual = 0;
				std::memcpy(&Actual, Bytes.data() + Index * sizeof(Actual), sizeof(Actual));
				uint32_t StagedWord = 0;
				std::memcpy(&StagedWord, StagedBytes.data() + Index * sizeof(StagedWord), sizeof(StagedWord));
				EXPECT_EQ(StagedWord, Index > 0 && Index <= Count ? Base + Index - 1 : Guard);
				if (Index == 0 || Index == Count + 1 || Index == 2 * Count + 2)
				{
					EXPECT_EQ(Actual, Guard);
				}
				else
				{
					EXPECT_EQ(Actual, Index <= Count ? Base + Index - 1 : SecondBase + Index - Count - 2);
				}
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

	TEST_P(FArdaNativeCudaGpu, CopiesGraphicsTextureMipThroughPitchedCudaBuffer)
	{
		constexpr uint32_t Width = 13, Height = 3, Addend = 71;
		const auto Loaded = LoadArdaNativeKernelLibrary(
		    std::filesystem::path(ARDA_NATIVE_TEST_KERNEL_DIR) / "arda-native-test.ardakernels");
		ASSERT_TRUE(Loaded) << Loaded.mMessage.c_str();
		FArdaNativeComputeRegistry Registry;
		ASSERT_TRUE(RegisterArdaCudaNativeComputeProvider(Registry));
		FArdaNativeOperand Operand;
		Operand.mId = "cuda-pitched-texture-test";
		Operand.mResources = {
		    {"Image", EArdaRHIResourceType::Texture, EArdaComputeAccess::ReadWrite, EArdaRHIFormat::R32UInt}};
		Operand.mParameterSize = 16;
		UseArdaNativeCudaResources(Operand);
		const FArdaNativeKernelLayout Layout{16,
		    8,
		    {{0, 8, 8, EArdaNativeArgumentKind::Buffer},
		        {8, 4, 4, EArdaNativeArgumentKind::Value},
		        {12, 4, 4, EArdaNativeArgumentKind::Value}}};
		auto Packaged = MakeArdaPackagedNativeOperand(
		    {"arda-native-test", ArdaNativeCudaProviderId, "texture-add", "texture-add-v1"},
		    Layout,
		    eastl::move(Operand));
		ASSERT_TRUE(Packaged) << Packaged.mStatus.mMessage.c_str();
		const auto Id = Packaged.mValue.mId;
		auto& Variant = Packaged.mValue.mVariants.front();
		Variant.mNativeResources = {{"Image",
		    EArdaRHIResourceType::Buffer,
		    EArdaComputeAccess::ReadWrite,
		    EArdaRHIFormat::Unknown,
		    ArdaNativeCudaBufferCategory}};
		Variant.mGraphicsStates = {EArdaRHIResourceState::UnorderedAccess};
		ASSERT_TRUE(Registry.RegisterOperand(eastl::move(Packaged.mValue)));

		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = Width * 2;
		TextureDesc.mHeight = Height * 2;
		TextureDesc.mMipLevels = 2;
		TextureDesc.mFormat = EArdaRHIFormat::R32UInt;
		TextureDesc.mUsage = EArdaRHITextureUsage::UnorderedAccess;
		auto Texture = mDevice->CreateTexture(TextureDesc);
		ASSERT_TRUE(Texture) << Texture.mStatus.mMessage.c_str();
		FArdaRHITextureSlice Slice;
		Slice.mMipLevel = 1;
		const auto Footprint = GetArdaRHITextureBufferFootprint(TextureDesc, Slice);
		ASSERT_TRUE(Footprint) << Footprint.mStatus.mMessage.c_str();
		const auto Pitch = Footprint.mValue.mLayout.mRowPitch;
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = Pitch * Height;
		BufferDesc.mUsage = EArdaRHIBufferUsage::None;
		auto Upload = mDevice->CreateBuffer(BufferDesc);
		auto Download = mDevice->CreateBuffer(BufferDesc);
		ASSERT_TRUE(Upload);
		ASSERT_TRUE(Download);
		eastl::vector<uint32_t> Initial(BufferDesc.mByteSize / sizeof(uint32_t));
		for (uint32_t Y = 0; Y < Height; ++Y)
		{
			for (uint32_t X = 0; X < Width; ++X)
			{
				Initial[Y * Pitch / sizeof(uint32_t) + X] = Y * Width + X;
			}
		}
		FArdaNativeInvocation Invocation;
		Invocation.mResources = {{Texture.mValue, {}, 1}};
		Invocation.mParameters.resize(16);
		std::memcpy(Invocation.mParameters.data() + 8, &Pitch, sizeof(Pitch));
		std::memcpy(Invocation.mParameters.data() + 12, &Addend, sizeof(Addend));
		Invocation.mLaunch.mGridSize[0] = Height;
		Invocation.mLaunch.mBlockSize[0] = Width;
		const FArdaNativeComputeContext Context{mDevice,
		    EArdaRHIQueueType::Graphics,
		    EArdaNativeComputePreference::NativeOnly};
		auto Plan = Registry.Prepare(Id.c_str(), Context, Invocation);
		ASSERT_TRUE(Plan) << Plan.mStatus.mMessage.c_str();

		auto Commands = mDevice->CreateCommandList(Context.mQueue);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		auto& List = *Commands.mValue;
		List.SetAutomaticBarriers(false);
		const FArdaRHITextureSubresourceRange Mip{1, 1};
		ASSERT_TRUE(List.SetBufferState(*Upload.mValue, EArdaRHIResourceState::CopyDest));
		List.CommitBarriers();
		ASSERT_TRUE(List.WriteBuffer(*Upload.mValue, Initial.data(), BufferDesc.mByteSize));
		ASSERT_TRUE(List.SetBufferState(*Upload.mValue, EArdaRHIResourceState::CopySource));
		ASSERT_TRUE(List.SetTextureState(*Texture.mValue, Mip, EArdaRHIResourceState::CopyDest));
		List.CommitBarriers();
		ASSERT_TRUE(List.CopyBufferToTexture(*Texture.mValue, Slice, *Upload.mValue, Footprint.mValue.mLayout));
		ASSERT_TRUE(List.SetTextureState(*Texture.mValue, Mip, EArdaRHIResourceState::UnorderedAccess));
		List.CommitBarriers();
		FArdaNativeComputeSession Session;
		const auto Recorded = Session.Append(eastl::move(Plan.mValue), List);
		ASSERT_TRUE(Recorded) << Recorded.mMessage.c_str();
		ASSERT_TRUE(Session.Flush(List));
		ASSERT_TRUE(List.AssertTextureState(*Texture.mValue, Mip, EArdaRHIResourceState::UnorderedAccess));
		ASSERT_TRUE(List.SetTextureState(*Texture.mValue, Mip, EArdaRHIResourceState::CopySource));
		ASSERT_TRUE(List.SetBufferState(*Download.mValue, EArdaRHIResourceState::CopyDest));
		List.CommitBarriers();
		ASSERT_TRUE(List.CopyTextureToBuffer(*Download.mValue, Footprint.mValue.mLayout, *Texture.mValue, Slice));
		ASSERT_TRUE(List.SetBufferState(*Download.mValue, EArdaRHIResourceState::CopySource));
		List.CommitBarriers();
		eastl::vector<uint8_t> Bytes;
		ASSERT_TRUE(List.CopyBufferDeviceToHost(*Download.mValue, Bytes));
		ASSERT_TRUE(List.Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
		ASSERT_EQ(Bytes.size(), BufferDesc.mByteSize);
		for (uint32_t Y = 0; Y < Height; ++Y)
		{
			for (uint32_t X = 0; X < Width; ++X)
			{
				uint32_t Actual = 0;
				std::memcpy(&Actual, Bytes.data() + Y * Pitch + X * sizeof(Actual), sizeof(Actual));
				EXPECT_EQ(Actual, Y * Width + X + Addend);
			}
		}
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
