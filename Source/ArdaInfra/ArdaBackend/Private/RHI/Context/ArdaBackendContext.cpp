#include "RHI/Context/ArdaBackendContext.h"

namespace arda
{
	FArdaBackendContext& GetBackendContext()
	{
		static FArdaBackendContext Context;
		return Context;
	}
}
