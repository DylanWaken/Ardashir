/** Resolved recording state crossing the facade-to-provider command boundary. */
#pragma once

#include "RHI/Providers/ArdaProviderObject.h"
#include "RHI/CUDA/ArdaRHICuda.h"
#include "RHI/Resources/ArdaRHIAccelerationStructures.h"
#include "RHI/Scheduling/ArdaRHIDrawTypes.h"
#include <EASTL/vector.h>

namespace arda
{
	struct FArdaProviderCudaBinding
	{
		FArdaProviderObjectRef mObject;
		EArdaComputeAccess mAccess = EArdaComputeAccess::Read;
		EArdaComputeBindingType mType = EArdaComputeBindingType::Buffer;
		FArdaRHIBufferRange mBufferRange;
		uint32_t mMipLevel = 0;
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
