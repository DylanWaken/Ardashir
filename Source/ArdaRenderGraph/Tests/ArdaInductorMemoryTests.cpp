#include "ArdaInductorMemory.h"

#include <gtest/gtest.h>

namespace
{
	using namespace arda;

	FArdaInductorMemoryRequest BufferRequest(uint32_t Identifier, uint32_t First, uint32_t Last, uint64_t Bytes = 65536)
	{
		FArdaInductorMemoryRequest Request;
		Request.mIdentifier = Identifier;
		Request.mBufferDesc.mByteSize = 256;
		Request.mBufferDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
		Request.mFirstUse = First;
		Request.mLastUse = Last;
		Request.mFirstUseNodes = {First};
		Request.mLastUseNodes = {Last};
		Request.mRequirements = {Bytes, 256, 1};
		return Request;
	}

	class FMemoryTestBuffer final : public IArdaRHIBuffer
	{
	public:
		FMemoryTestBuffer(FArdaRHIBufferDesc Desc, const void* Identity)
		    : mDesc(eastl::move(Desc)),
		      mIdentity(Identity)
		{
		}

		void AddRef() noexcept override
		{
			++mReferences;
		}

		void Release() noexcept override
		{
			if (--mReferences == 0)
			{
				delete this;
			}
		}

		EArdaRHIResourceType GetResourceType() const noexcept override
		{
			return EArdaRHIResourceType::Buffer;
		}

		const char* GetDebugName() const noexcept override
		{
			return "memory planner test";
		}

		const FArdaRHIBufferDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		const void* GetPhysicalIdentity() const noexcept override
		{
			return mIdentity;
		}

	private:
		FArdaRHIBufferDesc mDesc;
		const void* mIdentity;
		uint32_t mReferences = 0;
	};

	TEST(ArdaInductorMemory, ReusesCudaCommittedObjectAndPreservesRequiredOrdering)
	{
		auto A = BufferRequest(0, 0, 2);
		auto B = BufferRequest(1, 3, 5);
		A.mBufferDesc.mbCudaInterop = B.mBufferDesc.mbCudaInterop = true;
		A.mBufferDesc.mDebugName = "first";
		B.mBufferDesc.mDebugName = "second";
		B.mBufferDesc.mInitialState = EArdaRHIResourceState::CopyDest;
		B.mBufferDesc.mbKeepInitialState = true;
		const auto Result = PlanArdaInductorMemory({A, B});
		ASSERT_TRUE(Result);
		ASSERT_EQ(Result.mValue.mSlots.size(), 1u);
		EXPECT_EQ(Result.mValue.mOwnedBytes, 65536u);
		EXPECT_EQ(Result.mValue.mResourceSlots[0], Result.mValue.mResourceSlots[1]);
		ASSERT_EQ(Result.mValue.mAliasEdges.size(), 1u);
		EXPECT_EQ(Result.mValue.mAliasEdges[0].mProducer, 2u);
		EXPECT_EQ(Result.mValue.mAliasEdges[0].mConsumer, 3u);
		EXPECT_TRUE(Result.mValue.mRequests[0].mBufferDesc.mbCudaInterop);
	}

