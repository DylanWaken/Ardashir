/** Provider objects and translated resource/pipeline descriptors. */
#pragma once

#include "RHI/ArdaRHI.h"
#include <EASTL/shared_ptr.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

namespace arda
{
	class IArdaProviderObject
	{
	public:
		virtual ~IArdaProviderObject() = default;
		[[nodiscard]] virtual const void* GetIdentity() const noexcept = 0;

		/** Entire retained allocation, including parent heap capacity for placed resources. */
		[[nodiscard]] virtual FArdaRHIMemoryAllocationInfo GetMemoryAllocationInfo() const noexcept
		{
			return {};
		}

		[[nodiscard]] virtual FArdaCudaResourceInfo GetCudaResourceInfo() const noexcept
		{
			return {};
		}

		[[nodiscard]] virtual uint32_t GetDescriptorBaseIndex() const noexcept
		{
			return 0;
		}

		[[nodiscard]] virtual uint64_t GetWorkGraphBackingMemorySize() const noexcept
		{
			return 0;
		}
	};

	using FArdaProviderObjectRef = eastl::shared_ptr<IArdaProviderObject>;
	using FArdaProviderObjectResult = TArdaRHIResult<FArdaProviderObjectRef>;

	struct FArdaProviderCudaBinding
	{
		FArdaProviderObjectRef mObject;
		EArdaComputeAccess mAccess = EArdaComputeAccess::Read;
		EArdaComputeBindingType mType = EArdaComputeBindingType::Buffer;
		FArdaRHIBufferRange mBufferRange;
		uint32_t mMipLevel = 0;
	};

	struct FArdaProviderLifetimeStats
	{
		size_t mResourceDescriptors = 0;
		size_t mSamplerDescriptors = 0;
		size_t mDescriptorSets = 0;
		size_t mPendingSubmissions = 0;
	};

	struct FArdaProviderBinding
	{
		FArdaRHIBindingItem mItem;
		FArdaProviderObjectRef mObject;
	};

	struct FArdaProviderTextureTileMapping
	{
		eastl::vector<FArdaRHITiledTextureCoordinate> mCoordinates;
		eastl::vector<FArdaRHITiledTextureRegion> mRegions;
		eastl::vector<uint64_t> mByteOffsets;
		FArdaProviderObjectRef mHeap;
	};

	struct FArdaProviderBufferTileMapping
	{
		uint64_t mBufferOffset = 0;
		uint64_t mByteSize = 0;
		uint64_t mHeapOffset = 0;
		FArdaProviderObjectRef mHeap;
		bool mbCommit = true;
	};

	struct FArdaProviderFramebufferTarget
	{
		FArdaRHIFramebufferTarget mTarget;
		FArdaProviderObjectRef mTexture;
	};

	struct FArdaProviderFramebufferCreateInfo
	{
		const FArdaRHIFramebufferDesc& mDesc;
		eastl::vector<FArdaProviderFramebufferTarget> mColors;
		FArdaProviderFramebufferTarget mDepth;
	};

	struct FArdaProviderGraphicsPipelineCreateInfo
	{
		const FArdaRHIGraphicsPipelineDesc& mDesc;
		const FArdaRHIInputLayoutDesc* mInputLayout = nullptr;
		FArdaProviderObjectRef mVertexShader;
		FArdaProviderObjectRef mHullShader;
		FArdaProviderObjectRef mDomainShader;
		FArdaProviderObjectRef mGeometryShader;
		FArdaProviderObjectRef mPixelShader;
		eastl::vector<FArdaProviderObjectRef> mBindingLayouts;
	};

	struct FArdaProviderComputePipelineCreateInfo
	{
		const FArdaRHIComputePipelineDesc& mDesc;
		FArdaProviderObjectRef mComputeShader;
		eastl::vector<FArdaProviderObjectRef> mBindingLayouts;
	};

