#pragma once

#include "Graph/Node/ArdaDependencyGraphImpl.h"

#include <atomic>
#include <mutex>

namespace arda
{
	const FArdaDependencyGraph* GetArdaThreadGraphEdit(const FArdaDependencyGraph* Graph);
	void SetArdaThreadGraphEdit(const FArdaDependencyGraph* Graph, const FArdaDependencyGraph* Recording);

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
		~FArdaRevisionState();

		/** Caller holds mMutex. A successful compile stages the revision until an execution attempt. */
		bool IsLocked() const
		{
			return mbLocked;
		}

		/** Current construction, replaced by commits and compilation. */
		eastl::shared_ptr<FArdaImpl> mCurrent = eastl::make_shared<FArdaImpl>();
		/** Caller holds mMutex; shared by ordinary inspection and inherited recording snapshots. */
		FArdaGraphRevisionStats Snapshot() const;

		/** Preserve edit boundaries and order for revision identity. The graph owns the latest construction;
		 * active editors retain their own bases, so closed edits do not need older full graph snapshots.
		 */
		eastl::vector<eastl::string> mEditKeys;
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
		/** Serializes construction publication and compile/execute admission, never operation recording. */
		mutable std::recursive_mutex mMutex;
		/** Protected by mMutex together with publication of the current construction. */
		uint32_t mActiveEdits = 0;

		/** Caller holds mMutex. The last canceled scope can release its empty construction. */
		bool CancelEditReservation(bool bPreserveConstruction)
		{
			--mActiveEdits;
			return !bPreserveConstruction && mActiveEdits == 0 && mEditKeys.empty();
		}
		eastl::shared_ptr<std::atomic<uint64_t>> mResourceGenerations = eastl::make_shared<std::atomic<uint64_t>>(1);
		uint64_t mOptionsVersion = 0;
		uint32_t mCacheCapacity = 4;
		bool mbBuilding = false;
		bool mbLocked = false;
	};
}