	TEST(ArdaInductorMemory, InclusiveOverlapAndPersistentResourcesPreventReuse)
	{
		auto A = BufferRequest(0, 0, 2);
		auto B = BufferRequest(1, 2, 3);
		auto C = BufferRequest(2, 4, 5);
		C.mbPersistent = true;
		const auto Result = PlanArdaInductorMemory({A, B, C});
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mSlots.size(), 3u);
		EXPECT_TRUE(Result.mValue.mAliasEdges.empty());
	}

	TEST(ArdaInductorMemory, NativeDescriptorCompatibilityIncludesCudaUsageAndStride)
	{
		auto A = BufferRequest(0, 0, 0);
		auto B = BufferRequest(1, 1, 1);
		auto C = BufferRequest(2, 2, 2);
		auto D = BufferRequest(3, 3, 3);
		B.mBufferDesc.mbCudaInterop = true;
		C.mBufferDesc.mStructureStride = 16;
		D.mBufferDesc.mUsage = EArdaRHIBufferUsage::ShaderResource;
		const auto Result = PlanArdaInductorMemory({A, B, C, D});
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mSlots.size(), 4u);
	}

	TEST(ArdaInductorMemory, EveryQueueFrontierIsOrderedBeforeReuse)
	{
		auto A = BufferRequest(0, 0, 2);
		auto B = BufferRequest(1, 3, 4);
		A.mLastUseNodes = {11, 12, 12};
		B.mFirstUseNodes = {21, 22};
		const auto Result = PlanArdaInductorMemory({A, B});
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mSlots.size(), 1u);
		ASSERT_EQ(Result.mValue.mAliasEdges.size(), 4u);
		for (const auto& Edge : Result.mValue.mAliasEdges)
		{
			EXPECT_TRUE(Edge.mProducer == 11 || Edge.mProducer == 12);
			EXPECT_TRUE(Edge.mConsumer == 21 || Edge.mConsumer == 22);
		}
	}

	TEST(ArdaInductorMemory, SerializationCanBeForbiddenAndMissingFrontiersAreConservative)
	{
		auto A = BufferRequest(0, 0, 0);
		auto B = BufferRequest(1, 1, 1);
		FArdaInductorMemoryOptions Options;
		Options.mbAllowSerialization = false;
		auto Result = PlanArdaInductorMemory({A, B}, Options);
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mSlots.size(), 2u);
		Options.mHappensBefore = [](void*, uint32_t Producer, uint32_t Consumer)
		{
			return Producer < Consumer;
		};
		Result = PlanArdaInductorMemory({A, B}, Options);
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mSlots.size(), 1u);
		B.mFirstUseNodes.clear();
		Result = PlanArdaInductorMemory({A, B});
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mSlots.size(), 2u);
	}

	TEST(ArdaInductorMemory, BudgetCountsNativeSlotCapacityAndWorkspace)
	{
		auto A = BufferRequest(0, 0, 0);
		auto B = BufferRequest(1, 1, 1);
		FArdaInductorMemoryOptions Options;
		Options.mWorkspaceBytes = 4096;
		Options.mBudgetBytes = 65536 + 4096;
		auto Result = PlanArdaInductorMemory({A, B}, Options);
		ASSERT_TRUE(Result);
		EXPECT_TRUE(Result.mValue.mbBudgetAccepted);
		EXPECT_EQ(Result.mValue.mTotalBytes, Options.mBudgetBytes);
		--Options.mBudgetBytes;
		Result = PlanArdaInductorMemory({A, B}, Options);
		ASSERT_FALSE(Result);
		EXPECT_FALSE(Result.mValue.mbBudgetAccepted);
		EXPECT_EQ(Result.mValue.mTotalBytes, 65536u + 4096u);
		EXPECT_NE(Result.mStatus.mMessage.find("another schedule"), eastl::string::npos);
	}

	TEST(ArdaInductorMemory, ExternalPhysicalIdentityIsCountedOnceAcrossLogicalWrappers)
	{
		auto A = BufferRequest(0, 0, 5);
		auto B = BufferRequest(1, 0, 5);
		auto C = BufferRequest(2, 6, 7);
		int Identity = 0;
		A.mExternalBuffer = FArdaRHIBufferRef(new FMemoryTestBuffer(A.mBufferDesc, &Identity));
		B.mExternalBuffer = FArdaRHIBufferRef(new FMemoryTestBuffer(B.mBufferDesc, &Identity));
		const auto Result = PlanArdaInductorMemory({A, B, C});
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mExternalBytes, 65536u);
		EXPECT_EQ(Result.mValue.mOwnedBytes, 65536u);
		EXPECT_EQ(Result.mValue.mTotalBytes, 131072u);
		EXPECT_EQ(Result.mValue.mSlots.size(), 2u);
		EXPECT_TRUE(Result.mValue.mAliasEdges.empty());
	}

	TEST(ArdaInductorMemory, RejectsInconsistentExternalPhysicalDescriptions)
	{
		auto A = BufferRequest(0, 0, 1);
		auto B = BufferRequest(1, 0, 1, 131072);
		int Identity = 0;
		A.mExternalBuffer = FArdaRHIBufferRef(new FMemoryTestBuffer(A.mBufferDesc, &Identity));
		B.mExternalBuffer = FArdaRHIBufferRef(new FMemoryTestBuffer(B.mBufferDesc, &Identity));
		EXPECT_FALSE(PlanArdaInductorMemory({A, B}));
	}

	TEST(ArdaInductorMemory, ProducesStableIdentifierIndexedResults)
	{
		auto A = BufferRequest(0, 3, 3);
		auto B = BufferRequest(1, 0, 0);
		const auto Forward = PlanArdaInductorMemory({A, B});
		const auto Reverse = PlanArdaInductorMemory({B, A});
		ASSERT_TRUE(Forward);
		ASSERT_TRUE(Reverse);
		EXPECT_EQ(Forward.mValue.mResourceSlots, Reverse.mValue.mResourceSlots);
		EXPECT_EQ(Forward.mValue.mSlots[0].mResources, Reverse.mValue.mSlots[0].mResources);
		EXPECT_EQ(Reverse.mValue.mRequests[0].mIdentifier, 0u);
	}

	TEST(ArdaInductorMemory, RejectsInvalidRequestsAndAccountingOverflow)
	{
		auto A = BufferRequest(0, 0, 0);
		EXPECT_FALSE(PlanArdaInductorMemory({A, A}));
		A.mRequirements.mAlignment = 3;
		EXPECT_FALSE(PlanArdaInductorMemory({A}));
		A = BufferRequest(0, 0, 0, UINT64_MAX);
		FArdaInductorMemoryOptions Options;
		Options.mWorkspaceBytes = 1;
		EXPECT_FALSE(PlanArdaInductorMemory({A}, Options));
		A = BufferRequest(0, 2, 1);
		EXPECT_FALSE(PlanArdaInductorMemory({A}));
	}

	TEST(ArdaInductorMemory, EmptyPlanCanSatisfyZeroBudget)
	{
		FArdaInductorMemoryOptions Options;
		Options.mBudgetBytes = 0;
		const auto Result = PlanArdaInductorMemory({}, Options);
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mTotalBytes, 0u);
		EXPECT_TRUE(Result.mValue.mbBudgetAccepted);
	}

	TEST(ArdaInductorMemory, CulledResourcesRetainIdentifiersWithoutAllocations)
	{
		auto A = BufferRequest(0, 0, 0);
		FArdaInductorMemoryRequest Unused;
		Unused.mIdentifier = 1;
		Unused.mbUsed = false;
		auto B = BufferRequest(2, 1, 1);
		const auto Result = PlanArdaInductorMemory({Unused, A, B});
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mResourceSlots.size(), 3u);
		EXPECT_EQ(Result.mValue.mResourceSlots[1], UINT32_MAX);
		EXPECT_EQ(Result.mValue.mResourceBytes[1], 0u);
		EXPECT_EQ(Result.mValue.mOwnedBytes, 65536u);
	}

	FArdaInductorMemoryRequest PlacedBuffer(uint32_t Id, uint32_t First, uint32_t Last, uint64_t Bytes = 65536)
	{
		auto Request = BufferRequest(Id, First, Last, Bytes);
		Request.mBufferDesc.mByteSize = Bytes;
		Request.mbUseHeapPlacement = true;
		Request.mRequirements.mAlignment = 65536;
		return Request;
	}

	TEST(ArdaInductorMemory, NativeHeapAliasesDifferentBufferSizesAndUsages)
	{
		auto A = PlacedBuffer(0, 0, 1);
		auto B = PlacedBuffer(1, 2, 3, 131072);
		B.mBufferDesc.mUsage = EArdaRHIBufferUsage::ShaderResource;
		const auto Result = PlanArdaInductorMemory({A, B});
		ASSERT_TRUE(Result);
		const auto& Plan = Result.mValue;
		ASSERT_EQ(Plan.mHeaps.size(), 1u);
		EXPECT_EQ(Plan.mHeaps[0].mDesc.mCapacity, 131072u);
		EXPECT_EQ(Plan.mOwnedBytes, 131072u);
		EXPECT_EQ(Plan.mResourceOffsets[0], 0u);
		EXPECT_EQ(Plan.mResourceOffsets[1], 0u);
		EXPECT_NE(Plan.mResourceSlots[0], Plan.mResourceSlots[1]);
		ASSERT_EQ(Plan.mAliasEdges.size(), 1u);
		EXPECT_EQ(Plan.mAliasEdges[0].mProducer, 1u);
		EXPECT_EQ(Plan.mAliasEdges[0].mConsumer, 2u);
		ASSERT_EQ(Plan.mAliasActivations.size(), 2u);
		EXPECT_TRUE(Plan.mAliasActivations[0].mPreviousResources.empty());
		EXPECT_EQ(Plan.mAliasActivations[1].mPreviousResources, eastl::vector<uint32_t>({0}));
	}

	TEST(ArdaInductorMemory, NativePlacementPreservesEveryOverlappingPredecessor)
	{
		auto A = PlacedBuffer(0, 0, 4);
		auto B = PlacedBuffer(1, 1, 2);
		auto Scratch = PlacedBuffer(2, 5, 6, 131072);
		A.mLastUseNodes = {40, 41};
		B.mLastUseNodes = {20, 21};
		Scratch.mFirstUseNodes = {50, 51};
		const auto Result = PlanArdaInductorMemory({Scratch, B, A});
		ASSERT_TRUE(Result);
		const auto& Plan = Result.mValue;
		EXPECT_EQ(Plan.mResourceOffsets[0], 0u);
		EXPECT_EQ(Plan.mResourceOffsets[1], 65536u);
		EXPECT_EQ(Plan.mResourceOffsets[2], 0u);
		EXPECT_EQ(Plan.mOwnedBytes, 131072u);
		EXPECT_EQ(Plan.mAliasEdges.size(), 8u);
		ASSERT_EQ(Plan.mAliasActivations.size(), 4u);
		EXPECT_EQ(Plan.mAliasActivations[2].mPreviousResources, eastl::vector<uint32_t>({0, 1}));
		EXPECT_EQ(Plan.mAliasActivations[3].mPreviousResources, eastl::vector<uint32_t>({0, 1}));
	}

	TEST(ArdaInductorMemory, HeapAlignmentAndMemoryTypeIntersectionAreNativeConstraints)
	{
		auto A = PlacedBuffer(0, 0, 4);
		auto B = PlacedBuffer(1, 1, 3);
		auto C = PlacedBuffer(2, 2, 2);
		A.mRequirements.mMemoryTypeBits = 3;
		B.mRequirements.mMemoryTypeBits = 6;
		B.mRequirements.mAlignment = 131072;
		C.mRequirements.mMemoryTypeBits = 1;
		const auto Result = PlanArdaInductorMemory({A, B, C});
		ASSERT_TRUE(Result);
		const auto& Plan = Result.mValue;
		ASSERT_EQ(Plan.mHeaps.size(), 2u);
		EXPECT_EQ(Plan.mHeaps[0].mDesc.mMemoryTypeBits, 2u);
		EXPECT_EQ(Plan.mResourceOffsets[1], 131072u);
		EXPECT_EQ(Plan.mHeaps[0].mDesc.mCapacity, 262144u);
		EXPECT_NE(Plan.mResourceHeapIndices[1], Plan.mResourceHeapIndices[2]);
		EXPECT_EQ(Plan.mOwnedBytes, 327680u);
	}

	TEST(ArdaInductorMemory, HeapReuseHonorsExistingHappensBeforeAndReusePolicy)
	{
		auto A = PlacedBuffer(0, 0, 0);
		auto B = PlacedBuffer(1, 1, 1);
		FArdaInductorMemoryOptions Options;
		Options.mbAllowSerialization = false;
		auto Result = PlanArdaInductorMemory({A, B}, Options);
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mOwnedBytes, 131072u);
		EXPECT_TRUE(Result.mValue.mAliasEdges.empty());
		Options.mHappensBefore = [](void*, uint32_t Producer, uint32_t Consumer)
		{
			return Producer < Consumer;
		};
		Result = PlanArdaInductorMemory({A, B}, Options);
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mOwnedBytes, 65536u);
		Options.mbAllowReuse = false;
		Result = PlanArdaInductorMemory({A, B}, Options);
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mOwnedBytes, 131072u);
	}

	TEST(ArdaInductorMemory, DifferentTextureFormatsSuballocateDisjointRangesAndSeparateBufferHeaps)
	{
		auto A = PlacedBuffer(0, 0, 0);
		auto B = PlacedBuffer(1, 1, 1);
		for (auto* Request : {&A, &B})
		{
			Request->mKind = EArdaInductorMemoryKind::Texture;
			Request->mTextureDesc.mWidth = Request->mTextureDesc.mHeight = 32;
		}
		A.mTextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		B.mTextureDesc.mFormat = EArdaRHIFormat::R32Float;
		FArdaInductorMemoryOptions Options;
		Options.mbAllowTextureAliasing = false;
		const auto Result = PlanArdaInductorMemory({A, B, PlacedBuffer(2, 2, 2)}, Options);
		ASSERT_TRUE(Result);
		const auto& Plan = Result.mValue;
		EXPECT_EQ(Plan.mHeaps.size(), 2u);
		EXPECT_EQ(Plan.mResourceHeapIndices[0], Plan.mResourceHeapIndices[1]);
		EXPECT_NE(Plan.mResourceOffsets[0], Plan.mResourceOffsets[1]);
		EXPECT_NE(Plan.mResourceHeapIndices[1], Plan.mResourceHeapIndices[2]);
		EXPECT_TRUE(Plan.mAliasEdges.empty());
		ASSERT_EQ(Plan.mAliasActivations.size(), 1u);
		EXPECT_EQ(Plan.mAliasActivations[0].mResource, 2u);
	}

	TEST(ArdaInductorMemory, FrameBudgetMultipliesTransientPoolsAndCountsRetainedStorageOnce)
	{
		auto A = PlacedBuffer(0, 0, 0);
		auto Scratch = PlacedBuffer(1, 1, 1);
		auto Persistent = BufferRequest(2, 0, 1);
		Persistent.mbPersistent = true;
		auto External = BufferRequest(3, 0, 1);
		int Identity = 0;
		External.mExternalBuffer = FArdaRHIBufferRef(new FMemoryTestBuffer(External.mBufferDesc, &Identity));
		External.mExternalAllocationInfo = {&Identity, 65536, true};
		FArdaInductorMemoryOptions Options;
		Options.mFrameCount = 3;
		Options.mWorkspaceBytes = 4096;
		Options.mBudgetBytes = 5 * 65536 + 4096;
		auto Result = PlanArdaInductorMemory({A, Scratch, Persistent, External}, Options);
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mTransientBytes, 65536u);
		EXPECT_EQ(Result.mValue.mPersistentBytes, 65536u);
		EXPECT_EQ(Result.mValue.mOwnedBytes, 4u * 65536u);
		EXPECT_EQ(Result.mValue.mExternalBytes, 65536u);
		EXPECT_EQ(Result.mValue.mTotalBytes, Options.mBudgetBytes);
		--Options.mBudgetBytes;
		Result = PlanArdaInductorMemory({A, Scratch, Persistent, External}, Options);
		EXPECT_FALSE(Result);
		EXPECT_FALSE(Result.mValue.mbBudgetAccepted);
	}

	TEST(ArdaInductorMemory, NativePlacementRejectsUnsupportedDescriptorsAndOverflow)
	{
		auto A = PlacedBuffer(0, 0, 0);
		A.mBufferDesc.mbCudaInterop = true;
		EXPECT_FALSE(PlanArdaInductorMemory({A}));
		A = PlacedBuffer(0, 0, 0);
		A.mbPersistent = true;
		EXPECT_FALSE(PlanArdaInductorMemory({A}));
		A = PlacedBuffer(0, 0, 0, UINT64_MAX);
		EXPECT_FALSE(PlanArdaInductorMemory({A}));
		A = BufferRequest(0, 0, 0, UINT64_MAX / 2 + 1);
		FArdaInductorMemoryOptions Options;
		Options.mFrameCount = 2;
		EXPECT_FALSE(PlanArdaInductorMemory({A}, Options));
		Options.mFrameCount = 0;
		EXPECT_FALSE(PlanArdaInductorMemory({}, Options));
	}

	TEST(ArdaInductorMemory, ImportedPlacedObjectsCountTheCompleteSharedHeapOnce)
	{
		auto A = BufferRequest(0, 0, 0);
		auto B = BufferRequest(1, 1, 1, 131072);
		int FirstIdentity = 0, SecondIdentity = 0, HeapIdentity = 0;
		A.mExternalBuffer = FArdaRHIBufferRef(new FMemoryTestBuffer(A.mBufferDesc, &FirstIdentity));
		B.mExternalBuffer = FArdaRHIBufferRef(new FMemoryTestBuffer(B.mBufferDesc, &SecondIdentity));
		A.mExternalAllocationInfo = B.mExternalAllocationInfo = {&HeapIdentity, 1048576, true};
		FArdaInductorMemoryOptions Options;
		Options.mFrameCount = 3;
		Options.mBudgetBytes = 1048576;
		auto Result = PlanArdaInductorMemory({A, B}, Options);
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mExternalBytes, 1048576u);
		EXPECT_EQ(Result.mValue.mTotalBytes, 1048576u);
		EXPECT_EQ(Result.mValue.mSlots.size(), 2u);
		--Options.mBudgetBytes;
		EXPECT_FALSE(PlanArdaInductorMemory({A, B}, Options));
		B.mExternalAllocationInfo.mByteSize = 2097152;
		EXPECT_FALSE(PlanArdaInductorMemory({A, B}));
	}

	TEST(ArdaInductorMemory, UnknownImportedAllocationCannotSatisfyAHardBudget)
	{
		auto A = BufferRequest(0, 0, 0);
		int Identity = 0;
		A.mExternalBuffer = FArdaRHIBufferRef(new FMemoryTestBuffer(A.mBufferDesc, &Identity));
		EXPECT_TRUE(PlanArdaInductorMemory({A}));
		FArdaInductorMemoryOptions Options;
		Options.mBudgetBytes = 65536;
		const auto Result = PlanArdaInductorMemory({A}, Options);
		EXPECT_FALSE(Result);
		EXPECT_EQ(Result.mStatus.mCode, EArdaRHIResult::Unsupported);
		A.mExternalAllocationInfo = {&Identity, 65536, true};
		EXPECT_TRUE(PlanArdaInductorMemory({A}, Options));
	}

	TEST(ArdaInductorMemory, DifferentTextureFormatsAliasWithFirstOccupantActivation)
	{
		auto A = PlacedBuffer(0, 0, 0);
		auto B = PlacedBuffer(1, 1, 1);
		A.mKind = B.mKind = EArdaInductorMemoryKind::Texture;
		A.mTextureDesc.mFormat = EArdaRHIFormat::RGBA8UNorm;
		B.mTextureDesc.mFormat = EArdaRHIFormat::R32Float;
		auto Result = PlanArdaInductorMemory({A, B});
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mOwnedBytes, 65536u);
		EXPECT_EQ(Result.mValue.mResourceOffsets[0], Result.mValue.mResourceOffsets[1]);
		ASSERT_EQ(Result.mValue.mAliasActivations.size(), 2u);
		EXPECT_TRUE(Result.mValue.mAliasActivations[0].mPreviousResources.empty());
		EXPECT_EQ(Result.mValue.mAliasActivations[1].mPreviousResources, eastl::vector<uint32_t>({0}));
		B.mFirstUseNodes = {1, 2};
		Result = PlanArdaInductorMemory({A, B});
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mOwnedBytes, 131072u);
		EXPECT_TRUE(Result.mValue.mAliasActivations.empty());
	}

	TEST(ArdaInductorMemory, NativeHeapCapacityAlignmentAvoidsUnnecessaryVulkanPadding)
	{
		auto A = PlacedBuffer(0, 0, 0, 256);
		A.mRequirements.mAlignment = 256;
		A.mHeapAllocationAlignment = 1;
		const auto Result = PlanArdaInductorMemory({A});
		ASSERT_TRUE(Result);
		EXPECT_EQ(Result.mValue.mOwnedBytes, 256u);
	}

}
