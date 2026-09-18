/** @file ArdaRHIShaderBundle.h
 * Declares ShaderBundle definitions for the RHI shaders module.
 */

#pragma once

#include "RHI/Pipelines/ArdaRHIComputePipeline.h"
#include "RHI/Pipelines/ArdaRHIMeshletPipeline.h"
#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Shaders/ArdaRHIBindingSet.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>

namespace arda
{
	/** One executable compute or mesh record in a shader bundle. */
	struct FArdaRHIShaderBundleRecord
	{
		FArdaRHIComputePipelineRef mComputePipeline;
		FArdaRHIMeshletPipelineRef mMeshPipeline;
		eastl::vector<FArdaRHIBindingSetRef> mBindings;
		eastl::vector<uint8_t> mLocalArguments;
		uint32_t mGroupsX = 1;
		uint32_t mGroupsY = 1;
		uint32_t mGroupsZ = 1;
	};

	struct FArdaRHIShaderBundleDesc
	{
		uint32_t mMaxRecords = 0;
		bool mbMeshRecords = false;
		bool mbPersistent = false;
		eastl::string mDebugName;
	};

	class IArdaRHIShaderBundle : public virtual IArdaRHIResource
	{
	public:
		[[nodiscard]] virtual const FArdaRHIShaderBundleDesc& GetDesc() const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetRecordCount() const noexcept = 0;
	};
}
