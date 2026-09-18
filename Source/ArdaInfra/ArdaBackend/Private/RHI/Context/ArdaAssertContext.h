/** Process-wide ensure handling policy. */
#pragma once
#include "RHI/Config/ArdaAssert.h"
#include <EASTL/atomic.h>
namespace arda
{
	struct FArdaAssertContext
	{
		eastl::atomic<EArdaEnsureBehavior> mEnsureBehavior{EArdaEnsureBehavior::Break};
	};
	FArdaAssertContext& GetAssertContext() noexcept;
}
