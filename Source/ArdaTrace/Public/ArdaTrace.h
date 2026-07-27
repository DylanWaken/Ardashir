#pragma once

#include <cstdint>
#include <filesystem>
#include <EASTL/string.h>

namespace arda::trace
{
    /** Identifies a trace name that is registered once and reused by events. */
    class FArdaTraceName final
    {
    public:
        /**
         * Registers a stable trace label.
         * @param Name Null-terminated label that remains valid for the process lifetime.
         */
        explicit FArdaTraceName(const char* Name);

        /** Returns the process-wide identifier assigned to the label. */
        [[nodiscard]] std::uint32_t GetId() const noexcept;

    private:
        std::uint32_t mId;
    };

    /**
     * Starts a process-wide trace capture.
     * Capture lifecycle calls must not overlap active instrumented worker threads.
     * @param FilePath Destination for the binary trace.
     * @return True when the capture file was opened successfully.
     */
    [[nodiscard]] bool StartTraceCapture(const std::filesystem::path& FilePath);

    /**
     * Flushes all thread buffers and closes the active capture.
     * Capture lifecycle calls must not overlap active instrumented worker threads.
     * @return True when the complete capture was written successfully.
     */
    [[nodiscard]] bool StopTraceCapture();

    /** Returns whether a trace capture is currently accepting events. */
    [[nodiscard]] bool IsTraceCaptureActive() noexcept;

    /** Returns the most recent trace recorder error. */
    [[nodiscard]] eastl::string GetTraceError();

    /**
     * Assigns a display name to the calling thread.
     * @param ThreadName Name copied by the trace recorder.
     */
    void SetCurrentTraceThreadName(const char* ThreadName);

    /**
     * Records a numeric counter sample at the current timestamp.
     * @param Name Registered counter name.
     * @param Value Counter value.
     */
    void RecordTraceCounter(const FArdaTraceName& Name, double Value) noexcept;

    /**
     * Records an instantaneous marker at the current timestamp.
     * @param Name Registered marker name.
     */
    void RecordTraceMarker(const FArdaTraceName& Name) noexcept;

    namespace detail
    {
        /** Returns the next process-wide scope identifier. */
        [[nodiscard]] std::uint64_t AllocateScopeId() noexcept;

        /**
         * Records a completed CPU scope.
         * @param NameId Registered scope name identifier.
         * @param ScopeId Unique scope identifier.
         * @param ParentScopeId Parent scope identifier, or zero for a root scope.
         * @param StartNanoseconds Scope start on the steady-clock timeline.
         * @param EndNanoseconds Scope end on the steady-clock timeline.
         */
        void RecordScope(
            std::uint32_t NameId,
            std::uint64_t ScopeId,
            std::uint64_t ParentScopeId,
            std::uint64_t StartNanoseconds,
            std::uint64_t EndNanoseconds) noexcept;

        /** Returns a steady-clock timestamp in nanoseconds. */
        [[nodiscard]] std::uint64_t GetTraceTimestampNanoseconds() noexcept;
    }
}

#if !defined(ARDASHIR_ENABLE_TRACE)
#define ARDASHIR_ENABLE_TRACE 1
#endif

#if ARDASHIR_ENABLE_TRACE
#define ARDA_PRIVATE_JOIN_TRACE_NAMES_INNER(Prefix, Suffix) Prefix##Suffix
#define ARDA_PRIVATE_JOIN_TRACE_NAMES(Prefix, Suffix) \
    ARDA_PRIVATE_JOIN_TRACE_NAMES_INNER(Prefix, Suffix)

/** Records a numeric counter sample under a stable string label. */
#define ARDA_TRACE_COUNTER(Name, Value) \
    do \
    { \
        static const ::arda::trace::FArdaTraceName \
            ARDA_PRIVATE_JOIN_TRACE_NAMES(ArdaTraceCounterName, __LINE__)(Name); \
        ::arda::trace::RecordTraceCounter( \
            ARDA_PRIVATE_JOIN_TRACE_NAMES(ArdaTraceCounterName, __LINE__), \
            static_cast<double>(Value)); \
    } while (false)

/** Records an instantaneous marker under a stable string label. */
#define ARDA_TRACE_MARKER(Name) \
    do \
    { \
        static const ::arda::trace::FArdaTraceName \
            ARDA_PRIVATE_JOIN_TRACE_NAMES(ArdaTraceMarkerName, __LINE__)(Name); \
        ::arda::trace::RecordTraceMarker( \
            ARDA_PRIVATE_JOIN_TRACE_NAMES(ArdaTraceMarkerName, __LINE__)); \
    } while (false)
#else
#define ARDA_TRACE_COUNTER(Name, Value) ((void)0)
#define ARDA_TRACE_MARKER(Name) ((void)0)
#endif
