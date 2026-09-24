#include "Graph/Node/ArdaDependencyKey.h"

namespace arda
{
	const FArdaDependencyKeyBuilder::FArdaConstructionResourceContext*& FArdaDependencyKeyBuilder::
	    GetConstructionContext() noexcept
	{
		// External node callbacks must observe the scope installed by the graph
		// module, rather than a separate inline thread-local variable in their DLL.
		static thread_local const FArdaConstructionResourceContext* Context = nullptr;
		return Context;
	}
}
