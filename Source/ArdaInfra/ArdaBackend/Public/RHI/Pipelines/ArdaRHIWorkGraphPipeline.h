/** @file ArdaRHIWorkGraphPipeline.h
 * Declares WorkGraphPipeline definitions for the RHI pipelines module.
 */

#pragma once

#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Shaders/ArdaRHIBindingLayout.h"
#include "RHI/Shaders/ArdaRHIShader.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstddef>
#include <cstdint>

namespace arda
{
	/** Work-graph executable state object and backing-memory policy. */
	struct FArdaRHIWorkGraphPipelineDesc
	{
		eastl::string mProgramName;
		eastl::string mEntryPoint;
		eastl::vector<FArdaRHIShaderRef> mShaders;
		eastl::vector<FArdaRHIBindingLayoutRef> mGlobalBindingLayouts;
		uint32_t mMaxInputRecords = 1;
		/** Stable renderer-supplied semantic key for native cache integration. */
		uint64_t mPersistentCacheKey = 0;
		eastl::string mDebugName;

		bool operator==(const FArdaRHIWorkGraphPipelineDesc& O) const noexcept
		{
			return mProgramName == O.mProgramName && mEntryPoint == O.mEntryPoint && mShaders == O.mShaders &&
			    mGlobalBindingLayouts == O.mGlobalBindingLayouts && mMaxInputRecords == O.mMaxInputRecords;
		}
	};

	/** Retains the program and reusable backing memory. D3D12 initializes backing memory once, at
	 * the first actual submission, with a zero-record dispatch in an additional command list and GPU fence signal.
	 * Discarded recordings do not consume initialization. Same-queue reuse may overlap; cross-queue reuse
	 * waits on the last outstanding use of this backing memory through a GPU fence, without CPU waits.
	 * @ownership Pipeline handles retain program and backing allocation; submitted work retains initialization storage until GPU retirement.
	 * @threading Externally synchronize host submission; each cross-queue reuse is ordered by a GPU fence.
	 */
	class IArdaRHIWorkGraphPipeline : public virtual IArdaRHIResource
	{
	public:
		[[nodiscard]] virtual const FArdaRHIWorkGraphPipelineDesc& GetDesc() const noexcept = 0;
		[[nodiscard]] virtual uint64_t GetBackingMemorySize() const noexcept = 0;
	};

	/** Hashes the semantic work-graph pipeline descriptor. */
	[[nodiscard]] size_t HashValue(const FArdaRHIWorkGraphPipelineDesc& Value) noexcept;
}
