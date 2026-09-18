/** Native sampler facade resource. */
#pragma once

#include "RHI/Resources/ArdaRHISampler.h"
#include "RHI/Resources/ArdaRHIResourceImpl.h"

namespace arda::detail
{
	using FArdaSampler = TArdaNativeResource<IArdaRHISampler, FArdaRHISamplerDesc, EArdaRHIResourceType::Sampler>;
}
