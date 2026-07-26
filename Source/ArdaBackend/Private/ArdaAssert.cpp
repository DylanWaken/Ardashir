#include "ArdaBackendPch.h"

#include "ArdaAssert.h"

#include <atomic>
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

namespace arda::backend
{
    namespace
    {
        std::atomic<EArdaEnsureBehavior> gEnsureBehavior{
            EArdaEnsureBehavior::Break};

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

        std::string FormatAssertMessage(
            const char* format,
            std::va_list arguments)
        {
            if (!format)
            {
                return {};
            }

            std::va_list countArguments;
            va_copy(countArguments, arguments);
            const int requiredLength =
                std::vsnprintf(nullptr, 0, format, countArguments);
            va_end(countArguments);

            if (requiredLength < 0)
            {
                return "Assertion message formatting failed.";
            }

            std::string message(
                static_cast<std::size_t>(requiredLength) + 1,
                '\0');
            std::va_list formatArguments;
            va_copy(formatArguments, arguments);
            const int writtenLength = std::vsnprintf(
                message.data(),
                message.size(),
                format,
                formatArguments);
            va_end(formatArguments);

            if (writtenLength < 0)
            {
                return "Assertion message formatting failed.";
            }

            message.resize(static_cast<std::size_t>(writtenLength));
            return message;
        }
    }

    void SetEnsureBehavior(EArdaEnsureBehavior behavior) noexcept
    {
        gEnsureBehavior.store(behavior, std::memory_order_relaxed);
    }

    EArdaEnsureBehavior GetEnsureBehavior() noexcept
    {
        return gEnsureBehavior.load(std::memory_order_relaxed);
    }

    void ReportFatalCheck(
        const char* expression,
        const char* file,
        std::uint32_t line,
        const char* function) noexcept
    {
        ARDA_LOG(
            LogArdaAssert,
            Fatal,
            "Assertion failed: %s",
            expression ? expression : "<unknown>");
        ARDA_LOG(
            LogArdaAssert,
            Fatal,
            "  at %s (%s:%u)",
            function ? function : "<unknown>",
            file ? file : "<unknown>",
            line);
        DebugBreakIfAttached();
        TerminateProcess();
    }

    void ReportFatalCheckf(
        const char* expression,
        const char* file,
        std::uint32_t line,
        const char* function,
        const char* format,
        ...) noexcept
    {
        std::va_list arguments;
        va_start(arguments, format);
        const std::string message = FormatAssertMessage(format, arguments);
        va_end(arguments);

        ARDA_LOG(
            LogArdaAssert,
            Fatal,
            "Assertion failed: %s",
            expression ? expression : "<unknown>");
        ARDA_LOG(
            LogArdaAssert,
            Fatal,
            "  %s",
            message.c_str());
        ARDA_LOG(
            LogArdaAssert,
            Fatal,
            "  at %s (%s:%u)",
            function ? function : "<unknown>",
            file ? file : "<unknown>",
            line);
        DebugBreakIfAttached();
        TerminateProcess();
    }

    bool ReportEnsureFailure(
        const char* expression,
        const char* file,
        std::uint32_t line,
        const char* function) noexcept
    {
        ARDA_LOG(
            LogArdaAssert,
            Error,
            "Ensure failed: %s",
            expression ? expression : "<unknown>");
        ARDA_LOG(
            LogArdaAssert,
            Error,
            "  at %s (%s:%u)",
            function ? function : "<unknown>",
            file ? file : "<unknown>",
            line);

        if (GetEnsureBehavior() == EArdaEnsureBehavior::Break)
        {
            DebugBreakIfAttached();
        }

        return false;
    }

    bool ReportEnsureFailuref(
        const char* expression,
        const char* file,
        std::uint32_t line,
        const char* function,
        const char* format,
        ...) noexcept
    {
        std::va_list arguments;
        va_start(arguments, format);
        const std::string message = FormatAssertMessage(format, arguments);
        va_end(arguments);

        ARDA_LOG(
            LogArdaAssert,
            Error,
            "Ensure failed: %s",
            expression ? expression : "<unknown>");
        ARDA_LOG(
            LogArdaAssert,
            Error,
            "  %s",
            message.c_str());
        ARDA_LOG(
            LogArdaAssert,
            Error,
            "  at %s (%s:%u)",
            function ? function : "<unknown>",
            file ? file : "<unknown>",
            line);

        if (GetEnsureBehavior() == EArdaEnsureBehavior::Break)
        {
            DebugBreakIfAttached();
        }

        return false;
    }
}
