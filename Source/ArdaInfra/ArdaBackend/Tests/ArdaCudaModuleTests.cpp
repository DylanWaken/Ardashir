#include "ArdaBackend.h"
#include "ArdaTestBackend.h"
#include "CUDA/ArdaCudaModule.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include <gtest/gtest.h>
#include <cstring>

namespace arda
{
	namespace
	{
		FArdaCudaModuleDesc MakeCudaModuleArtifact()
		{
			// Mixed-width individual parameters deliberately differ from the nvcc single-struct convention.
			constexpr char Source[] = R"ptx(.version 8.0
.target sm_75
.address_size 64
.visible .entry ArdaImportedFill(.param .u64 output, .param .u32 count, .param .u64 base, .param .f32 bias)
{
 .reg .pred p;
 .reg .b32 i, n;
 .reg .b64 ptr, off, value, extra;
 .reg .f32 f;
 mov.u32 i, %tid.x;
 ld.param.u32 n, [count];
 setp.ge.u32 p, i, n;
 @p bra done;
 ld.param.u64 ptr, [output];
 ld.param.u64 value, [base];
 ld.param.f32 f, [bias];
 cvt.rzi.u64.f32 extra, f;
 add.u64 value, value, extra;
 cvt.u64.u32 off, i;
 add.u64 value, value, off;
 shl.b64 off, off, 3;
 add.u64 ptr, ptr, off;
 st.global.u64 [ptr], value;
done:
 ret;
}
)ptx";
			FArdaCudaModuleDesc Desc;
			Desc.mFormat = EArdaCudaModuleFormat::Ptx;
			Desc.mBuild.mIdentity = "ArdaImportedFill.ptx.v1";
			Desc.mBuild.mArchitectures.push_back({75, false});
			Desc.mImage.assign(Source, Source + sizeof(Source) - 1);
			return Desc;
		}

