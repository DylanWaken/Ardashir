/** Resolved shader and layout inputs supplied to native pipeline factories. */
#pragma once

#include "RHI/Providers/ArdaProviderObject.h"
#include "RHI/Pipelines/ArdaRHIComputePipeline.h"
#include "RHI/Pipelines/ArdaRHIGraphicsPipeline.h"
#include "RHI/Pipelines/ArdaRHIMeshletPipeline.h"
#include "RHI/Pipelines/ArdaRHIRayTracingPipeline.h"
#include "RHI/Pipelines/ArdaRHIWorkGraphPipeline.h"
#include "RHI/Shaders/ArdaRHIInputLayout.h"
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace arda
{
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
}
