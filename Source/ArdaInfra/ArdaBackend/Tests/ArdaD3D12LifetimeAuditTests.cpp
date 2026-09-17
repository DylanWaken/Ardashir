#include "ArdaTestBackend.h"
#include "ArdaExternalInterop.h"
#include <gtest/gtest.h>

#if defined(ARDA_TEST_NATIVE_D3D12)
#include <d3d12.h>
#include "ArdaD3D12AuditDevice.h"
#include "ArdaD3D12AuditQueue.h"
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <cstring>
#include <atomic>
#include <thread>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
	using namespace arda;
	using Microsoft::WRL::ComPtr;

	struct FArdaD3D12AuditHost
	{
		ComPtr<ID3D12Device> mDevice;
		ComPtr<ID3D12CommandQueue> mQueue;
		ComPtr<ID3D12CommandQueue> mComputeQueue;
		ComPtr<ID3D12Fence> mGate;
	};

	struct FArdaD3D12AuditProvider : IArdaExternalDeviceProvider
	{
		eastl::shared_ptr<FArdaD3D12AuditHost> mHost;
		ComPtr<ID3D12Device> mDeviceOverride;
		ComPtr<ID3D12CommandQueue> mQueueOverride;
		eastl::shared_ptr<void> mLifetimeOverride;

		const char* GetBackendName() const noexcept override
		{
			return "native-d3d12";
		}

		bool GetExternalDeviceDesc(FArdaExternalDeviceDesc& Out) const override
		{
			Out = {};
			Out.mBackendName = "native-d3d12";
			Out.mNativeApi = "d3d12";
			Out.mDevice = FArdaNativeObject(mDeviceOverride ? mDeviceOverride.Get() : mHost->mDevice.Get());
			Out.mQueues.push_back({EArdaRHIQueueType::Graphics,
			    FArdaNativeObject(mQueueOverride ? mQueueOverride.Get() : mHost->mQueue.Get()),
			    0,
			    0});
			Out.mQueues.push_back({EArdaRHIQueueType::Compute, FArdaNativeObject(mHost->mComputeQueue.Get()), 0, 0});
			return true;
		}

		eastl::shared_ptr<void> GetLifetimeToken() const override
		{
			return mLifetimeOverride ? mLifetimeOverride : mHost;
		}
	};

	class FArdaD3D12LifetimeProbe final : public IUnknown
	{
	public:
		explicit FArdaD3D12LifetimeProbe(eastl::shared_ptr<std::atomic<bool>> Released)
		    : mReleased(eastl::move(Released))
		{
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Interface, void** Object) override
		{
			if (!Object)
			{
				return E_POINTER;
			}
			*Object = nullptr;
			if (Interface != __uuidof(IUnknown))
			{
				return E_NOINTERFACE;
			}
			*Object = this;
			AddRef();
			return S_OK;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++mReferences;
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG Remaining = --mReferences;
			if (!Remaining)
			{
				delete this;
			}
			return Remaining;
		}

	private:
		~FArdaD3D12LifetimeProbe()
		{
			mReleased->store(true);
		}

		std::atomic<ULONG> mReferences{1};
		eastl::shared_ptr<std::atomic<bool>> mReleased;
	};

	class FArdaD3D12LifetimeAudit : public testing::Test
	{
	protected:
		FArdaD3D12AuditProvider mProvider;
		FArdaRHIDeviceRef mDevice;
		ComPtr<ID3D12InfoQueue> mDiagnostics;
		bool mbRegistered = false;

		void SetUp() override
		{
			ShutdownBackend();
#if ARDA_TEST_ENABLE_VALIDATION
			ComPtr<ID3D12Debug> Debug;
			if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&Debug))))
			{
				GTEST_SKIP() << "D3D12 validation unavailable";
			}
			Debug->EnableDebugLayer();
