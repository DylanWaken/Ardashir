#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace arda::trace
{
    /** Describes one completed CPU scope from a capture. */
    struct FArdaTraceScope
    {
        /** Identifier unique within the process. */
        std::uint64_t ScopeId = 0;
        /** Parent identifier, or zero for a root scope. */
        std::uint64_t ParentScopeId = 0;
        /** Identifier of the thread that executed the scope. */
        std::uint32_t ThreadId = 0;
        /** Identifier of the scope label. */
        std::uint32_t NameId = 0;
        /** Scope start relative to the capture clock epoch. */
        std::uint64_t StartNanoseconds = 0;
        /** Scope end relative to the capture clock epoch. */
        std::uint64_t EndNanoseconds = 0;
    };

    /** Describes one sampled numeric counter. */
    struct FArdaTraceCounter
    {
        /** Identifier of the thread that sampled the counter. */
        std::uint32_t ThreadId = 0;
        /** Identifier of the counter label. */
        std::uint32_t NameId = 0;
        /** Sample timestamp relative to the capture clock epoch. */
        std::uint64_t TimestampNanoseconds = 0;
        /** Sampled numeric value. */
        double Value = 0.0;
    };

    /** Describes one instantaneous marker. */
    struct FArdaTraceMarker
    {
        /** Identifier of the thread that emitted the marker. */
        std::uint32_t ThreadId = 0;
        /** Identifier of the marker label. */
        std::uint32_t NameId = 0;
        /** Marker timestamp relative to the capture clock epoch. */
        std::uint64_t TimestampNanoseconds = 0;
    };

    /** Contains the decoded data required by an offline trace viewer. */
    struct FArdaTraceSession
    {
        /** Steady-clock timestamp captured when recording began. */
        std::uint64_t OriginNanoseconds = 0;
        /** Registered labels keyed by trace name identifier. */
        std::unordered_map<std::uint32_t, std::string> Names;
        /** Display names keyed by trace thread identifier. */
        std::unordered_map<std::uint32_t, std::string> Threads;
        /** Completed CPU scopes. */
        std::vector<FArdaTraceScope> Scopes;
        /** Numeric counter samples. */
        std::vector<FArdaTraceCounter> Counters;
        /** Instantaneous markers. */
        std::vector<FArdaTraceMarker> Markers;
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
        std::string& OutError);
}
