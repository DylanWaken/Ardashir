/** @file ArdaLinkedBackends.h
 *  @brief Declares registration of backend libraries selected by the build.
 */
#pragma once

namespace arda::backend::private_api
{
    /** Registers every backend library selected in the current build. */
    void RegisterLinkedBackendModules();
}
