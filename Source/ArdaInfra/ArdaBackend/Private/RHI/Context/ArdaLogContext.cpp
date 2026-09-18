#include "RHI/Context/ArdaLogContext.h"
#include <cstdio>
namespace arda
{
	void DefaultLogOutput(const FArdaLogRecord& Record, void*) noexcept
	{
		std::fprintf(stderr,
		    "[%s][%s] %s\n",
		    Record.mCategory ? Record.mCategory : "",
		    ToString(Record.mVerbosity),
		    Record.mMessage ? Record.mMessage : "");
	}

	FArdaLogContext& GetLogState()
	{
		static FArdaLogContext state;
		return state;
	}

}