	struct FArdaProviderMeshletPipelineCreateInfo
	{
		const FArdaRHIMeshletPipelineDesc& mDesc;
		FArdaProviderObjectRef mAmplificationShader;
		FArdaProviderObjectRef mMeshShader;
		FArdaProviderObjectRef mPixelShader;
		eastl::vector<FArdaProviderObjectRef> mBindingLayouts;
	};

	struct FArdaProviderWorkGraphPipelineCreateInfo
	{
		FArdaRHIWorkGraphPipelineDesc mDesc;
		eastl::vector<FArdaProviderObjectRef> mShaders;
		eastl::vector<FArdaProviderObjectRef> mBindingLayouts;
	};

	struct FArdaProviderRayTracingShader
	{
		eastl::string mExportName;
		eastl::string mEntryPoint;
		FArdaProviderObjectRef mShader;
		FArdaProviderObjectRef mLocalBindingLayout;
	};

	struct FArdaProviderRayTracingHitGroup
	{
		eastl::string mExportName;
		FArdaProviderRayTracingShader mClosestHit;
		FArdaProviderRayTracingShader mAnyHit;
		FArdaProviderRayTracingShader mIntersection;
		FArdaProviderObjectRef mLocalBindingLayout;
		bool mbProceduralPrimitive = false;
	};

	struct FArdaProviderRayTracingPipelineCreateInfo
	{
		const FArdaRHIRayTracingPipelineDesc& mDesc;
		eastl::vector<FArdaProviderRayTracingShader> mShaders;
		eastl::vector<FArdaProviderRayTracingHitGroup> mHitGroups;
		eastl::vector<FArdaProviderObjectRef> mGlobalBindingLayouts;
	};

	struct FArdaProviderVertexBufferBinding
	{
		FArdaProviderObjectRef mBuffer;
		uint32_t mSlot = 0;
		uint64_t mOffset = 0;
		uint32_t mStride = 0;
		uint64_t mSize = 0;
	};

	struct FArdaProviderGraphicsState
	{
		FArdaProviderObjectRef mPipeline;
		FArdaProviderObjectRef mFramebuffer;
		eastl::vector<FArdaProviderObjectRef> mBindings;
		eastl::vector<FArdaProviderVertexBufferBinding> mVertexBuffers;
		FArdaProviderObjectRef mIndexBuffer;
		EArdaRHIFormat mIndexFormat = EArdaRHIFormat::R32UInt;
		uint64_t mIndexOffset = 0;
		eastl::vector<FArdaRHIViewport> mViewports;
		eastl::vector<FArdaRHIRect> mScissors;
	};

	struct FArdaProviderComputeState
	{
		FArdaProviderObjectRef mPipeline;
		eastl::vector<FArdaProviderObjectRef> mBindings;
	};

	struct FArdaProviderMeshletState
	{
		FArdaProviderObjectRef mPipeline;
		FArdaProviderObjectRef mFramebuffer;
		eastl::vector<FArdaProviderObjectRef> mBindings;
		eastl::vector<FArdaRHIViewport> mViewports;
		eastl::vector<FArdaRHIRect> mScissors;
	};

	struct FArdaProviderRayTracingState
	{
		FArdaProviderObjectRef mShaderTable;
		eastl::vector<FArdaProviderObjectRef> mBindings;
	};

	/** Backend-resolved BLAS geometry. Facade references never cross this boundary. */
	struct FArdaProviderRayTracingGeometry
	{
		FArdaRHIRayTracingGeometryDesc mDesc;
		FArdaProviderObjectRef mIndexBuffer;
		FArdaProviderObjectRef mVertexOrAABBBuffer;
		FArdaProviderObjectRef mOpacityMicromap;
		FArdaProviderObjectRef mOpacityMicromapIndexBuffer;
	};

	/** Backend-resolved TLAS instance. */
	struct FArdaProviderRayTracingInstance
	{
		float mTransform[3][4]{};
		uint32_t mInstanceID = 0;
		uint32_t mInstanceMask = 0xff;
		uint32_t mInstanceContributionToHitGroupIndex = 0;
		uint32_t mFlags = 0;
		FArdaProviderObjectRef mBottomLevelAccelStruct;
	};
}
