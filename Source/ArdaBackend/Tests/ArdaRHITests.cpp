#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "RHI/ArdaRHICapabilities.h"
#include "RHI/ArdaRHIRef.h"
#include "RHI/ArdaRHIResource.h"
#include "RHI/ArdaRHIResources.h"

#include <gtest/gtest.h>

#include <atomic>

namespace
{
	bool ConfigureLinkedBackend(arda::FArdaBackendConfiguration& Configuration)
	{
		const auto Modules = arda::EnumerateBackendModules();
		if (Modules.empty())
		{
			return false;
		}
		Configuration.mBackendName = Modules.front().mName;
		return arda::ConfigureBackend(Configuration);
	}

	struct FBackendShutdownGuard
	{
		~FBackendShutdownGuard()
		{
			arda::ShutdownBackend();
		}
	};

	class FFakeResource final : public arda::IArdaRHIResource
	{
	public:
		explicit FFakeResource(std::atomic<int>& Destructions)
		    : mDestructions(Destructions)
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

		arda::EArdaRHIResourceType GetResourceType() const noexcept override
		{
			return arda::EArdaRHIResourceType::Buffer;
		}

		const char* GetDebugName() const noexcept override
		{
			return "Fake";
		}

	private:
		~FFakeResource() override
		{
			++mDestructions;
		}

		std::atomic<uint32_t> mReferences{0};
		std::atomic<int>& mDestructions;
	};
}

TEST(ArdaRHI, IntrusiveReferencesCopyMoveAndRelease)
{
	using namespace arda;
	std::atomic<int> Destructions{0};

	TArdaRHIRef<IArdaRHIResource> A(new FFakeResource(Destructions));
	EXPECT_TRUE(A);
	{
		auto B = A;
		auto C = std::move(B);
		EXPECT_FALSE(B);
		EXPECT_EQ(C.Get(), A.Get());
		C.Reset();
		EXPECT_EQ(Destructions.load(), 0);
	}
	A.Reset();
	EXPECT_EQ(Destructions.load(), 1);
}

TEST(ArdaRHI, DescriptorEqualityAndHashAreStable)
{
	using namespace arda;
	FArdaRHITextureDesc A;
	A.mWidth = 128;
	A.mHeight = 64;
	A.mFormat = EArdaRHIFormat::RGBA8UNorm;
	A.mDebugName = "Color";
	const FArdaRHITextureDesc B = A;

	EXPECT_EQ(A, B);
	EXPECT_EQ(HashValue(A), HashValue(B));

	FArdaRHITextureDesc C = A;
	C.mMipLevels = 2;
	EXPECT_FALSE(A == C);
	EXPECT_NE(HashValue(A), HashValue(C));
}

TEST(ArdaRHI, FormatStorageMetadataCoversEveryKnownFormat)
{
	using namespace arda;
	for (uint32_t Value = 1; Value < static_cast<uint32_t>(EArdaRHIFormat::Count); ++Value)
	{
		const auto Format = static_cast<EArdaRHIFormat>(Value);
		const FArdaRHIFormatInfo& Info = GetArdaRHIFormatInfo(Format);
		EXPECT_GT(Info.mBytesPerBlock, 0u) << Value;
		EXPECT_GT(Info.mBlockWidth, 0u) << Value;
		EXPECT_GT(Info.mBlockHeight, 0u) << Value;
	}

	EXPECT_EQ(GetArdaRHIFormatElementSize(EArdaRHIFormat::RGBA8UNorm), 4u);
	EXPECT_EQ(GetArdaRHIFormatElementSize(EArdaRHIFormat::BC1UNorm), 0u);
	EXPECT_FALSE(IsArdaRHIFormatKnown(EArdaRHIFormat::Unknown));
	EXPECT_FALSE(IsArdaRHIFormatKnown(EArdaRHIFormat::Count));
	EXPECT_EQ(GetArdaRHIFormatPlaneCount(EArdaRHIFormat::Unknown), 1u);
	EXPECT_EQ(GetArdaRHIFormatPlaneCount(EArdaRHIFormat::D32S8), 2u);
	EXPECT_EQ(GetArdaRHIFormatPlaneCount(EArdaRHIFormat::D32), 1u);
	EXPECT_EQ(GetArdaRHITextureMipExtent(16, 2), 4u);
	EXPECT_EQ(GetArdaRHITextureMipExtent(16, 40), 1u);
	const auto& Block = GetArdaRHIFormatInfo(EArdaRHIFormat::BC1UNorm);
	EXPECT_EQ(Block.mBytesPerBlock, 8u);
	EXPECT_EQ(Block.mBlockWidth, 4u);
	EXPECT_EQ(Block.mBlockHeight, 4u);
}

