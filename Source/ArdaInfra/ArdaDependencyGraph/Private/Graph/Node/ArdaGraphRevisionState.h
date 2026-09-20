#pragma once

#include "Graph/Node/ArdaDependencyGraphImpl.h"

namespace arda
{
	/** Closed edit snapshots are immutable; each contains the accumulated construction prefix. */
	struct FArdaQueuedGraphEdit
	{
		uint64_t mHash = 0;
		eastl::string mKey;
		eastl::shared_ptr<FArdaDependencyGraph::FArdaImpl> mSnapshot;
	};

	struct FArdaCachedGraphRevision
	{
		uint64_t mHash = 0;
		eastl::string mKey;
		eastl::shared_ptr<FArdaDependencyGraph::FArdaImpl> mGraph;
		/** Construction identity is independent of later adaptive schedule versions. */
		uint64_t mRevision = 0;
	};

	/** Owns construction transactions separately from immutable compiled revisions and their runtimes. */
	struct FArdaDependencyGraph::FArdaRevisionState
	{
		eastl::vector<FArdaQueuedGraphEdit> mQueue;
		eastl::vector<FArdaCachedGraphRevision> mCache;
		eastl::shared_ptr<FArdaImpl> mPersistent;
		eastl::string mPersistentKey;
		uint64_t mPersistentHash = 0;
		uint64_t mPersistentRevision = 0;
		uint64_t mCompileCount = 0;
		uint64_t mNextRevision = 1;
		uint64_t mCacheHitCount = 0;
		uint64_t mNextResourceGeneration = 1;
		uint64_t mNextEditGeneration = 1;
		uint64_t mNextFrameSequence = 1;
		uint64_t mSavedOperationName = 1;
		uint32_t mCacheCapacity = 4;
		bool mbBuilding = false;
		bool mbLocked = false;
	};
}
