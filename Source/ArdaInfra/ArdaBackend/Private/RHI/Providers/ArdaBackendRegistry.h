/** @file ArdaBackendRegistry.h
 *  @brief Declares private active-module registry operations.
 */
#pragma once

#include "RHI/Providers/ArdaBackendProvider.h"

namespace arda
{
	/** Publishes the active module while the backend state lock is held. */
	void SetActiveBackendModule(const IArdaBackendModule* Module) noexcept;
}