TEST(ArdaRHI, QueueIndexAndShaderStageClassificationHaveOneMapping)
{
	using namespace arda;
	static_assert(ArdaRHIQueueTypeCount == 3);
	EXPECT_EQ(GetArdaRHIQueueIndex(EArdaRHIQueueType::Graphics), 0u);
	EXPECT_EQ(GetArdaRHIQueueIndex(EArdaRHIQueueType::Compute), 1u);
	EXPECT_EQ(GetArdaRHIQueueIndex(EArdaRHIQueueType::Copy), 2u);

	EXPECT_TRUE(IsArdaRHIRayTracingShaderStage(EArdaRHIShaderStage::RayGeneration));
	EXPECT_TRUE(IsArdaRHIRayTracingShaderStage(EArdaRHIShaderStage::Callable));
	EXPECT_FALSE(IsArdaRHIRayTracingShaderStage(EArdaRHIShaderStage::Vertex));
	EXPECT_FALSE(IsArdaRHIRayTracingShaderStage(EArdaRHIShaderStage::RayGeneration | EArdaRHIShaderStage::Miss));
}

TEST(ArdaRHI, RayTracingTierIsDerivedFromAbilities)
{
	using namespace arda;
	FArdaRHIRayTracingCapabilities Capabilities;
	EXPECT_EQ(Capabilities.GetTier(), EArdaRHIRayTracingTier::None);

	Capabilities.mbInfrastructure = true;
	EXPECT_EQ(Capabilities.GetTier(), EArdaRHIRayTracingTier::Software);
	Capabilities.mbHardwareAccelerated = true;
	EXPECT_EQ(Capabilities.GetTier(), EArdaRHIRayTracingTier::None);
	Capabilities.mbOpacityMicromaps = true;
	EXPECT_EQ(Capabilities.GetTier(), EArdaRHIRayTracingTier::None);
	Capabilities.mbOpacityMicromaps = false;
	Capabilities.mbAccelerationStructures = true;
	EXPECT_EQ(Capabilities.GetTier(), EArdaRHIRayTracingTier::HardwareAccelerationStructures);
	Capabilities.mbInlineRayQueries = true;
	EXPECT_EQ(Capabilities.GetTier(), EArdaRHIRayTracingTier::HardwareInlineQueries);
	Capabilities.mbOpacityMicromaps = true;
	EXPECT_EQ(Capabilities.GetTier(), EArdaRHIRayTracingTier::HardwareOpacityMicromaps);
}

