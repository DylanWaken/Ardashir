#pragma once

#include <array>
#include <cstdint>

namespace arda::trace::detail
{
    inline constexpr std::array<char, 8> TraceMagic = {'A', 'R', 'D', 'A', 'T', 'R', 'C', '1'};
    inline constexpr std::uint32_t TraceVersion = 1;
    inline constexpr std::uint32_t TraceEndianMarker = 0x01020304;

    enum class EArdaTraceRecordType : std::uint8_t
    {
        Name = 1,
        Thread = 2,
        Scope = 3,
        Counter = 4,
        Marker = 5,
        CaptureEnd = 255
    };

    enum class EArdaBufferedEventType : std::uint8_t
    {
        Scope,
        Counter,
        Marker
    };

    struct FArdaBufferedEvent
    {
        EArdaBufferedEventType mType = EArdaBufferedEventType::Marker;
        std::uint32_t mThreadId = 0;
        std::uint32_t mNameId = 0;
        std::uint64_t mPrimaryId = 0;
        std::uint64_t mSecondaryId = 0;
        std::uint64_t mStartNanoseconds = 0;
        std::uint64_t mEndNanoseconds = 0;
        double mValue = 0.0;
    };
}