		FArdaCudaModuleKernelDesc MakeCudaModuleSignature()
		{
			FArdaCudaModuleKernelDesc Desc;
			Desc.mEntryPoint = "ArdaImportedFill";
			Desc.mParameterSize = 32;
			Desc.mParameterAlignment = 8;
			Desc.mArguments = {{0, 8, 8, EArdaCudaModuleArgumentKind::Buffer},
			    {8, 4, 4, EArdaCudaModuleArgumentKind::Value},
			    {16, 8, 8, EArdaCudaModuleArgumentKind::Value},
			    {24, 4, 4, EArdaCudaModuleArgumentKind::Value}};
			return Desc;
		}
	}

	TEST(ArdaCudaModule, OwnsArtifactAndRejectsMalformedImages)
	{
		auto Desc = MakeCudaModuleArtifact();
		auto Module = CreateArdaCudaModule(Desc);
		ASSERT_TRUE(Module);
		EXPECT_EQ(Module.mValue->GetDesc().mImage.back(), 0);
		Desc.mImage[0] = '!';
		EXPECT_EQ(Module.mValue->GetDesc().mImage.front(), '.');
		Desc.mImage[4] = 0;
		EXPECT_FALSE(CreateArdaCudaModule(Desc));
		Desc = MakeCudaModuleArtifact();
		Desc.mBuild.mArchitectures.clear();
		EXPECT_FALSE(CreateArdaCudaModule(Desc));
		Desc = MakeCudaModuleArtifact();
		Desc.mFormat = EArdaCudaModuleFormat::Cubin;
		EXPECT_FALSE(CreateArdaCudaModule(Desc));
	}

	TEST(ArdaCudaModule, RejectsOverflowOverlapAndUndeclaredResourceAddresses)
	{
		auto Module = CreateArdaCudaModule(MakeCudaModuleArtifact());
		ASSERT_TRUE(Module);
		auto Desc = MakeCudaModuleSignature();
		auto Entry = CreateArdaCudaModuleKernel(Module.mValue, Desc);
		ASSERT_TRUE(Entry);
		FArdaCudaKernel Kernel;
		Kernel.mEntry = Entry.mValue;
		Kernel.mParameters.resize(32);
		EXPECT_FALSE(ValidateArdaCudaModuleParameters(Desc, Kernel));
		Kernel.mPatches.push_back({0, 0, 8});
		EXPECT_TRUE(ValidateArdaCudaModuleParameters(Desc, Kernel));
		Kernel.mPatches.push_back({0, 16, 8});
		EXPECT_FALSE(ValidateArdaCudaModuleParameters(Desc, Kernel));
		Desc.mArguments[1].mOffset = 4;
		EXPECT_FALSE(CreateArdaCudaModuleKernel(Module.mValue, Desc));
		Desc = MakeCudaModuleSignature();
		Desc.mArguments.back().mSize = size_t(-1);
		EXPECT_FALSE(CreateArdaCudaModuleKernel(Module.mValue, Desc));
		Desc = MakeCudaModuleSignature();
		Desc.mArguments[0].mSize = 4;
		EXPECT_FALSE(CreateArdaCudaModuleKernel(Module.mValue, Desc));
	}

	TEST(ArdaCudaModule, InternsOnlyIdenticalImagesAndBuildContracts)
	{
		auto Module = CreateArdaCudaModule(MakeCudaModuleArtifact());
		auto Same = CreateArdaCudaModule(MakeCudaModuleArtifact());
		ASSERT_TRUE(Module);
		ASSERT_TRUE(Same);
		EXPECT_EQ(Module.mValue.get(), Same.mValue.get());
		auto OtherCode = MakeCudaModuleArtifact();
		OtherCode.mImage.push_back(' ');
		auto Different = CreateArdaCudaModule(OtherCode);
		ASSERT_TRUE(Different);
		EXPECT_NE(Module.mValue.get(), Different.mValue.get());
		auto OtherBuild = MakeCudaModuleArtifact();
		OtherBuild.mBuild.mbFastMath = true;
		auto Fast = CreateArdaCudaModule(OtherBuild);
		ASSERT_TRUE(Fast);
		EXPECT_NE(Module.mValue.get(), Fast.mValue.get());
		eastl::weak_ptr<const FArdaCudaModule> Weak = Fast.mValue;
		Fast.mValue.reset();
		EXPECT_TRUE(Weak.expired()); // The intern table never extends artifact lifetime.
	}

	TEST(ArdaCudaModule, PtxVirtualArchitectureCanAdvanceAcrossNativeGenerations)
	{
		auto Module = CreateArdaCudaModule(MakeCudaModuleArtifact());
		ASSERT_TRUE(Module);
		auto Entry = CreateArdaCudaModuleKernel(Module.mValue, MakeCudaModuleSignature());
		ASSERT_TRUE(Entry);
		FArdaCudaKernel Kernel;
		Kernel.mEntry = Entry.mValue;
		Kernel.mParameters.resize(32);
		Kernel.mPatches.push_back({0, 0, 8});
		FArdaCudaCapabilities Caps;
		Caps.mLaunchMode = EArdaCudaLaunchMode::ContextSwitch;
		Caps.mComputeCapability = 120;
		Caps.mMaxThreadsPerBlock = 1024;
		for (uint32_t Axis = 0; Axis < 3; ++Axis)
		{
			Caps.mMaxBlockSize[Axis] = 1024;
			Caps.mMaxGridSize[Axis] = 65535;
		}
		EXPECT_TRUE(ValidateArdaCudaKernels({Kernel}, 1, Caps));
		Caps.mComputeCapability = 70;
		EXPECT_FALSE(ValidateArdaCudaKernels({Kernel}, 1, Caps));
	}

	class FArdaCudaModuleGpu : public testing::TestWithParam<const char*>
	{
	protected:
		FArdaTestDiagnosticCallback mDiagnostics;
		FArdaRHIDeviceRef mDevice;

		void SetUp() override
		{
			ShutdownBackend();
			auto Config = MakeArdaTestBackendConfiguration();
			Config.mBackendName = GetParam();
			Config.mCudaExecutionMode = EArdaCudaExecutionMode::ContextSwitch;
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
	};

	TEST_P(FArdaCudaModuleGpu, ImportsMixedArgumentPtxAndRetainsItThroughCapturedReplay)
	{
		constexpr uint32_t Count = 65;
		constexpr uint64_t Base = 0x100000007ull;
		const float Bias = 3.5f;
		FArdaRHIBufferDesc BufferDesc;
		BufferDesc.mByteSize = (Count + 4) * sizeof(uint64_t);
		BufferDesc.mbCudaInterop = true;
		BufferDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::ShaderResource;
		auto Buffer = mDevice->CreateBuffer(BufferDesc);
		ASSERT_TRUE(Buffer);
		auto Module = CreateArdaCudaModule(MakeCudaModuleArtifact());
		ASSERT_TRUE(Module);
		auto Entry = CreateArdaCudaModuleKernel(Module.mValue, MakeCudaModuleSignature());
		ASSERT_TRUE(Entry);
		auto Batch = eastl::make_shared<FArdaCudaGraphBatch>();
		Batch->mCache = eastl::make_shared<FArdaCudaGraphCache>(EArdaCudaGraphMode::Require);
		Batch->mResources.push_back(Buffer.mValue);
		FArdaCudaDispatch Dispatch;
		Dispatch.mBindings.push_back({Buffer.mValue, EArdaComputeAccess::Write, {16, Count * 8}, 0});
		Dispatch.mKernels.resize(1);
		auto& Kernel = Dispatch.mKernels.front();
		Kernel.mEntry = Entry.mValue;
		Kernel.mParameters.resize(32, 0);
		std::memcpy(Kernel.mParameters.data() + 8, &Count, sizeof(Count));
		std::memcpy(Kernel.mParameters.data() + 16, &Base, sizeof(Base));
		std::memcpy(Kernel.mParameters.data() + 24, &Bias, sizeof(Bias));
		Kernel.mPatches.push_back({0, 0, 8});
		Kernel.mBlockSize[0] = 128;
		Kernel.mGraphBatch = Batch;
		Entry.mValue.reset();
		Module.mValue.reset();
		for (uint32_t Repeat = 0; Repeat < 2; ++Repeat)
		{
			auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Commands);
			ASSERT_TRUE(Commands.mValue->Open());
			eastl::vector<uint64_t> Guards(Count + 4, 0xdeadbeefdeadbeefull);
			ASSERT_TRUE(Commands.mValue->WriteBuffer(*Buffer.mValue, Guards.data(), Guards.size() * 8));
			auto Status = Commands.mValue->DispatchCuda(Dispatch);
			ASSERT_TRUE(Status) << Status.mMessage.c_str();
			eastl::vector<uint8_t> Bytes;
			ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Bytes));
			ASSERT_TRUE(Commands.mValue->Close());
			ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
			Commands.mValue.Reset();
			ASSERT_TRUE(mDevice->WaitForIdle());
			ASSERT_EQ(Bytes.size(), Guards.size() * 8);
			for (uint32_t Index = 0; Index < Count + 4; ++Index)
			{
				uint64_t Actual = 0;
				std::memcpy(&Actual, Bytes.data() + Index * 8, 8);
				EXPECT_EQ(Actual, Index < 2 || Index >= Count + 2 ? Guards[Index] : Base + 3 + Index - 2);
			}
			if (Repeat == 0)
			{
				// Exceed the provider's bounded limit cache while the original capture retains its entry.
				auto OtherModule = CreateArdaCudaModule(MakeCudaModuleArtifact());
				ASSERT_TRUE(OtherModule);
				eastl::vector<FArdaCudaDispatch> Evictions;
				for (uint32_t Index = 0; Index < 257; ++Index)
				{
					auto OtherEntry = CreateArdaCudaModuleKernel(OtherModule.mValue, MakeCudaModuleSignature());
					ASSERT_TRUE(OtherEntry);
					auto OtherDispatch = Dispatch;
					OtherDispatch.mKernels.front().mEntry = eastl::move(OtherEntry.mValue);
					OtherDispatch.mKernels.front().mGraphBatch.reset();
					Evictions.push_back(eastl::move(OtherDispatch));
				}
				auto Evict = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
				ASSERT_TRUE(Evict);
				ASSERT_TRUE(Evict.mValue->Open());
				ASSERT_TRUE(Evict.mValue->DispatchCudaSequence(Evictions));
				ASSERT_TRUE(Evict.mValue->Close());
				ASSERT_TRUE(mDevice->ExecuteCommandList(Evict.mValue));
				ASSERT_TRUE(mDevice->WaitForIdle());
			}
		}
		EXPECT_EQ(Batch->mCache->GetStats().mCaptureCount, 1u);
		EXPECT_EQ(Batch->mCache->GetStats().mCacheHitCount, 1u);
	}

	TEST_P(FArdaCudaModuleGpu, RejectsManifestAbiMismatchBeforeEnqueue)
	{
		auto Module = CreateArdaCudaModule(MakeCudaModuleArtifact());
		ASSERT_TRUE(Module);
		auto Desc = MakeCudaModuleSignature();
		Desc.mArguments[0].mKind = EArdaCudaModuleArgumentKind::Value;
		Desc.mArguments[1].mSize = 8; // Valid storage schema, but the real count parameter is only four bytes.
		auto Entry = CreateArdaCudaModuleKernel(Module.mValue, Desc);
		ASSERT_TRUE(Entry);
		FArdaCudaDispatch Dispatch;
		Dispatch.mKernels.resize(1);
		Dispatch.mKernels.front().mEntry = Entry.mValue;
		Dispatch.mKernels.front().mParameters.resize(32);
		auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		const auto Status = Commands.mValue->DispatchCuda(Dispatch);
		EXPECT_FALSE(Status);
		EXPECT_EQ(Status.mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_NE(Status.mMessage.find("ABI"), eastl::string::npos);
	}

	INSTANTIATE_TEST_SUITE_P(Native, FArdaCudaModuleGpu, testing::Values("native-d3d12", "native-vulkan"));
}
