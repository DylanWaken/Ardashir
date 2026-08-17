/** @file ArdaBackendRegistry.h
 *  @brief Declares private active-module registry operations.
 */
#pragma once

#include "ArdaBackendProvider.h"

namespace arda::backend::private_api
{
    /** Publishes the active module while the backend state lock is held. */
    void SetActiveBackendModule(const IArdaBackendModule* Module) noexcept;
}