#endif
			mProvider.mHost = eastl::make_shared<FArdaD3D12AuditHost>();
			auto& Host = *mProvider.mHost;
			if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&Host.mDevice))))
			{
				GTEST_SKIP() << "D3D12 device unavailable";
			}
			D3D12_COMMAND_QUEUE_DESC Queue{};
			Queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			ASSERT_TRUE(SUCCEEDED(Host.mDevice->CreateCommandQueue(&Queue, IID_PPV_ARGS(&Host.mQueue))));
			Queue.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
			ASSERT_TRUE(SUCCEEDED(Host.mDevice->CreateCommandQueue(&Queue, IID_PPV_ARGS(&Host.mComputeQueue))));
			ASSERT_TRUE(SUCCEEDED(Host.mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Host.mGate))));
			(void)Host.mDevice.As(&mDiagnostics);
			ASSERT_TRUE(RegisterExternalDeviceProvider(mProvider));
			mbRegistered = true;
			auto Configuration = MakeArdaTestBackendConfiguration();
			Configuration.mBackendName = "native-d3d12";
			Configuration.mDeviceSource = EArdaDeviceSource::ExternalProvider;
			Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
			ASSERT_TRUE(ConfigureBackend(Configuration));
			ARDA_REQUIRE_BACKEND();
			mDevice = GetDevice();
			ASSERT_TRUE(mDevice);
		}

		void TearDown() override
		{
			// Always release the host queue, including after an assertion interrupts the test.
			if (mProvider.mHost && mProvider.mHost->mGate)
			{
				EXPECT_TRUE(SUCCEEDED(mProvider.mHost->mGate->Signal(1)));
			}
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
				mDevice.Reset();
			}
			ShutdownBackend();
			if (mbRegistered)
			{
				EXPECT_TRUE(UnregisterExternalDeviceProvider(mProvider));
			}
			EXPECT_TRUE(ConfigureBackend(MakeArdaTestBackendConfiguration()));
			if (mDiagnostics)
			{
				for (UINT64 Index = 0; Index < mDiagnostics->GetNumStoredMessages(); ++Index)
				{
					SIZE_T Size = 0;
					ASSERT_TRUE(SUCCEEDED(mDiagnostics->GetMessage(Index, nullptr, &Size)));
					std::vector<uint8_t> Bytes(Size);
					auto* Message = reinterpret_cast<D3D12_MESSAGE*>(Bytes.data());
					ASSERT_TRUE(SUCCEEDED(mDiagnostics->GetMessage(Index, Message, &Size)));
					EXPECT_GT(Message->Severity, D3D12_MESSAGE_SEVERITY_ERROR) << Message->pDescription;
				}
			}
		}
	};
}

TEST_F(FArdaD3D12LifetimeAudit, ResetWhileSubmissionIsBlockedPreservesBothRecordings)
{
	FArdaRHIBufferDesc Desc;
	Desc.mByteSize = sizeof(uint32_t);
	auto First = mDevice->CreateBuffer(Desc);
	auto Second = mDevice->CreateBuffer(Desc);
	ASSERT_TRUE(First);
	ASSERT_TRUE(Second);
	auto Commands = mDevice->CreateCommandList();
	ASSERT_TRUE(Commands);
	const uint32_t FirstValue = 0x12345678u;
	const uint32_t SecondValue = 0x87654321u;
	ASSERT_TRUE(Commands.mValue->Open());
	ASSERT_TRUE(Commands.mValue->WriteBuffer(*First.mValue, &FirstValue, sizeof(FirstValue)));
	ASSERT_TRUE(Commands.mValue->Close());
	auto& Host = *mProvider.mHost;
	ASSERT_TRUE(SUCCEEDED(Host.mQueue->Wait(Host.mGate.Get(), 1)));
	auto FirstSubmission = mDevice->ExecuteCommandList(Commands.mValue);
	ASSERT_TRUE(FirstSubmission);
	auto Pending = mDevice->PollSubmission(FirstSubmission.mValue);
	ASSERT_TRUE(Pending);
	ASSERT_FALSE(Pending.mValue);
	ASSERT_TRUE(Commands.mValue->Reset());
	ASSERT_TRUE(Commands.mValue->WriteBuffer(*Second.mValue, &SecondValue, sizeof(SecondValue)));
	ASSERT_TRUE(Commands.mValue->Close());
	auto SecondSubmission = mDevice->ExecuteCommandList(Commands.mValue);
	ASSERT_TRUE(SecondSubmission);
	ASSERT_TRUE(SUCCEEDED(Host.mGate->Signal(1)));
	ASSERT_TRUE(mDevice->WaitForSubmission(SecondSubmission.mValue));

	ASSERT_TRUE(Commands.mValue->Reset());
	eastl::vector<uint8_t> FirstReadback;
	eastl::vector<uint8_t> SecondReadback;
	ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*First.mValue, FirstReadback, 0, sizeof(FirstValue)));
	ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Second.mValue, SecondReadback, 0, sizeof(SecondValue)));
	ASSERT_TRUE(Commands.mValue->Close());
	ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
	ASSERT_EQ(FirstReadback.size(), sizeof(FirstValue));
	ASSERT_EQ(SecondReadback.size(), sizeof(SecondValue));
	uint32_t Actual = 0;
	std::memcpy(&Actual, FirstReadback.data(), sizeof(Actual));
	EXPECT_EQ(Actual, FirstValue);
	std::memcpy(&Actual, SecondReadback.data(), sizeof(Actual));
	EXPECT_EQ(Actual, SecondValue);
}

