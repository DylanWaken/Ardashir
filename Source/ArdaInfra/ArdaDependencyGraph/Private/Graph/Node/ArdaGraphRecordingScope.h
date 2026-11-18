#pragma once

#include "Graph/Node/ArdaGraphRevision.h"
#include "RHI/Config/ArdaRHIStatus.h"

namespace arda
{
	/** Nested callbacks may use independent graphs. Each graph retains its entry-time revision stats. */
	class FArdaGraphRecordingScope final
	{
	public:
		FArdaGraphRecordingScope(const void* RevisionState, FArdaGraphRevisionStats Stats)
		    : mRevisionStats(Stats),
		      mRevisionState(RevisionState),
		      mPrevious(mCurrent)
		{
			mCurrent = this;
		}

		~FArdaGraphRecordingScope()
		{
			mCurrent = mPrevious;
		}

		FArdaGraphRecordingScope(const FArdaGraphRecordingScope&) = delete;
		FArdaGraphRecordingScope& operator=(const FArdaGraphRecordingScope&) = delete;

		static const FArdaGraphRecordingScope* Find(const void* RevisionState) noexcept
		{
			for (auto* Scope = mCurrent; Scope; Scope = Scope->mPrevious)
			{
				if (Scope->mRevisionState == RevisionState)
				{
					return Scope;
				}
			}
			return nullptr;
		}

		const FArdaGraphRevisionStats mRevisionStats;

	private:
		const void* mRevisionState;
		const FArdaGraphRecordingScope* mPrevious;
		static inline thread_local const FArdaGraphRecordingScope* mCurrent = nullptr;
	};

	inline FArdaRHIStatus CheckArdaGraphRecordingReentry(const void* RevisionState)
	{
		return FArdaGraphRecordingScope::Find(RevisionState)
		    ? FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
		          "Graph mutation and serialization cannot reenter the graph from its recording callbacks.")
		    : FArdaRHIStatus{};
	}
}
