/** @file ArdaRHIGpuFence.h
 * Declares GpuFence definitions for the RHI scheduling module.
 */

#pragma once

#include "RHI/Resources/ArdaRHIResource.h"


namespace arda
{
	/** Native queue fence facade; one signal is tracked at a time. */
	class IArdaRHIGpuFence : public virtual IArdaRHIResource
	{
	};
}