TEST_F(FArdaD3D12LifetimeAudit, ShaderTableKeepsBoundGenerationAndLastSuccessfulCommit)
{
	if (!mDevice->GetCapabilities().mRayTracing.mbPipelineShaders)
	{
		GTEST_SKIP() << "D3D12 ray-tracing pipelines unavailable";
	}
	std::ifstream Stream(std::filesystem::path(ARDA_BACKEND_TEST_SHADER_DIR) / "ArdaRayTracingTest.dxil",
	    std::ios::binary | std::ios::ate);
	ASSERT_TRUE(Stream);
	ASSERT_GT(Stream.tellg(), 0);
	std::vector<uint8_t> Bytecode(static_cast<size_t>(Stream.tellg()));
	Stream.seekg(0);
	Stream.read(reinterpret_cast<char*>(Bytecode.data()), static_cast<std::streamsize>(Bytecode.size()));
	ASSERT_TRUE(Stream);
	FArdaRHIShaderDesc ShaderDesc;
	ShaderDesc.mStage = EArdaRHIShaderStage::RayGeneration;
	ShaderDesc.mEntryPoint = "RayGen";
	ShaderDesc.mBytecode = Bytecode.data();
	ShaderDesc.mBytecodeSize = Bytecode.size();
	auto Shader = mDevice->CreateShader(ShaderDesc);
	ASSERT_TRUE(Shader) << Shader.mStatus.mMessage.c_str();
	FArdaRHIBindingLayoutDesc LayoutDesc;
	LayoutDesc.mVisibility = EArdaRHIShaderStage::AllRayTracing;
	LayoutDesc.mItems.push_back({0, 1, EArdaRHIBindingType::StructuredBufferUAV});
	auto Layout = mDevice->CreateBindingLayout(LayoutDesc);
	ASSERT_TRUE(Layout);
	FArdaRHIRayTracingPipelineDesc PipelineDesc;
	PipelineDesc.mShaders.push_back({"RayGen", Shader.mValue, Layout.mValue});
	PipelineDesc.mMaxPayloadSize = sizeof(uint32_t);
	PipelineDesc.mMaxRecursionDepth = 1;
	auto Pipeline = mDevice->CreateRayTracingPipeline(PipelineDesc);
	ASSERT_TRUE(Pipeline) << Pipeline.mStatus.mMessage.c_str();

	FArdaRHIBufferDesc OutputDesc;
	OutputDesc.mByteSize = OutputDesc.mStructureStride = sizeof(uint32_t);
	OutputDesc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess;
	auto OutputA = mDevice->CreateBuffer(OutputDesc);
	auto OutputB = mDevice->CreateBuffer(OutputDesc);
	ASSERT_TRUE(OutputA);
	ASSERT_TRUE(OutputB);
	FArdaRHIBindingSetDesc BindingsDesc;
	BindingsDesc.mLayout = Layout.mValue;
	FArdaRHIBindingItem Item;
	Item.mType = EArdaRHIBindingType::StructuredBufferUAV;
	Item.mResource = TArdaRHIRef<IArdaRHIResource>(OutputA.mValue.Get());
	BindingsDesc.mItems.push_back(Item);
	auto BindingsA = mDevice->CreateBindingSet(BindingsDesc);
	BindingsDesc.mItems[0].mResource = TArdaRHIRef<IArdaRHIResource>(OutputB.mValue.Get());
	auto BindingsB = mDevice->CreateBindingSet(BindingsDesc);
	ASSERT_TRUE(BindingsA);
	ASSERT_TRUE(BindingsB);
	FArdaRHIShaderTableDesc TableDesc;
	TableDesc.mMaxEntries = 1;
	auto Table = mDevice->CreateShaderTable(Pipeline.mValue, TableDesc);
	ASSERT_TRUE(Table);
	ASSERT_TRUE(mDevice->SetShaderTableRayGeneration(Table.mValue, "RayGen", BindingsA.mValue));

	// Uncommitted edits must not release the descriptors encoded by the last commit.
	FArdaRHIShaderTableRecordDesc Record;
	Record.mExportName = "RayGen";
	Record.mBindings = BindingsB.mValue;
	ASSERT_TRUE(mDevice->SetShaderTableRecord(Table.mValue, Record));
	BindingsA.mValue.Reset();
	auto Commands = mDevice->CreateCommandList();
	ASSERT_TRUE(Commands);
	ASSERT_TRUE(Commands.mValue->Open());
	const uint32_t Zero = 0;
	ASSERT_TRUE(Commands.mValue->WriteBuffer(*OutputA.mValue, &Zero, sizeof(Zero)));
	ASSERT_TRUE(Commands.mValue->WriteBuffer(*OutputB.mValue, &Zero, sizeof(Zero)));
	ASSERT_TRUE(Commands.mValue->SetBufferState(*OutputA.mValue, EArdaRHIResourceState::UnorderedAccess));
	ASSERT_TRUE(Commands.mValue->SetBufferState(*OutputB.mValue, EArdaRHIResourceState::UnorderedAccess));
	FArdaRHIRayTracingState State;
	State.mShaderTable = Table.mValue;
	ASSERT_TRUE(Commands.mValue->SetRayTracingState(State));
	ASSERT_TRUE(mDevice->CommitShaderTable(Table.mValue));
	// Recommit after binding, before dispatch: the first dispatch still targets A.
	ASSERT_TRUE(Commands.mValue->DispatchRays(1, 1, 1));
	eastl::vector<uint8_t> ReadbackA;
	ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*OutputA.mValue, ReadbackA, 0, sizeof(uint32_t)));

	Record.mExportName = "MissingExport";
	ASSERT_TRUE(mDevice->SetShaderTableRecord(Table.mValue, Record));
	// GetShaderIdentifier reports this deliberately missing export through the native debug layer too.
	D3D12_MESSAGE_ID ExpectedDiagnostic = D3D12_MESSAGE_ID_GET_SHADER_IDENTIFIER_ERROR;
	D3D12_INFO_QUEUE_FILTER Filter{};
	Filter.DenyList.NumIDs = 1;
	Filter.DenyList.pIDList = &ExpectedDiagnostic;
	if (mDiagnostics)
	{
		ASSERT_TRUE(SUCCEEDED(mDiagnostics->PushStorageFilter(&Filter)));
	}
	EXPECT_FALSE(mDevice->CommitShaderTable(Table.mValue));
	if (mDiagnostics)
	{
		mDiagnostics->PopStorageFilter();
	}
	// A failed commit leaves B available to newly bound commands.
	ASSERT_TRUE(Commands.mValue->SetRayTracingState(State));
	ASSERT_TRUE(Commands.mValue->DispatchRays(1, 1, 1));
	eastl::vector<uint8_t> ReadbackB;
	ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*OutputB.mValue, ReadbackB, 0, sizeof(uint32_t)));
	ASSERT_TRUE(Commands.mValue->Close());
	ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
	for (const auto* Readback : {&ReadbackA, &ReadbackB})
	{
		ASSERT_EQ(Readback->size(), sizeof(uint32_t));
		uint32_t Value = 0;
		std::memcpy(&Value, Readback->data(), sizeof(Value));
		EXPECT_EQ(Value, 0xA11CEu);
	}
}

