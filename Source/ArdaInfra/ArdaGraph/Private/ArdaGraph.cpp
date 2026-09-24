#include "ArdaGraph.h"

// Algorithms are templates so payload types remain independent of this module.

namespace arda
{
	uint64_t AllocateArdaGraphIdentity() noexcept
	{
		// One module owns the sequence even when graph templates are instantiated
		// by several shared-library consumers in the same process.
		static eastl::atomic<uint64_t> Next{1};
		return Next.fetch_add(1, eastl::memory_order_relaxed);
	}
}
