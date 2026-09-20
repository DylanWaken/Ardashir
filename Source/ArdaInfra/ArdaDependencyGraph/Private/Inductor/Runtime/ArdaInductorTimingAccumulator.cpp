#include "Inductor/Runtime/ArdaInductorTimingState.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cmath>

namespace arda
{
	void FArdaInductorTimingAccumulator::Configure(double Alpha, uint32_t HistoryCapacity)
	{
		std::lock_guard<std::mutex> Lock(mMutex);
		mAlpha = Alpha;
		mHistoryCapacity = HistoryCapacity;
		mHistory.clear();
		mHistoryCursor = 0;
	}

	void FArdaInductorTimingAccumulator::Add(const eastl::string& Name,
	    double Seconds,
	    const eastl::vector<FArdaGraphNodeHandle>& Nodes,
	    uint64_t Sequence,
	    EArdaRHIQueueType Queue,
	    uint64_t Epoch)
	{
		if (!std::isfinite(Seconds) || Seconds < 0)
		{
			return;
		}
		eastl::string Key = Nodes.empty() ? "name:" : "nodes:";
		if (Nodes.empty())
		{
			Key += Name;
		}
		for (const auto Node : Nodes)
		{
			for (uint64_t V : {Node.GetGraphIdentity(), uint64_t(Node.GetIndex()), uint64_t(Node.GetGeneration())})
			{
				Key.append(reinterpret_cast<const char*>(&V), sizeof(V));
			}
		}
		std::lock_guard<std::mutex> Lock(mMutex);
		if (Epoch != UINT64_MAX && Epoch != mEpoch)
		{
			return;
		}
		auto& Sample = mSamples[Key];
		// Native records are consumed once by their frame receipt. A late receipt
		// still contributes to collection-order statistics and bounded history.
		const bool LatestFrame =
		    !Sample.mSampleCount || Sequence > Sample.mLastFrameSequence || (!Sequence && !Sample.mLastFrameSequence);
		Sample.mNodeName = Name;
		Sample.mNodes = Nodes;
		Sample.mGpuSeconds =
		    Sample.mSampleCount ? Sample.mGpuSeconds + mAlpha * (Seconds - Sample.mGpuSeconds) : Seconds;
		Sample.mMinGpuSeconds = Sample.mSampleCount ? eastl::min(Sample.mMinGpuSeconds, Seconds) : Seconds;
		Sample.mMaxGpuSeconds = eastl::max(Sample.mMaxGpuSeconds, Seconds);
		if (LatestFrame)
		{
			Sample.mLastGpuSeconds = Seconds;
			Sample.mLastFrameSequence = Sequence;
			Sample.mQueue = Queue;
		}
		if (Sample.mSampleCount != UINT32_MAX)
		{
			++Sample.mSampleCount;
		}
		if (mHistoryCapacity && Nodes.size() == 1)
		{
			FArdaInductorNodeTiming Record{Nodes.front(), Name, Sequence, Seconds, Queue};
			if (mHistory.size() < mHistoryCapacity)
			{
				mHistory.push_back(eastl::move(Record));
			}
			else
			{
				mHistory[mHistoryCursor] = eastl::move(Record);
				mHistoryCursor = (mHistoryCursor + 1) % mHistoryCapacity;
			}
		}
	}

	eastl::vector<FArdaInductorTimingSample> FArdaInductorTimingAccumulator::Snapshot() const
	{
		std::lock_guard<std::mutex> Lock(mMutex);
		eastl::vector<FArdaInductorTimingSample> Result;
		for (const auto& Pair : mSamples)
		{
			Result.push_back(Pair.second);
		}
		eastl::sort(Result.begin(),
		    Result.end(),
		    [](const auto& A, const auto& B)
		    {
			    return A.mNodeName != B.mNodeName ? A.mNodeName < B.mNodeName : A.mNodes < B.mNodes;
		    });
		return Result;
	}

	eastl::vector<FArdaInductorNodeTiming> FArdaInductorTimingAccumulator::History() const
	{
		std::lock_guard<std::mutex> Lock(mMutex);
		auto Result = mHistory;
		eastl::sort(Result.begin(),
		    Result.end(),
		    [](const auto& A, const auto& B)
		    {
			    return A.mFrameSequence != B.mFrameSequence ? A.mFrameSequence < B.mFrameSequence : A.mNode < B.mNode;
		    });
		return Result;
	}

	uint64_t FArdaInductorTimingAccumulator::GetEpoch() const
	{
		std::lock_guard<std::mutex> Lock(mMutex);
		return mEpoch;
	}

	void FArdaInductorTimingAccumulator::Clear()
	{
		std::lock_guard<std::mutex> Lock(mMutex);
		mSamples.clear();
		mHistory.clear();
		mHistoryCursor = 0;
		++mEpoch;
	}

}