TEST_F(FArdaD3D12LifetimeAudit, SparseRemappingReleasesOnlyCompletelyUnmappedHeapOwners)
{
	if (!mDevice->GetCapabilities().mResidency.mbReservedBuffers)
	{
		GTEST_SKIP() << "D3D12 reserved buffers unavailable";
	}
	constexpr uint64_t Tile = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	FArdaRHIBufferDesc BufferDesc;
	BufferDesc.mByteSize = Tile * 2;
	BufferDesc.mbTiled = true;
	auto Buffer = mDevice->CreateBuffer(BufferDesc);
	ASSERT_TRUE(Buffer);
	FArdaRHIHeapDesc HeapDesc;
	HeapDesc.mCapacity = Tile * 2;
	auto HeapA = mDevice->CreateHeap(HeapDesc);
	auto HeapB = mDevice->CreateHeap(HeapDesc);
	ASSERT_TRUE(HeapA);
	ASSERT_TRUE(HeapB);
	auto ReleasedA = eastl::make_shared<std::atomic<bool>>(false);
	auto ReleasedB = eastl::make_shared<std::atomic<bool>>(false);
	const GUID ProbeId{0x10844ea8, 0x8f51, 0x4b78, {0x83, 0x53, 0x5f, 0x6a, 0x47, 0xc7, 0x5d, 0x4c}};
	const auto AttachProbe = [&](const FArdaRHIHeapRef& Heap, const eastl::shared_ptr<std::atomic<bool>>& Released)
	{
		// D3D12 heap allocation identities are their native heap pointers.
		auto* Native = static_cast<ID3D12Heap*>(const_cast<void*>(Heap->GetMemoryAllocationInfo().mIdentity));
		auto* Probe = new FArdaD3D12LifetimeProbe(Released);
		const HRESULT Result = Native->SetPrivateDataInterface(ProbeId, Probe);
		Probe->Release();
		return Result;
	};
	ASSERT_TRUE(SUCCEEDED(AttachProbe(HeapA.mValue, ReleasedA)));
	ASSERT_TRUE(SUCCEEDED(AttachProbe(HeapB.mValue, ReleasedB)));
	FArdaRHIBufferTileMapping Mapping;
	Mapping.mByteSize = Tile * 2;
	Mapping.mHeap = HeapA.mValue;
	ASSERT_TRUE(mDevice->UpdateBufferTileMappings(Buffer.mValue, {Mapping}, EArdaRHIQueueType::Graphics));
	HeapA.mValue.Reset();
	Mapping.mHeap = HeapB.mValue;
	Mapping.mByteSize = Tile;
	ASSERT_TRUE(mDevice->UpdateBufferTileMappings(Buffer.mValue, {Mapping}, EArdaRHIQueueType::Compute));
	HeapB.mValue.Reset();
	Mapping.mHeap.Reset();
	EXPECT_FALSE(ReleasedA->load());
	EXPECT_FALSE(ReleasedB->load());

	Mapping.mbCommit = false;
	auto Invalid = Mapping;
	Invalid.mBufferOffset = Tile * 2;
	EXPECT_FALSE(mDevice->UpdateBufferTileMappings(Buffer.mValue, {Mapping, Invalid}, EArdaRHIQueueType::Graphics));
	EXPECT_FALSE(ReleasedB->load());
	Mapping.mBufferOffset = Tile;
	ASSERT_TRUE(mDevice->UpdateBufferTileMappings(Buffer.mValue, {Mapping}, EArdaRHIQueueType::Graphics));
	mDevice->RunGarbageCollection();
	mDevice->TrimGpuAllocator();
	EXPECT_TRUE(ReleasedA->load());
	EXPECT_FALSE(ReleasedB->load());
	Mapping.mBufferOffset = 0;
	ASSERT_TRUE(mDevice->UpdateBufferTileMappings(Buffer.mValue, {Mapping}, EArdaRHIQueueType::Compute));
	mDevice->RunGarbageCollection();
	mDevice->TrimGpuAllocator();
	EXPECT_TRUE(ReleasedB->load());
}

