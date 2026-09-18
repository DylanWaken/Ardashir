#include "RHI/Context/ArdaExternalResourceContext.h"

namespace arda
{
	FArdaResourceProviderRegistry& GetResourceProviderRegistry()
	{
		static FArdaResourceProviderRegistry Registry;
		return Registry;
	}

}
