#pragma once

#include <cstdint>
#include <filesystem>
#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

namespace arda::trace
{
    /** Describes one completed CPU scope from a capture. */
    struct FArdaTraceScope
    {
        /** Identifier unique within the process. */
        std::uint64_t mScopeId = 0;
        /** Parent identifier, or zero for a root scope. */
        std::uint64_t mParentScopeId = 0;
        /** Identifier of the thread that executed the scope. */
        std::uint32_t mThreadId = 0;
        /** Identifier of the scope label. */
        std::uint32_t mNameId = 0;
        /** Scope start relative to the capture clock epoch. */
        std::uint64_t mStartNanoseconds = 0;
        /** Scope end relative to the capture clock epoch. */
        std::uint64_t mEndNanoseconds = 0;
    };

    /** Describes one sampled numeric counter. */
    struct FArdaTraceCounter
    {
        /** Identifier of the thread that sampled the counter. */
        std::uint32_t mThreadId = 0;
        /** Identifier of the counter label. */
        std::uint32_t mNameId = 0;
        /** Sample timestamp relative to the capture clock epoch. */
        std::uint64_t mTimestampNanoseconds = 0;
        /** Sampled numeric value. */
        double mValue = 0.0;
    };

    /** Describes one instantaneous marker. */
    struct FArdaTraceMarker
    {
        /** Identifier of the thread that emitted the marker. */
        std::uint32_t mThreadId = 0;
        /** Identifier of the marker label. */
        std::uint32_t mNameId = 0;
        /** Marker timestamp relative to the capture clock epoch. */
        std::uint64_t mTimestampNanoseconds = 0;
    };

    /** Contains the decoded data required by an offline trace viewer. */
    struct FArdaTraceSession
    {
        /** Steady-clock timestamp captured when recording began. */
        std::uint64_t mOriginNanoseconds = 0;
        /** Registered labels keyed by trace name identifier. */
        eastl::unordered_map<std::uint32_t, eastl::string> mNames;
        /** Display names keyed by trace thread identifier. */
        eastl::unordered_map<std::uint32_t, eastl::string> mThreads;
        /** Completed CPU scopes. */
        eastl::vector<FArdaTraceScope> mScopes;
        /** Numeric counter samples. */
        eastl::vector<FArdaTraceCounter> mCounters;
        /** Instantaneous markers. */
        eastl::vector<FArdaTraceMarker> mMarkers;
    };

    /**
     * Reads and validates an Arda binary trace capture.
     * @param FilePath Capture file to decode.
     * @param OutSession Receives decoded data after complete validation.
     * @param OutError Receives a diagnostic message when decoding fails.
     * @return True when the complete capture was decoded successfully.
     */
    [[nodiscard]] bool ReadTraceCapture(
        const std::filesystem::path& FilePath,
        FArdaTraceSession& OutSession,
        eastl::string& OutError);
}
