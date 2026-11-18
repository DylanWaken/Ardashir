/** Standalone graph preprocessing with immutable input and an editable graph result. */
#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include <EASTL/unique_ptr.h>

namespace arda
{
	class FArdaDependencyGraph;

	class FArdaPreprocessor final
	{
	public:
		/** Clone and transform Source using its configured rules. Failure leaves Source unchanged. */
		static TArdaRHIResult<eastl::unique_ptr<FArdaDependencyGraph>> Preprocess(const FArdaDependencyGraph& Source);

	private:
		friend class FArdaDependencyGraph;
		friend class FArdaInductor;
		static FArdaRHIStatus PreprocessPending(FArdaDependencyGraph& Graph);
	};
}
