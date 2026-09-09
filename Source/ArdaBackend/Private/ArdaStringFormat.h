#pragma once

#include <EASTL/string.h>
#include <cstdarg>
#include <cstdio>

namespace arda
{
    inline eastl::string FormatArdaMessage(
        const char* Format, std::va_list Arguments, const char* FailureMessage)
    {
        if (!Format)
            return {};

        std::va_list CountArguments;
        va_copy(CountArguments, Arguments);
        const int RequiredLength = std::vsnprintf(nullptr, 0, Format, CountArguments);
        va_end(CountArguments);
        if (RequiredLength < 0)
            return FailureMessage;

        eastl::string Message(static_cast<size_t>(RequiredLength) + 1, '\0');
        std::va_list FormatArguments;
        va_copy(FormatArguments, Arguments);
        const int WrittenLength = std::vsnprintf(Message.data(), Message.size(), Format, FormatArguments);
        va_end(FormatArguments);
        if (WrittenLength < 0 || static_cast<size_t>(WrittenLength) >= Message.size())
            return FailureMessage;
        Message.resize(static_cast<size_t>(WrittenLength));
        return Message;
    }
}
