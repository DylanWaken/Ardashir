/** Native framebuffer facade resource. */
#pragma once

#include "RHI/Resources/ArdaRHIFramebuffer.h"
#include "RHI/Resources/ArdaRHIResourceImpl.h"

namespace arda::detail
{
	using FArdaFramebuffer =
	    TArdaNativeResource<IArdaRHIFramebuffer, FArdaRHIFramebufferDesc, EArdaRHIResourceType::Framebuffer>;
}
