#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "ArdaTestComputeOperand.h"
#include "RHI/Scheduling/ArdaCudaSequence.h"
#include "RHI/Shaders/ArdaGlobalShaderMap.h"

#include <gtest/gtest.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	using namespace arda;

	class FArdaBindingBoundaryDiagnostics final : public IArdaDiagnosticCallback
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

	class FArdaBindingBoundaryBase : public testing::Test
	{
	protected:
		virtual const char* GetBackendName() const = 0;

		virtual EArdaCudaExecutionMode GetCudaExecutionMode() const
		{
			return EArdaCudaExecutionMode::Automatic;
		}

		void SetUp() override
		{
			ShutdownBackend();
			auto Configuration = MakeArdaTestBackendConfiguration();
			Configuration.mBackendName = GetBackendName();
			Configuration.mCudaExecutionMode = GetCudaExecutionMode();
			Configuration.mMessageCallback = &mDiagnostics;
			Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
			ASSERT_TRUE(ConfigureBackend(Configuration));
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
			mDevice = GetDevice();
			ASSERT_TRUE(mDevice);
		}

		void TearDown() override
		{
			const bool MissingValidationSkip = testing::Test::IsSkipped() && ArdaTestValidationEnabled &&
			    GetBackendInitializeResult() == EArdaInitializeResult::ValidationUnavailable;
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
			}
			mDevice = {};
			ShutdownBackend();
			if (!MissingValidationSkip)
			{
				EXPECT_EQ(mDiagnostics.mWarnings.load(), 0u);
			}
			static_cast<void>(ConfigureBackend(MakeArdaTestBackendConfiguration()));
		}

		FArdaRHIBufferRef MakeBuffer(uint64_t ByteSize = 16)
		{
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = ByteSize;
			Desc.mStructureStride = sizeof(uint32_t);
			Desc.mUsage = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::UnorderedAccess;
			Desc.mInitialState = EArdaRHIResourceState::UnorderedAccess;
			return mDevice->CreateBuffer(Desc).mValue;
		}

		FArdaRHIShaderRef MakeComputeShader(const char* Artifact, const char* EntryPoint)
		{
			const std::string Path =
			    std::string(ARDA_BACKEND_TEST_SHADER_DIR "/") + Artifact + GetShaderArtifactExtension(GetBackendName());
			std::ifstream Stream(Path, std::ios::binary);
			EXPECT_TRUE(Stream) << Path;
			if (!Stream)
			{
				return {};
			}
			const std::vector<char> Bytes((std::istreambuf_iterator<char>(Stream)), {});
			FArdaRHIShaderDesc Desc;
			Desc.mStage = EArdaRHIShaderStage::Compute;
			Desc.mEntryPoint = EntryPoint;
			Desc.mBytecode = Bytes.data();
			Desc.mBytecodeSize = Bytes.size();
			auto Shader = mDevice->CreateShader(Desc);
			EXPECT_TRUE(Shader) << Shader.mStatus.mMessage.c_str();
			return Shader.mValue;
		}

		FArdaBindingBoundaryDiagnostics mDiagnostics;
		FArdaRHIDeviceRef mDevice;
	};

	class FArdaBindingBoundaryTest : public FArdaBindingBoundaryBase, public testing::WithParamInterface<const char*>
	{
	protected:
		const char* GetBackendName() const override
		{
			return GetParam();
		}
	};

	TEST_P(FArdaBindingBoundaryTest, RejectsExtraDuplicateAndMismatchedFixedDescriptors)
	{
		auto Buffer = MakeBuffer();
		ASSERT_TRUE(Buffer);
		FArdaRHIBindingLayoutDesc LayoutDesc;
		LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
		LayoutDesc.mItems = {{0, 1, EArdaRHIBindingType::StructuredBufferUAV}};
		auto Layout = mDevice->CreateBindingLayout(LayoutDesc);
		ASSERT_TRUE(Layout);
		FArdaRHIBindingItem Item;
		Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
		Item.mResource = FArdaRHIResourceRef(Buffer.Get());
		FArdaRHIBindingSetDesc Desc;
		Desc.mLayout = Layout.mValue;
		Desc.mItems = {Item};
		ASSERT_TRUE(mDevice->CreateBindingSet(Desc));

		// Required slot zero is populated, so these additional writes used to bypass completeness checks.
		for (const auto Slot : {0u, 1u, UINT32_MAX})
		{
			Desc.mItems = {Item, Item};
			Desc.mItems.back().mSlot = Slot;
			const auto Result = mDevice->CreateBindingSet(Desc);
			EXPECT_FALSE(Result);
			EXPECT_EQ(Result.mStatus.mCode, EArdaRHIResult::InvalidArgument);
		}
		Desc.mItems = {Item, Item};
		Desc.mItems.back().mArrayElement = UINT32_MAX;
		EXPECT_EQ(mDevice->CreateBindingSet(Desc).mStatus.mCode, EArdaRHIResult::InvalidArgument);

		// A valid same-device native object is insufficient: its descriptor union must match its kind.
		auto Sampler = mDevice->CreateSampler({});
		ASSERT_TRUE(Sampler);
		Desc.mItems = {Item};
		Desc.mItems[0].mResource = FArdaRHIResourceRef(Sampler.mValue.Get());
		EXPECT_EQ(mDevice->CreateBindingSet(Desc).mStatus.mCode, EArdaRHIResult::InvalidArgument);
	}

	TEST_P(FArdaBindingBoundaryTest, RejectsInvalidBufferAndTextureViewsWithoutClamping)
	{
		auto Buffer = MakeBuffer();
		ASSERT_TRUE(Buffer);
		FArdaRHIBindingLayoutDesc LayoutDesc;
		LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
		LayoutDesc.mItems = {{0, 1, EArdaRHIBindingType::StructuredBufferUAV}};
		auto Layout = mDevice->CreateBindingLayout(LayoutDesc);
		ASSERT_TRUE(Layout);
		FArdaRHIBindingSetDesc Desc;
		Desc.mLayout = Layout.mValue;
		Desc.mItems.resize(1);
		Desc.mItems[0].mType = EArdaRHIBindingType::StructuredBufferUAV;
		Desc.mItems[0].mResource = FArdaRHIResourceRef(Buffer.Get());
		for (const FArdaRHIBufferRange Range : {FArdaRHIBufferRange{16, ArdaRHIWholeBuffer},
		         FArdaRHIBufferRange{0, 17},
		         FArdaRHIBufferRange{UINT64_MAX, 4},
		         FArdaRHIBufferRange{0, 0}})
		{
			Desc.mItems[0].mView.mBufferRange = Range;
			EXPECT_EQ(mDevice->CreateBindingSet(Desc).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		}

		FArdaRHITextureDesc TextureDesc;
		TextureDesc.mWidth = TextureDesc.mHeight = 4;
		TextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		TextureDesc.mUsage = EArdaRHITextureUsage::ShaderResource;
		auto Texture = mDevice->CreateTexture(TextureDesc);
		ASSERT_TRUE(Texture);
		LayoutDesc.mItems = {{0, 1, EArdaRHIBindingType::TextureSRV}};
		Layout = mDevice->CreateBindingLayout(LayoutDesc);
		ASSERT_TRUE(Layout);
		Desc.mLayout = Layout.mValue;
		Desc.mItems[0] = {};
		Desc.mItems[0].mType = EArdaRHIBindingType::TextureSRV;
		Desc.mItems[0].mResource = FArdaRHIResourceRef(Texture.mValue.Get());
		for (const FArdaRHITextureSubresourceRange Range : {FArdaRHITextureSubresourceRange{1, 1},
		         FArdaRHITextureSubresourceRange{0, 2},
		         FArdaRHITextureSubresourceRange{0, 1, 0, 1, 1, 1},
		         FArdaRHITextureSubresourceRange{0, 1, 0, 1, 0, 0}})
		{
			Desc.mItems[0].mView.mTextureRange = Range;
			EXPECT_EQ(mDevice->CreateBindingSet(Desc).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		}
	}

	TEST_P(FArdaBindingBoundaryTest, BindlessWritesRejectInvalidKindsAndRangesTransactionally)
	{
		auto Buffer = MakeBuffer();
		ASSERT_TRUE(Buffer);
		FArdaRHIBindlessLayoutDesc LayoutDesc;
		LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
		LayoutDesc.mMaxCapacity = 2;
		LayoutDesc.mRegisterSpaces = {{0, 1, EArdaRHIBindingType::StructuredBufferUAV}};
		auto Layout = mDevice->CreateBindlessLayout(LayoutDesc);
		ASSERT_TRUE(Layout);
		auto Table = mDevice->CreateDescriptorTable(Layout.mValue);
		ASSERT_TRUE(Table);
		FArdaRHIBindingItem Item;
		Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
		Item.mResource = FArdaRHIResourceRef(Buffer.Get());
		ASSERT_TRUE(mDevice->WriteDescriptorTable(Table.mValue, Item));
		const auto Before = Table.mValue->GetDesc().mItems;

		Item.mView.mBufferRange = {16, 4};
		EXPECT_EQ(mDevice->WriteDescriptorTable(Table.mValue, Item).mCode, EArdaRHIResult::InvalidArgument);
		Item.mView = {};
		auto Sampler = mDevice->CreateSampler({});
		ASSERT_TRUE(Sampler);
		Item.mResource = FArdaRHIResourceRef(Sampler.mValue.Get());
		EXPECT_EQ(mDevice->WriteDescriptorTable(Table.mValue, Item).mCode, EArdaRHIResult::InvalidArgument);
		ASSERT_EQ(Table.mValue->GetDesc().mItems.size(), Before.size());
		EXPECT_EQ(Table.mValue->GetDesc().mItems[0].mResource, Before[0].mResource);
		EXPECT_EQ(Table.mValue->GetDesc().mItems[0].mView, Before[0].mView);
	}

	TEST_P(FArdaBindingBoundaryTest, TypedViewsOwnFixedAndBindlessDescriptors)
	{
		// Vulkan's portable storage-buffer offset alignment is at most 256 bytes.
		auto Buffer = MakeBuffer(512);
		ASSERT_TRUE(Buffer);
		FArdaRHIViewDesc ViewDesc;
		ViewDesc.mBufferRange = {256, 16};
		auto SRV = mDevice->CreateShaderResourceView(FArdaRHIResourceRef(Buffer.Get()), ViewDesc);
		auto UAV = mDevice->CreateUnorderedAccessView(FArdaRHIResourceRef(Buffer.Get()), ViewDesc);
		ASSERT_TRUE(SRV);
		ASSERT_TRUE(UAV);

		// Both access kinds must preserve the retained range through the same fixed and bindless boundary.
		for (bool bWrite : {false, true})
		{
			SCOPED_TRACE(bWrite);
			const auto Type =
			    bWrite ? EArdaRHIBindingType::StructuredBufferUAV : EArdaRHIBindingType::StructuredBufferSRV;
			FArdaRHIBindingItem Item;
			Item.mSlot = 5;
			Item.mType = Type;
			Item.mResource = bWrite ? FArdaRHIResourceRef(UAV.mValue.Get()) : FArdaRHIResourceRef(SRV.mValue.Get());
			FArdaRHIBindingLayoutDesc LayoutDesc;
			LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
			LayoutDesc.mRegisterSpace = 3;
			LayoutDesc.mItems = {{5, 1, Type}};
			auto Layout = mDevice->CreateBindingLayout(LayoutDesc);
			ASSERT_TRUE(Layout);
			FArdaRHIBindingSetDesc Desc;
			Desc.mLayout = Layout.mValue;
			Desc.mItems = {Item};
			auto Set = mDevice->CreateBindingSet(Desc);
			ASSERT_TRUE(Set);
			EXPECT_EQ(Set.mValue->GetDesc().mItems[0].mView, ViewDesc);
			EXPECT_EQ(Desc.mItems[0].mView, FArdaRHIViewDesc{});
			Desc.mItems[0].mView = ViewDesc;
			ASSERT_TRUE(mDevice->CreateBindingSet(Desc));

			FArdaRHIBindlessLayoutDesc BindlessDesc;
			BindlessDesc.mVisibility = EArdaRHIShaderStage::Compute;
			BindlessDesc.mRegisterSpace = 3;
			BindlessDesc.mMaxCapacity = 2;
			BindlessDesc.mRegisterSpaces = {{5, 1, Type}};
			auto Bindless = mDevice->CreateBindlessLayout(BindlessDesc);
			ASSERT_TRUE(Bindless);
			auto Table = mDevice->CreateDescriptorTable(Bindless.mValue);
			ASSERT_TRUE(Table);
			EXPECT_TRUE(Table.mValue->GetDesc().mItems.empty());
			ASSERT_TRUE(mDevice->WriteDescriptorTable(Table.mValue, Item));
			EXPECT_EQ(Table.mValue->GetDesc().mItems[0].mView, ViewDesc);

			// A conflicting inline range or opposite access wrapper must fail without replacing a valid table entry.
			FArdaRHIBindingItem InvalidItem = Item;
			InvalidItem.mView.mBufferRange = {272, 16};
			Desc.mItems = {InvalidItem};
			EXPECT_EQ(mDevice->CreateBindingSet(Desc).mStatus.mCode, EArdaRHIResult::InvalidArgument);
			EXPECT_EQ(mDevice->WriteDescriptorTable(Table.mValue, InvalidItem).mCode, EArdaRHIResult::InvalidArgument);
			InvalidItem = Item;
			InvalidItem.mResource =
			    bWrite ? FArdaRHIResourceRef(SRV.mValue.Get()) : FArdaRHIResourceRef(UAV.mValue.Get());
			Desc.mItems = {InvalidItem};
			EXPECT_EQ(mDevice->CreateBindingSet(Desc).mStatus.mCode, EArdaRHIResult::InvalidArgument);
			EXPECT_EQ(mDevice->WriteDescriptorTable(Table.mValue, InvalidItem).mCode, EArdaRHIResult::InvalidArgument);
			ASSERT_EQ(Table.mValue->GetDesc().mItems.size(), 1u);
			EXPECT_EQ(Table.mValue->GetDesc().mItems[0].mResource, Item.mResource);
			EXPECT_EQ(Table.mValue->GetDesc().mItems[0].mView, ViewDesc);
			ASSERT_TRUE(mDevice->ResizeDescriptorTable(Table.mValue, 2, true));
			EXPECT_EQ(Table.mValue->GetDesc().mItems[0].mView, ViewDesc);
		}
	}

	TEST_P(FArdaBindingBoundaryTest, TypedUavLimitsGpuWritesToItsRetainedBufferRange)
	{
		auto Shader = MakeComputeShader("ArdaShaderStructTest", "ShaderStructTestCS");
		ASSERT_TRUE(Shader);
		auto Buffer = MakeBuffer(512);
		ASSERT_TRUE(Buffer);
		FArdaRHIViewDesc ViewDesc;
		ViewDesc.mBufferRange = {256, 4};
		auto View = mDevice->CreateUnorderedAccessView(FArdaRHIResourceRef(Buffer.Get()), ViewDesc);
		ASSERT_TRUE(View);
		FArdaRHIBindingItem Item;
		Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
		Item.mResource = FArdaRHIResourceRef(View.mValue.Get());

		// A shader-relative index zero must refer to the view's first element, for both binding mechanisms.
		for (bool bBindless : {false, true})
		{
			SCOPED_TRACE(bBindless);
			FArdaRHIBindingLayoutRef Layout;
			FArdaRHIBindingSetRef Set;
			if (bBindless)
			{
				FArdaRHIBindlessLayoutDesc Desc;
				Desc.mVisibility = EArdaRHIShaderStage::Compute;
				Desc.mMaxCapacity = 1;
				Desc.mRegisterSpaces = {{0, 1, EArdaRHIBindingType::StructuredBufferUAV}};
				auto CreatedLayout = mDevice->CreateBindlessLayout(Desc);
				ASSERT_TRUE(CreatedLayout);
				Layout = CreatedLayout.mValue;
				auto Table = mDevice->CreateDescriptorTable(Layout);
				ASSERT_TRUE(Table);
				ASSERT_TRUE(mDevice->WriteDescriptorTable(Table.mValue, Item));
				Set = Table.mValue;
			}
			else
			{
				FArdaRHIBindingLayoutDesc Desc;
				Desc.mVisibility = EArdaRHIShaderStage::Compute;
				Desc.mItems = {{0, 1, EArdaRHIBindingType::StructuredBufferUAV}};
				auto CreatedLayout = mDevice->CreateBindingLayout(Desc);
				ASSERT_TRUE(CreatedLayout);
				Layout = CreatedLayout.mValue;
				FArdaRHIBindingSetDesc SetDesc;
				SetDesc.mLayout = Layout;
				SetDesc.mItems = {Item};
				auto CreatedSet = mDevice->CreateBindingSet(SetDesc);
				ASSERT_TRUE(CreatedSet);
				Set = CreatedSet.mValue;
			}
			FArdaRHIComputePipelineDesc PipelineDesc;
			PipelineDesc.mComputeShader = Shader;
			PipelineDesc.mBindingLayouts = {Layout};
			auto Pipeline = mDevice->CreateComputePipeline(PipelineDesc);
			ASSERT_TRUE(Pipeline);
			FArdaRHIComputeState State;
			State.mPipeline = Pipeline.mValue;
			State.mBindings = {Set};

			auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Commands);
			ASSERT_TRUE(Commands.mValue->Open());
			const uint32_t Initial[128] = {};
			ASSERT_TRUE(Commands.mValue->WriteBuffer(*Buffer, Initial, sizeof(Initial)));
			ASSERT_TRUE(Commands.mValue->SetBufferState(*Buffer, EArdaRHIResourceState::UnorderedAccess));
			ASSERT_TRUE(Commands.mValue->SetComputeState(State));
			Commands.mValue->Dispatch(1, 1, 1);
			eastl::vector<uint8_t> Readback;
			ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Buffer, Readback, 0, sizeof(Initial)));
			ASSERT_TRUE(Commands.mValue->Close());
			ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
			ASSERT_EQ(Readback.size(), sizeof(Initial));
			uint32_t Actual[128] = {};
			std::memcpy(Actual, Readback.data(), sizeof(Actual));
			for (uint32_t Index = 0; Index < 128; ++Index)
			{
				EXPECT_EQ(Actual[Index], Index == 64 ? 0xA2DAu : 0u) << Index;
			}
		}
	}

	TEST_P(FArdaBindingBoundaryTest, InvalidPushConstantsFailCloseAndSubmissionUntilReset)
	{
		auto Shader = MakeComputeShader("ArdaShaderStructTest", "ShaderStructTestCS");
		ASSERT_TRUE(Shader);

		FArdaRHIBindingLayoutDesc LayoutDesc;
		LayoutDesc.mVisibility = EArdaRHIShaderStage::Compute;
		LayoutDesc.mItems = {{0, 1, EArdaRHIBindingType::StructuredBufferUAV},
		    {0, sizeof(uint32_t), EArdaRHIBindingType::PushConstants}};
		auto Layout = mDevice->CreateBindingLayout(LayoutDesc);
		ASSERT_TRUE(Layout);
		FArdaRHIComputePipelineDesc PipelineDesc;
		PipelineDesc.mComputeShader = Shader;
		PipelineDesc.mBindingLayouts = {Layout.mValue};
		auto Pipeline = mDevice->CreateComputePipeline(PipelineDesc);
		ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();
		auto Buffer = MakeBuffer();
		ASSERT_TRUE(Buffer);
		FArdaRHIBindingSetDesc SetDesc;
		SetDesc.mLayout = Layout.mValue;
		SetDesc.mItems.resize(1);
		SetDesc.mItems[0].mType = EArdaRHIBindingType::StructuredBufferUAV;
		SetDesc.mItems[0].mResource = FArdaRHIResourceRef(Buffer.Get());
		auto Set = mDevice->CreateBindingSet(SetDesc);
		ASSERT_TRUE(Set);
		FArdaRHIComputeState State;
		State.mPipeline = Pipeline.mValue;
		State.mBindings = {Set.mValue};
		auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		const uint32_t Value = 7;

		// No pipeline and malformed/oversized byte spans fail before the native driver reads Data.
		Commands.mValue->SetPushConstants(&Value, sizeof(Value));
		EXPECT_EQ(Commands.mValue->Close().mCode, EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(mDevice->ExecuteCommandList(Commands.mValue).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		for (size_t Size : {size_t(0), size_t(3), size_t(8), SIZE_MAX})
		{
			ASSERT_TRUE(Commands.mValue->Reset());
			ASSERT_TRUE(Commands.mValue->SetComputeState(State));
			Commands.mValue->SetPushConstants(&Value, Size);
			EXPECT_EQ(Commands.mValue->Close().mCode, EArdaRHIResult::InvalidArgument);
			EXPECT_EQ(mDevice->ExecuteCommandList(Commands.mValue).mStatus.mCode, EArdaRHIResult::InvalidArgument);
		}
		ASSERT_TRUE(Commands.mValue->Reset());
		ASSERT_TRUE(Commands.mValue->SetComputeState(State));
		Commands.mValue->SetPushConstants(nullptr, sizeof(Value));
		EXPECT_EQ(Commands.mValue->Close().mCode, EArdaRHIResult::InvalidArgument);

		// Successful reset clears the deferred error and retains the ordinary valid recording path.
		ASSERT_TRUE(Commands.mValue->Reset());
		ASSERT_TRUE(Commands.mValue->SetComputeState(State));
		Commands.mValue->SetPushConstants(&Value, sizeof(Value));
		ASSERT_TRUE(Commands.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
	}

#if defined(ARDA_TEST_NATIVE_D3D12)
	class FArdaD3D12PushConstantBoundaryTest : public FArdaBindingBoundaryBase
	{
	protected:
		const char* GetBackendName() const override
		{
			return "native-d3d12";
		}
	};

	class FArdaD3D12CudaPushConstantBoundaryTest : public FArdaD3D12PushConstantBoundaryTest
	{
	protected:
		EArdaCudaExecutionMode GetCudaExecutionMode() const override
		{
			return EArdaCudaExecutionMode::ContextSwitch;
		}
	};

	TEST_F(FArdaD3D12CudaPushConstantBoundaryTest, ReplayDropsConstantsOnlyWhenPipelineIdentityChanges)
	{
		const auto Cuda = mDevice->GetCudaCapabilities();
		if (!Cuda)
		{
			GTEST_SKIP() << Cuda.mUnavailableReason.c_str();
		}
		ASSERT_EQ(Cuda.mLaunchMode, EArdaCudaLaunchMode::ContextSwitch);
		FArdaAddOperand Operand(mDevice);
		const auto Support = Operand.GetOperandSupport();
		if (!Support && Support.mCode == EArdaRHIResult::Unsupported)
		{
			GTEST_SKIP() << Support.mMessage.c_str();
		}
		ASSERT_TRUE(Support) << Support.mMessage.c_str();
		auto Shader = MakeComputeShader("ArdaPushConstantReplayTest", "PushConstantReplayCS");
		ASSERT_TRUE(Shader);
		auto Output = MakeBuffer();
		ASSERT_TRUE(Output);

		// Distinct pipelines consume the same first DWORD but declare different legal push capacities.
		FArdaRHIComputeState States[2];
		for (uint32_t Index = 0; Index < 2; ++Index)
		{
			FArdaRHIBindingLayoutDesc Desc;
			Desc.mVisibility = EArdaRHIShaderStage::Compute;
			Desc.mItems = {{0, 2, EArdaRHIBindingType::StructuredBufferUAV},
			    {0, (2 - Index) * uint32_t(sizeof(uint32_t)), EArdaRHIBindingType::PushConstants}};
			auto Layout = mDevice->CreateBindingLayout(Desc);
			ASSERT_TRUE(Layout);
			FArdaRHIBindingSetDesc SetDesc;
			SetDesc.mLayout = Layout.mValue;
			for (uint32_t Element = 0; Element < 2; ++Element)
			{
				FArdaRHIBindingItem Item;
				Item.mArrayElement = Element;
				Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
				Item.mResource = FArdaRHIResourceRef(Output.Get());
				SetDesc.mItems.push_back(Item);
			}
			auto Set = mDevice->CreateBindingSet(SetDesc);
			ASSERT_TRUE(Set);
			FArdaRHIComputePipelineDesc PipelineDesc;
			PipelineDesc.mComputeShader = Shader;
			PipelineDesc.mBindingLayouts = {Layout.mValue};
			auto Pipeline = mDevice->CreateComputePipeline(PipelineDesc);
			ASSERT_TRUE(Pipeline);
			States[Index].mPipeline = Pipeline.mValue;
			States[Index].mBindings = {Set.mValue};
		}

		FArdaRHIBufferDesc CudaDesc;
		CudaDesc.mByteSize = sizeof(uint32_t);
		CudaDesc.mUsage = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::UnorderedAccess;
		CudaDesc.mbCudaInterop = true;
		auto CudaBuffer = mDevice->CreateBuffer(CudaDesc);
		ASSERT_TRUE(CudaBuffer);
		FArdaAddParameters Parameters;
		Parameters.mInput.mBuffer = Parameters.mOutput.mBuffer = CudaBuffer.mValue;
		Parameters.mCount = 1;
		FArdaCudaSequence Sequence(mDevice);
		for (uint32_t Bias : {3u, 5u})
		{
			Parameters.mBias = Bias;
			ASSERT_TRUE(Sequence.Add(Operand, Parameters));
		}
		auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Commands);
		ASSERT_TRUE(Commands.mValue->Open());
		const uint32_t Initial = 11;
		ASSERT_TRUE(Commands.mValue->WriteBuffer(*CudaBuffer.mValue, &Initial, sizeof(Initial)));
		ASSERT_TRUE(Commands.mValue->SetComputeState(States[0]));
		const uint32_t Previous[2] = {7, 19};
		Commands.mValue->SetPushConstants(Previous, sizeof(Previous));
		ASSERT_TRUE(Commands.mValue->SetComputeState(States[1]));
		ASSERT_TRUE(Sequence.DispatchDeferred(*Commands.mValue));
		const uint32_t Value = 37;
		Commands.mValue->SetPushConstants(&Value, sizeof(Value));
		Commands.mValue->Dispatch(1, 1, 1);

		// Same-pipeline rebinding must retain valid bytes for the following CUDA segment replay.
		ASSERT_TRUE(Commands.mValue->SetComputeState(States[1]));
		ASSERT_TRUE(Sequence.DispatchDeferred(*Commands.mValue));
		Commands.mValue->Dispatch(1, 1, 1);
		eastl::vector<uint8_t> Readback;
		eastl::vector<uint8_t> CudaReadback;
		ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Output, Readback, 0, sizeof(Value)));
		ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*CudaBuffer.mValue, CudaReadback, 0, sizeof(Initial)));
		ASSERT_TRUE(Commands.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
		ASSERT_EQ(Readback.size(), sizeof(Value));
		ASSERT_EQ(CudaReadback.size(), sizeof(Initial));
		uint32_t Actual = 0;
		uint32_t CudaActual = 0;
		std::memcpy(&Actual, Readback.data(), sizeof(Actual));
		std::memcpy(&CudaActual, CudaReadback.data(), sizeof(CudaActual));
		EXPECT_EQ(Actual, Value);
		EXPECT_EQ(CudaActual, Initial + 2 * (3 + 5));
	}

	TEST_F(FArdaD3D12PushConstantBoundaryTest, BroadcastsEveryBlockAcrossDescriptorAndRegisterSpaceBoundaries)
	{
		// Each shader output consumes a separate push block, so readback verifies broadcasting without validation.
		auto Shader = MakeComputeShader("ArdaPushConstantBroadcastTest", "PushConstantBroadcastCS");
		ASSERT_TRUE(Shader);
		FArdaRHIBufferRef Outputs[] = {MakeBuffer(), MakeBuffer(), MakeBuffer()};
		for (const auto& Output : Outputs)
		{
			ASSERT_TRUE(Output);
		}

		// Exercise consecutive, interleaved, trailing, and push-only layouts before the next descriptor table.
		for (uint32_t Arrangement = 0; Arrangement < 4; ++Arrangement)
		{
			SCOPED_TRACE(Arrangement);
			FArdaRHIBindingLayoutDesc FirstDesc;
			FirstDesc.mVisibility = EArdaRHIShaderStage::Compute;
			FirstDesc.mItems = {{0, sizeof(uint32_t), EArdaRHIBindingType::PushConstants},
			    {1, sizeof(uint32_t), EArdaRHIBindingType::PushConstants}};
			if (Arrangement < 3)
			{
				FirstDesc.mItems.insert(FirstDesc.mItems.begin() + Arrangement,
				    {0, 2, EArdaRHIBindingType::StructuredBufferUAV});
			}
			auto FirstLayout = mDevice->CreateBindingLayout(FirstDesc);
			ASSERT_TRUE(FirstLayout);
			FArdaRHIBindingSetDesc FirstSetDesc;
			FirstSetDesc.mLayout = FirstLayout.mValue;
			if (Arrangement < 3)
			{
				for (uint32_t Index = 0; Index < 2; ++Index)
				{
					FArdaRHIBindingItem Item;
					Item.mArrayElement = Index;
					Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
					Item.mResource = FArdaRHIResourceRef(Outputs[Index].Get());
					FirstSetDesc.mItems.push_back(Item);
				}
			}
			auto FirstSet = mDevice->CreateBindingSet(FirstSetDesc);
			ASSERT_TRUE(FirstSet);
			eastl::vector<FArdaRHIBindingLayoutRef> Layouts = {FirstLayout.mValue};
			eastl::vector<FArdaRHIBindingSetRef> Sets = {FirstSet.mValue};

			if (Arrangement == 3)
			{
				FArdaRHIBindingLayoutDesc ArrayDesc;
				ArrayDesc.mVisibility = EArdaRHIShaderStage::Compute;
				ArrayDesc.mItems = {{0, 2, EArdaRHIBindingType::StructuredBufferUAV}};
				auto ArrayLayout = mDevice->CreateBindingLayout(ArrayDesc);
				ASSERT_TRUE(ArrayLayout);
				FArdaRHIBindingSetDesc ArraySetDesc;
				ArraySetDesc.mLayout = ArrayLayout.mValue;
				for (uint32_t Index = 0; Index < 2; ++Index)
				{
					FArdaRHIBindingItem Item;
					Item.mArrayElement = Index;
					Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
					Item.mResource = FArdaRHIResourceRef(Outputs[Index].Get());
					ArraySetDesc.mItems.push_back(Item);
				}
				auto ArraySet = mDevice->CreateBindingSet(ArraySetDesc);
				ASSERT_TRUE(ArraySet);
				Layouts.push_back(ArrayLayout.mValue);
				Sets.push_back(ArraySet.mValue);
			}

			FArdaRHIBindingLayoutDesc LastDesc;
			LastDesc.mVisibility = EArdaRHIShaderStage::Compute;
			LastDesc.mRegisterSpace = 3;
			LastDesc.mItems = {{0, sizeof(uint32_t), EArdaRHIBindingType::PushConstants},
			    {0, 1, EArdaRHIBindingType::StructuredBufferUAV}};
			auto LastLayout = mDevice->CreateBindingLayout(LastDesc);
			ASSERT_TRUE(LastLayout);
			FArdaRHIBindingSetDesc LastSetDesc;
			LastSetDesc.mLayout = LastLayout.mValue;
			FArdaRHIBindingItem LastItem;
			LastItem.mType = EArdaRHIBindingType::StructuredBufferUAV;
			LastItem.mResource = FArdaRHIResourceRef(Outputs[2].Get());
			LastSetDesc.mItems = {LastItem};
			auto LastSet = mDevice->CreateBindingSet(LastSetDesc);
			ASSERT_TRUE(LastSet);
			Layouts.push_back(LastLayout.mValue);
			Sets.push_back(LastSet.mValue);

			FArdaRHIComputePipelineDesc PipelineDesc;
			PipelineDesc.mComputeShader = Shader;
			PipelineDesc.mBindingLayouts = Layouts;
			auto Pipeline = mDevice->CreateComputePipeline(PipelineDesc);
			ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();
			FArdaRHIComputeState State;
			State.mPipeline = Pipeline.mValue;
			State.mBindings = Sets;

			// Repeated recording proves every block receives each new value, including after command-list reset.
			auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Commands);
			ASSERT_TRUE(Commands.mValue->Open());
			for (uint32_t Iteration = 0; Iteration < 2; ++Iteration)
			{
				if (Iteration)
				{
					ASSERT_TRUE(Commands.mValue->Reset());
				}
				for (const auto& Output : Outputs)
				{
					ASSERT_TRUE(Commands.mValue->SetBufferState(*Output, EArdaRHIResourceState::UnorderedAccess));
				}
				ASSERT_TRUE(Commands.mValue->SetComputeState(State));
				const uint32_t Value = 37 + Arrangement * 100 + Iteration;
				Commands.mValue->SetPushConstants(&Value, sizeof(Value));
				Commands.mValue->Dispatch(1, 1, 1);
				eastl::vector<uint8_t> Readbacks[3];
				for (uint32_t Index = 0; Index < 3; ++Index)
				{
					ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Outputs[Index],
					    Readbacks[Index],
					    0,
					    sizeof(uint32_t)));
				}
				ASSERT_TRUE(Commands.mValue->Close());
				ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
				for (const auto& Readback : Readbacks)
				{
					ASSERT_EQ(Readback.size(), sizeof(uint32_t));
					uint32_t Actual = 0;
					std::memcpy(&Actual, Readback.data(), sizeof(Actual));
					EXPECT_EQ(Actual, Value);
				}
			}
		}
	}
#endif

	const char* const NativeBackends[] = {
#if defined(ARDA_TEST_NATIVE_D3D12)
	    "native-d3d12",
#endif
#if defined(ARDA_TEST_NATIVE_VULKAN)
	    "native-vulkan",
#endif
	};
	INSTANTIATE_TEST_SUITE_P(Native, FArdaBindingBoundaryTest, testing::ValuesIn(NativeBackends));
}
