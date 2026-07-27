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
        explicit FArdaLogCategory(
            const char* name,
            EArdaLogVerbosity minimumVerbosity = EArdaLogVerbosity::Log) noexcept;

        /** Returns the stable name emitted with records from this category. */
        [[nodiscard]] const char* GetName() const noexcept;
        /** Changes the least-important verbosity emitted by this category. */
        void SetMinimumVerbosity(EArdaLogVerbosity verbosity) noexcept;
        /** Returns the category's current minimum verbosity. */
        [[nodiscard]] EArdaLogVerbosity GetMinimumVerbosity() const noexcept;
        /** Returns whether a record at the requested verbosity would be emitted. */
        [[nodiscard]] bool IsEnabled(EArdaLogVerbosity verbosity) const noexcept;

    private:
        const char* mName;
        eastl::atomic<EArdaLogVerbosity> mMinimumVerbosity;
    };

    /**
     * Describes one formatted log record.
     * All pointed-to text remains valid only for the duration of the output callback.
     */
    struct FArdaLogRecord
    {
        const char* mCategory = "";
        EArdaLogVerbosity mVerbosity = EArdaLogVerbosity::Log;
        const char* mMessage = "";
        const char* mFile = "";
        std::uint32_t mLine = 0;
        const char* mFunction = "";
    };

    /**
     * Receives a structured log record synchronously.
     * The callback may log recursively and must not throw.
     */
    using FArdaLogOutput = void (*)(
        const FArdaLogRecord& record,
        void* userData) noexcept;

    /**
     * Replaces the process-wide log output.
     * The caller owns userData and must keep it valid while the callback is installed.
     * Passing null restores the default stderr output.
     */
    void SetLogOutput(FArdaLogOutput output, void* userData = nullptr) noexcept;
    /** Restores the default stderr log output. */
    void ResetLogOutput() noexcept;
    /** Returns a readable name for a log verbosity. */
    [[nodiscard]] const char* ToString(EArdaLogVerbosity verbosity) noexcept;

    /** Formats and emits a record when the category permits its verbosity. */
    void Logf(
        const FArdaLogCategory& category,
        EArdaLogVerbosity verbosity,
        const char* file,
        std::uint32_t line,
        const char* function,
        const char* format,
        ...) noexcept;
}

#define ARDA_DECLARE_LOG_CATEGORY_EXTERN(CategoryName) \
    extern ::arda::backend::FArdaLogCategory CategoryName

#define ARDA_DEFINE_LOG_CATEGORY(CategoryName, DefaultVerbosity) \
    ::arda::backend::FArdaLogCategory CategoryName( \
        #CategoryName, \
        ::arda::backend::EArdaLogVerbosity::DefaultVerbosity)

#define ARDA_DEFINE_LOG_CATEGORY_NAMED(CategoryName, ScopeName, DefaultVerbosity) \
    ::arda::backend::FArdaLogCategory CategoryName( \
        ScopeName, \
        ::arda::backend::EArdaLogVerbosity::DefaultVerbosity)

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