TEST(ArdaRHI, TextureCopyAndResolveUseCentralRegionPolicy)
{
	using namespace arda;
	FArdaRHITextureDesc Source;
	Source.mWidth = 16;
	Source.mHeight = 8;
	Source.mDepth = 1;
	Source.mMipLevels = 2;
	Source.mFormat = EArdaRHIFormat::RGBA8UNorm;
	FArdaRHITextureDesc Destination = Source;

	FArdaRHITextureSlice SourceSlice;
	SourceSlice.mMipLevel = 1;
	SourceSlice.mX = 2;
	FArdaRHITextureSlice DestinationSlice;
	DestinationSlice.mMipLevel = 1;
	DestinationSlice.mX = 1;
	FArdaRHITextureCopyExtent Extent;
	EXPECT_TRUE(ResolveArdaRHITextureCopyExtent(Destination, DestinationSlice, Source, SourceSlice, Extent));
	EXPECT_EQ(Extent.mWidth, 6u);
	EXPECT_EQ(Extent.mHeight, 4u);
	EXPECT_EQ(Extent.mDepth, 1u);

	DestinationSlice.mX = 3;
	EXPECT_FALSE(ResolveArdaRHITextureCopyExtent(Destination, DestinationSlice, Source, SourceSlice, Extent));
	DestinationSlice.mX = 0;
	Destination.mFormat = EArdaRHIFormat::BGRA8UNorm;
	EXPECT_FALSE(ResolveArdaRHITextureCopyExtent(Destination, DestinationSlice, Source, SourceSlice, Extent));

	Source.mFormat = EArdaRHIFormat::BC1UNorm;
	Destination = Source;
	SourceSlice = {};
	DestinationSlice = {};
	SourceSlice.mX = 1;
	EXPECT_FALSE(ResolveArdaRHITextureCopyExtent(Destination, DestinationSlice, Source, SourceSlice, Extent));
	SourceSlice.mX = 4;
	SourceSlice.mWidth = 4;
	DestinationSlice.mX = 4;
	EXPECT_TRUE(ResolveArdaRHITextureCopyExtent(Destination, DestinationSlice, Source, SourceSlice, Extent));

	Source.mFormat = EArdaRHIFormat::RGBA8UNorm;
	Destination = Source;
	Destination.mSampleCount = 1;
	Source.mSampleCount = 4;
	SourceSlice = {};
	DestinationSlice = {};
	EXPECT_TRUE(ValidateArdaRHITextureResolve(Destination, DestinationSlice, Source, SourceSlice, Extent));
	EXPECT_EQ(Extent.mWidth, 16u);
	EXPECT_EQ(Extent.mHeight, 8u);
	SourceSlice.mX = 1;
	EXPECT_FALSE(ValidateArdaRHITextureResolve(Destination, DestinationSlice, Source, SourceSlice, Extent));
	SourceSlice = {};
	Destination.mWidth = 8;
	EXPECT_FALSE(ValidateArdaRHITextureResolve(Destination, DestinationSlice, Source, SourceSlice, Extent));
}

TEST(ArdaRHI, InputLayoutIdentityContainsOnlyVertexAttributes)
{
	using namespace arda;
	FArdaRHIInputLayoutDesc First;
	First.mAttributes.push_back({"POSITION", EArdaRHIFormat::RGB32Float, 1, 0, 0, 12, false});
	const FArdaRHIInputLayoutDesc Same = First;
	EXPECT_EQ(First, Same);
	EXPECT_EQ(HashValue(First), HashValue(Same));

	FArdaRHIInputLayoutDesc Different = First;
	Different.mAttributes.front().mOffset = 12;
	EXPECT_FALSE(First == Different);
	EXPECT_NE(HashValue(First), HashValue(Different));
}

TEST(ArdaRHI, NativeImportDescriptorEqualityIncludesLifetimeTokenIdentity)
{
	using namespace arda;
	auto FirstToken = eastl::make_shared<int>(1);
	auto SecondToken = eastl::make_shared<int>(1);

	FArdaRHINativeTextureImportDesc Texture;
	Texture.mNativeObject = 77;
	Texture.mNativeType = EArdaRHINativeResourceType::D3D12Resource;
	Texture.mTexture.mFormat = EArdaRHIFormat::RGBA8UNorm;
	Texture.mLifetimeToken = FirstToken;
	const auto SameTextureToken = Texture;
	auto DifferentTextureToken = Texture;
	DifferentTextureToken.mLifetimeToken = SecondToken;
	EXPECT_EQ(Texture, SameTextureToken);
	EXPECT_FALSE(Texture == DifferentTextureToken);

	FArdaRHINativeBufferImportDesc Buffer;
	Buffer.mNativeObject = 88;
	Buffer.mNativeType = EArdaRHINativeResourceType::D3D12Resource;
	Buffer.mBuffer.mByteSize = 64;
	Buffer.mLifetimeToken = FirstToken;
	const auto SameBufferToken = Buffer;
	auto DifferentBufferToken = Buffer;
	DifferentBufferToken.mLifetimeToken = SecondToken;
	EXPECT_EQ(Buffer, SameBufferToken);
	EXPECT_FALSE(Buffer == DifferentBufferToken);
}

