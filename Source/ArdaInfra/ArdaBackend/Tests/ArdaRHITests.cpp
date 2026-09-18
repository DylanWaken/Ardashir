#include "ArdaTestValidation.h"
#include "ArdaBackend.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "RHI/Config/ArdaRHICapabilities.h"
#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "ArdaBackend.h"

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

	struct FArdaBackendShutdownGuard
	{
		~FArdaBackendShutdownGuard()
		{
			arda::ShutdownBackend();
		}
	};

	class FArdaFakeResource final : public arda::IArdaRHIResource
	{
	public:
		explicit FArdaFakeResource(std::atomic<int>& Destructions)
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
		~FArdaFakeResource() override
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

	TArdaRHIRef<IArdaRHIResource> A(new FArdaFakeResource(Destructions));
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

TEST(ArdaRHI, TextureDescriptorsRejectInvalidDomainsAndShapes)
{
	using namespace arda;
	FArdaRHITextureDesc Base;
	Base.mWidth = 16;
	Base.mHeight = 8;
	Base.mFormat = EArdaRHIFormat::RGBA8UNorm;
	ASSERT_TRUE(Validate(Base));
	const auto Reject = [&Base](auto Change)
	{
		auto Desc = Base;
		Change(Desc);
		EXPECT_EQ(Validate(Desc).mCode, EArdaRHIResult::InvalidArgument);
	};
	Reject(
	    [](auto& D)
	    {
		    D.mDimension = static_cast<EArdaRHITextureDimension>(255);
	    });
	Reject(
	    [](auto& D)
	    {
		    D.mFormat = EArdaRHIFormat::Count;
	    });
	Reject(
	    [](auto& D)
	    {
		    D.mUsage = static_cast<EArdaRHITextureUsage>(1u << 15);
	    });
	Reject(
	    [](auto& D)
	    {
		    D.mInitialState = EArdaRHIResourceState::VertexBuffer;
	    });
	Reject(
	    [](auto& D)
	    {
		    D.mWidth = 0;
	    });
	Reject(
	    [](auto& D)
	    {
		    D.mDepth = 2;
	    });
	Reject(
	    [](auto& D)
	    {
		    D.mArraySize = 2;
	    });
	Reject(
	    [](auto& D)
	    {
		    D.mDimension = EArdaRHITextureDimension::Texture1D;
	    });
	Reject(
	    [](auto& D)
	    {
		    D.mbVirtual = D.mbTiled = true;
	    });

	Base.mDimension = EArdaRHITextureDimension::Texture2DArray;
	Base.mArraySize = 3;
	EXPECT_TRUE(Validate(Base));
	Base.mDimension = EArdaRHITextureDimension::Texture3D;
	Base.mArraySize = 1;
	Base.mDepth = 3;
	EXPECT_TRUE(Validate(Base));
	Base.mArraySize = 2;
	EXPECT_FALSE(Validate(Base));
}

TEST(ArdaRHI, TextureDescriptorsBoundMipChainsAndCubeFaces)
{
	using namespace arda;
	FArdaRHITextureDesc Desc;
	Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
	Desc.mWidth = 9;
	Desc.mHeight = 5;
	Desc.mMipLevels = 4;
	EXPECT_TRUE(Validate(Desc));
	Desc.mMipLevels = 5;
	EXPECT_FALSE(Validate(Desc));
	Desc.mWidth = UINT32_MAX;
	Desc.mMipLevels = 32;
	EXPECT_TRUE(Validate(Desc));
	Desc.mMipLevels = 33;
	EXPECT_FALSE(Validate(Desc));

	Desc.mDimension = EArdaRHITextureDimension::TextureCube;
	Desc.mWidth = Desc.mHeight = 8;
	Desc.mMipLevels = 4;
	Desc.mArraySize = 6;
	EXPECT_TRUE(Validate(Desc));
	Desc.mWidth = 7;
	EXPECT_FALSE(Validate(Desc));
	Desc.mWidth = 8;
	Desc.mArraySize = 12;
	EXPECT_FALSE(Validate(Desc));
	Desc.mDimension = EArdaRHITextureDimension::TextureCubeArray;
	EXPECT_TRUE(Validate(Desc));
	Desc.mArraySize = 7;
	EXPECT_FALSE(Validate(Desc));
}

TEST(ArdaRHI, TextureDescriptorsValidateMultisamplingWithoutDroppingLegacyTwoDimensionalForms)
{
	using namespace arda;
	FArdaRHITextureDesc Desc;
	Desc.mWidth = Desc.mHeight = 8;
	Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
	Desc.mSampleCount = 4;
	EXPECT_TRUE(Validate(Desc));
	Desc.mDimension = EArdaRHITextureDimension::Texture2DMS;
	EXPECT_TRUE(Validate(Desc));
	Desc.mSampleCount = 1;
	EXPECT_FALSE(Validate(Desc));
	Desc.mSampleCount = 3;
	EXPECT_FALSE(Validate(Desc));
	Desc.mSampleCount = 4;
	Desc.mMipLevels = 2;
	EXPECT_FALSE(Validate(Desc));
	Desc.mMipLevels = 1;
	Desc.mDimension = EArdaRHITextureDimension::Texture3D;
	EXPECT_FALSE(Validate(Desc));
	Desc.mDimension = EArdaRHITextureDimension::Texture2D;
	Desc.mFormat = EArdaRHIFormat::BC1UNorm;
	EXPECT_FALSE(Validate(Desc));
}

TEST(ArdaRHI, TextureDescriptorsValidateFormatUsage)
{
	using namespace arda;
	FArdaRHITextureDesc Desc;
	Desc.mFormat = EArdaRHIFormat::D24S8;
	Desc.mUsage = EArdaRHITextureUsage::DepthStencil | EArdaRHITextureUsage::ShaderResource;
	EXPECT_TRUE(Validate(Desc));
	Desc.mUsage |= EArdaRHITextureUsage::RenderTarget;
	EXPECT_FALSE(Validate(Desc));
	Desc.mUsage = EArdaRHITextureUsage::UnorderedAccess;
	EXPECT_FALSE(Validate(Desc));
	Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
	EXPECT_TRUE(Validate(Desc));
	Desc.mUsage = EArdaRHITextureUsage::DepthStencil;
	EXPECT_FALSE(Validate(Desc));
	Desc.mFormat = EArdaRHIFormat::BC1UNorm;
	Desc.mUsage = EArdaRHITextureUsage::ShaderResource;
	EXPECT_TRUE(Validate(Desc));
	Desc.mUsage |= EArdaRHITextureUsage::RenderTarget;
	EXPECT_FALSE(Validate(Desc));
}

TEST(ArdaRHI, BufferDescriptorsRejectInvalidDomainsAndStructuredStrides)
{
	using namespace arda;
	FArdaRHIBufferDesc Desc;
	Desc.mByteSize = 68;
	Desc.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::ShaderResource;
	Desc.mStructureStride = 16;
	EXPECT_TRUE(Validate(Desc)); // Allocation padding need not be a complete structured element.
	for (uint32_t Stride : {0u, 3u, 72u})
	{
		Desc.mStructureStride = Stride;
		EXPECT_FALSE(Validate(Desc));
	}
	Desc.mStructureStride = 16;
	Desc.mUsage |= EArdaRHIBufferUsage::Raw;
	EXPECT_TRUE(Validate(Desc)); // One allocation can back both raw and structured views.
	Desc.mFormat = EArdaRHIFormat::Count;
	EXPECT_FALSE(Validate(Desc));
	Desc.mFormat = EArdaRHIFormat::D32;
	EXPECT_FALSE(Validate(Desc));
	Desc.mFormat = EArdaRHIFormat::BC1UNorm;
	EXPECT_FALSE(Validate(Desc));
	Desc.mFormat = EArdaRHIFormat::Unknown;
	Desc.mCpuAccess = static_cast<EArdaRHICpuAccess>(255);
	EXPECT_FALSE(Validate(Desc));
	Desc.mCpuAccess = EArdaRHICpuAccess::None;
	Desc.mUsage = static_cast<EArdaRHIBufferUsage>(1u << 15);
	EXPECT_FALSE(Validate(Desc));
	Desc.mUsage = EArdaRHIBufferUsage::None;
	Desc.mInitialState = EArdaRHIResourceState::RenderTarget;
	EXPECT_FALSE(Validate(Desc));
}

TEST(ArdaRHI, BufferDescriptorsRespectCpuHeapAndStorageContracts)
{
	using namespace arda;
	FArdaRHIBufferDesc Desc;
	Desc.mByteSize = 256;
	Desc.mCpuAccess = EArdaRHICpuAccess::Write;
	Desc.mUsage = EArdaRHIBufferUsage::Vertex | EArdaRHIBufferUsage::AccelStructBuildInput;
	Desc.mInitialState = EArdaRHIResourceState::AccelStructBuildInput;
	EXPECT_TRUE(Validate(Desc));
	Desc.mUsage |= EArdaRHIBufferUsage::UnorderedAccess;
	EXPECT_FALSE(Validate(Desc));
	Desc.mUsage = EArdaRHIBufferUsage::None;
	Desc.mInitialState = EArdaRHIResourceState::CopyDest;
	EXPECT_FALSE(Validate(Desc));
	Desc.mCpuAccess = EArdaRHICpuAccess::Read;
	EXPECT_TRUE(Validate(Desc));
	Desc.mInitialState = EArdaRHIResourceState::CopySource;
	EXPECT_FALSE(Validate(Desc));
	Desc.mInitialState = EArdaRHIResourceState::Common;
	Desc.mUsage = EArdaRHIBufferUsage::Constant;
	EXPECT_FALSE(Validate(Desc));
	Desc.mUsage = EArdaRHIBufferUsage::None;
	Desc.mbTiled = true;
	EXPECT_FALSE(Validate(Desc));
	Desc.mCpuAccess = EArdaRHICpuAccess::None;
	EXPECT_TRUE(Validate(Desc));
	Desc.mbVirtual = true;
	EXPECT_FALSE(Validate(Desc));
	Desc.mbTiled = Desc.mbVirtual = false;
	Desc.mUsage = EArdaRHIBufferUsage::Constant | EArdaRHIBufferUsage::Volatile;
	EXPECT_FALSE(Validate(Desc));
	Desc.mMaxVersions = 3;
	EXPECT_TRUE(Validate(Desc));
}

TEST(ArdaRHI, TextureAdmissionUsesDimensionSpecificLimitsAndAuthoritativeFormatFacts)
{
	using namespace arda;
	FArdaRHICapabilities C;
	C.mLimits.mMaxTexture2D = 8;
	C.mLimits.mMaxTexture3D = 4;
	C.mLimits.mMaxTextureArrayLayers = 6;
	FArdaRHITextureDesc Desc;
	Desc.mWidth = Desc.mHeight = 8;
	Desc.mFormat = EArdaRHIFormat::RGBA8UNorm;
	EXPECT_TRUE(ValidateResourceCapabilities(Desc, C, {}));
	Desc.mWidth = 9;
	EXPECT_EQ(ValidateResourceCapabilities(Desc, C, {}).mCode, EArdaRHIResult::Unsupported);
	Desc.mWidth = 8;
	Desc.mDimension = EArdaRHITextureDimension::Texture3D;
	EXPECT_FALSE(ValidateResourceCapabilities(Desc, C, {}));
	Desc.mWidth = Desc.mHeight = Desc.mDepth = 4;
	EXPECT_TRUE(ValidateResourceCapabilities(Desc, C, {}));
	Desc.mDimension = EArdaRHITextureDimension::Texture2DArray;
	Desc.mDepth = 1;
	Desc.mArraySize = 7;
	EXPECT_FALSE(ValidateResourceCapabilities(Desc, C, {}));
	Desc.mArraySize = 6;
	C.mbFormatSupportReported = true;
	EXPECT_FALSE(ValidateResourceCapabilities(Desc, C, {}));
	FArdaRHIFormatSupport Format;
	Format.mNativeFormat = 1;
	Format.mbTexture2D = Format.mbShaderResource = true;
	Format.mSampleCounts = 1 | 4;
	EXPECT_TRUE(ValidateResourceCapabilities(Desc, C, Format));
	Desc.mSampleCount = 2;
	EXPECT_FALSE(ValidateResourceCapabilities(Desc, C, Format));
	Desc.mSampleCount = 4;
	EXPECT_TRUE(ValidateResourceCapabilities(Desc, C, Format));
	Desc.mUsage |= EArdaRHITextureUsage::UnorderedAccess;
	EXPECT_FALSE(ValidateResourceCapabilities(Desc, C, Format));
	Format.mbStorage = true;
	EXPECT_TRUE(ValidateResourceCapabilities(Desc, C, Format));
	Desc.mWidth = 0;
	EXPECT_EQ(ValidateResourceCapabilities(Desc, C, Format).mCode, EArdaRHIResult::InvalidArgument);
}

TEST(ArdaRHI, BufferAdmissionSeparatesAllocationLimitsFromBindingRanges)
{
	using namespace arda;
	FArdaRHICapabilities C;
	C.mLimits.mMaxBufferSize = 1024;
	C.mLimits.mMaxUniformBufferRange = 256;
	C.mLimits.mMaxStorageBufferRange = 256;
	FArdaRHIBufferDesc Desc;
	Desc.mByteSize = 1024;
	Desc.mUsage = EArdaRHIBufferUsage::Constant;
	EXPECT_TRUE(ValidateResourceCapabilities(Desc, C, {}));
	Desc.mByteSize = 1025;
	EXPECT_EQ(ValidateResourceCapabilities(Desc, C, {}).mCode, EArdaRHIResult::Unsupported);
	Desc.mByteSize = 1024;
	Desc.mUsage = EArdaRHIBufferUsage::ShaderResource;
	Desc.mFormat = EArdaRHIFormat::R32UInt;
	C.mbFormatSupportReported = true;
	FArdaRHIFormatSupport Format;
	EXPECT_FALSE(ValidateResourceCapabilities(Desc, C, Format));
	Format.mNativeFormat = 1;
	Format.mbBufferShaderResource = true;
	EXPECT_TRUE(ValidateResourceCapabilities(Desc, C, Format));
	Desc.mUsage |= EArdaRHIBufferUsage::UnorderedAccess;
	EXPECT_FALSE(ValidateResourceCapabilities(Desc, C, Format));
	Format.mbBufferStorage = true;
	EXPECT_TRUE(ValidateResourceCapabilities(Desc, C, Format));
	Desc.mUsage |= EArdaRHIBufferUsage::Structured;
	Desc.mStructureStride = 16;
	EXPECT_TRUE(ValidateResourceCapabilities(Desc, C, {}));
}

TEST(ArdaRHI, ResourceAdmissionRequiresReportedStorageFeatures)
{
	using namespace arda;
	FArdaRHICapabilities C;
	FArdaRHIBufferDesc Buffer;
	Buffer.mByteSize = 256;
	Buffer.mbTiled = true;
	EXPECT_FALSE(ValidateResourceCapabilities(Buffer, C, {}));
	C.mResidency.mbReservedBuffers = true;
	EXPECT_TRUE(ValidateResourceCapabilities(Buffer, C, {}));
	FArdaRHITextureDesc Texture;
	Texture.mFormat = EArdaRHIFormat::RGBA8UNorm;
	Texture.mbTiled = true;
	EXPECT_FALSE(ValidateResourceCapabilities(Texture, C, {}));
	C.mResidency.mbReservedTexture2D = true;
	EXPECT_TRUE(ValidateResourceCapabilities(Texture, C, {}));
	Texture.mDimension = EArdaRHITextureDimension::Texture3D;
	EXPECT_FALSE(ValidateResourceCapabilities(Texture, C, {}));
	C.mResidency.mbReservedTexture3D = true;
	EXPECT_TRUE(ValidateResourceCapabilities(Texture, C, {}));
	Texture.mbTiled = false;
	Texture.mbVirtual = true;
	EXPECT_FALSE(ValidateResourceCapabilities(Texture, C, {}));
	C.mbVirtualResources = true;
	EXPECT_TRUE(ValidateResourceCapabilities(Texture, C, {}));
}

TEST(ArdaRHI, VertexAttributesRejectInvalidFormatsAndOverflowingRanges)
{
	using namespace arda;
	FArdaRHIVertexAttributeDesc Attribute;
	Attribute.mSemanticName = "POSITION";
	Attribute.mFormat = EArdaRHIFormat::RGB32Float;
	Attribute.mElementStride = 32;
	Attribute.mOffset = 20;
	EXPECT_TRUE(Validate(Attribute));
	Attribute.mOffset = 21;
	EXPECT_FALSE(Validate(Attribute));
	Attribute.mOffset = UINT32_MAX;
	EXPECT_FALSE(Validate(Attribute));
	Attribute.mOffset = 0;
	Attribute.mArraySize = UINT32_MAX;
	EXPECT_FALSE(Validate(Attribute));
	Attribute.mArraySize = 1;
	for (auto Format : {EArdaRHIFormat::D32, EArdaRHIFormat::BC1UNorm, EArdaRHIFormat::Count})
	{
		Attribute.mFormat = Format;
		EXPECT_FALSE(Validate(Attribute));
	}
}

TEST(ArdaRHI, VertexAttributeAlignmentUsesComponentsAndPackedElementSizes)
{
	using namespace arda;
	FArdaRHIVertexAttributeDesc Attribute;
	Attribute.mSemanticName = "TEXCOORD";
	for (const auto Format : {EArdaRHIFormat::RGBA8UNorm,
	         EArdaRHIFormat::RGBA16Float,
	         EArdaRHIFormat::RGB32Float,
	         EArdaRHIFormat::R10G10B10A2UNorm})
	{
		Attribute.mFormat = Format;
		const uint32_t Alignment = Format == EArdaRHIFormat::RGBA8UNorm ? 1
		    : Format == EArdaRHIFormat::RGBA16Float                     ? 2
		                                                                : 4;
		EXPECT_EQ(GetArdaRHIVertexFormatAlignment(Format), Alignment);
		Attribute.mOffset = Alignment;
		Attribute.mElementStride = GetArdaRHIFormatElementSize(Format) + Alignment;
		EXPECT_TRUE(Validate(Attribute));
		if (Alignment > 1)
		{
			--Attribute.mOffset;
			EXPECT_FALSE(Validate(Attribute));
			++Attribute.mOffset;
			++Attribute.mElementStride;
			EXPECT_FALSE(Validate(Attribute));
		}
	}
	for (const auto Format :
	    {EArdaRHIFormat::Unknown, EArdaRHIFormat::D16, EArdaRHIFormat::BC1UNorm, EArdaRHIFormat::Count})
	{
		EXPECT_EQ(GetArdaRHIVertexFormatAlignment(Format), 0u);
	}
}

TEST(ArdaRHI, InitialStatesRequireDeclaredUsageWithoutRestrictingCopyOnlyStorage)
{
	using namespace arda;
	FArdaRHIBufferDesc Buffer;
	Buffer.mByteSize = 64;
	Buffer.mUsage = EArdaRHIBufferUsage::None;
	for (const auto State : {EArdaRHIResourceState::Unknown,
	         EArdaRHIResourceState::Common,
	         EArdaRHIResourceState::CopySource,
	         EArdaRHIResourceState::CopyDest})
	{
		Buffer.mInitialState = State;
		EXPECT_TRUE(Validate(Buffer));
	}
	Buffer.mCpuAccess = EArdaRHICpuAccess::Write;
	Buffer.mInitialState = EArdaRHIResourceState::CopySource;
	Buffer.mbKeepInitialState = true;
	EXPECT_TRUE(Validate(Buffer));
	Buffer.mCpuAccess = EArdaRHICpuAccess::Read;
	Buffer.mInitialState = EArdaRHIResourceState::CopyDest;
	EXPECT_TRUE(Validate(Buffer));
	Buffer.mCpuAccess = EArdaRHICpuAccess::None;
	for (const auto State : {EArdaRHIResourceState::ShaderResource,
	         EArdaRHIResourceState::UnorderedAccess,
	         EArdaRHIResourceState::VertexBuffer,
	         EArdaRHIResourceState::IndexBuffer,
	         EArdaRHIResourceState::ConstantBuffer,
	         EArdaRHIResourceState::IndirectArgument})
	{
		Buffer.mInitialState = State;
		EXPECT_FALSE(Validate(Buffer));
	}
	Buffer.mInitialState = EArdaRHIResourceState::VertexBuffer | EArdaRHIResourceState::ShaderResource;
	Buffer.mUsage = EArdaRHIBufferUsage::Vertex | EArdaRHIBufferUsage::ShaderResource;
	EXPECT_TRUE(Validate(Buffer));
	FArdaRHITextureDesc Texture;
	Texture.mFormat = EArdaRHIFormat::RGBA8UNorm;
	Texture.mUsage = EArdaRHITextureUsage::None;
	Texture.mInitialState = EArdaRHIResourceState::CopyDest;
	EXPECT_TRUE(Validate(Texture));
	for (const auto State : {EArdaRHIResourceState::ShaderResource,
	         EArdaRHIResourceState::UnorderedAccess,
	         EArdaRHIResourceState::RenderTarget,
	         EArdaRHIResourceState::DepthRead,
	         EArdaRHIResourceState::DepthWrite})
	{
		Texture.mInitialState = State;
		EXPECT_FALSE(Validate(Texture));
	}
	Texture.mInitialState = EArdaRHIResourceState::RenderTarget;
	Texture.mUsage = EArdaRHITextureUsage::RenderTarget;
	EXPECT_TRUE(Validate(Texture));
}

TEST(ArdaRHI, FramebufferAttachmentResolutionPreservesDefaultMipAndDepthStencilAspects)
{
	using namespace arda;
	FArdaRHITextureDesc Texture;
	Texture.mWidth = Texture.mHeight = 16;
	Texture.mMipLevels = 5;
	Texture.mArraySize = 4;
	Texture.mDimension = EArdaRHITextureDimension::Texture2DArray;
	Texture.mFormat = EArdaRHIFormat::D24S8;
	Texture.mUsage = EArdaRHITextureUsage::DepthStencil;
	FArdaRHIFramebufferAttachment Attachment, Resolved;
	Attachment.mSubresources.mBaseMipLevel = 2;
	Attachment.mSubresources.mBaseArraySlice = 1;
	Attachment.mbReadOnly = true;
	ASSERT_TRUE(ResolveArdaRHIFramebufferAttachment(Texture, Attachment, true, Resolved));
	EXPECT_EQ(Resolved.mFormat, Texture.mFormat);
	EXPECT_EQ(Resolved.mSubresources.mBaseMipLevel, 2u);
	EXPECT_EQ(Resolved.mSubresources.mMipLevelCount, 1u);
	EXPECT_EQ(Resolved.mSubresources.mArraySliceCount, 3u);
	EXPECT_EQ(Resolved.mSubresources.mPlaneCount, 2u);
	EXPECT_TRUE(Resolved.mbReadOnly);
	Attachment.mSubresources.mMipLevelCount = 2;
	EXPECT_FALSE(ResolveArdaRHIFramebufferAttachment(Texture, Attachment, true, Resolved));
	Attachment.mSubresources.mMipLevelCount = 1;
	Attachment.mSubresources.mBasePlane = 1;
	Attachment.mSubresources.mPlaneCount = 1;
	EXPECT_EQ(ResolveArdaRHIFramebufferAttachment(Texture, Attachment, true, Resolved).mCode,
	    EArdaRHIResult::Unsupported);
	Attachment.mSubresources.mBasePlane = 0;
	Attachment.mSubresources.mArraySliceCount = UINT32_MAX - 1;
	EXPECT_FALSE(ResolveArdaRHIFramebufferAttachment(Texture, Attachment, true, Resolved));
}

TEST(ArdaRHI, FramebufferAttachmentFormatAndUsageAreValidatedBeforeNativeCreation)
{
	using namespace arda;
	FArdaRHITextureDesc Texture;
	Texture.mFormat = EArdaRHIFormat::RGBA8UNorm;
	FArdaRHIFramebufferAttachment Attachment, Resolved;
	EXPECT_FALSE(ResolveArdaRHIFramebufferAttachment(Texture, Attachment, false, Resolved));
	Texture.mUsage = EArdaRHITextureUsage::RenderTarget;
	ASSERT_TRUE(ResolveArdaRHIFramebufferAttachment(Texture, Attachment, false, Resolved));
	Attachment.mbReadOnly = true;
	EXPECT_FALSE(ResolveArdaRHIFramebufferAttachment(Texture, Attachment, false, Resolved));
	Attachment.mbReadOnly = false;
	Attachment.mFormat = EArdaRHIFormat::SRGBA8UNorm;
	EXPECT_FALSE(ResolveArdaRHIFramebufferAttachment(Texture, Attachment, false, Resolved));
	Texture.mUsage |= EArdaRHITextureUsage::Typeless;
	EXPECT_TRUE(ResolveArdaRHIFramebufferAttachment(Texture, Attachment, false, Resolved));
	EXPECT_TRUE(IsArdaRHITextureViewFormatCompatible(Texture, EArdaRHIFormat::RGBA8UInt));
	EXPECT_FALSE(IsArdaRHITextureViewFormatCompatible(Texture, EArdaRHIFormat::BGRA8UNorm));
	EXPECT_FALSE(IsArdaRHITextureViewFormatCompatible(Texture, EArdaRHIFormat::R32Float));
	EXPECT_FALSE(IsArdaRHITextureViewFormatCompatible(Texture, EArdaRHIFormat::Count));
	EXPECT_FALSE(ResolveArdaRHIFramebufferAttachment(Texture, Attachment, true, Resolved));
	Attachment.mSubresources.mMipLevelCount = 0;
	EXPECT_FALSE(ResolveArdaRHIFramebufferAttachment(Texture, Attachment, false, Resolved));
}

TEST(ArdaRHI, InputLayoutsRequireConsistentBindingStrideAndInstanceRate)
{
	using namespace arda;
	FArdaRHIVertexAttributeDesc Position;
	Position.mSemanticName = "POSITION";
	Position.mFormat = EArdaRHIFormat::RGB32Float;
	Position.mElementStride = 24;
	auto Normal = Position;
	Normal.mSemanticName = "NORMAL";
	Normal.mOffset = 12;
	FArdaRHIInputLayoutDesc Layout;
	Layout.mAttributes = {Position, Normal};
	EXPECT_TRUE(Validate(Layout));
	Layout.mAttributes[1].mElementStride = 28;
	EXPECT_FALSE(Validate(Layout));
	Layout.mAttributes[1].mElementStride = 24;
	Layout.mAttributes[1].mbInstanced = true;
	EXPECT_FALSE(Validate(Layout));
	Layout.mAttributes[1].mBufferIndex = 1;
	EXPECT_TRUE(Validate(Layout));
}

TEST(ArdaRHI, EqualDescriptorsHashSignedZeroIdentically)
{
	using namespace arda;
	for (auto Channel : {&FArdaRHIColor::mR, &FArdaRHIColor::mG, &FArdaRHIColor::mB, &FArdaRHIColor::mA})
	{
		FArdaRHITextureDesc Positive;
		Positive.mClearValue.*Channel = 0.0f;
		auto Negative = Positive;
		Negative.mClearValue.*Channel = -0.0f;
		ASSERT_EQ(Positive, Negative);
		EXPECT_EQ(HashValue(Positive), HashValue(Negative));

		FArdaRHISamplerDesc Sampler;
		Sampler.mBorderColor.*Channel = 0.0f;
		auto NegativeSampler = Sampler;
		NegativeSampler.mBorderColor.*Channel = -0.0f;
		ASSERT_EQ(Sampler, NegativeSampler);
		EXPECT_EQ(HashValue(Sampler), HashValue(NegativeSampler));
	}
	for (auto Field : {&FArdaRHISamplerDesc::mMaxAnisotropy, &FArdaRHISamplerDesc::mMipBias})
	{
		FArdaRHISamplerDesc Positive;
		Positive.*Field = 0.0f;
		auto Negative = Positive;
		Negative.*Field = -0.0f;
		ASSERT_EQ(Positive, Negative);
		EXPECT_EQ(HashValue(Positive), HashValue(Negative));
	}
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
	arda::FArdaBackendConfiguration Configuration = arda::MakeArdaTestBackendConfiguration();
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
	arda::FArdaBackendConfiguration Configuration = arda::MakeArdaTestBackendConfiguration();
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
	FArdaBackendShutdownGuard Shutdown;
	arda::FArdaBackendConfiguration Configuration = arda::MakeArdaTestBackendConfiguration();
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
	TArdaRHIRef<IArdaRHIResource> Resource(new FArdaFakeResource(Destructions));

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
