/** Pipeline, framebuffer and logical-state facade resources. */
#pragma once

#include "RHI/Resources/ArdaRHIResourceImpl.h"

namespace arda::detail
{
	using FArdaFramebuffer =
	    TArdaNativeResource<IArdaRHIFramebuffer, FArdaRHIFramebufferDesc, EArdaRHIResourceType::Framebuffer>;
	using FArdaGraphicsPipeline = TArdaNativeResource<IArdaRHIGraphicsPipeline,
	    FArdaRHIGraphicsPipelineDesc,
	    EArdaRHIResourceType::GraphicsPipeline>;
	using FArdaComputePipeline = TArdaNativeResource<IArdaRHIComputePipeline,
	    FArdaRHIComputePipelineDesc,
	    EArdaRHIResourceType::ComputePipeline>;
	using FArdaMeshletPipeline = TArdaNativeResource<IArdaRHIMeshletPipeline,
	    FArdaRHIMeshletPipelineDesc,
	    EArdaRHIResourceType::MeshletPipeline>;
	using FArdaRayTracingPipeline = TArdaNativeResource<IArdaRHIRayTracingPipeline,
	    FArdaRHIRayTracingPipelineDesc,
	    EArdaRHIResourceType::RayTracingPipeline>;

	class FArdaWorkGraphPipeline final : public FArdaResource, public IArdaRHIWorkGraphPipeline
	{
	public:
		FArdaWorkGraphPipeline(FArdaRHIWorkGraphPipelineDesc Desc,
		    FArdaProviderObjectRef Native,
		    uint64_t BackingMemorySize,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(EArdaRHIResourceType::WorkGraphPipeline,
		          Desc.mDebugName,
		          Owner,
		          eastl::move(LifetimeTracker)),
		      mDesc(eastl::move(Desc)),
		      mNative(eastl::move(Native)),
		      mBackingMemorySize(BackingMemorySize)
		{
		}

		const FArdaRHIWorkGraphPipelineDesc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		uint64_t GetBackingMemorySize() const noexcept override
		{
			return mBackingMemorySize;
		}

		FArdaRHIWorkGraphPipelineDesc mDesc;
		FArdaProviderObjectRef mNative;
		uint64_t mBackingMemorySize = 0;
	};

	template <typename Interface, typename Desc, EArdaRHIResourceType Type>
	class TArdaLogicalState final : public FArdaResource, public Interface
	{
	public:
		TArdaLogicalState(Desc Descriptor,
		    const void* Owner,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker)
		    : FArdaResource(Type, "CachedState", Owner, eastl::move(LifetimeTracker)),
		      mDesc(eastl::move(Descriptor))
		{
		}

		const Desc& GetDesc() const noexcept override
		{
			return mDesc;
		}

		Desc mDesc;
	};

	using FArdaRasterState =
	    TArdaLogicalState<IArdaRHIRasterState, FArdaRHIRasterState, EArdaRHIResourceType::RasterState>;
	using FArdaBlendState =
	    TArdaLogicalState<IArdaRHIBlendState, FArdaRHIBlendState, EArdaRHIResourceType::BlendState>;
	using FArdaDepthStencilState = TArdaLogicalState<IArdaRHIDepthStencilState,
	    FArdaRHIDepthStencilState,
	    EArdaRHIResourceType::DepthStencilState>;
}