TEST(ArdaRHI, CacheKeyDescriptorsIgnoreDebugLabels)
{
	using namespace arda;
	FArdaRHISamplerDesc A;
	A.mDebugName = "First";
	FArdaRHISamplerDesc B = A;
	B.mDebugName = "Second";
	EXPECT_EQ(A, B);
	EXPECT_EQ(HashValue(A), HashValue(B));

	FArdaRHIBindingLayoutDesc LayoutA;
	LayoutA.mVisibility = EArdaRHIShaderStage::Pixel;
	LayoutA.mItems.push_back({0, 1, EArdaRHIBindingType::Sampler});
	FArdaRHIBindingLayoutDesc LayoutB = LayoutA;
	LayoutB.mDebugName = "Diagnostic-only";
	EXPECT_EQ(LayoutA, LayoutB);
	EXPECT_EQ(HashValue(LayoutA), HashValue(LayoutB));
	EXPECT_TRUE(Validate(LayoutA));
}

TEST(ArdaRHI, SamplerCacheReusesEvictsAndTrims)
{
	using namespace arda;
	arda::ShutdownBackend();
	arda::FArdaBackendConfiguration Configuration;
	Configuration.mbEnableValidation = false;
	ASSERT_TRUE(ConfigureLinkedBackend(Configuration));
	if (!arda::InitializeBackend())
	{
		GTEST_SKIP() << arda::GetBackendError().c_str();
	}

	arda::FArdaRHIDeviceRef Device = arda::GetDevice();
	ASSERT_TRUE(Device);
	arda::FArdaRHISamplerDesc Desc;
	auto First = Device->CreateSampler(Desc);
	auto Reused = Device->CreateSampler(Desc);
	ASSERT_TRUE(First);
	ASSERT_TRUE(Reused);
	EXPECT_EQ(First.mValue.Get(), Reused.mValue.Get());

	arda::FArdaRHIBindingLayoutDesc LayoutDesc;
	LayoutDesc.mVisibility = arda::EArdaRHIShaderStage::Pixel;
	LayoutDesc.mItems.push_back({0, 1, arda::EArdaRHIBindingType::Sampler});
	auto LayoutA = Device->CreateBindingLayout(LayoutDesc);
	auto LayoutB = Device->CreateBindingLayout(LayoutDesc);
	ASSERT_TRUE(LayoutA);
	ASSERT_TRUE(LayoutB);
	EXPECT_EQ(LayoutA.mValue.Get(), LayoutB.mValue.Get());

	arda::FArdaRHIRasterState RasterDesc;
	auto RasterA = Device->CreateRasterState(RasterDesc);
	auto RasterB = Device->CreateRasterState(RasterDesc);
	ASSERT_TRUE(RasterA);
	ASSERT_TRUE(RasterB);
	EXPECT_EQ(RasterA.mValue.Get(), RasterB.mValue.Get());

	auto TextureReference = Device->CreateTextureReference();
	ASSERT_TRUE(TextureReference);
	EXPECT_FALSE(TextureReference.mValue->GetTexture());

	for (uint32_t Index = 1; Index <= 64; ++Index)
	{
		arda::FArdaRHISamplerDesc Unique = Desc;
		Unique.mMipBias = static_cast<float>(Index);
		ASSERT_TRUE(Device->CreateSampler(Unique));
	}
	auto Recreated = Device->CreateSampler(Desc);
	ASSERT_TRUE(Recreated);
	EXPECT_NE(First.mValue.Get(), Recreated.mValue.Get());
	EXPECT_TRUE(First.mValue);
	EXPECT_LE(Device->GetDescriptorCacheStats().mSamplers, 64u);

	Device->TrimDescriptorCaches();
	EXPECT_EQ(Device->GetDescriptorCacheStats().mSamplers, 0u);
	EXPECT_EQ(Device->GetDescriptorCacheStats().mBindingLayouts, 0u);
	EXPECT_EQ(Device->GetDescriptorCacheStats().mRasterStates, 0u);
	EXPECT_TRUE(First.mValue);
	First.mValue = nullptr;
	Reused.mValue = nullptr;
	Recreated.mValue = nullptr;
	LayoutA.mValue = nullptr;
	LayoutB.mValue = nullptr;
	RasterA.mValue = nullptr;
	RasterB.mValue = nullptr;
	TextureReference.mValue = nullptr;
	Device = nullptr;
	arda::ShutdownBackend();
}

