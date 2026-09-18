/** Configuration resolution requires the backend context lock. */
#pragma once
#include "RHI/Context/ArdaBackendContext.h"
namespace arda
{
	bool ResolveConfiguration(FArdaBackendContext& State, FArdaBackendConfiguration& Configuration, IArdaBackendModule*& OutModule, bool bValidateRuntimeProvider);
}
