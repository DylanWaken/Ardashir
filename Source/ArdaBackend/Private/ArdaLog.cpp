#include "ArdaBackendPch.h"

#include "ArdaLog.h"

#include <cstdarg>

namespace arda::backend
{
    namespace
    {
        void DefaultLogOutput(
            const FArdaLogRecord& Record,
            void*) noexcept
        {
            std::fprintf(
                stderr,
                "[%s][%s] %s\n",
                Record.mCategory ? Record.mCategory : "",
                ToString(Record.mVerbosity),
                Record.mMessage ? Record.mMessage : "");
        }

        struct FArdaLogState
        {
            std::mutex mMutex;
            FArdaLogOutput mOutput = &DefaultLogOutput;
            void* mUserData = nullptr;
        };

        FArdaLogState& GetLogState()
        {
            static FArdaLogState state;
            return state;
        }

        void DispatchLogRecord(const FArdaLogRecord& Record) noexcept
        {
            FArdaLogOutput output = nullptr;
            void* userData = nullptr;
            {
                auto& state = GetLogState();
                std::lock_guard<std::mutex> lock(state.mMutex);
                output = state.mOutput;
                userData = state.mUserData;
            }

            output(Record, userData);
        }

        std::string FormatLogMessage(
            const char* Format,
            std::va_list Arguments)
        {
            if (!Format)
            {
                return {};
            }

            std::va_list countArguments;
            va_copy(countArguments, Arguments);
            const int requiredLength =
                std::vsnprintf(nullptr, 0, Format, countArguments);
            va_end(countArguments);

            if (requiredLength < 0)
            {
                return "Log message formatting failed.";
            }

            std::string message(
                static_cast<std::size_t>(requiredLength) + 1,
                '\0');
            std::va_list formatArguments;
            va_copy(formatArguments, Arguments);
            const int writtenLength = std::vsnprintf(
                message.data(),
                message.size(),
                Format,
                formatArguments);
            va_end(formatArguments);

            if (writtenLength < 0)
            {
                return "Log message formatting failed.";
            }

            message.resize(static_cast<std::size_t>(writtenLength));
            return message;
        }
    }

    FArdaLogCategory::FArdaLogCategory(
        const char* name,
        EArdaLogVerbosity minimumVerbosity) noexcept
        : mName(name ? name : "")
        , mMinimumVerbosity(minimumVerbosity)
    {
    }

    const char* FArdaLogCategory::GetName() const noexcept
    {
        return mName;
    }

    void FArdaLogCategory::SetMinimumVerbosity(
        EArdaLogVerbosity verbosity) noexcept
    {
        mMinimumVerbosity.store(verbosity, std::memory_order_relaxed);
    }

    EArdaLogVerbosity FArdaLogCategory::GetMinimumVerbosity() const noexcept
    {
        return mMinimumVerbosity.load(std::memory_order_relaxed);
    }

    bool FArdaLogCategory::IsEnabled(
        EArdaLogVerbosity verbosity) const noexcept
    {
        const auto minimumVerbosity = GetMinimumVerbosity();
        return verbosity != EArdaLogVerbosity::Off &&
            minimumVerbosity != EArdaLogVerbosity::Off &&
            static_cast<std::uint8_t>(verbosity) >=
                static_cast<std::uint8_t>(minimumVerbosity);
    }

    void SetLogOutput(FArdaLogOutput output, void* userData) noexcept
    {
        auto& state = GetLogState();
        std::lock_guard<std::mutex> lock(state.mMutex);
        state.mOutput = output ? output : &DefaultLogOutput;
        state.mUserData = output ? userData : nullptr;
    }

    void ResetLogOutput() noexcept
    {
        SetLogOutput(nullptr);
    }

    const char* ToString(EArdaLogVerbosity verbosity) noexcept
    {
        switch (verbosity)
        {
        case EArdaLogVerbosity::VeryVerbose:
            return "VeryVerbose";
        case EArdaLogVerbosity::Verbose:
            return "Verbose";
        case EArdaLogVerbosity::Log:
            return "Log";
        case EArdaLogVerbosity::Display:
            return "Display";
        case EArdaLogVerbosity::Warning:
            return "Warning";
        case EArdaLogVerbosity::Error:
            return "Error";
        case EArdaLogVerbosity::Fatal:
            return "Fatal";
        case EArdaLogVerbosity::Off:
            return "Off";
        }
        return "Unknown";
    }

    void Logf(
        const FArdaLogCategory& category,
        EArdaLogVerbosity verbosity,
        const char* file,
        std::uint32_t line,
        const char* function,
        const char* format,
        ...) noexcept
    {
        if (!category.IsEnabled(verbosity))
        {
            return;
        }

        std::va_list arguments;
        va_start(arguments, format);
        const std::string message = FormatLogMessage(format, arguments);
        va_end(arguments);

        const FArdaLogRecord record{
            category.GetName(),
            verbosity,
            message.c_str(),
            file ? file : "",
            line,
            function ? function : ""};
        DispatchLogRecord(record);
    }
}