TEST(ArdaRHI, NativeImportRejectsNonPortableTransferredOwnership)
{
	using namespace arda;
	arda::ShutdownBackend();
	arda::FArdaBackendConfiguration Configuration;
	Configuration.mbEnableValidation = false;
	ASSERT_TRUE(ConfigureLinkedBackend(Configuration));
	if (!arda::InitializeBackend())
	{
		GTEST_SKIP() << arda::GetBackendError().c_str();
	}

	arda::FArdaRHINativeTextureImportDesc Desc;
	Desc.mNativeObject = 1;
	Desc.mOwnership = arda::EArdaRHINativeOwnership::Transferred;
	Desc.mTexture.mFormat = arda::EArdaRHIFormat::RGBA8UNorm;
	const auto Result = arda::GetDevice()->ImportNativeTexture(Desc);
	EXPECT_FALSE(Result);
	EXPECT_EQ(Result.mStatus.mCode, arda::EArdaRHIResult::Unsupported);
	arda::ShutdownBackend();
}

TEST(ArdaRHI, NativeBufferImportValidationIsDeterministic)
{
	using namespace arda;
	arda::ShutdownBackend();
	FBackendShutdownGuard Shutdown;
	arda::FArdaBackendConfiguration Configuration;
	Configuration.mbEnableValidation = false;
	ASSERT_TRUE(ConfigureLinkedBackend(Configuration));
	if (!arda::InitializeBackend())
	{
		GTEST_SKIP() << arda::GetBackendError().c_str();
	}

	const arda::FArdaRHIDeviceRef Device = arda::GetDevice();
	ASSERT_TRUE(Device);

	arda::FArdaRHINativeBufferImportDesc Null;
	Null.mBuffer.mByteSize = 64;
	auto NullResult = Device->ImportNativeBuffer(Null);
	EXPECT_FALSE(NullResult);
	EXPECT_EQ(NullResult.mStatus.mCode, arda::EArdaRHIResult::InvalidArgument);
	EXPECT_NE(NullResult.mStatus.mMessage.find("null"), eastl::string::npos);

	auto Transferred = Null;
	Transferred.mNativeObject = 1;
	Transferred.mOwnership = arda::EArdaRHINativeOwnership::Transferred;
	auto TransferredResult = Device->ImportNativeBuffer(Transferred);
	EXPECT_FALSE(TransferredResult);
	EXPECT_EQ(TransferredResult.mStatus.mCode, arda::EArdaRHIResult::Unsupported);

	auto InvalidDescriptor = Null;
	InvalidDescriptor.mNativeObject = 1;
	InvalidDescriptor.mBuffer.mByteSize = 0;
	auto InvalidDescriptorResult = Device->ImportNativeBuffer(InvalidDescriptor);
	EXPECT_FALSE(InvalidDescriptorResult);
	EXPECT_EQ(InvalidDescriptorResult.mStatus.mCode, arda::EArdaRHIResult::InvalidArgument);

	auto WrongType = Null;
	WrongType.mNativeObject = 1;
	WrongType.mNativeType = arda::GetBackendConfiguration().mBackendName == "native-d3d12"
	    ? arda::EArdaRHINativeResourceType::VulkanBuffer
	    : arda::EArdaRHINativeResourceType::D3D12Resource;
	auto WrongTypeResult = Device->ImportNativeBuffer(WrongType);
	EXPECT_FALSE(WrongTypeResult);
	EXPECT_EQ(WrongTypeResult.mStatus.mCode, arda::EArdaRHIResult::Unsupported);
	EXPECT_NE(WrongTypeResult.mStatus.mMessage.find("does not match"), eastl::string::npos);
}

TEST(ArdaRHI, BindingItemsRetainTheirResources)
{
	using namespace arda;
	std::atomic<int> Destructions{0};
	TArdaRHIRef<IArdaRHIResource> Resource(new FFakeResource(Destructions));

	{
		FArdaRHIBindingItem Item;
		Item.mResource = Resource;
		Resource.Reset();
		EXPECT_EQ(Destructions.load(), 0);
	}

	EXPECT_EQ(Destructions.load(), 1);
}

