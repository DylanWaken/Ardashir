/** Process-wide diagnostic output and its synchronization. */
#pragma once
#include "ArdaLog.h"
#include <mutex>
namespace arda
{
	void DefaultLogOutput(const FArdaLogRecord& Record, void*) noexcept;
	struct FArdaLogContext
	{
		std::mutex mMutex;
		FArdaLogOutput mOutput = &DefaultLogOutput;
		void* mUserData = nullptr;
	};

	FArdaLogContext& GetLogState();
}
