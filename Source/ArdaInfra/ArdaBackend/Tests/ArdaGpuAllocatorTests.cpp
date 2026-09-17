#include "Allocator/ArdaGpuAllocator.h"
#include "RHI/ArdaRHIDevicePrivate.h"

#include <gtest/gtest.h>

#include <atomic>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace
{
	using namespace arda;

	struct FArdaAllocatorObjectCounts
	{
		std::atomic<uint64_t> mDestroyedBuffers{0};
		std::atomic<uint64_t> mDestroyedTextures{0};
		std::atomic<uint64_t> mDestroyedHeaps{0};
	};

	/** Native stand-in with allocation identity and destruction observable independently of the cache. */
	class FArdaAllocatorObject final : public IArdaProviderObject
	{
	public:
		FArdaAllocatorObject(EArdaRHIResourceType Type,
		    uint64_t ByteSize,
		    eastl::shared_ptr<FArdaAllocatorObjectCounts> Counts)
		    : mType(Type),
		      mByteSize(ByteSize),
		      mCounts(eastl::move(Counts))
		{
		}

		~FArdaAllocatorObject() override
		{
			if (mType == EArdaRHIResourceType::Buffer)
			{
				++mCounts->mDestroyedBuffers;
			}
			else if (mType == EArdaRHIResourceType::Texture)
			{
				++mCounts->mDestroyedTextures;
			}
			else
			{
				++mCounts->mDestroyedHeaps;
			}
		}

		const void* GetIdentity() const noexcept override
		{
			return this;
		}

		FArdaRHIMemoryAllocationInfo GetMemoryAllocationInfo() const noexcept override
		{
			return mHeap ? mHeap->GetMemoryAllocationInfo()
			             : FArdaRHIMemoryAllocationInfo{this, mByteSize, mByteSize != 0};
		}

		FArdaProviderObjectRef mHeap;

	private:
		EArdaRHIResourceType mType;
		uint64_t mByteSize;
		eastl::shared_ptr<FArdaAllocatorObjectCounts> mCounts;
	};

	/** Native metadata stand-in used to exercise facade ownership without a graphics driver. */
	class FArdaAllocatorMetadataObject final : public IArdaProviderObject
	{
	public:
		const void* GetIdentity() const noexcept override
		{
			return this;
		}
	};

	/** Uses the real device facade and allocator, replacing only native GPU allocation. */
	class FArdaAllocatorProvider final : public IArdaRHIProviderDevice
	{
	public:
		FArdaAllocatorProvider()
		{
			mCapabilities.mbHeaps = true;
			mCapabilities.mbVirtualResources = true;
		}

		const FArdaRHICapabilities& GetCapabilities() const noexcept override
		{
			return mCapabilities;
		}

		EArdaRHINativeResourceType GetTextureImportType() const noexcept override
		{
			return EArdaRHINativeResourceType::BackendDefined;
		}

		EArdaRHINativeResourceType GetBufferImportType() const noexcept override
		{
			return EArdaRHINativeResourceType::BackendDefined;
		}

		FArdaProviderObjectResult CreateBuffer(const FArdaRHIBufferDesc& Desc) override
		{
			++mBufferCreationAttempts;
			if (mbFailBufferCreation)
			{
				return {{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Injected allocation failure.")};
			}
			return {eastl::make_shared<FArdaAllocatorObject>(EArdaRHIResourceType::Buffer,
			            Desc.mbVirtual ? 0 : Desc.mByteSize,
			            mCounts),
			    {}};
		}

		FArdaProviderObjectResult CreateTexture(const FArdaRHITextureDesc& Desc) override
		{
			++mTextureCreationAttempts;
			return {eastl::make_shared<FArdaAllocatorObject>(EArdaRHIResourceType::Texture,
			            Desc.mbVirtual ? 0 : TextureSize(Desc),
			            mCounts),
			    {}};
		}

		FArdaProviderObjectResult CreateHeap(const FArdaRHIHeapDesc& Desc) override
		{
			++mHeapCreationAttempts;
			return {eastl::make_shared<FArdaAllocatorObject>(EArdaRHIResourceType::Heap, Desc.mCapacity, mCounts), {}};
		}

		bool CanReuseBuffer(const FArdaProviderObjectRef&, const FArdaRHIBufferDesc&) const noexcept override
		{
			return mbAllowResourceReuse;
		}

		bool CanReuseBufferForQueue(const FArdaProviderObjectRef&,
		    const FArdaRHIBufferDesc&,
		    EArdaRHIQueueType) const noexcept override
		{
			return mbAllowResourceReuse;
		}

		bool CanReuseTexture(const FArdaProviderObjectRef&, const FArdaRHITextureDesc&) const noexcept override
		{
			return mbAllowResourceReuse;
		}

		TArdaRHIResult<FArdaRHIMemoryRequirements> QueryBufferMemoryRequirements(
		    const FArdaRHIBufferDesc& Desc) override
		{
			return {{Desc.mByteSize, 256, 1}, {}};
		}

		TArdaRHIResult<FArdaRHIMemoryRequirements> QueryTextureMemoryRequirements(
		    const FArdaRHITextureDesc& Desc) override
		{
			return {{TextureSize(Desc), 256, 1}, {}};
		}

		TArdaRHIResult<FArdaRHIMemoryRequirements> GetBufferMemoryRequirements(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc& Desc) override
		{
			if (mbRetainQueriedBuffer)
			{
				// Captures the facade's lease, exactly as native submitted work retains its dependencies.
				mPendingBuffer = Buffer;
			}
			return QueryBufferMemoryRequirements(Desc);
		}

		TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(const FArdaProviderObjectRef&,
		    const FArdaRHITextureDesc& Desc) override
		{
			return QueryTextureMemoryRequirements(Desc);
		}

		FArdaRHIStatus BindBufferMemory(const FArdaProviderObjectRef& Buffer,
		    const FArdaRHIBufferDesc&,
		    const FArdaProviderObjectRef& Heap,
		    uint64_t) override
		{
			++mBufferBindingAttempts;
			if (mbFailMemoryBinding)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Injected heap binding failure.");
			}
			static_cast<FArdaAllocatorObject*>(Buffer.get())->mHeap = Heap;
			return {};
		}

		FArdaRHIStatus BindTextureMemory(const FArdaProviderObjectRef& Texture,
		    const FArdaRHITextureDesc&,
		    const FArdaProviderObjectRef& Heap,
		    uint64_t) override
		{
			++mTextureBindingAttempts;
			if (mbFailMemoryBinding)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Injected heap binding failure.");
			}
			static_cast<FArdaAllocatorObject*>(Texture.get())->mHeap = Heap;
			return {};
		}

#define ARDA_ALLOCATOR_UNSUPPORTED_OBJECT(Method, Parameter)                                                           \
	FArdaProviderObjectResult Method(const Parameter&) override                                                        \
	{                                                                                                                  \
		return {{}, Unsupported()};                                                                                    \
	}
		ARDA_ALLOCATOR_UNSUPPORTED_OBJECT(CreateStagingTexture, FArdaRHIStagingTextureDesc)
		ARDA_ALLOCATOR_UNSUPPORTED_OBJECT(ImportTexture, FArdaRHINativeTextureImportDesc)
		ARDA_ALLOCATOR_UNSUPPORTED_OBJECT(ImportBuffer, FArdaRHINativeBufferImportDesc)
		ARDA_ALLOCATOR_UNSUPPORTED_OBJECT(CreateSampler, FArdaRHISamplerDesc)
		ARDA_ALLOCATOR_UNSUPPORTED_OBJECT(CreateFramebuffer, FArdaProviderFramebufferCreateInfo)
		ARDA_ALLOCATOR_UNSUPPORTED_OBJECT(CreateComputePipeline, FArdaProviderComputePipelineCreateInfo)
#undef ARDA_ALLOCATOR_UNSUPPORTED_OBJECT

		FArdaProviderObjectResult CreateShader(const FArdaRHIShaderDesc&) override
		{
			return {eastl::make_shared<FArdaAllocatorMetadataObject>(), {}};
		}

		FArdaProviderObjectResult CreateBindingLayout(const FArdaRHIBindingLayoutDesc&) override
		{
			return {eastl::make_shared<FArdaAllocatorMetadataObject>(), {}};
		}

		FArdaProviderObjectResult CreateGraphicsPipeline(const FArdaProviderGraphicsPipelineCreateInfo&) override
		{
			return {eastl::make_shared<FArdaAllocatorMetadataObject>(), {}};
		}

		FArdaProviderObjectResult CreateBindingSet(const FArdaRHIBindingSetDesc&,
		    const FArdaProviderObjectRef&,
		    const eastl::vector<FArdaProviderBinding>&) override
		{
			return {eastl::make_shared<FArdaAllocatorMetadataObject>(), {}};
		}

		TArdaRHIResult<FArdaRHIStagingTextureMapping> MapStagingTexture(const FArdaProviderObjectRef&,
		    const FArdaRHITextureSlice&,
		    EArdaRHICpuAccess) override
		{
			return {{}, Unsupported()};
		}

		FArdaRHIStatus UnmapStagingTexture(const FArdaProviderObjectRef&) override
		{
			return Unsupported();
		}

		TArdaRHIResult<void*> MapBuffer(const FArdaProviderObjectRef&, uint64_t, size_t) override
		{
			return {{}, Unsupported()};
		}

		void UnmapBuffer(const FArdaProviderObjectRef&) noexcept override
		{
		}

		TArdaRHIResult<eastl::unique_ptr<IArdaProviderCommandList>> CreateCommandList(EArdaRHIQueueType, bool) override
		{
			return {{}, Unsupported()};
		}

		TArdaRHIResult<uint64_t> ExecuteCommandList(IArdaProviderCommandList&, EArdaRHIQueueType) override
		{
			return {{}, Unsupported()};
		}

		FArdaRHIStatus WaitForIdle() override
		{
			mPendingBuffer.reset();
			return {};
		}

		void RunGarbageCollection() override
		{
		}

		void FlushPipelineCache() noexcept override
		{
		}

		eastl::shared_ptr<FArdaAllocatorObjectCounts> mCounts = eastl::make_shared<FArdaAllocatorObjectCounts>();
		FArdaProviderObjectRef mPendingBuffer;
		std::atomic<uint64_t> mBufferCreationAttempts{0};
		std::atomic<uint64_t> mTextureCreationAttempts{0};
		std::atomic<uint64_t> mHeapCreationAttempts{0};
		std::atomic<uint64_t> mBufferBindingAttempts{0};
		std::atomic<uint64_t> mTextureBindingAttempts{0};
		bool mbFailBufferCreation = false;
		bool mbFailMemoryBinding = false;
		bool mbAllowResourceReuse = true;
		bool mbRetainQueriedBuffer = false;

	private:
		static uint64_t TextureSize(const FArdaRHITextureDesc& Desc)
		{
			return uint64_t(Desc.mWidth) * Desc.mHeight * Desc.mDepth * Desc.mArraySize * 4;
		}

		static FArdaRHIStatus Unsupported()
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Not used by the CPU allocator fixture.");
		}

		FArdaRHICapabilities mCapabilities;
	};

	class FArdaGpuAllocatorTest : public testing::Test
	{
	protected:
		static FArdaRHIBufferDesc BufferDesc()
		{
			FArdaRHIBufferDesc Desc;
			Desc.mByteSize = 1024;
			Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
			return Desc;
		}

		static FArdaRHITextureDesc TextureDesc()
		{
			FArdaRHITextureDesc Desc;
			Desc.mWidth = Desc.mHeight = 16;
			Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
			return Desc;
		}

		static FArdaRHIHeapDesc HeapDesc()
		{
			FArdaRHIHeapDesc Desc;
			Desc.mCapacity = 65536;
			Desc.mMemoryTypeBits = 1;
			return Desc;
		}

		eastl::shared_ptr<FArdaAllocatorProvider> mProvider = eastl::make_shared<FArdaAllocatorProvider>();
		FArdaRHIDeviceRef mDevice = CreateArdaRHIDevice(mProvider);
	};

	TEST_F(FArdaGpuAllocatorTest, ReleasedCommittedResourcesReuseNativeObjectsAcrossDebugNames)
	{
		auto BufferDescriptor = BufferDesc();
		BufferDescriptor.mDebugName = "First graph buffer";
		auto Buffer = mDevice->CreateBuffer(BufferDescriptor);
		ASSERT_TRUE(Buffer);
		const void* BufferIdentity = Buffer.mValue->GetPhysicalIdentity();
		Buffer.mValue.Reset();

		BufferDescriptor.mDebugName = "Second graph buffer";
		auto ReusedBuffer = mDevice->CreateBuffer(BufferDescriptor);
		ASSERT_TRUE(ReusedBuffer);
		EXPECT_EQ(ReusedBuffer.mValue->GetPhysicalIdentity(), BufferIdentity);
		EXPECT_STREQ(ReusedBuffer.mValue->GetDebugName(), "Second graph buffer");
		EXPECT_EQ(mProvider->mBufferCreationAttempts, 1u);

		auto TextureDescriptor = TextureDesc();
		TextureDescriptor.mDebugName = "First graph texture";
		auto Texture = mDevice->CreateTexture(TextureDescriptor);
		ASSERT_TRUE(Texture);
		const void* TextureIdentity = Texture.mValue->GetPhysicalIdentity();
		Texture.mValue.Reset();

		TextureDescriptor.mDebugName = "Second graph texture";
		auto ReusedTexture = mDevice->CreateTexture(TextureDescriptor);
		ASSERT_TRUE(ReusedTexture);
		EXPECT_EQ(ReusedTexture.mValue->GetPhysicalIdentity(), TextureIdentity);
		EXPECT_STREQ(ReusedTexture.mValue->GetDebugName(), "Second graph texture");
		EXPECT_EQ(mProvider->mTextureCreationAttempts, 1u);
		const auto Stats = mDevice->GetGpuAllocatorStats();
		EXPECT_EQ(Stats.mBufferCacheHits, 1u);
		EXPECT_EQ(Stats.mTextureCacheHits, 1u);
	}

	TEST_F(FArdaGpuAllocatorTest, ConcurrentResourceAndHeapReferencesNeverReuseOneAllocation)
	{
		const auto FirstBuffer = mDevice->CreateBuffer(BufferDesc());
		const auto SecondBuffer = mDevice->CreateBuffer(BufferDesc());
		const auto FirstTexture = mDevice->CreateTexture(TextureDesc());
		const auto SecondTexture = mDevice->CreateTexture(TextureDesc());
		const auto FirstHeap = mDevice->CreateHeap(HeapDesc());
		const auto SecondHeap = mDevice->CreateHeap(HeapDesc());
		ASSERT_TRUE(FirstBuffer && SecondBuffer && FirstTexture && SecondTexture && FirstHeap && SecondHeap);

		EXPECT_NE(FirstBuffer.mValue->GetPhysicalIdentity(), SecondBuffer.mValue->GetPhysicalIdentity());
		EXPECT_NE(FirstTexture.mValue->GetPhysicalIdentity(), SecondTexture.mValue->GetPhysicalIdentity());
		EXPECT_NE(FirstHeap.mValue->GetMemoryAllocationInfo().mIdentity,
		    SecondHeap.mValue->GetMemoryAllocationInfo().mIdentity);
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mCachedResources, 0u);
	}

	TEST_F(FArdaGpuAllocatorTest, NativeSubmissionLeasePreventsReuseAndTrimUntilCompletion)
	{
		auto Buffer = mDevice->CreateBuffer(BufferDesc());
		ASSERT_TRUE(Buffer);
		const void* PendingIdentity = Buffer.mValue->GetPhysicalIdentity();
		mProvider->mbRetainQueriedBuffer = true;
		ASSERT_TRUE(mDevice->GetBufferMemoryRequirements(Buffer.mValue));
		mProvider->mbRetainQueriedBuffer = false;
		Buffer.mValue.Reset();

		mDevice->RunGarbageCollection();
		mDevice->TrimGpuAllocator();
		EXPECT_EQ(mProvider->mCounts->mDestroyedBuffers, 0u);
		const auto IndependentBuffer = mDevice->CreateBuffer(BufferDesc());
		ASSERT_TRUE(IndependentBuffer);
		EXPECT_NE(IndependentBuffer.mValue->GetPhysicalIdentity(), PendingIdentity);

		mProvider->mPendingBuffer.reset();
		const auto ReusedBuffer = mDevice->CreateBuffer(BufferDesc());
		ASSERT_TRUE(ReusedBuffer);
		EXPECT_EQ(ReusedBuffer.mValue->GetPhysicalIdentity(), PendingIdentity);
		EXPECT_EQ(mProvider->mBufferCreationAttempts, 2u);
	}

	TEST_F(FArdaGpuAllocatorTest, CompleteHeapAndPlacedObjectSetSurvivesGraphLifetimeChanges)
	{
		auto Heap = mDevice->CreateHeap(HeapDesc());
		ASSERT_TRUE(Heap);
		auto BufferDescriptor = BufferDesc();
		BufferDescriptor.mbVirtual = true;
		auto TextureDescriptor = TextureDesc();
		TextureDescriptor.mbVirtual = true;
		auto Buffer = mDevice->CreatePlacedBuffer(BufferDescriptor, Heap.mValue, 0);
		auto Texture = mDevice->CreatePlacedTexture(TextureDescriptor, Heap.mValue, 4096);
		ASSERT_TRUE(Buffer && Texture);
		const void* HeapIdentity = Heap.mValue->GetMemoryAllocationInfo().mIdentity;
		const void* BufferIdentity = Buffer.mValue->GetPhysicalIdentity();
		const void* TextureIdentity = Texture.mValue->GetPhysicalIdentity();

		// Releasing a graph returns its complete heap plus its cached native placed objects.
		Buffer.mValue.Reset();
		Texture.mValue.Reset();
		Heap.mValue.Reset();
		mDevice->RunGarbageCollection();
		auto HeapDescriptor = HeapDesc();
		HeapDescriptor.mDebugName = "Replacement graph heap";
		auto ReusedHeap = mDevice->CreateHeap(HeapDescriptor);
		ASSERT_TRUE(ReusedHeap);
		EXPECT_EQ(ReusedHeap.mValue->GetMemoryAllocationInfo().mIdentity, HeapIdentity);
		const auto ReusedBuffer = mDevice->CreatePlacedBuffer(BufferDescriptor, ReusedHeap.mValue, 0);
		const auto ReusedTexture = mDevice->CreatePlacedTexture(TextureDescriptor, ReusedHeap.mValue, 4096);
		ASSERT_TRUE(ReusedBuffer && ReusedTexture);
		EXPECT_EQ(ReusedBuffer.mValue->GetPhysicalIdentity(), BufferIdentity);
		EXPECT_EQ(ReusedTexture.mValue->GetPhysicalIdentity(), TextureIdentity);
		EXPECT_EQ(mProvider->mHeapCreationAttempts, 1u);
		EXPECT_EQ(mProvider->mBufferCreationAttempts, 1u);
		EXPECT_EQ(mProvider->mTextureCreationAttempts, 1u);
	}

	TEST_F(FArdaGpuAllocatorTest, PlacedCacheRequiresMatchingHeapOffsetAndResourceDescriptor)
	{
		const auto FirstHeap = mDevice->CreateHeap(HeapDesc());
		const auto SecondHeap = mDevice->CreateHeap(HeapDesc());
		ASSERT_TRUE(FirstHeap && SecondHeap);
		auto Descriptor = BufferDesc();
		Descriptor.mbVirtual = true;
		auto Original = mDevice->CreatePlacedBuffer(Descriptor, FirstHeap.mValue, 0);
		ASSERT_TRUE(Original);
		const void* Identity = Original.mValue->GetPhysicalIdentity();
		Original.mValue.Reset();

		const auto OtherOffset = mDevice->CreatePlacedBuffer(Descriptor, FirstHeap.mValue, 4096);
		const auto OtherHeap = mDevice->CreatePlacedBuffer(Descriptor, SecondHeap.mValue, 0);
		auto LargerDescriptor = Descriptor;
		LargerDescriptor.mByteSize *= 2;
		const auto OtherSize = mDevice->CreatePlacedBuffer(LargerDescriptor, FirstHeap.mValue, 0);
		ASSERT_TRUE(OtherOffset && OtherHeap && OtherSize);
		EXPECT_NE(OtherOffset.mValue->GetPhysicalIdentity(), Identity);
		EXPECT_NE(OtherHeap.mValue->GetPhysicalIdentity(), Identity);
		EXPECT_NE(OtherSize.mValue->GetPhysicalIdentity(), Identity);

		const auto ExactMatch = mDevice->CreatePlacedBuffer(Descriptor, FirstHeap.mValue, 0);
		ASSERT_TRUE(ExactMatch);
		EXPECT_EQ(ExactMatch.mValue->GetPhysicalIdentity(), Identity);
		EXPECT_EQ(mProvider->mBufferCreationAttempts, 4u);
	}

	TEST_F(FArdaGpuAllocatorTest, CommittedCacheRequiresNativeDescriptorCompatibility)
	{
		auto Original = mDevice->CreateBuffer(BufferDesc());
		ASSERT_TRUE(Original);
		const void* Identity = Original.mValue->GetPhysicalIdentity();
		Original.mValue.Reset();
		auto Descriptor = BufferDesc();
		Descriptor.mStructureStride = 16;
		const auto DifferentStride = mDevice->CreateBuffer(Descriptor);
		Descriptor = BufferDesc();
		Descriptor.mUsage = EArdaRHIBufferUsage::ShaderResource;
		const auto DifferentUsage = mDevice->CreateBuffer(Descriptor);
		Descriptor = BufferDesc();
		Descriptor.mInitialState = EArdaRHIResourceState::CopyDest;
		const auto DifferentState = mDevice->CreateBuffer(Descriptor);
		Descriptor = BufferDesc();
		Descriptor.mbKeepInitialState = true;
		const auto DifferentRestorePolicy = mDevice->CreateBuffer(Descriptor);
		ASSERT_TRUE(DifferentStride && DifferentUsage && DifferentState && DifferentRestorePolicy);
		EXPECT_NE(DifferentStride.mValue->GetPhysicalIdentity(), Identity);
		EXPECT_NE(DifferentUsage.mValue->GetPhysicalIdentity(), Identity);
		EXPECT_NE(DifferentState.mValue->GetPhysicalIdentity(), Identity);
		EXPECT_NE(DifferentRestorePolicy.mValue->GetPhysicalIdentity(), Identity);

		const auto ExactMatch = mDevice->CreateBuffer(BufferDesc());
		ASSERT_TRUE(ExactMatch);
		EXPECT_EQ(ExactMatch.mValue->GetPhysicalIdentity(), Identity);
	}

	TEST_F(FArdaGpuAllocatorTest, TextureCacheRequiresInitialStateAndRestorePolicyCompatibility)
	{
		auto Descriptor = TextureDesc();
		Descriptor.mInitialState = EArdaRHIResourceState::Common;
		auto Original = mDevice->CreateTexture(Descriptor);
		ASSERT_TRUE(Original);
		const void* Identity = Original.mValue->GetPhysicalIdentity();
		Original.mValue.Reset();

		auto DifferentDescriptor = Descriptor;
		DifferentDescriptor.mInitialState = EArdaRHIResourceState::CopyDest;
		const auto DifferentState = mDevice->CreateTexture(DifferentDescriptor);
		DifferentDescriptor = Descriptor;
		DifferentDescriptor.mbKeepInitialState = true;
		const auto DifferentRestorePolicy = mDevice->CreateTexture(DifferentDescriptor);
		ASSERT_TRUE(DifferentState && DifferentRestorePolicy);
		EXPECT_NE(DifferentState.mValue->GetPhysicalIdentity(), Identity);
		EXPECT_NE(DifferentRestorePolicy.mValue->GetPhysicalIdentity(), Identity);

		const auto ExactMatch = mDevice->CreateTexture(Descriptor);
		ASSERT_TRUE(ExactMatch);
		EXPECT_EQ(ExactMatch.mValue->GetPhysicalIdentity(), Identity);
		EXPECT_EQ(mProvider->mTextureCreationAttempts, 3u);
	}

	TEST_F(FArdaGpuAllocatorTest, DeviceCachesAreIsolated)
	{
		auto Original = mDevice->CreateBuffer(BufferDesc());
		ASSERT_TRUE(Original);
		const void* Identity = Original.mValue->GetPhysicalIdentity();
		Original.mValue.Reset();

		const auto OtherProvider = eastl::make_shared<FArdaAllocatorProvider>();
		const auto OtherDevice = CreateArdaRHIDevice(OtherProvider);
		const auto OtherBuffer = OtherDevice->CreateBuffer(BufferDesc());
		ASSERT_TRUE(OtherBuffer);
		EXPECT_NE(OtherBuffer.mValue->GetPhysicalIdentity(), Identity);
		EXPECT_EQ(OtherProvider->mBufferCreationAttempts, 1u);
		EXPECT_EQ(OtherDevice->GetGpuAllocatorStats().mBufferCacheHits, 0u);
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mCachedResources, 1u);
	}

	TEST_F(FArdaGpuAllocatorTest, SurvivingResourcesBelongToTheirOriginalFacadeGeneration)
	{
		auto OldBuffer = mDevice->CreateBuffer(BufferDesc());
		auto OldTexture = mDevice->CreateTexture(TextureDesc());
		auto OldHeap = mDevice->CreateHeap(HeapDesc());
		ASSERT_TRUE(OldBuffer && OldTexture && OldHeap);
		auto OldView = mDevice->CreateShaderResourceView(OldTexture.mValue, {});
		auto OldReference = mDevice->CreateTextureReference(OldTexture.mValue);
		FArdaRHIBindingLayoutDesc BindingLayoutDesc;
		BindingLayoutDesc.mVisibility = EArdaRHIShaderStage::Pixel;
		BindingLayoutDesc.mItems.push_back({0, 1, EArdaRHIBindingType::TextureSRV});
		auto OldBindingLayout = mDevice->CreateBindingLayout(BindingLayoutDesc);
		FArdaRHIVertexAttributeDesc Attribute;
		Attribute.mSemanticName = "POSITION";
		Attribute.mFormat = EArdaRHIFormat::RGB32Float;
		Attribute.mElementStride = 12;
		auto OldInputLayout = mDevice->CreateInputLayout({Attribute});
		const uint32_t Bytecode = 1;
		FArdaRHIShaderDesc ShaderDesc;
		ShaderDesc.mStage = EArdaRHIShaderStage::Vertex;
		ShaderDesc.mBytecode = &Bytecode;
		ShaderDesc.mBytecodeSize = sizeof(Bytecode);
		auto OldShader = mDevice->CreateShader(ShaderDesc);
		ASSERT_TRUE(OldView && OldReference && OldBindingLayout && OldInputLayout && OldShader);

		mDevice.Reset();
		EXPECT_EQ(mProvider->mCounts->mDestroyedBuffers, 0u);
		EXPECT_EQ(mProvider->mCounts->mDestroyedTextures, 0u);
		EXPECT_EQ(mProvider->mCounts->mDestroyedHeaps, 0u);
		mDevice = CreateArdaRHIDevice(mProvider);
		auto NewBuffer = mDevice->CreateBuffer(BufferDesc());
		auto NewTexture = mDevice->CreateTexture(TextureDesc());
		auto NewHeap = mDevice->CreateHeap(HeapDesc());
		auto NewBindingLayout = mDevice->CreateBindingLayout(BindingLayoutDesc);
		auto NewInputLayout = mDevice->CreateInputLayout({Attribute});
		auto NewShader = mDevice->CreateShader(ShaderDesc);
		ASSERT_TRUE(NewBuffer && NewTexture && NewHeap && NewBindingLayout && NewInputLayout && NewShader);
		auto NewView = mDevice->CreateShaderResourceView(NewTexture.mValue, {});
		ASSERT_TRUE(NewView);

		EXPECT_EQ(mDevice->GetBufferMemoryRequirements(OldBuffer.mValue).mStatus.mCode, EArdaRHIResult::WrongDevice);
		EXPECT_EQ(mDevice->GetTextureMemoryRequirements(OldTexture.mValue).mStatus.mCode, EArdaRHIResult::WrongDevice);
		EXPECT_EQ(mDevice->CreateShaderResourceView(OldTexture.mValue, {}).mStatus.mCode, EArdaRHIResult::WrongDevice);
		EXPECT_EQ(mDevice->CreateTextureReference(OldTexture.mValue).mStatus.mCode, EArdaRHIResult::WrongDevice);
		EXPECT_EQ(mDevice->SetTextureReference(OldReference.mValue, NewTexture.mValue).mCode,
		    EArdaRHIResult::WrongDevice);
		EXPECT_EQ(mDevice->CreatePlacedBuffer(BufferDesc(), OldHeap.mValue, 0).mStatus.mCode,
		    EArdaRHIResult::WrongDevice);
		EXPECT_TRUE(mDevice->GetBufferMemoryRequirements(NewBuffer.mValue));
		EXPECT_TRUE(mDevice->GetTextureMemoryRequirements(NewTexture.mValue));
		EXPECT_TRUE(mDevice->CreateTextureReference(NewTexture.mValue));

		const auto CreateSet = [&](const FArdaRHIBindingLayoutRef& Layout, const FArdaRHIShaderResourceViewRef& View)
		{
			FArdaRHIBindingSetDesc Desc;
			Desc.mLayout = Layout;
			Desc.mItems.push_back({0, 0, EArdaRHIBindingType::TextureSRV, View, {}});
			return mDevice->CreateBindingSet(Desc);
		};
		EXPECT_EQ(CreateSet(OldBindingLayout.mValue, NewView.mValue).mStatus.mCode, EArdaRHIResult::WrongDevice);
		EXPECT_EQ(CreateSet(NewBindingLayout.mValue, OldView.mValue).mStatus.mCode, EArdaRHIResult::WrongDevice);
		EXPECT_TRUE(CreateSet(NewBindingLayout.mValue, NewView.mValue));
		const auto CreatePipeline = [&](const FArdaRHIInputLayoutRef& Layout, const FArdaRHIShaderRef& Shader)
		{
			FArdaRHIGraphicsPipelineDesc Desc;
			Desc.mInputLayout = Layout;
			Desc.mVertexShader = Shader;
			return mDevice->CreateGraphicsPipeline(Desc);
		};
		EXPECT_EQ(CreatePipeline(OldInputLayout.mValue, NewShader.mValue).mStatus.mCode, EArdaRHIResult::WrongDevice);
		EXPECT_EQ(CreatePipeline(NewInputLayout.mValue, OldShader.mValue).mStatus.mCode, EArdaRHIResult::WrongDevice);
		EXPECT_TRUE(CreatePipeline(NewInputLayout.mValue, NewShader.mValue));

		// Facade-only wrappers retain the old allocation until the final dependent reference is released.
		OldBuffer.mValue.Reset();
		OldTexture.mValue.Reset();
		OldHeap.mValue.Reset();
		OldReference.mValue.Reset();
		EXPECT_EQ(mProvider->mCounts->mDestroyedBuffers, 1u);
		EXPECT_EQ(mProvider->mCounts->mDestroyedHeaps, 1u);
		EXPECT_EQ(mProvider->mCounts->mDestroyedTextures, 0u);
		OldView.mValue.Reset();
		EXPECT_EQ(mProvider->mCounts->mDestroyedTextures, 1u);
		EXPECT_TRUE(mDevice->GetBufferMemoryRequirements(NewBuffer.mValue));
		EXPECT_TRUE(mDevice->GetTextureMemoryRequirements(NewTexture.mValue));
	}

	TEST_F(FArdaGpuAllocatorTest, FramebufferValidationRequiresCompatibleAttachmentSets)
	{
		FArdaRHIFramebufferDesc Framebuffer;
		EXPECT_FALSE(Validate(Framebuffer));
		auto Desc = TextureDesc();
		Desc.mUsage = EArdaRHITextureUsage::RenderTarget;
		const auto Color = mDevice->CreateTexture(Desc);
		ASSERT_TRUE(Color);
		Framebuffer.mColorAttachments.push_back({Color.mValue, {}});
		EXPECT_TRUE(Validate(Framebuffer));
		Desc.mWidth = Desc.mHeight = 8;
		const auto SmallerColor = mDevice->CreateTexture(Desc);
		ASSERT_TRUE(SmallerColor);
		Framebuffer.mColorAttachments.push_back({SmallerColor.mValue, {}});
		EXPECT_TRUE(Validate(Framebuffer));
		Desc.mSampleCount = 4;
		const auto MultisampledColor = mDevice->CreateTexture(Desc);
		ASSERT_TRUE(MultisampledColor);
		Framebuffer.mColorAttachments.back().mTexture = MultisampledColor.mValue;
		EXPECT_FALSE(Validate(Framebuffer));
		Framebuffer.mColorAttachments.back().mTexture.Reset();
		EXPECT_FALSE(Validate(Framebuffer));
		Framebuffer.mColorAttachments.assign(ArdaRHIMaxRenderTargets + 1, {Color.mValue, {}});
		EXPECT_FALSE(Validate(Framebuffer));
		Framebuffer.mColorAttachments.clear();
		Desc.mFormat = EArdaRHIFormat::D24S8;
		Desc.mUsage = EArdaRHITextureUsage::DepthStencil;
		const auto Depth = mDevice->CreateTexture(Desc);
		ASSERT_TRUE(Depth);
		Framebuffer.mDepthAttachment.mTexture = Depth.mValue;
		EXPECT_TRUE(Validate(Framebuffer));
		Framebuffer.mColorAttachments.push_back({Color.mValue, {}});
		EXPECT_FALSE(Validate(Framebuffer));
	}

	TEST_F(FArdaGpuAllocatorTest, ExplicitTrimDestroysIdleResourcesAndPreservesLiveAllocations)
	{
		auto IdleBuffer = mDevice->CreateBuffer(BufferDesc());
		auto IdleTexture = mDevice->CreateTexture(TextureDesc());
		auto IdleHeap = mDevice->CreateHeap(HeapDesc());
		const auto LiveBuffer = mDevice->CreateBuffer(BufferDesc());
		ASSERT_TRUE(IdleBuffer && IdleTexture && IdleHeap && LiveBuffer);
		IdleBuffer.mValue.Reset();
		IdleTexture.mValue.Reset();
		IdleHeap.mValue.Reset();

		mDevice->TrimGpuAllocator();
		EXPECT_EQ(mProvider->mCounts->mDestroyedBuffers, 1u);
		EXPECT_EQ(mProvider->mCounts->mDestroyedTextures, 1u);
		EXPECT_EQ(mProvider->mCounts->mDestroyedHeaps, 1u);
		const auto Stats = mDevice->GetGpuAllocatorStats();
		EXPECT_EQ(Stats.mCachedResources, 0u);
		EXPECT_EQ(Stats.mCachedHeapBytes, 0u);
		EXPECT_EQ(Stats.mCachedCommittedBytes, 0u);
		EXPECT_EQ(Stats.mCommittedBytes, BufferDesc().mByteSize);
	}

	TEST_F(FArdaGpuAllocatorTest, FailedCreationLeavesPreviouslyCachedResourcesReusable)
	{
		auto Original = mDevice->CreateBuffer(BufferDesc());
		ASSERT_TRUE(Original);
		const void* Identity = Original.mValue->GetPhysicalIdentity();
		Original.mValue.Reset();

		mProvider->mbFailBufferCreation = true;
		auto Larger = BufferDesc();
		Larger.mByteSize *= 2;
		const auto Failed = mDevice->CreateBuffer(Larger);
		EXPECT_FALSE(Failed);
		EXPECT_EQ(Failed.mStatus.mCode, EArdaRHIResult::BackendFailure);
		const auto Cached = mDevice->CreateBuffer(BufferDesc());
		ASSERT_TRUE(Cached);
		EXPECT_EQ(Cached.mValue->GetPhysicalIdentity(), Identity);
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mBufferCreations, 1u);
		EXPECT_EQ(mProvider->mBufferCreationAttempts, 2u);
	}

	TEST_F(FArdaGpuAllocatorTest, ProviderStateRejectionAllocatesFreshResources)
	{
		auto Buffer = mDevice->CreateBuffer(BufferDesc());
		auto Texture = mDevice->CreateTexture(TextureDesc());
		ASSERT_TRUE(Buffer && Texture);
		Buffer.mValue.Reset();
		Texture.mValue.Reset();

		mProvider->mbAllowResourceReuse = false;
		const auto FreshBuffer = mDevice->CreateBuffer(BufferDesc());
		const auto FreshTexture = mDevice->CreateTexture(TextureDesc());
		ASSERT_TRUE(FreshBuffer && FreshTexture);
		EXPECT_EQ(mProvider->mBufferCreationAttempts, 2u);
		EXPECT_EQ(mProvider->mTextureCreationAttempts, 2u);
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mBufferCacheHits, 0u);
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mTextureCacheHits, 0u);
	}

	TEST_F(FArdaGpuAllocatorTest, ResourceEvictionRequiresBothIdleAgeAndExceededCapacity)
	{
		FArdaGpuAllocatorOptions Options;
		Options.mResourceRetentionCycles = 3;
		Options.mBufferCacheCapacity = 1;
		Options.mTextureCacheCapacity = 1;
		ASSERT_TRUE(mDevice->SetGpuAllocatorOptions(Options));
		auto FirstBuffer = mDevice->CreateBuffer(BufferDesc());
		auto SecondBuffer = mDevice->CreateBuffer(BufferDesc());
		auto FirstTexture = mDevice->CreateTexture(TextureDesc());
		auto SecondTexture = mDevice->CreateTexture(TextureDesc());
		ASSERT_TRUE(FirstBuffer && SecondBuffer && FirstTexture && SecondTexture);
		FirstBuffer.mValue.Reset();
		SecondBuffer.mValue.Reset();
		FirstTexture.mValue.Reset();
		SecondTexture.mValue.Reset();

		for (uint32_t Cycle = 0; Cycle < 2; ++Cycle)
		{
			mDevice->RunGarbageCollection();
		}
		EXPECT_EQ(mProvider->mCounts->mDestroyedBuffers, 0u);
		EXPECT_EQ(mProvider->mCounts->mDestroyedTextures, 0u);
		mDevice->RunGarbageCollection();
		EXPECT_EQ(mProvider->mCounts->mDestroyedBuffers, 1u);
		EXPECT_EQ(mProvider->mCounts->mDestroyedTextures, 1u);
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mCollectionCycle, 3u);

		// Entries within the soft capacity remain useful even when their idle age exceeds the delay.
		for (uint32_t Cycle = 0; Cycle < 5; ++Cycle)
		{
			mDevice->RunGarbageCollection();
		}
		EXPECT_EQ(mProvider->mCounts->mDestroyedBuffers, 1u);
		EXPECT_EQ(mProvider->mCounts->mDestroyedTextures, 1u);
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mCachedResources, 2u);
	}

	TEST_F(FArdaGpuAllocatorTest, HeapIdleRetentionRestartsAfterReuse)
	{
		FArdaGpuAllocatorOptions Options;
		Options.mHeapRetentionCycles = 3;
		ASSERT_TRUE(mDevice->SetGpuAllocatorOptions(Options));
		auto Heap = mDevice->CreateHeap(HeapDesc());
		ASSERT_TRUE(Heap);
		const void* Identity = Heap.mValue->GetMemoryAllocationInfo().mIdentity;
		Heap.mValue.Reset();
		mDevice->RunGarbageCollection();
		mDevice->RunGarbageCollection();
		EXPECT_EQ(mProvider->mCounts->mDestroyedHeaps, 0u);

		auto ReusedHeap = mDevice->CreateHeap(HeapDesc());
		ASSERT_TRUE(ReusedHeap);
		EXPECT_EQ(ReusedHeap.mValue->GetMemoryAllocationInfo().mIdentity, Identity);
		ReusedHeap.mValue.Reset();
		mDevice->RunGarbageCollection();
		mDevice->RunGarbageCollection();
		EXPECT_EQ(mProvider->mCounts->mDestroyedHeaps, 0u);
		mDevice->RunGarbageCollection();
		EXPECT_EQ(mProvider->mCounts->mDestroyedHeaps, 1u);
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mHeapBytes, 0u);
	}

	TEST_F(FArdaGpuAllocatorTest, ZeroCacheCapacityStillHonorsResourceRetention)
	{
		FArdaGpuAllocatorOptions Options;
		Options.mResourceRetentionCycles = 2;
		Options.mBufferCacheCapacity = 0;
		ASSERT_TRUE(mDevice->SetGpuAllocatorOptions(Options));
		auto Buffer = mDevice->CreateBuffer(BufferDesc());
		ASSERT_TRUE(Buffer);
		Buffer.mValue.Reset();
		mDevice->RunGarbageCollection();
		EXPECT_EQ(mProvider->mCounts->mDestroyedBuffers, 0u);
		mDevice->RunGarbageCollection();
		EXPECT_EQ(mProvider->mCounts->mDestroyedBuffers, 1u);
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mCachedResources, 0u);
	}

	TEST_F(FArdaGpuAllocatorTest, ZeroByteBudgetReclaimsIdleStorageWithoutEvictingActiveLeases)
	{
		auto IdleBuffer = mDevice->CreateBuffer(BufferDesc());
		auto IdleHeap = mDevice->CreateHeap(HeapDesc());
		const auto ActiveBuffer = mDevice->CreateBuffer(BufferDesc());
		const auto ActiveHeap = mDevice->CreateHeap(HeapDesc());
		ASSERT_TRUE(IdleBuffer && IdleHeap && ActiveBuffer && ActiveHeap);
		IdleBuffer.mValue.Reset();
		IdleHeap.mValue.Reset();
		FArdaGpuAllocatorOptions Options;
		Options.mMaxCachedBytes = 0;
		ASSERT_TRUE(mDevice->SetGpuAllocatorOptions(Options));

		mDevice->RunGarbageCollection();
		EXPECT_EQ(mProvider->mCounts->mDestroyedBuffers, 1u);
		EXPECT_EQ(mProvider->mCounts->mDestroyedHeaps, 1u);
		const auto Stats = mDevice->GetGpuAllocatorStats();
		EXPECT_EQ(Stats.mCachedCommittedBytes, 0u);
		EXPECT_EQ(Stats.mCachedHeapBytes, 0u);
		EXPECT_EQ(Stats.mCommittedBytes, BufferDesc().mByteSize);
		EXPECT_EQ(Stats.mHeapBytes, HeapDesc().mCapacity);
	}

	TEST_F(FArdaGpuAllocatorTest, TrimmingPlacedObjectsReleasesTheirParentHeapOnce)
	{
		auto Heap = mDevice->CreateHeap(HeapDesc());
		ASSERT_TRUE(Heap);
		auto BufferDescriptor = BufferDesc();
		BufferDescriptor.mbVirtual = true;
		auto TextureDescriptor = TextureDesc();
		TextureDescriptor.mbVirtual = true;
		auto Buffer = mDevice->CreatePlacedBuffer(BufferDescriptor, Heap.mValue, 0);
		auto Texture = mDevice->CreatePlacedTexture(TextureDescriptor, Heap.mValue, 4096);
		ASSERT_TRUE(Buffer && Texture);
		Buffer.mValue.Reset();
		Texture.mValue.Reset();
		Heap.mValue.Reset();

		mDevice->TrimGpuAllocator();
		EXPECT_EQ(mProvider->mCounts->mDestroyedBuffers, 1u);
		EXPECT_EQ(mProvider->mCounts->mDestroyedTextures, 1u);
		EXPECT_EQ(mProvider->mCounts->mDestroyedHeaps, 1u);
		const auto Stats = mDevice->GetGpuAllocatorStats();
		EXPECT_EQ(Stats.mHeapBytes, 0u);
		EXPECT_EQ(Stats.mCachedResources, 0u);
		EXPECT_EQ(Stats.mCommittedBytes, 0u);
	}

	TEST_F(FArdaGpuAllocatorTest, FailedPlacedCreationDoesNotCacheFailedResourcesOrPinTheirHeap)
	{
		auto Heap = mDevice->CreateHeap(HeapDesc());
		ASSERT_TRUE(Heap);
		const void* HeapIdentity = Heap.mValue->GetMemoryAllocationInfo().mIdentity;
		auto BufferDescriptor = BufferDesc();
		BufferDescriptor.mbVirtual = true;
		auto TextureDescriptor = TextureDesc();
		TextureDescriptor.mbVirtual = true;
		mProvider->mbFailMemoryBinding = true;
		const auto FailedBuffer = mDevice->CreatePlacedBuffer(BufferDescriptor, Heap.mValue, 0);
		const auto FailedTexture = mDevice->CreatePlacedTexture(TextureDescriptor, Heap.mValue, 4096);
		EXPECT_FALSE(FailedBuffer);
		EXPECT_FALSE(FailedTexture);
		EXPECT_EQ(FailedBuffer.mStatus.mCode, EArdaRHIResult::BackendFailure);
		EXPECT_EQ(FailedTexture.mStatus.mCode, EArdaRHIResult::BackendFailure);
		EXPECT_EQ(mProvider->mCounts->mDestroyedBuffers, 1u);
		EXPECT_EQ(mProvider->mCounts->mDestroyedTextures, 1u);
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mCachedResources, 0u);

		Heap.mValue.Reset();
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mCachedHeapBytes, HeapDesc().mCapacity);
		mProvider->mbFailMemoryBinding = false;
		auto ReusedHeap = mDevice->CreateHeap(HeapDesc());
		ASSERT_TRUE(ReusedHeap);
		EXPECT_EQ(ReusedHeap.mValue->GetMemoryAllocationInfo().mIdentity, HeapIdentity);
		auto Buffer = mDevice->CreatePlacedBuffer(BufferDescriptor, ReusedHeap.mValue, 0);
		auto Texture = mDevice->CreatePlacedTexture(TextureDescriptor, ReusedHeap.mValue, 4096);
		ASSERT_TRUE(Buffer && Texture);
		Buffer.mValue.Reset();
		Texture.mValue.Reset();
		ReusedHeap.mValue.Reset();
		mDevice->TrimGpuAllocator();
		EXPECT_EQ(mProvider->mCounts->mDestroyedBuffers, 2u);
		EXPECT_EQ(mProvider->mCounts->mDestroyedTextures, 2u);
		EXPECT_EQ(mProvider->mCounts->mDestroyedHeaps, 1u);
	}

	TEST_F(FArdaGpuAllocatorTest, FailedVirtualBindingCanRetryWithoutChangingTheNativeObject)
	{
		const auto Heap = mDevice->CreateHeap(HeapDesc());
		ASSERT_TRUE(Heap);
		auto Descriptor = BufferDesc();
		Descriptor.mbVirtual = true;
		auto Buffer = mDevice->CreateBuffer(Descriptor);
		ASSERT_TRUE(Buffer);
		const void* Identity = Buffer.mValue->GetPhysicalIdentity();
		mProvider->mbFailMemoryBinding = true;
		EXPECT_EQ(mDevice->BindBufferMemory(Buffer.mValue, Heap.mValue, 0).mCode, EArdaRHIResult::BackendFailure);
		EXPECT_EQ(Buffer.mValue->GetPhysicalIdentity(), Identity);
		EXPECT_FALSE(Buffer.mValue->GetMemoryAllocationInfo().mbKnown);

		mProvider->mbFailMemoryBinding = false;
		ASSERT_TRUE(mDevice->BindBufferMemory(Buffer.mValue, Heap.mValue, 0));
		EXPECT_EQ(Buffer.mValue->GetPhysicalIdentity(), Identity);
		EXPECT_EQ(Buffer.mValue->GetMemoryAllocationInfo().mIdentity, Heap.mValue->GetMemoryAllocationInfo().mIdentity);
		EXPECT_EQ(mProvider->mBufferCreationAttempts, 1u);
		EXPECT_EQ(mProvider->mBufferBindingAttempts, 2u);
		EXPECT_EQ(mDevice->BindBufferMemory(Buffer.mValue, Heap.mValue, 4096).mCode, EArdaRHIResult::InvalidState);
	}

	TEST_F(FArdaGpuAllocatorTest, InvalidPlacementIsRejectedBeforeNativeResourceCreation)
	{
		const auto Heap = mDevice->CreateHeap(HeapDesc());
		ASSERT_TRUE(Heap);
		auto BufferDescriptor = BufferDesc();
		BufferDescriptor.mbVirtual = true;
		auto TextureDescriptor = TextureDesc();
		TextureDescriptor.mbVirtual = true;
		for (const uint64_t Offset : {uint64_t(1), HeapDesc().mCapacity, std::numeric_limits<uint64_t>::max()})
		{
			EXPECT_EQ(mDevice->CreatePlacedBuffer(BufferDescriptor, Heap.mValue, Offset).mStatus.mCode,
			    EArdaRHIResult::InvalidArgument);
			EXPECT_EQ(mDevice->CreatePlacedTexture(TextureDescriptor, Heap.mValue, Offset).mStatus.mCode,
			    EArdaRHIResult::InvalidArgument);
		}
		EXPECT_EQ(mDevice->CreatePlacedBuffer(BufferDesc(), Heap.mValue, 0).mStatus.mCode,
		    EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(mDevice->CreatePlacedTexture(TextureDesc(), Heap.mValue, 0).mStatus.mCode,
		    EArdaRHIResult::InvalidArgument);

		auto IncompatibleHeapDescriptor = HeapDesc();
		IncompatibleHeapDescriptor.mMemoryTypeBits = 2;
		const auto IncompatibleHeap = mDevice->CreateHeap(IncompatibleHeapDescriptor);
		ASSERT_TRUE(IncompatibleHeap);
		EXPECT_EQ(mDevice->CreatePlacedBuffer(BufferDescriptor, IncompatibleHeap.mValue, 0).mStatus.mCode,
		    EArdaRHIResult::InvalidArgument);
		EXPECT_EQ(mDevice->CreatePlacedTexture(TextureDescriptor, IncompatibleHeap.mValue, 0).mStatus.mCode,
		    EArdaRHIResult::InvalidArgument);

		const auto OtherProvider = eastl::make_shared<FArdaAllocatorProvider>();
		const auto OtherDevice = CreateArdaRHIDevice(OtherProvider);
		const auto OtherHeap = OtherDevice->CreateHeap(HeapDesc());
		ASSERT_TRUE(OtherHeap);
		EXPECT_EQ(mDevice->CreatePlacedBuffer(BufferDescriptor, OtherHeap.mValue, 0).mStatus.mCode,
		    EArdaRHIResult::WrongDevice);
		EXPECT_EQ(mDevice->CreatePlacedTexture(TextureDescriptor, OtherHeap.mValue, 0).mStatus.mCode,
		    EArdaRHIResult::WrongDevice);
		EXPECT_EQ(mProvider->mBufferCreationAttempts, 0u);
		EXPECT_EQ(mProvider->mTextureCreationAttempts, 0u);
		EXPECT_EQ(mProvider->mBufferBindingAttempts, 0u);
		EXPECT_EQ(mProvider->mTextureBindingAttempts, 0u);
	}

	TEST_F(FArdaGpuAllocatorTest, ConcurrentAcquisitionReturnAndTrimPreserveExclusiveLeasesAndAccounting)
	{
		std::atomic<bool> bStart{false};
		std::atomic<uint32_t> WorkersRemaining{4};
		std::atomic<uint32_t> Failures{0};
		std::mutex IdentityMutex;
		std::unordered_set<const void*> ActiveIdentities;
		eastl::vector<std::thread> Workers;
		for (uint32_t Worker = 0; Worker < 4; ++Worker)
		{
			Workers.emplace_back(
			    [&]
			    {
				    while (!bStart.load(std::memory_order_acquire))
				    {
					    std::this_thread::yield();
				    }
				    for (uint32_t Iteration = 0; Iteration < 64; ++Iteration)
				    {
					    const auto Heap = mDevice->CreateHeap(HeapDesc());
					    if (!Heap)
					    {
						    ++Failures;
						    continue;
					    }
					    auto BufferDescriptor = BufferDesc();
					    BufferDescriptor.mbVirtual = true;
					    auto TextureDescriptor = TextureDesc();
					    TextureDescriptor.mbVirtual = true;
					    const auto Buffer = mDevice->CreatePlacedBuffer(BufferDescriptor, Heap.mValue, 0);
					    const auto Texture = mDevice->CreatePlacedTexture(TextureDescriptor, Heap.mValue, 4096);
					    if (!Buffer || !Texture)
					    {
						    ++Failures;
						    continue;
					    }
					    const void* Identities[] = {Heap.mValue->GetMemoryAllocationInfo().mIdentity,
					        Buffer.mValue->GetPhysicalIdentity(),
					        Texture.mValue->GetPhysicalIdentity()};
					    {
						    std::lock_guard<std::mutex> Lock(IdentityMutex);
						    for (const void* Identity : Identities)
						    {
							    if (!ActiveIdentities.insert(Identity).second)
							    {
								    ++Failures;
							    }
						    }
					    }
					    std::this_thread::yield();
					    {
						    std::lock_guard<std::mutex> Lock(IdentityMutex);
						    for (const void* Identity : Identities)
						    {
							    ActiveIdentities.erase(Identity);
						    }
					    }
				    }
				    --WorkersRemaining;
			    });
		}
		std::thread Collector(
		    [&]
		    {
			    while (!bStart.load(std::memory_order_acquire))
			    {
				    std::this_thread::yield();
			    }
			    while (WorkersRemaining.load(std::memory_order_acquire))
			    {
				    mDevice->RunGarbageCollection();
				    mDevice->TrimGpuAllocator();
				    (void)mDevice->GetGpuAllocatorStats();
				    std::this_thread::yield();
			    }
		    });
		bStart.store(true, std::memory_order_release);
		for (auto& Worker : Workers)
		{
			Worker.join();
		}
		Collector.join();

		mDevice->TrimGpuAllocator();
		EXPECT_EQ(Failures.load(), 0u);
		EXPECT_TRUE(ActiveIdentities.empty());
		EXPECT_EQ(mProvider->mCounts->mDestroyedBuffers.load(), mProvider->mBufferCreationAttempts.load());
		EXPECT_EQ(mProvider->mCounts->mDestroyedTextures.load(), mProvider->mTextureCreationAttempts.load());
		EXPECT_EQ(mProvider->mCounts->mDestroyedHeaps.load(), mProvider->mHeapCreationAttempts.load());
		const auto Stats = mDevice->GetGpuAllocatorStats();
		EXPECT_EQ(Stats.mHeapBytes, 0u);
		EXPECT_EQ(Stats.mCommittedBytes, 0u);
		EXPECT_EQ(Stats.mCachedResources, 0u);
	}

	TEST_F(FArdaGpuAllocatorTest, InternalBufferCacheReusesOnlyTheRequestedQueueAndSharesGraphicsWithFacade)
	{
		const auto Descriptor = BufferDesc();
		auto ComputeBuffer = mProvider->AllocateBuffer(Descriptor, EArdaRHIQueueType::Compute);
		ASSERT_TRUE(ComputeBuffer);
		const void* ComputeIdentity = ComputeBuffer.mValue->GetIdentity();
		ComputeBuffer.mValue.reset();

		auto CopyBuffer = mProvider->AllocateBuffer(Descriptor, EArdaRHIQueueType::Copy);
		ASSERT_TRUE(CopyBuffer);
		const void* CopyIdentity = CopyBuffer.mValue->GetIdentity();
		EXPECT_NE(CopyIdentity, ComputeIdentity);
		CopyBuffer.mValue.reset();
		auto GraphicsBuffer = mDevice->CreateBuffer(Descriptor);
		ASSERT_TRUE(GraphicsBuffer);
		const void* GraphicsIdentity = GraphicsBuffer.mValue->GetPhysicalIdentity();
		EXPECT_NE(GraphicsIdentity, ComputeIdentity);
		EXPECT_NE(GraphicsIdentity, CopyIdentity);
		GraphicsBuffer.mValue.Reset();

		// Equal descriptors on another queue cannot consume an idle queue-qualified allocation.
		auto ReusedCompute = mProvider->AllocateBuffer(Descriptor, EArdaRHIQueueType::Compute);
		auto ReusedCopy = mProvider->AllocateBuffer(Descriptor, EArdaRHIQueueType::Copy);
		auto ReusedGraphics = mDevice->CreateBuffer(Descriptor);
		ASSERT_TRUE(ReusedCompute && ReusedCopy && ReusedGraphics);
		EXPECT_EQ(ReusedCompute.mValue->GetIdentity(), ComputeIdentity);
		EXPECT_EQ(ReusedCopy.mValue->GetIdentity(), CopyIdentity);
		EXPECT_EQ(ReusedGraphics.mValue->GetPhysicalIdentity(), GraphicsIdentity);
		EXPECT_EQ(mProvider->mBufferCreationAttempts, 3u);
		EXPECT_EQ(mDevice->GetGpuAllocatorStats().mBufferCacheHits, 3u);

		ReusedGraphics.mValue.Reset();
		auto DefaultQueueBuffer = mProvider->AllocateBuffer(Descriptor);
		ASSERT_TRUE(DefaultQueueBuffer);
		EXPECT_EQ(DefaultQueueBuffer.mValue->GetIdentity(), GraphicsIdentity);
		ReusedCompute.mValue.reset();
		ReusedCopy.mValue.reset();
		DefaultQueueBuffer.mValue.reset();
		mDevice->TrimGpuAllocator();
		EXPECT_EQ(mProvider->mCounts->mDestroyedBuffers, 3u);
		const auto Stats = mDevice->GetGpuAllocatorStats();
		EXPECT_EQ(Stats.mBufferCacheHits, 4u);
		EXPECT_EQ(Stats.mCommittedBytes, 0u);
		EXPECT_EQ(Stats.mCachedCommittedBytes, 0u);
		EXPECT_EQ(Stats.mCachedResources, 0u);
	}
}