TEST(ArdaRHI, QueueCapabilitiesUseArdaQueueTypes)
{
	arda::FArdaRHICapabilities Capabilities;
	Capabilities.mQueues.mbCompute = true;

	EXPECT_TRUE(Capabilities.IsQueueSupported(arda::EArdaRHIQueueType::Graphics));
	EXPECT_TRUE(Capabilities.IsQueueSupported(arda::EArdaRHIQueueType::Compute));
	EXPECT_FALSE(Capabilities.IsQueueSupported(arda::EArdaRHIQueueType::Copy));
	EXPECT_TRUE(Capabilities.mQueues.mbCompute);
}

TEST(ArdaRHI, AdvancedResourceDescriptorsRemainBackendOpaque)
{
	using namespace arda;

	FArdaRHIAccelStructDesc AccelStruct;
	AccelStruct.mbTopLevel = true;
	AccelStruct.mTopLevelMaxInstances = 16;
	AccelStruct.mBuildFlags =
	    EArdaRHIAccelStructBuildFlags::AllowUpdate | EArdaRHIAccelStructBuildFlags::PreferFastTrace;

	FArdaRHIBindlessLayoutDesc Bindless;
	Bindless.mVisibility = EArdaRHIShaderStage::Compute | EArdaRHIShaderStage::Pixel;
	Bindless.mMaxCapacity = 1024;
	Bindless.mRegisterSpaces.push_back({0, 1, EArdaRHIBindingType::TextureSRV});

	EXPECT_TRUE(HasAnyFlags(AccelStruct.mBuildFlags, EArdaRHIAccelStructBuildFlags::AllowUpdate));
	EXPECT_TRUE(HasAnyFlags(Bindless.mVisibility, EArdaRHIShaderStage::Compute));
	EXPECT_EQ(Bindless.mRegisterSpaces.size(), 1u);
	EXPECT_EQ(static_cast<uint16_t>(EArdaRHIShaderStage::RayGeneration), 0x100u);
}

TEST(ArdaRHI, CapabilityAdmissionReportsEveryMissingAdvancedAbility)
{
	using namespace arda;
	FArdaRHIFeatureRequirements Requirements;
	Requirements.mbRequireRayTracingInfrastructure = true;
	Requirements.mbRequireHardwareRayTracing = true;
	Requirements.mbRequireRayTracingPipelines = true;
	Requirements.mbRequireAccelerationStructures = true;
	Requirements.mbRequireAccelerationStructureUpdate = true;
	Requirements.mbRequireAccelerationStructureCompaction = true;
	Requirements.mbRequireIndirectRayDispatch = true;
	Requirements.mbRequireLocalShaderTableArguments = true;
	Requirements.mbRequireOpacityMicromaps = true;
	Requirements.mbRequireMeshShaders = true;
	Requirements.mbRequireUnboundedDescriptors = true;
	Requirements.mbRequireUpdateAfterBind = true;
	Requirements.mbRequireDirectDescriptorIndexing = true;
	Requirements.mbRequireDedicatedComputeQueue = true;
	Requirements.mbRequireDedicatedCopyQueue = true;
	Requirements.mbRequireGpuQueueWaits = true;
	Requirements.mbRequireSparseResidency = true;
	Requirements.mbRequireStreamingBudget = true;
	Requirements.mbRequireSamplerFeedback = true;
	Requirements.mbRequireWorkGraphs = true;
	Requirements.mbRequireShaderBundles = true;
	Requirements.mbRequireCustomPresent = true;
	Requirements.mbRequireNativeFloat16 = true;
	Requirements.mbRequireNativeInt8 = true;

	const FArdaRHICapabilities Empty;
	const auto Report = Empty.Evaluate(Requirements);
	EXPECT_FALSE(Report.IsSupported());
	EXPECT_EQ(Report.mMissingAbilities.size(), 24u);
	const auto Status = Report.ToStatus();
	EXPECT_EQ(Status.mCode, EArdaRHIResult::Unsupported);
	for (const char* Name : {"ray-tracing infrastructure",
	         "hardware ray tracing",
	         "ray-tracing pipelines",
	         "acceleration structures",
	         "acceleration-structure update",
	         "acceleration-structure compaction",
	         "indirect ray dispatch",
	         "local shader-table arguments",
	         "opacity micromaps",
	         "mesh shaders",
	         "unbounded descriptors",
	         "descriptor update-after-bind",
	         "direct descriptor indexing",
	         "dedicated compute queue",
	         "dedicated copy queue",
	         "GPU queue waits",
	         "sparse residency",
	         "streaming budget telemetry",
	         "native sampler feedback",
	         "work graphs",
	         "shader bundles",
	         "custom present",
	         "native float16",
	         "native int8"})
	{
		EXPECT_NE(Status.mMessage.find(Name), eastl::string::npos) << Name;
	}
}

