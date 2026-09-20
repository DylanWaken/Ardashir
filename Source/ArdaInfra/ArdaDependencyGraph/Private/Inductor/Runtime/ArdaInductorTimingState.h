#pragma once

#include "Inductor/Runtime/ArdaInductorTiming.h"
#include "RHI/Scheduling/ArdaCudaSequence.h"

#include <mutex>
#include <EASTL/unordered_map.h>

namespace arda
{
	class FArdaInductorTimingAccumulator
	{
	public:
		void Configure(double Alpha, uint32_t HistoryCapacity);
		void Add(const eastl::string& Name,
		    double Seconds,
		    const eastl::vector<FArdaGraphNodeHandle>& Nodes = {},
		    uint64_t Sequence = 0,
		    EArdaRHIQueueType Queue = EArdaRHIQueueType::Graphics,
		    uint64_t Epoch = UINT64_MAX);
		eastl::vector<FArdaInductorTimingSample> Snapshot() const;
		eastl::vector<FArdaInductorNodeTiming> History() const;
		uint64_t GetEpoch() const;
		void Clear();

	private:
		mutable std::mutex mMutex;
		eastl::unordered_map<eastl::string, FArdaInductorTimingSample> mSamples;
		eastl::vector<FArdaInductorNodeTiming> mHistory;
		double mAlpha = 0.2;
		uint32_t mHistoryCapacity = 4096;
		uint32_t mHistoryCursor = 0;
		uint64_t mEpoch = 0;
	};

	struct FArdaInductorFrameTimer
	{
		eastl::string mName;
		FArdaRHITimerQueryRef mQuery;
		eastl::vector<FArdaGraphNodeHandle> mNodes;
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
	};

	struct FArdaInductorCudaFrameTimer
	{
		eastl::shared_ptr<FArdaCudaTimingQuery> mQuery;
		eastl::vector<FArdaGraphNodeHandle> mNodes;
		eastl::vector<eastl::string> mNames;
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
	};
}
