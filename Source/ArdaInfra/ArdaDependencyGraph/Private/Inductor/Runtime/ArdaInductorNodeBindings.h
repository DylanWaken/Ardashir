#pragma once

#include "Inductor/Rules/ArdaInductorPipeline.h"
#include <EASTL/unordered_map.h>

namespace arda
{
	/** Native objects resolved together for one node, indexed by authored slot/name. */
	struct FArdaInductorNodeBindings
	{
		eastl::unordered_map<eastl::string, FArdaInductorResolvedPipeline> mPipelines;
		FArdaRHIFramebufferRef mFramebuffer;
		eastl::unordered_map<eastl::string, eastl::vector<FArdaRHIBindingSetRef>> mBindings;
		eastl::unordered_map<eastl::string, FArdaRHIDescriptorTableRef> mDescriptorTables;
		eastl::unordered_map<eastl::string, FArdaRHIShaderTableRef> mShaderTables;
		eastl::unordered_map<eastl::string, eastl::vector<uint8_t>> mShaderParameters;
	};
}
