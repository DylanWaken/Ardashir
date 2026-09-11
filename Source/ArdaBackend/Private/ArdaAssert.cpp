#include "ArdaBackendCorePch.h"

#include "ArdaAssert.h"
#include "ArdaStringFormat.h"

#include <EASTL/atomic.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

ARDA_DEFINE_LOG_CATEGORY(LogArdaAssert, Error);

namespace arda
{
	namespace
	{
		eastl::atomic<EArdaEnsureBehavior> gEnsureBehavior{EArdaEnsureBehavior::Break};

		void DebugBreakIfAttached() noexcept
		{
#if defined(_WIN32)
			if (::IsDebuggerPresent())
			{
				::DebugBreak();
			}
#elif defined(__has_builtin)
#if __has_builtin(__builtin_trap)
			__builtin_trap();
#endif
#endif
		}

		void TerminateProcess() noexcept
		{
			std::fflush(nullptr);
			std::abort();
		}

		bool ReportAssertionFailure(bool bFatal,
		    const char* Expression,
		    const char* File,
		    uint32_t Line,
		    const char* Function,
		    const char* Message = nullptr) noexcept
		{
			const auto Verbosity = bFatal ? EArdaLogVerbosity::Fatal : EArdaLogVerbosity::Error;
			Logf(LogArdaAssert,
			    Verbosity,
			    File,
			    Line,
			    Function,
			    "%s failed: %s",
			    bFatal ? "Assertion" : "Ensure",
			    Expression ? Expression : "<unknown>");
			if (Message)
			{
				Logf(LogArdaAssert, Verbosity, File, Line, Function, "  %s", Message);
			}
			Logf(LogArdaAssert,
			    Verbosity,
			    File,
			    Line,
			    Function,
			    "  at %s (%s:%u)",
			    Function ? Function : "<unknown>",
			    File ? File : "<unknown>",
			    Line);
			if (bFatal || GetEnsureBehavior() == EArdaEnsureBehavior::Break)
			{
				DebugBreakIfAttached();
			}
			if (bFatal)
			{
				TerminateProcess();
			}
			return false;
		}

	}

	void SetEnsureBehavior(EArdaEnsureBehavior behavior) noexcept
	{
		gEnsureBehavior.store(behavior, eastl::memory_order_relaxed);
	}

	EArdaEnsureBehavior GetEnsureBehavior() noexcept
	{
		return gEnsureBehavior.load(eastl::memory_order_relaxed);
	}

	void ReportFatalCheck(const char* expression, const char* file, std::uint32_t line, const char* function) noexcept
	{
		ReportAssertionFailure(true, expression, file, line, function);
	}

	void ReportFatalCheckf(const char* expression,
	    const char* file,
	    std::uint32_t line,
	    const char* function,
	    const char* format,
	    ...) noexcept
	{
		std::va_list arguments;
		va_start(arguments, format);
		const eastl::string message = FormatArdaMessage(format, arguments, "Assertion message formatting failed.");
		va_end(arguments);

		ReportAssertionFailure(true, expression, file, line, function, message.c_str());
	}

	bool ReportEnsureFailure(const char* expression,
	    const char* file,
	    std::uint32_t line,
	    const char* function) noexcept
	{
		return ReportAssertionFailure(false, expression, file, line, function);
	}

	bool ReportEnsureFailuref(const char* expression,
	    const char* file,
	    std::uint32_t line,
	    const char* function,
	    const char* format,
	    ...) noexcept
	{
		std::va_list arguments;
		va_start(arguments, format);
		const eastl::string message = FormatArdaMessage(format, arguments, "Assertion message formatting failed.");
		va_end(arguments);

		return ReportAssertionFailure(false, expression, file, line, function, message.c_str());
	}
}
