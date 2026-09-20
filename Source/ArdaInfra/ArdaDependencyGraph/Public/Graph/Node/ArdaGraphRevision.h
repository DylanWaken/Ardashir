/** Persistent construction-cache diagnostics. */
#pragma once

#include <cstdint>

namespace arda
{
	/** Graph-wide counters and the current recording/compilation state. */
	struct FArdaGraphRevisionStats
	{
		/** Ordered hash of queued edits, or the most recently staged construction when the queue is empty. */
		uint64_t mConstructionHash = 0;
		/** Selected persistent compilation identity; cache hits retain it. */
		uint64_t mPersistentRevision = 0;
		/** Number of successful compiler invocations, excluding cache hits. */
		uint64_t mCompileCount = 0;
		/** Number of constructions served by the persistent revision cache. */
		uint64_t mCacheHitCount = 0;
		/** Number of retained reusable revisions; defaults to a maximum of four. */
		uint32_t mCachedRevisionCount = 0;
		/** Number of closed transactions waiting for compilation. */
		uint32_t mPendingEditCount = 0;
		/** Successful compilation prevents edits until execution is attempted. */
		bool mbEditQueueLocked = false;
	};
}