TEST_F(FArdaD3D12LifetimeAudit, ReservedPrefixPreservesDataAcrossResizeAndExtremeRequests)
{
	if (!mDevice->GetCapabilities().mResidency.mbReservedBuffers)
	{
		GTEST_SKIP() << "D3D12 reserved buffers unavailable";
	}
	constexpr uint64_t Tile = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	FArdaRHIBufferDesc Desc;
	Desc.mByteSize = Tile * 4;
	Desc.mbTiled = true;
	auto Buffer = mDevice->CreateBuffer(Desc);
	ASSERT_TRUE(Buffer);
	ASSERT_TRUE(mDevice->CommitReservedResource(Buffer.mValue, Tile, EArdaRHIQueueType::Graphics));
	auto Commands = mDevice->CreateCommandList();
	ASSERT_TRUE(Commands);
	const uint32_t Expected = 0xABCD1234;
	ASSERT_TRUE(Commands.mValue->Open());
	ASSERT_TRUE(Commands.mValue->WriteBuffer(*Buffer.mValue, &Expected, sizeof(Expected)));
	ASSERT_TRUE(Commands.mValue->Close());
	ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
	for (uint64_t Bytes : {Tile, Tile * 2, Tile * 2, Tile, Tile * 3, UINT64_MAX, UINT64_MAX - 1})
	{
		SCOPED_TRACE(Bytes);
		ASSERT_TRUE(mDevice->CommitReservedResource(Buffer.mValue, Bytes, EArdaRHIQueueType::Compute));
		ASSERT_TRUE(Commands.mValue->Reset());
		eastl::vector<uint8_t> Readback;
		ASSERT_TRUE(Commands.mValue->CopyBufferDeviceToHost(*Buffer.mValue, Readback, 0, sizeof(Expected)));
		ASSERT_TRUE(Commands.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
		ASSERT_EQ(Readback.size(), sizeof(Expected));
		uint32_t Actual = 0;
		std::memcpy(&Actual, Readback.data(), sizeof(Actual));
		EXPECT_EQ(Actual, Expected);
	}
	ASSERT_TRUE(mDevice->CommitReservedResource(Buffer.mValue, 0, EArdaRHIQueueType::Graphics));
}

TEST_F(FArdaD3D12LifetimeAudit, ConcurrentIdleAndSubmissionRetireIncreasingReceipts)
{
	std::atomic<bool> Stop{false};
	std::atomic<bool> WaitSucceeded{true};
	std::thread Waiter(
	    [&]
	    {
		    while (!Stop.load())
		    {
			    if (!mDevice->WaitForIdle())
			    {
				    WaitSucceeded = false;
				    break;
			    }
			    std::this_thread::yield();
		    }
	    });
	// Join before reporting failures so assertion exits cannot strand a thread.
	bool Succeeded = true;
	uint64_t Previous = 0;
	for (uint32_t Iteration = 0; Iteration < 64 && Succeeded; ++Iteration)
	{
		auto Commands = mDevice->CreateCommandList();
		Succeeded = bool(Commands) && bool(Commands.mValue->Open()) && bool(Commands.mValue->Close());
		if (Succeeded)
		{
			auto Submitted = mDevice->ExecuteCommandList(Commands.mValue);
			Succeeded =
			    bool(Submitted) && Submitted.mValue > Previous && bool(mDevice->WaitForSubmission(Submitted.mValue));
			Previous = Submitted.mValue;
		}
	}
	Stop = true;
	Waiter.join();
	EXPECT_TRUE(Succeeded);
	EXPECT_TRUE(WaitSucceeded.load());
}

TEST_F(FArdaD3D12LifetimeAudit, PartialInitializationFenceFailureCleansUpAndCanRetry)
{
	ASSERT_TRUE(mDevice->WaitForIdle());
	mDevice.Reset();
	ShutdownBackend();
	for (uint32_t FailedFence = 1; FailedFence <= ArdaRHIQueueTypeCount; ++FailedFence)
	{
		SCOPED_TRACE(FailedFence);
		auto* Injected = new FArdaD3D12AuditDevice(mProvider.mHost->mDevice.Get());
		Injected->mFailFenceCall = FailedFence;
		mProvider.mDeviceOverride.Attach(Injected);
		EXPECT_FALSE(InitializeBackend());
		EXPECT_EQ(Injected->mFenceCalls, FailedFence);
		ShutdownBackend();
	}
	mProvider.mDeviceOverride.Reset();
	ARDA_REQUIRE_BACKEND();
	mDevice = GetDevice();
	ASSERT_TRUE(mDevice);
}

TEST_F(FArdaD3D12LifetimeAudit, RepeatedAllocateSubmitReopenAndTrimReturnToBaseline)
{
	ASSERT_TRUE(mDevice->WaitForIdle());
	mDevice->TrimDescriptorCaches();
	mDevice->TrimGpuAllocator();
	const auto Before = mDevice->GetResourceLifetimeStats();
	const auto BeforeMemory = mDevice->GetGpuAllocatorStats();
	for (uint32_t Iteration = 0; Iteration < 32; ++Iteration)
	{
		SCOPED_TRACE(Iteration);
		{
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 4096;
			Desc.mUsage = EArdaRHIBufferUsage::Raw | EArdaRHIBufferUsage::UnorderedAccess;
			auto Buffer = mDevice->CreateBuffer(Desc);
			auto Timer = mDevice->CreateTimerQuery();
			auto Commands = mDevice->CreateCommandList();
			ASSERT_TRUE(Buffer);
			ASSERT_TRUE(Timer);
			ASSERT_TRUE(Commands);
			ASSERT_TRUE(Commands.mValue->Open());
			ASSERT_TRUE(Commands.mValue->BeginTimerQuery(*Timer.mValue));
			ASSERT_TRUE(Commands.mValue->ClearBufferUInt(*Buffer.mValue, Iteration));
			ASSERT_TRUE(Commands.mValue->EndTimerQuery(*Timer.mValue));
			ASSERT_TRUE(Commands.mValue->Close());
			ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
			ASSERT_TRUE(Commands.mValue->Reset());
			ASSERT_TRUE(Commands.mValue->WriteBuffer(*Buffer.mValue, &Iteration, sizeof(Iteration)));
			ASSERT_TRUE(Commands.mValue->Close());
			ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
		}
		ASSERT_TRUE(mDevice->WaitForIdle());
		mDevice->RunGarbageCollection();
		mDevice->TrimDescriptorCaches();
		mDevice->TrimGpuAllocator();
		const auto After = mDevice->GetResourceLifetimeStats();
		for (size_t Type = 0; Type < static_cast<size_t>(EArdaRHIResourceType::Count); ++Type)
		{
			EXPECT_EQ(After.mLiveResources[Type], Before.mLiveResources[Type]) << Type;
		}
		EXPECT_EQ(After.mResourceDescriptors, Before.mResourceDescriptors);
		EXPECT_EQ(After.mSamplerDescriptors, Before.mSamplerDescriptors);
		EXPECT_EQ(After.mPendingSubmissions, Before.mPendingSubmissions);
		const auto AfterMemory = mDevice->GetGpuAllocatorStats();
		EXPECT_EQ(AfterMemory.mHeapBytes, BeforeMemory.mHeapBytes);
		EXPECT_EQ(AfterMemory.mCommittedBytes, BeforeMemory.mCommittedBytes);
	}
}

TEST_F(FArdaD3D12LifetimeAudit, ReportsNativeFormatFactsAndDispatchLimits)
{
	const auto& Capabilities = mDevice->GetCapabilities();
	EXPECT_TRUE(Capabilities.mbFormatSupportReported);
	EXPECT_EQ(Capabilities.mLimits.mMaxComputeWorkGroupCount[0], D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION);
	EXPECT_EQ(Capabilities.mLimits.mMaxTexture2D, D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION);
	const auto Format = mDevice->QueryFormatSupport(EArdaRHIFormat::RGBA8UNorm);
	D3D12_FEATURE_DATA_FORMAT_SUPPORT Native{DXGI_FORMAT_R8G8B8A8_UNORM};
	ASSERT_TRUE(SUCCEEDED(
	    mProvider.mHost->mDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &Native, sizeof(Native))));
	EXPECT_EQ(Format.mNativeFormat, DXGI_FORMAT_R8G8B8A8_UNORM);
	EXPECT_EQ(Format.mbTexture2D, (Native.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) != 0);
	EXPECT_EQ(Format.mbColorAttachment, (Native.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) != 0);
	EXPECT_EQ(Format.mbStorageStore, (Native.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0);
	EXPECT_TRUE(Format.mSampleCounts & 1);
	EXPECT_EQ(mDevice->QueryFormatSupport(EArdaRHIFormat::Unknown).mNativeFormat, 0u);
	const auto Before = mDevice->CaptureDiagnosticSnapshot();
	const auto After = mDevice->CaptureDiagnosticSnapshot();
	ASSERT_TRUE(Before);
	ASSERT_TRUE(After);
	EXPECT_EQ(After.mValue.mBackendName, "native-d3d12");
	EXPECT_EQ(After.mValue.mDeviceStatus, EArdaRHIResult::Success);
	ASSERT_EQ(After.mValue.mQueues.size(), Before.mValue.mQueues.size());
	for (size_t Index = 0; Index < After.mValue.mQueues.size(); ++Index)
	{
		EXPECT_EQ(After.mValue.mQueues[Index].mLastSubmitted, Before.mValue.mQueues[Index].mLastSubmitted);
	}
}

TEST_F(FArdaD3D12LifetimeAudit, DiagnosticSnapshotCopiesAndBoundsInjectedDredData)
{
	ASSERT_TRUE(mDevice->WaitForIdle());
	mDevice.Reset();
	ShutdownBackend();
	auto* Injected = new FArdaD3D12AuditDevice(mProvider.mHost->mDevice.Get());
	mProvider.mDeviceOverride.Attach(Injected);
	ARDA_REQUIRE_BACKEND();
	mDevice = GetDevice();
	Injected->mRemovedReason = DXGI_ERROR_DEVICE_REMOVED;
	Injected->mDredOverride.Attach(new FArdaD3D12AuditDred());
	auto Snapshot = mDevice->CaptureDiagnosticSnapshot();
	ASSERT_TRUE(Snapshot);
	EXPECT_EQ(Snapshot.mValue.mNativeErrorCode, DXGI_ERROR_DEVICE_REMOVED);
	EXPECT_EQ(Snapshot.mValue.mDeviceStatus, EArdaRHIResult::BackendFailure);
	EXPECT_TRUE(Snapshot.mValue.mbNativeFaultDataAvailable);
	EXPECT_TRUE(Snapshot.mValue.mbTruncated);
	EXPECT_EQ(Snapshot.mValue.mBreadcrumbs.size(), ArdaRHIMaxDiagnosticEntries);
	for (const auto& Breadcrumb : Snapshot.mValue.mBreadcrumbs)
	{
		EXPECT_LE(Breadcrumb.size(), 1024u);
	}
	ASSERT_EQ(Snapshot.mValue.mFaults.size(), 2u);
	EXPECT_EQ(Snapshot.mValue.mFaults[0].mAddress, 0xABC000u);
	Injected->mDredOverride.Reset();
	Injected->mRemovedReason = S_OK;
	EXPECT_NE(Snapshot.mValue.mFaults[1].mDescription.find("Injected live buffer"), eastl::string::npos);
}

TEST_F(FArdaD3D12LifetimeAudit, FramebufferViewsHonorMipLayerAndReadOnlyDepth)
{
	ASSERT_TRUE(mDevice->WaitForIdle());
	mDevice.Reset();
	ShutdownBackend();
	auto* Observed = new FArdaD3D12AuditDevice(mProvider.mHost->mDevice.Get());
	mProvider.mDeviceOverride.Attach(Observed);
	ARDA_REQUIRE_BACKEND();
	mDevice = GetDevice();
	FArdaRHITextureDesc ColorDesc;
	ColorDesc.mWidth = ColorDesc.mHeight = 16;
	ColorDesc.mMipLevels = 3;
	ColorDesc.mArraySize = 3;
	ColorDesc.mDimension = EArdaRHITextureDimension::Texture2DArray;
	ColorDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
	ColorDesc.mUsage = EArdaRHITextureUsage::RenderTarget;
	auto Color = mDevice->CreateTexture(ColorDesc);
	ASSERT_TRUE(Color) << Color.mStatus.mMessage.c_str();
	FArdaRHITextureDesc DepthDesc = ColorDesc;
	DepthDesc.mFormat = EArdaRHIFormat::D24S8;
	DepthDesc.mUsage = EArdaRHITextureUsage::DepthStencil;
	auto Depth = mDevice->CreateTexture(DepthDesc);
	ASSERT_TRUE(Depth);
	FArdaRHIFramebufferAttachment Attachment;
	Attachment.mSubresources = {1, 1, 1, 1};
	FArdaRHIFramebufferDesc Desc;
	Desc.mColorAttachments.push_back({Color.mValue, Attachment});
	Attachment.mbReadOnly = true;
	Desc.mDepthAttachment = {Depth.mValue, Attachment};
	const auto BeforeRtv = Observed->mRtvCount;
	const auto BeforeDsv = Observed->mDsvCount;
	auto Framebuffer = mDevice->CreateFramebuffer(Desc);
	ASSERT_TRUE(Framebuffer) << Framebuffer.mStatus.mMessage.c_str();
	EXPECT_EQ(Observed->mRtvCount, BeforeRtv + 1);
	EXPECT_EQ(Observed->mDsvCount, BeforeDsv + 1);
	EXPECT_EQ(Observed->mLastRtv.Format, DXGI_FORMAT_R8G8B8A8_UNORM);
	EXPECT_EQ(Observed->mLastRtv.ViewDimension, D3D12_RTV_DIMENSION_TEXTURE2DARRAY);
	EXPECT_EQ(Observed->mLastRtv.Texture2DArray.MipSlice, 1u);
	EXPECT_EQ(Observed->mLastRtv.Texture2DArray.FirstArraySlice, 1u);
	EXPECT_EQ(Observed->mLastRtv.Texture2DArray.ArraySize, 1u);
	EXPECT_EQ(Observed->mLastDsv.Format, DXGI_FORMAT_D24_UNORM_S8_UINT);
	EXPECT_EQ(Observed->mLastDsv.Texture2DArray.MipSlice, 1u);
	EXPECT_EQ(Observed->mLastDsv.Texture2DArray.FirstArraySlice, 1u);
	EXPECT_EQ(Observed->mLastDsv.Texture2DArray.ArraySize, 1u);
	EXPECT_EQ(Observed->mLastDsv.Flags, D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL);
	ASSERT_TRUE(mDevice->CreateFramebuffer(Desc));
	EXPECT_EQ(Observed->mRtvCount, BeforeRtv + 1);
	EXPECT_EQ(Observed->mDsvCount, BeforeDsv + 1);
	Desc.mDepthAttachment.mAttachment.mbReadOnly = false;
	ASSERT_TRUE(mDevice->CreateFramebuffer(Desc));
	EXPECT_EQ(Observed->mDsvCount, BeforeDsv + 2);
	EXPECT_EQ(Observed->mLastDsv.Flags, D3D12_DSV_FLAG_NONE);
	Desc.mDepthAttachment.mAttachment.mSubresources.mPlaneCount = 1;
	const auto DepthOnly = mDevice->CreateFramebuffer(Desc);
	EXPECT_FALSE(DepthOnly);
	EXPECT_EQ(DepthOnly.mStatus.mCode, EArdaRHIResult::Unsupported);
	EXPECT_EQ(Observed->mDsvCount, BeforeDsv + 2);
}

TEST_F(FArdaD3D12LifetimeAudit, FailedSparseSignalRetainsIntermediateHeapAndShutdownUntilRetired)
{
	if (!mDevice->GetCapabilities().mResidency.mbReservedBuffers)
	{
		GTEST_SKIP() << "D3D12 reserved buffers unavailable";
	}
	ASSERT_TRUE(mDevice->WaitForIdle());
	mDevice.Reset();
	ShutdownBackend();
	auto* Queue = new FArdaD3D12AuditQueue(mProvider.mHost->mQueue.Get());
	mProvider.mQueueOverride.Attach(Queue);
	auto LifetimeReleased = eastl::make_shared<std::atomic<bool>>(false);
	mProvider.mLifetimeOverride = eastl::shared_ptr<void>(mProvider.mHost.get(),
	    [Host = mProvider.mHost, LifetimeReleased](void*)
	    {
		    *LifetimeReleased = true;
	    });
	ARDA_REQUIRE_BACKEND();
	mDevice = GetDevice();
	constexpr uint64_t Tile = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	FArdaRHIBufferDesc BufferDesc;
	BufferDesc.mByteSize = Tile;
	BufferDesc.mbTiled = true;
	auto Buffer = mDevice->CreateBuffer(BufferDesc);
	ASSERT_TRUE(Buffer);
	FArdaRHIHeapDesc HeapDesc;
	HeapDesc.mCapacity = Tile;
	auto HeapA = mDevice->CreateHeap(HeapDesc);
	auto HeapB = mDevice->CreateHeap(HeapDesc);
	ASSERT_TRUE(HeapA);
	ASSERT_TRUE(HeapB);
	auto HeapReleased = eastl::make_shared<std::atomic<bool>>(false);
	const GUID ProbeId{0x995cb530, 0x441e, 0x4bc5, {0x80, 0x3a, 0xce, 0x9f, 0x82, 0xe3, 0x2e, 0x74}};
	auto* NativeHeap = static_cast<ID3D12Heap*>(const_cast<void*>(HeapA.mValue->GetMemoryAllocationInfo().mIdentity));
	auto* Probe = new FArdaD3D12LifetimeProbe(HeapReleased);
	const HRESULT Attached = NativeHeap->SetPrivateDataInterface(ProbeId, Probe);
	Probe->Release();
	ASSERT_TRUE(SUCCEEDED(Attached));

	struct FArdaSignalFailureGuard
	{
		ComPtr<FArdaD3D12AuditQueue> mQueue;
		ComPtr<ID3D12Fence> mGate;

		~FArdaSignalFailureGuard()
		{
			mQueue->mbFailSignal = false;
			(void)mGate->Signal(1);
		}
	} Guard{Queue, mProvider.mHost->mGate};

	ASSERT_TRUE(SUCCEEDED(Queue->Wait(mProvider.mHost->mGate.Get(), 1)));
	Queue->mbFailSignal = true;
	FArdaRHIBufferTileMapping MappingA;
	MappingA.mByteSize = Tile;
	MappingA.mHeap = HeapA.mValue;
	auto MappingB = MappingA;
	MappingB.mHeap = HeapB.mValue;
	EXPECT_FALSE(mDevice->UpdateBufferTileMappings(Buffer.mValue, {MappingA, MappingB}, EArdaRHIQueueType::Graphics));
	MappingA.mHeap.Reset();
	MappingB.mHeap.Reset();
	HeapA.mValue.Reset();
	HeapB.mValue.Reset();
	mDevice->TrimGpuAllocator();
	EXPECT_FALSE(HeapReleased->load());
	Buffer.mValue.Reset();
	mDevice.Reset();
	ShutdownBackend();
	mProvider.mLifetimeOverride.reset();
	EXPECT_FALSE(LifetimeReleased->load());
	EXPECT_FALSE(HeapReleased->load());
	Queue->mbFailSignal = false;
	ASSERT_TRUE(SUCCEEDED(mProvider.mHost->mGate->Signal(1)));
	mProvider.mQueueOverride.Reset();
	ARDA_REQUIRE_BACKEND();
	mDevice = GetDevice();
	ASSERT_TRUE(mDevice->WaitForIdle());
	mDevice->RunGarbageCollection();
	EXPECT_TRUE(LifetimeReleased->load());
	EXPECT_TRUE(HeapReleased->load());
}
#endif
