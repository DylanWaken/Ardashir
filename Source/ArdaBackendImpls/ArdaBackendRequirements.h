#pragma once

#include "ArdaBackend.h"

namespace arda
{
    inline FArdaRHIStatus ValidateArdaBackendRequirements(
        const FArdaRHICapabilities& Capabilities,
        const FArdaBackendConfiguration& Configuration)
    {
        const auto Profile = Capabilities.Evaluate(
            GetArdaRHIProfileRequirements(Configuration.mRequiredDeviceProfile));
        if (!Profile.IsSupported())
            return Profile.ToStatus();
        return Capabilities.Evaluate(Configuration.mRequiredFeatures).ToStatus();
    }
}