TEST(ArdaRHI, CapabilityAdmissionAcceptsCompleteAdvancedDesktopProfile)
{
	using namespace arda;
	FArdaRHICapabilities Capabilities;
	Capabilities.mRayTracing.mbInfrastructure = true;
	Capabilities.mRayTracing.mbHardwareAccelerated = true;
	Capabilities.mRayTracing.mbPipelineShaders = true;
	Capabilities.mRayTracing.mbAccelerationStructures = true;
	Capabilities.mRayTracing.mbBuildUpdate = true;
	Capabilities.mRayTracing.mbCompaction = true;
	Capabilities.mRayTracing.mbIndirectDispatch = true;
	Capabilities.mRayTracing.mbLocalShaderTableArguments = true;
	Capabilities.mRayTracing.mbOpacityMicromaps = true;
	Capabilities.mMeshShaderTier = EArdaRHIMeshShaderTier::MeshAndAmplificationShaders;
	Capabilities.mDescriptors.mbUnboundedArrays = true;
	Capabilities.mDescriptors.mbUpdateAfterBind = true;
	Capabilities.mDescriptors.mbDirectResourceHeapIndexing = true;
	Capabilities.mQueues.mbDedicatedComputeFamily = true;
	Capabilities.mQueues.mbDedicatedCopyFamily = true;
	Capabilities.mQueues.mbGpuWaits = true;
	Capabilities.mResidency.mbSparseBinding = true;
	Capabilities.mResidency.mbStreamingBudget = true;
	Capabilities.mSamplerFeedbackTier = EArdaRHISamplerFeedbackTier::UnrestrictedAddressingAndViews;
	Capabilities.mWorkGraphTier = EArdaRHIWorkGraphTier::MeshNodes;
	Capabilities.mbShaderBundleDispatch = true;
	Capabilities.mbCustomPresent = true;
	Capabilities.mMachineLearning.mbNativeFloat16 = true;
	Capabilities.mMachineLearning.mbNativeInt8 = true;

	FArdaRHIFeatureRequirements Requirements;
	Requirements.mbRequireRayTracingInfrastructure = true;
	Requirements.mbRequireHardwareRayTracing = true;
	Requirements.mbRequireRayTracingPipelines = true;
	Requirements.mbRequireAccelerationStructures = true;
	Requirements.mbRequireAccelerationStructureUpdate = true;
	Requirements.mbRequireAccelerationStructureCompaction = true;
	Requirements.mbRequireIndirectRayDispatch = true;
	Requirements.mbRequireLocalShaderTableArguments = true;
	Requirements.mbRequireOpacityMicromaps = true;
	Requirements.mbRequireMeshShaders = true;
	Requirements.mbRequireUnboundedDescriptors = true;
	Requirements.mbRequireUpdateAfterBind = true;
	Requirements.mbRequireDirectDescriptorIndexing = true;
	Requirements.mbRequireDedicatedComputeQueue = true;
	Requirements.mbRequireDedicatedCopyQueue = true;
	Requirements.mbRequireGpuQueueWaits = true;
	Requirements.mbRequireSparseResidency = true;
	Requirements.mbRequireStreamingBudget = true;
	Requirements.mbRequireSamplerFeedback = true;
	Requirements.mbRequireWorkGraphs = true;
	Requirements.mbRequireShaderBundles = true;
	Requirements.mbRequireCustomPresent = true;
	Requirements.mbRequireNativeFloat16 = true;
	Requirements.mbRequireNativeInt8 = true;

	const auto Report = Capabilities.Evaluate(Requirements);
	EXPECT_TRUE(Report.IsSupported());
	EXPECT_TRUE(Report.ToStatus());
	EXPECT_TRUE(Report.mMissingAbilities.empty());
}
