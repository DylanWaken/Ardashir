#pragma once

#include "ArdaLog.h"

#include <cstdint>

ARDA_DECLARE_LOG_CATEGORY_EXTERN(LogArdaAssert);

namespace arda::backend
{
    /** Controls how failed ensure assertions are handled at runtime. */
    enum class EArdaEnsureBehavior : std::uint8_t
    {
        Log,
        Break,
    };

    /** Replaces the process-wide ensure behavior. */
    void SetEnsureBehavior(EArdaEnsureBehavior behavior) noexcept;
    /** Returns the current ensure behavior. */
    [[nodiscard]] EArdaEnsureBehavior GetEnsureBehavior() noexcept;

    /** Logs and terminates the process after a failed check. */
    [[noreturn]] void ReportFatalCheck(
        const char* expression,
        const char* file,
        std::uint32_t line,
        const char* function) noexcept;

    /** Logs a formatted message and terminates after a failed check. */
    [[noreturn]] void ReportFatalCheckf(
        const char* expression,
        const char* file,
        std::uint32_t line,
        const char* function,
        const char* format,
        ...) noexcept;

    /** Logs a failed ensure and returns false. */
    [[nodiscard]] bool ReportEnsureFailure(
        const char* expression,
        const char* file,
        std::uint32_t line,
        const char* function) noexcept;

    /** Logs a formatted ensure failure and returns false. */
    [[nodiscard]] bool ReportEnsureFailuref(
        const char* expression,
        const char* file,
        std::uint32_t line,
        const char* function,
        const char* format,
        ...) noexcept;
}

#if !defined(NDEBUG) || defined(ARDA_ENABLE_CHECKS)
    #define ARDA_CHECK_ENABLED 1
#else
    #define ARDA_CHECK_ENABLED 0
#endif

#if ARDA_CHECK_ENABLED
    #define ARDA_CHECK(Condition) \
        do \
        { \
            if (!(Condition)) \
            { \
                ::arda::backend::ReportFatalCheck( \
                    #Condition, \
                    __FILE__, \
                    static_cast<std::uint32_t>(__LINE__), \
                    __func__); \
            } \
        } while (false)

    #define ARDA_CHECKF(Condition, Format, ...) \
        do \
        { \
            if (!(Condition)) \
            { \
                ::arda::backend::ReportFatalCheckf( \
                    #Condition, \
                    __FILE__, \
                    static_cast<std::uint32_t>(__LINE__), \
                    __func__, \
                    Format, \
                    ##__VA_ARGS__); \
            } \
        } while (false)

    #define ARDA_VERIFY(Condition) \
        (!!(Condition) || \
            (::arda::backend::ReportFatalCheck( \
                 #Condition, \
                 __FILE__, \
                 static_cast<std::uint32_t>(__LINE__), \
                 __func__), \
             false))

    #define ARDA_VERIFYF(Condition, Format, ...) \
        (!!(Condition) || \
            (::arda::backend::ReportFatalCheckf( \
                 #Condition, \
                 __FILE__, \
                 static_cast<std::uint32_t>(__LINE__), \
                 __func__, \
                 Format, \
                 ##__VA_ARGS__), \
             false))
#else
    #define ARDA_CHECK(Condition) \
        do \
        { \
            (void)sizeof(Condition); \
        } while (false)

    #define ARDA_CHECKF(Condition, Format, ...) \
        do \
        { \
            (void)sizeof(Condition); \
        } while (false)

    #define ARDA_VERIFY(Condition) (!!(Condition))
    #define ARDA_VERIFYF(Condition, Format, ...) (!!(Condition))
#endif

#define ARDA_ENSURE(Condition) \
    (!!(Condition) || \
        ::arda::backend::ReportEnsureFailure( \
            #Condition, \
            __FILE__, \
            static_cast<std::uint32_t>(__LINE__), \
            __func__))

#define ARDA_ENSURE_MSGF(Condition, Format, ...) \
    (!!(Condition) || \
        ::arda::backend::ReportEnsureFailuref( \
            #Condition, \
            __FILE__, \
            static_cast<std::uint32_t>(__LINE__), \
            __func__, \
            Format, \
            ##__VA_ARGS__))

/** Reports an unconditional programmer error and terminates. */
#define ARDA_CHECK_MSG(Format, ...) ARDA_CHECKF(false, Format, ##__VA_ARGS__)