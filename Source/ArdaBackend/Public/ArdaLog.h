/** @file ArdaLog.h
 *  @brief Declares structured logging categories, records, outputs, and macros.
 */
#pragma once

#include <EASTL/atomic.h>
#include <cstdint>

namespace arda::backend
{
    /** Controls the importance and filtering of a log record. */
    enum class EArdaLogVerbosity : std::uint8_t
    {
        VeryVerbose,
        Verbose,
        Log,
        Display,
        Warning,
        Error,
        Fatal,
        Off
    };

    /** Defines a named log scope and its runtime minimum verbosity. */
    class FArdaLogCategory
    {
    public:
        /**
         * Creates a named log category.
         * @param name Stable null-terminated category name.
         * @param minimumVerbosity Least-important verbosity initially emitted.
         */
        explicit FArdaLogCategory(
            const char* name,
            EArdaLogVerbosity minimumVerbosity = EArdaLogVerbosity::Log) noexcept;

        /** @return The stable name emitted with records from this category. */
        [[nodiscard]] const char* GetName() const noexcept;
        /**
         * Changes the least-important verbosity emitted by this category.
         * @param verbosity New minimum verbosity.
         */
        void SetMinimumVerbosity(EArdaLogVerbosity verbosity) noexcept;
        /** @return The category's current minimum verbosity. */
        [[nodiscard]] EArdaLogVerbosity GetMinimumVerbosity() const noexcept;
        /**
         * Tests whether a record would be emitted.
         * @param verbosity Verbosity to test.
         * @return True when the category permits the requested verbosity.
         */
        [[nodiscard]] bool IsEnabled(EArdaLogVerbosity verbosity) const noexcept;

    private:
        /** Stable null-terminated category name. */
        const char* mName;
        /** Least-important verbosity currently emitted. */
        eastl::atomic<EArdaLogVerbosity> mMinimumVerbosity;
    };

    /**
     * Describes one formatted log record.
     * All pointed-to text remains valid only for the duration of the output callback.
     */
    struct FArdaLogRecord
    {
        /** Name of the category that emitted the record. */
        const char* mCategory = "";
        /** Severity and filtering verbosity of the record. */
        EArdaLogVerbosity mVerbosity = EArdaLogVerbosity::Log;
        /** Formatted record message. */
        const char* mMessage = "";
        /** Source file that emitted the record. */
        const char* mFile = "";
        /** Source line that emitted the record. */
        std::uint32_t mLine = 0;
        /** Function that emitted the record. */
        const char* mFunction = "";
    };

    /**
     * Receives a structured log record synchronously.
     * The callback may log recursively and must not throw.
     * @param record Record being emitted.
     * @param userData Caller-owned context supplied to SetLogOutput.
     */
    using FArdaLogOutput = void (*)(
        const FArdaLogRecord& record,
        void* userData) noexcept;

    /**
     * Replaces the process-wide log output.
     * The caller owns userData and must keep it valid while the callback is installed.
     * Passing null restores the default stderr output.
     * @param output Output callback to install, or null for the default output.
     * @param userData Caller-owned context passed to the callback.
     */
    void SetLogOutput(FArdaLogOutput output, void* userData = nullptr) noexcept;
    /** Restores the default stderr log output. */
    void ResetLogOutput() noexcept;
    /**
     * Returns a readable name for a log verbosity.
     * @param verbosity Verbosity to name.
     * @return A stable null-terminated verbosity name.
     */
    [[nodiscard]] const char* ToString(EArdaLogVerbosity verbosity) noexcept;

    /**
     * Formats and emits a record when the category permits its verbosity.
     * @param category Category used for filtering and labeling.
     * @param verbosity Record verbosity.
     * @param file Source file emitting the record.
     * @param line Source line emitting the record.
     * @param function Function emitting the record.
     * @param format printf-style record format.
     */
    void Logf(
        const FArdaLogCategory& category,
        EArdaLogVerbosity verbosity,
        const char* file,
        std::uint32_t line,
        const char* function,
        const char* format,
        ...) noexcept;
}

/** Declares an externally defined log category. */
#define ARDA_DECLARE_LOG_CATEGORY_EXTERN(CategoryName) \
    extern ::arda::backend::FArdaLogCategory CategoryName

/**
 * Defines a log category whose emitted name matches its symbol.
 * @param CategoryName Category variable name.
 * @param DefaultVerbosity Initial minimum verbosity enumerator.
 */
#define ARDA_DEFINE_LOG_CATEGORY(CategoryName, DefaultVerbosity) \
    ::arda::backend::FArdaLogCategory CategoryName( \
        #CategoryName, \
        ::arda::backend::EArdaLogVerbosity::DefaultVerbosity)

/**
 * Defines a log category with an explicit emitted scope name.
 * @param CategoryName Category variable name.
 * @param ScopeName Stable null-terminated emitted name.
 * @param DefaultVerbosity Initial minimum verbosity enumerator.
 */
#define ARDA_DEFINE_LOG_CATEGORY_NAMED(CategoryName, ScopeName, DefaultVerbosity) \
    ::arda::backend::FArdaLogCategory CategoryName( \
        ScopeName, \
        ::arda::backend::EArdaLogVerbosity::DefaultVerbosity)

/**
 * Emits a formatted log record when the category permits the verbosity.
 * @param CategoryName Log category expression.
 * @param Verbosity EArdaLogVerbosity enumerator suffix.
 */
#define ARDA_LOG(CategoryName, Verbosity, ...) \
    do \
    { \
        const auto& ArdaLogCategory = (CategoryName); \
        constexpr auto ArdaLogVerbosity = \
            ::arda::backend::EArdaLogVerbosity::Verbosity; \
        if (ArdaLogCategory.IsEnabled(ArdaLogVerbosity)) \
        { \
            ::arda::backend::Logf( \
                ArdaLogCategory, \
                ArdaLogVerbosity, \
                __FILE__, \
                static_cast<std::uint32_t>(__LINE__), \
                __func__, \
                __VA_ARGS__); \
        } \
    } while (false)
