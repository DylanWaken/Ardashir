#include "RHI/Context/ArdaBackendRegistryContext.h"

namespace arda
{
	FArdaBackendModuleRegistry& GetBackendModuleRegistry()
	{
		static FArdaBackendModuleRegistry Registry;
		return Registry;
	}

}
