/** @file ArdaAssert.h
 *  @brief Declares assertion reporting and runtime assertion macros.
 */
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

    /**
     * Replaces the process-wide ensure behavior.
     * @param behavior Behavior to apply to subsequent failed ensures.
     */
    void SetEnsureBehavior(EArdaEnsureBehavior behavior) noexcept;
    /** @return The current ensure behavior. */
    [[nodiscard]] EArdaEnsureBehavior GetEnsureBehavior() noexcept;

    /**
     * Logs and terminates the process after a failed check.
     * @param expression Text of the failed expression.
     * @param file Source file containing the check.
     * @param line Source line containing the check.
     * @param function Function containing the check.
     */
    [[noreturn]] void ReportFatalCheck(
        const char* expression,
        const char* file,
        std::uint32_t line,
        const char* function) noexcept;

    /**
     * Logs a formatted message and terminates after a failed check.
     * @param expression Text of the failed expression.
     * @param file Source file containing the check.
     * @param line Source line containing the check.
     * @param function Function containing the check.
     * @param format printf-style diagnostic format.
     */
    [[noreturn]] void ReportFatalCheckf(
        const char* expression,
        const char* file,
        std::uint32_t line,
        const char* function,
        const char* format,
        ...) noexcept;

    /**
     * Logs a failed ensure.
     * @param expression Text of the failed expression.
     * @param file Source file containing the ensure.
     * @param line Source line containing the ensure.
     * @param function Function containing the ensure.
     * @return Always false.
     */
    [[nodiscard]] bool ReportEnsureFailure(
        const char* expression,
        const char* file,
        std::uint32_t line,
        const char* function) noexcept;

    /**
     * Logs a formatted ensure failure.
     * @param expression Text of the failed expression.
     * @param file Source file containing the ensure.
     * @param line Source line containing the ensure.
     * @param function Function containing the ensure.
     * @param format printf-style diagnostic format.
     * @return Always false.
     */
    [[nodiscard]] bool ReportEnsureFailuref(
        const char* expression,
        const char* file,
        std::uint32_t line,
        const char* function,
        const char* format,
        ...) noexcept;
}

#if !defined(NDEBUG) || defined(ARDA_ENABLE_CHECKS)
    /** Indicates that fatal check evaluation is compiled in. */
    #define ARDA_CHECK_ENABLED 1
#else
    /** Indicates that fatal check evaluation is compiled out. */
    #define ARDA_CHECK_ENABLED 0
#endif

#if ARDA_CHECK_ENABLED
    /**
     * Verifies a condition when checks are enabled and terminates on failure.
     * @param Condition Expression to verify.
     */
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

    /**
     * Verifies a condition with a formatted diagnostic and terminates on failure.
     * @param Condition Expression to verify.
     * @param Format printf-style diagnostic format.
     */
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

    /**
     * Evaluates a condition and terminates on failure.
     * @param Condition Expression to verify.
     * @return The truth value of Condition.
     */
    #define ARDA_VERIFY(Condition) \
        (!!(Condition) || \
            (::arda::backend::ReportFatalCheck( \
                 #Condition, \
                 __FILE__, \
                 static_cast<std::uint32_t>(__LINE__), \
                 __func__), \
             false))

    /**
     * Evaluates a condition with a formatted diagnostic and terminates on failure.
     * @param Condition Expression to verify.
     * @param Format printf-style diagnostic format.
     * @return The truth value of Condition.
     */
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
    /** Compiles out a fatal condition check without evaluating it. */
    #define ARDA_CHECK(Condition) \
        do \
        { \
            (void)sizeof(Condition); \
        } while (false)

    /** Compiles out a formatted fatal condition check without evaluating it. */
    #define ARDA_CHECKF(Condition, Format, ...) \
        do \
        { \
            (void)sizeof(Condition); \
        } while (false)

    /** Evaluates a condition when fatal checks are disabled. */
    #define ARDA_VERIFY(Condition) (!!(Condition))
    /** Evaluates a condition and ignores its formatted diagnostic when checks are disabled. */
    #define ARDA_VERIFYF(Condition, Format, ...) (!!(Condition))
#endif

/**
 * Evaluates a recoverable assertion and reports failures.
 * @param Condition Expression to verify.
 * @return The truth value of Condition.
 */
#define ARDA_ENSURE(Condition) \
    (!!(Condition) || \
        ::arda::backend::ReportEnsureFailure( \
            #Condition, \
            __FILE__, \
            static_cast<std::uint32_t>(__LINE__), \
            __func__))

/**
 * Evaluates a recoverable assertion and reports failures with a formatted message.
 * @param Condition Expression to verify.
 * @param Format printf-style diagnostic format.
 * @return The truth value of Condition.
 */
#define ARDA_ENSURE_MSGF(Condition, Format, ...) \
    (!!(Condition) || \
        ::arda::backend::ReportEnsureFailuref( \
            #Condition, \
            __FILE__, \
            static_cast<std::uint32_t>(__LINE__), \
            __func__, \
            Format, \
            ##__VA_ARGS__))

/**
 * Reports an unconditional programmer error and terminates.
 * @param Format printf-style diagnostic format.
 */
#define ARDA_CHECK_MSG(Format, ...) ARDA_CHECKF(false, Format, ##__VA_ARGS__)