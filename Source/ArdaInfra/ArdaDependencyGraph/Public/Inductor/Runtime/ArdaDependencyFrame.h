/** Retained completion identity for an executed dependency graph frame. */
#pragma once

#include <EASTL/shared_ptr.h>

namespace arda
{
	/** Retained completion receipt, valid after a frame slot is reused or the graph is edited. */
	struct FArdaDependencyFrameTicket
	{
		struct FArdaState;

		explicit operator bool() const noexcept
		{
			return bool(mState);
		}

	private:
		eastl::shared_ptr<FArdaState> mState;
		friend struct FArdaInductorRuntime;
	};
	struct FArdaInductorFrame;
}
