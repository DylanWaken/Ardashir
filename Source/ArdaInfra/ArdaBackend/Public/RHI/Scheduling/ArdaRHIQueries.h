/** @file ArdaRHIQueries.h
 * Declares Queries definitions for the RHI scheduling module.
 */

#pragma once

#include "RHI/Resources/ArdaRHIResource.h"


namespace arda
{
	/** Interface for event query. */
	class IArdaRHIEventQuery : public virtual IArdaRHIResource
	{
	};

	/** Interface for timer query. */
	class IArdaRHITimerQuery : public virtual IArdaRHIResource
	{
	};
}
