#include "ArdaTestBackend.h"
#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ArdaInductorMemory.h"
#include "ArdaDependencyGraph.h"

#include <gtest/gtest.h>
#include <EASTL/algorithm.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace
{
	using namespace arda;

	class FHeapGpuDiagnostics final : public IArdaDiagnosticCallback
	{
	public:
		void Message(EArdaDiagnosticSeverity Severity, const char* Text) override
		{
			if (Severity == EArdaDiagnosticSeverity::Error || Severity == EArdaDiagnosticSeverity::Fatal)
			{
				++mErrors;
				std::fprintf(stderr, "Inductor heap validation: %s\n", Text ? Text : "");
			}
		}

		std::atomic<uint32_t> mErrors{0};
	};

	class ArdaInductorMemoryGpu : public testing::TestWithParam<const char*>
	{
	protected:
		void SetUp() override
		{
			ShutdownBackend();
			if (!FindBackendModule(GetParam()))
			{
				GTEST_SKIP() << "Backend not built";
			}
			FArdaBackendConfiguration Configuration;
			Configuration.mBackendName = GetParam();
			Configuration.mbEnableValidation = true;
			Configuration.mMessageCallback = &mDiagnostics;
			ASSERT_TRUE(ConfigureBackend(Configuration));
			ARDA_REQUIRE_BACKEND() << GetBackendError().c_str();
			mDevice = GetDevice();
			ASSERT_TRUE(mDevice);
		}

		void TearDown() override
		{
			if (mDevice)
			{
				EXPECT_TRUE(mDevice->WaitForIdle());
				mDevice->RunGarbageCollection();
				mDevice.Reset();
			}
			ShutdownBackend();
			EXPECT_EQ(mDiagnostics.mErrors.load(), 0u);
		}

		FHeapGpuDiagnostics mDiagnostics;
		FArdaRHIDeviceRef mDevice;
	};

	TEST_P(ArdaInductorMemoryGpu, DifferentBufferSizesAliasAcrossIndependentFramePools)
	{
		eastl::vector<FArdaInductorMemoryRequest> Requests(3);
		for (uint32_t Index = 0; Index < Requests.size(); ++Index)
		{
			auto& Request = Requests[Index];
			Request.mIdentifier = Index;
			Request.mBufferDesc.mByteSize = Index == 1 ? 131072 : 256;
			Request.mBufferDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			Request.mFirstUse = Request.mLastUse = Index;
			Request.mFirstUseNodes = Request.mLastUseNodes = {Index};
		}
		Requests[2].mbPersistent = true;
		ASSERT_TRUE(DescribeArdaInductorMemory(*mDevice, Requests));
		ASSERT_TRUE(Requests[0].mbUseHeapPlacement);
		ASSERT_TRUE(Requests[1].mbUseHeapPlacement);
		EXPECT_FALSE(Requests[2].mbUseHeapPlacement);
		FArdaInductorMemoryOptions Options;
		Options.mFrameCount = 2;
		const auto Planned = PlanArdaInductorMemory(Requests, Options);
		ASSERT_TRUE(Planned) << Planned.mStatus.mMessage.c_str();
		const auto& Plan = Planned.mValue;
		ASSERT_EQ(Plan.mHeaps.size(), 1u);
		EXPECT_EQ(Plan.mResourceOffsets[0], Plan.mResourceOffsets[1]);
		const auto First = MaterializeArdaInductorMemory(*mDevice, Plan);
		ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
		const auto Second = MaterializeArdaInductorMemory(*mDevice, Plan, &First.mValue);
		ASSERT_TRUE(Second) << Second.mStatus.mMessage.c_str();
		EXPECT_NE(First.mValue.mBuffers[0]->GetPhysicalIdentity(), First.mValue.mBuffers[1]->GetPhysicalIdentity());
		EXPECT_NE(First.mValue.mHeaps[0].Get(), Second.mValue.mHeaps[0].Get());
		EXPECT_EQ(First.mValue.mBuffers[2].Get(), Second.mValue.mBuffers[2].Get());
		EXPECT_EQ(First.mValue.mAllocatedBytes + Second.mValue.mAllocatedBytes, Plan.mOwnedBytes);
		const auto HeapInfo = First.mValue.mHeaps[0]->GetMemoryAllocationInfo();
		ASSERT_TRUE(HeapInfo.mbKnown);
		EXPECT_EQ(HeapInfo.mByteSize, Plan.mHeaps[0].mDesc.mCapacity);
		EXPECT_TRUE(First.mValue.mBuffers[0]->GetMemoryAllocationInfo() == HeapInfo);
		EXPECT_TRUE(First.mValue.mBuffers[1]->GetMemoryAllocationInfo() == HeapInfo);
		eastl::vector<FArdaInductorMemoryRequest> Imported(2);
		for (uint32_t Index = 0; Index < Imported.size(); ++Index)
		{
			Imported[Index].mIdentifier = Index;
			Imported[Index].mExternalBuffer = First.mValue.mBuffers[Index];
		}
		ASSERT_TRUE(DescribeArdaInductorMemory(*mDevice, Imported));
		FArdaInductorMemoryOptions ImportOptions;
		ImportOptions.mBudgetBytes = HeapInfo.mByteSize;
		const auto ImportedPlan = PlanArdaInductorMemory(Imported, ImportOptions);
		ASSERT_TRUE(ImportedPlan) << ImportedPlan.mStatus.mMessage.c_str();
		EXPECT_EQ(ImportedPlan.mValue.mExternalBytes, HeapInfo.mByteSize);
		const auto ImportedPool = MaterializeArdaInductorMemory(*mDevice, ImportedPlan.mValue);
		ASSERT_TRUE(ImportedPool) << ImportedPool.mStatus.mMessage.c_str();
		EXPECT_EQ(ImportedPool.mValue.mAllocatedBytes, 0u);
		EXPECT_EQ(ImportedPool.mValue.mExternalBytes, HeapInfo.mByteSize);
		--ImportOptions.mBudgetBytes;
		EXPECT_FALSE(PlanArdaInductorMemory(Imported, ImportOptions));
		for (uint32_t Frame = 0; Frame < 4; ++Frame)
		{
			const auto& Pool = Frame % 2 ? Second.mValue : First.mValue;
			auto Commands = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
			ASSERT_TRUE(Commands);
			ASSERT_TRUE(Commands.mValue->Open());
			eastl::vector<uint8_t> Readbacks[2];
			for (uint32_t Index = 0; Index < 2; ++Index)
			{
				auto& Buffer = *Pool.mBuffers[Index];
				ASSERT_TRUE(Commands.mValue->AliasingBarrier(nullptr, &Buffer));
				const uint32_t Value = 0xABCD0000u + Frame * 16 + Index;
				ASSERT_TRUE(Commands.mValue->ClearBufferUInt(Buffer, Value));
				ASSERT_TRUE(
				    Commands.mValue->CopyBufferDeviceToHost(Buffer, Readbacks[Index], 0, Buffer.GetDesc().mByteSize));
			}
			ASSERT_TRUE(Commands.mValue->Close());
			ASSERT_TRUE(mDevice->ExecuteCommandList(Commands.mValue));
			ASSERT_TRUE(mDevice->WaitForIdle());
			for (uint32_t Index = 0; Index < 2; ++Index)
			{
				ASSERT_EQ(Readbacks[Index].size(), Pool.mBuffers[Index]->GetDesc().mByteSize);
				for (size_t Byte = 0; Byte < Readbacks[Index].size(); Byte += sizeof(uint32_t))
				{
					uint32_t Actual = 0;
					std::memcpy(&Actual, Readbacks[Index].data() + Byte, sizeof(Actual));
					ASSERT_EQ(Actual, 0xABCD0000u + Frame * 16 + Index) << "frame " << Frame << ", byte " << Byte;
				}
			}
		}
	}

	TEST_P(ArdaInductorMemoryGpu, EfficiencyWithLooseBudgetKeepsIndependentWorkspaceUsersParallel)
	{
		const auto& Caps = mDevice->GetCapabilities();
		if (!Caps.IsQueueSupported(EArdaRHIQueueType::Compute) || !Caps.mQueues.mbGpuWaits)
		{
			GTEST_SKIP() << "Independent compute queues are unavailable";
		}
		static const FArdaRHIStatus Registered = []
		{
			for (const auto Kind : {EArdaDependencyNodeKind::Graphics, EArdaDependencyNodeKind::Compute})
			{
				TArdaDependencyNodeDefinition<uint32_t> Definition;
				Definition.mName = Kind == EArdaDependencyNodeKind::Graphics ? "test.inductor.workspace-graphics"
				                                                             : "test.inductor.workspace-compute";
				Definition.mKind = Kind;
				Definition.mCanonicalKey = [](const uint32_t& Cost)
				{
					return eastl::string(reinterpret_cast<const char*>(&Cost), sizeof(Cost));
				};
				Definition.mDescribe = [](const uint32_t& Cost)
				{
					FArdaDependencyNodeDesc Desc;
					Desc.mbSideEffect = true;
					Desc.mEstimatedCost = Cost;
					Desc.mTransientWorkspaceBytes = 4096;
					return Desc;
				};
				Definition.mRecord = [](FArdaDependencyExecutionContext&, const uint32_t&)
				{
					return FArdaRHIStatus{};
				};
				if (auto Status = FArdaNodeRegistry::Get().Register(eastl::move(Definition)); !Status)
				{
					return Status;
				}
			}
			return FArdaRHIStatus{};
		}();
		ASSERT_TRUE(Registered);
		FArdaDependencyGraph Graph(mDevice);
		ASSERT_TRUE(Graph.BeginGraphEdit());
		FArdaInductorOptions Options;
		Options.mObjective = EArdaInductorObjective::Efficiency;
		Options.mMaxVramBytes = 1024 * 1024;
		Options.mFramesInFlight = 2;
		Options.mMinimumAsyncChain = 1;
		Options.mMinimumAsyncSlack = 1;
		ASSERT_TRUE(Graph.SetOptions(Options));
		FArdaRHIBufferDesc RetainedDesc;
		RetainedDesc.mByteSize = 4096;
		const auto Retained = mDevice->CreateBuffer(RetainedDesc);
		ASSERT_TRUE(Retained);
		ASSERT_TRUE(Graph.ImportBuffer("retained-with-no-users", Retained.mValue));
		const auto Graphics =
		    Graph.AttachOrFind("independent-graphics", "test.inductor.workspace-graphics", uint32_t(10));
		const auto Compute = Graph.AttachOrFind("independent-compute", "test.inductor.workspace-compute", uint32_t(10));
		ASSERT_TRUE(Graphics);
		ASSERT_TRUE(Compute);
		const auto Compiled = Graph.EndGraphEdit();
		ASSERT_TRUE(Compiled) << Compiled.mMessage.c_str();
		const auto& Result = Graph.GetCompileResult();
		EXPECT_TRUE(Result.mMemoryDependencies.empty());
		EXPECT_DOUBLE_EQ(Result.mEstimatedExecutionCost, 10.0);
		const auto Position = eastl::find(Result.mExecutionOrder.begin(), Result.mExecutionOrder.end(), Compute.mValue);
		ASSERT_NE(Position, Result.mExecutionOrder.end());
		EXPECT_EQ(Result.mQueues[size_t(Position - Result.mExecutionOrder.begin())], EArdaRHIQueueType::Compute);
		EXPECT_EQ(Result.mWorkspaceResourceIds.size(), 2u);
		const auto First = Graph.Submit();
		ASSERT_TRUE(First) << First.mStatus.mMessage.c_str();
		const auto Second = Graph.Submit();
		ASSERT_TRUE(Second) << Second.mStatus.mMessage.c_str();
		const auto FirstDone = Graph.Wait(First.mValue), SecondDone = Graph.Wait(Second.mValue);
		ASSERT_TRUE(FirstDone.mStatus) << FirstDone.mStatus.mMessage.c_str();
		ASSERT_TRUE(SecondDone.mStatus) << SecondDone.mStatus.mMessage.c_str();
		// Retention affects the budget, but unused imports must not add inter-frame waits.
		EXPECT_EQ(SecondDone.mQueueWaitCount, FirstDone.mQueueWaitCount);
	}

	TEST_P(ArdaInductorMemoryGpu, RawImportsRequireAllocationHintsForHardCapsAndRetainBorrowedLifetime)
	{
		for (const bool bTexture : {false, true})
		{
			SCOPED_TRACE(bTexture ? "texture" : "buffer");
			FArdaRHIBufferRef Buffer;
			FArdaRHITextureRef Texture;
			if (bTexture)
			{
				FArdaRHITextureDesc Desc;
				Desc.mWidth = Desc.mHeight = 32;
				Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
				Desc.mInitialState = EArdaRHIResourceState::Common;
				auto Created = mDevice->CreateTexture(Desc);
				ASSERT_TRUE(Created);
				Texture = eastl::move(Created.mValue);
			}
			else
			{
				FArdaRHIBufferDesc Desc;
				Desc.mByteSize = 4096;
				Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
				auto Created = mDevice->CreateBuffer(Desc);
				ASSERT_TRUE(Created);
				Buffer = eastl::move(Created.mValue);
			}
			TArdaRHIRef<IArdaRHIResource> Original(bTexture ? static_cast<IArdaRHIResource*>(Texture.Get())
			                                                : static_cast<IArdaRHIResource*>(Buffer.Get()));
			const auto Allocation = Original->GetMemoryAllocationInfo();
			ASSERT_TRUE(Allocation.mbKnown);
			ASSERT_NE(Allocation.mIdentity, nullptr);
			const auto NativeRequirements = bTexture ? mDevice->GetTextureMemoryRequirements(Texture)
			                                         : mDevice->GetBufferMemoryRequirements(Buffer);
			ASSERT_TRUE(NativeRequirements);
			EXPECT_EQ(Allocation.mByteSize, NativeRequirements.mValue.mSize);
			const void* NativeIdentity = bTexture ? Texture->GetPhysicalIdentity() : Buffer->GetPhysicalIdentity();
			auto Lifetime = eastl::make_shared<TArdaRHIRef<IArdaRHIResource>>(Original);
			eastl::weak_ptr<TArdaRHIRef<IArdaRHIResource>> WeakLifetime = Lifetime;
			auto Import = [&](FArdaRHIMemoryAllocationInfo Info,
			                  EArdaRHINativeOwnership Ownership,
			                  FArdaInductorMemoryRequest& Out) -> FArdaRHIStatus
			{
				if (bTexture)
				{
					FArdaRHINativeTextureImportDesc Desc;
					Desc.mNativeObject = reinterpret_cast<uintptr_t>(NativeIdentity);
					Desc.mNativeType = std::strcmp(GetParam(), "native-d3d12") == 0
					    ? EArdaRHINativeResourceType::D3D12Resource
					    : EArdaRHINativeResourceType::VulkanImage;
					Desc.mTexture = Texture->GetDesc();
					Desc.mInitialState = EArdaRHIResourceState::Common;
					Desc.mOwnership = Ownership;
					Desc.mLifetimeToken = Lifetime;
					Desc.mMemoryAllocationInfo = Info;
					auto Imported = mDevice->ImportNativeTexture(Desc);
					if (!Imported)
					{
						return Imported.mStatus;
					}
					Out.mKind = EArdaInductorMemoryKind::Texture;
					Out.mExternalTexture = eastl::move(Imported.mValue);
				}
				else
				{
					FArdaRHINativeBufferImportDesc Desc;
					Desc.mNativeObject = reinterpret_cast<uintptr_t>(NativeIdentity);
					Desc.mNativeType = std::strcmp(GetParam(), "native-d3d12") == 0
					    ? EArdaRHINativeResourceType::D3D12Resource
					    : EArdaRHINativeResourceType::VulkanBuffer;
					Desc.mBuffer = Buffer->GetDesc();
					Desc.mInitialState = EArdaRHIResourceState::Common;
					Desc.mOwnership = Ownership;
					Desc.mLifetimeToken = Lifetime;
					Desc.mMemoryAllocationInfo = Info;
					auto Imported = mDevice->ImportNativeBuffer(Desc);
					if (!Imported)
					{
						return Imported.mStatus;
					}
					Out.mExternalBuffer = eastl::move(Imported.mValue);
				}
				return {};
			};
			FArdaInductorMemoryRequest Unknown, Known, Rejected;
			ASSERT_TRUE(Import({}, EArdaRHINativeOwnership::Borrowed, Unknown));
			const auto UnknownInfo = bTexture ? Unknown.mExternalTexture->GetMemoryAllocationInfo()
			                                  : Unknown.mExternalBuffer->GetMemoryAllocationInfo();
			EXPECT_FALSE(UnknownInfo.mbKnown);
			eastl::vector<FArdaInductorMemoryRequest> UnknownRequests{Unknown};
			ASSERT_TRUE(DescribeArdaInductorMemory(*mDevice, UnknownRequests));
			EXPECT_TRUE(PlanArdaInductorMemory(UnknownRequests));
			FArdaInductorMemoryOptions Options;
			Options.mBudgetBytes = Allocation.mByteSize;
			const auto UnknownPlan = PlanArdaInductorMemory(UnknownRequests, Options);
			EXPECT_FALSE(UnknownPlan);
			EXPECT_EQ(UnknownPlan.mStatus.mCode, EArdaRHIResult::Unsupported);
			ASSERT_TRUE(Import(Allocation, EArdaRHINativeOwnership::Borrowed, Known));
			const auto KnownInfo = bTexture ? Known.mExternalTexture->GetMemoryAllocationInfo()
			                                : Known.mExternalBuffer->GetMemoryAllocationInfo();
			EXPECT_TRUE(KnownInfo == Allocation);
			if (bTexture)
			{
				EXPECT_NE(Known.mExternalTexture.Get(), Unknown.mExternalTexture.Get());
				EXPECT_EQ(Known.mExternalTexture->GetPhysicalIdentity(), NativeIdentity);
			}
			else
			{
				EXPECT_NE(Known.mExternalBuffer.Get(), Unknown.mExternalBuffer.Get());
				EXPECT_EQ(Known.mExternalBuffer->GetPhysicalIdentity(), NativeIdentity);
			}
			auto InvalidInfo = Allocation;
			InvalidInfo.mByteSize = 1;
			EXPECT_EQ(Import(InvalidInfo, EArdaRHINativeOwnership::Borrowed, Rejected).mCode,
			    EArdaRHIResult::InvalidArgument);
			InvalidInfo = Allocation;
			InvalidInfo.mIdentity = nullptr;
			EXPECT_EQ(Import(InvalidInfo, EArdaRHINativeOwnership::Borrowed, Rejected).mCode,
			    EArdaRHIResult::InvalidArgument);
			EXPECT_EQ(Import(Allocation, EArdaRHINativeOwnership::Transferred, Rejected).mCode,
			    EArdaRHIResult::Unsupported);
			Lifetime.reset();
			Original.Reset();
			Texture.Reset();
			Buffer.Reset();
			EXPECT_FALSE(WeakLifetime.expired());
			eastl::vector<FArdaInductorMemoryRequest> KnownRequests{Known};
			ASSERT_TRUE(DescribeArdaInductorMemory(*mDevice, KnownRequests));
			const auto KnownPlan = PlanArdaInductorMemory(KnownRequests, Options);
			ASSERT_TRUE(KnownPlan) << KnownPlan.mStatus.mMessage.c_str();
			EXPECT_EQ(KnownPlan.mValue.mExternalBytes, Allocation.mByteSize);
			EXPECT_TRUE(MaterializeArdaInductorMemory(*mDevice, KnownPlan.mValue));
		}
	}

	TEST_P(ArdaInductorMemoryGpu, BufferAndTextureHeapCapacitiesMatchNativeAlignmentAndAllocationMetadata)
	{
		eastl::vector<FArdaInductorMemoryRequest> Requests(2);
		Requests[0].mBufferDesc.mByteSize = 256;
		Requests[0].mBufferDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		Requests[1].mIdentifier = 1;
		Requests[1].mKind = EArdaInductorMemoryKind::Texture;
		Requests[1].mTextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		ASSERT_TRUE(DescribeArdaInductorMemory(*mDevice, Requests));
		const auto Planned = PlanArdaInductorMemory(Requests);
		ASSERT_TRUE(Planned);
		const auto& Plan = Planned.mValue;
		ASSERT_EQ(Plan.mHeaps.size(), 2u);
		const auto Pool = MaterializeArdaInductorMemory(*mDevice, Plan);
		ASSERT_TRUE(Pool) << Pool.mStatus.mMessage.c_str();
		for (uint32_t Index = 0; Index < Requests.size(); ++Index)
		{
			const auto& Request = Requests[Index];
			const uint32_t HeapIndex = Plan.mResourceHeapIndices[Index];
			ASSERT_LT(HeapIndex, Plan.mHeaps.size());
			const auto& Heap = Plan.mHeaps[HeapIndex];
			const uint64_t Alignment =
			    eastl::max(Request.mRequirements.mAlignment, mDevice->GetCapabilities().mHeapAllocationAlignment);
			EXPECT_EQ(Heap.mDesc.mCapacity % Alignment, 0u);
			EXPECT_GE(Heap.mDesc.mCapacity, Request.mRequirements.mSize);
			EXPECT_LT(Heap.mDesc.mCapacity - Request.mRequirements.mSize, Alignment);
			const auto HeapInfo = Pool.mValue.mHeaps[HeapIndex]->GetMemoryAllocationInfo();
			EXPECT_TRUE(HeapInfo.mbKnown);
			EXPECT_EQ(HeapInfo.mByteSize, Heap.mDesc.mCapacity);
			const auto ResourceInfo = Index == 0 ? Pool.mValue.mBuffers[Index]->GetMemoryAllocationInfo()
			                                     : Pool.mValue.mTextures[Index]->GetMemoryAllocationInfo();
			EXPECT_TRUE(ResourceInfo == HeapInfo);
		}
	}

	TEST_P(ArdaInductorMemoryGpu, SeparatelyRecordedASBarrierPreservesSubmittedBuildLifecycle)
	{
		const auto& Ray = mDevice->GetCapabilities().mRayTracing;
		if (!Ray.mbBottomLevel || !Ray.mbCompaction)
		{
			GTEST_SKIP() << "BLAS compaction is unavailable";
		}
		const float Vertices[9] = {-1.f, -1.f, 0.f, 0.f, 1.f, 0.f, 1.f, -1.f, 0.f};
		FArdaRHIBufferDesc VertexDesc;
		VertexDesc.mByteSize = sizeof(Vertices);
		VertexDesc.mUsage = EArdaRHIBufferUsage::Vertex | EArdaRHIBufferUsage::AccelStructBuildInput;
		VertexDesc.mCpuAccess = EArdaRHICpuAccess::Write;
		VertexDesc.mInitialState = EArdaRHIResourceState::AccelStructBuildInput;
		auto Input = mDevice->CreateBuffer(VertexDesc);
		ASSERT_TRUE(Input);
		FArdaRHIRayTracingGeometryDesc Geometry;
		Geometry.mVertexOrAABBBuffer = Input.mValue;
		Geometry.mVertexFormat = EArdaRHIFormat::RGB32Float;
		Geometry.mVertexOrAABBCount = 3;
		Geometry.mStride = sizeof(float) * 3;
		FArdaRHIAccelStructDesc Desc;
		Desc.mBottomLevelGeometries = {Geometry};
		Desc.mBuildFlags =
		    EArdaRHIAccelStructBuildFlags::AllowCompaction | EArdaRHIAccelStructBuildFlags::PreferFastTrace;
		auto AS = mDevice->CreateAccelStruct(Desc);
		ASSERT_TRUE(AS) << AS.mStatus.mMessage.c_str();
		const auto ASAllocation = AS.mValue->GetMemoryAllocationInfo();
		const auto InputAllocation = Input.mValue->GetMemoryAllocationInfo();
		ASSERT_TRUE(ASAllocation.mbKnown);
		ASSERT_TRUE(InputAllocation.mbKnown);
		FArdaInductorMemoryRequest Request;
		Request.mKind = EArdaInductorMemoryKind::AccelerationStructure;
		Request.mExternalAccelerationStructure = AS.mValue;
		eastl::vector<FArdaInductorMemoryRequest> Requests{Request};
		ASSERT_TRUE(DescribeArdaInductorMemory(*mDevice, Requests));
		FArdaInductorMemoryOptions Options;
		Options.mBudgetBytes = ASAllocation.mByteSize + InputAllocation.mByteSize;
		auto Plan = PlanArdaInductorMemory(Requests, Options);
		ASSERT_TRUE(Plan) << Plan.mStatus.mMessage.c_str();
		EXPECT_EQ(Plan.mValue.mExternalBytes, Options.mBudgetBytes);
		--Options.mBudgetBytes;
		EXPECT_FALSE(PlanArdaInductorMemory(Requests, Options));

		// Both lists are recorded while global lifecycle is Unbuilt, just like graph epilogues.
		auto Build = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		auto Epilogue = mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
		ASSERT_TRUE(Build);
		ASSERT_TRUE(Epilogue);
		ASSERT_TRUE(Epilogue.mValue->Open());
		ASSERT_TRUE(Epilogue.mValue->SetAccelStructState(*AS.mValue, EArdaRHIResourceState::AccelStructRead));
		ASSERT_TRUE(Epilogue.mValue->Close());
		ASSERT_TRUE(Build.mValue->Open());
		ASSERT_TRUE(Build.mValue->WriteBuffer(*Input.mValue, Vertices, sizeof(Vertices)));
		ASSERT_TRUE(Build.mValue->BuildBottomLevelAccelStruct(*AS.mValue, {Geometry}, Desc.mBuildFlags));
		ASSERT_TRUE(Build.mValue->Close());
		ASSERT_TRUE(mDevice->ExecuteCommandList(Build.mValue));
		ASSERT_TRUE(mDevice->ExecuteCommandList(Epilogue.mValue));
		ASSERT_TRUE(mDevice->WaitForIdle());
		EXPECT_EQ(AS.mValue->GetBuildState(), EArdaRHIAccelStructBuildState::Built);
		auto Size = mDevice->GetAccelStructCompactedSize(AS.mValue);
		ASSERT_TRUE(Size) << Size.mStatus.mMessage.c_str();
		EXPECT_GT(Size.mValue, 0u);
	}

	INSTANTIATE_TEST_SUITE_P(Native, ArdaInductorMemoryGpu, testing::Values("native-d3d12", "native-vulkan"));
}
