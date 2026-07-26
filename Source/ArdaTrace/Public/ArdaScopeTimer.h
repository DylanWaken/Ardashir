#pragma once

#include "ArdaTrace.h"

#include <cstdint>

namespace arda::trace
{
    /** Records the execution interval and parent relationship of a lexical scope. */
    class FArdaScopeTimer final
    {
    public:
        /**
         * Begins a scope associated with a registered trace name.
         * @param Name Registered name used by the offline viewer.
         */
        explicit FArdaScopeTimer(const FArdaTraceName& Name) noexcept
            : bRecording(IsTraceCaptureActive())
            , NameId(Name.GetId())
            , ScopeId(bRecording ? detail::AllocateScopeId() : 0)
            , ParentScopeId(bRecording ? ActiveScopeId : 0)
            , StartNanoseconds(bRecording ? detail::GetTraceTimestampNanoseconds() : 0)
        {
            if (bRecording)
            {
                ActiveScopeId = ScopeId;
            }
        }

        /** Completes and records the scope before restoring its parent scope. */
        ~FArdaScopeTimer() noexcept
        {
            if (!bRecording)
            {
                return;
            }

            const std::uint64_t EndNanoseconds = detail::GetTraceTimestampNanoseconds();
            ActiveScopeId = ParentScopeId;
            detail::RecordScope(
                NameId,
                ScopeId,
                ParentScopeId,
                StartNanoseconds,
                EndNanoseconds);
        }

        /** Scope timers cannot be copied because each instance records once. */
        FArdaScopeTimer(const FArdaScopeTimer&) = delete;
        /** Scope timers cannot be copy-assigned. */
        FArdaScopeTimer& operator=(const FArdaScopeTimer&) = delete;
        /** Scope timers cannot be moved because they are bound to one scope. */
        FArdaScopeTimer(FArdaScopeTimer&&) = delete;
        /** Scope timers cannot be move-assigned. */
        FArdaScopeTimer& operator=(FArdaScopeTimer&&) = delete;

    private:
        inline static thread_local std::uint64_t ActiveScopeId = 0;

        bool bRecording;
        std::uint32_t NameId;
        std::uint64_t ScopeId;
        std::uint64_t ParentScopeId;
        std::uint64_t StartNanoseconds;
    };
}

#if ARDASHIR_ENABLE_TRACE
/** Times the enclosing scope and uses its function name as the trace label. */
#define ARDA_SCOPE_TIMER() \
    ::arda::trace::FArdaScopeTimer \
        ARDA_PRIVATE_JOIN_TRACE_NAMES(ArdaScopeTimer, __LINE__)( \
            [](const char* FunctionName) -> const ::arda::trace::FArdaTraceName& \
            { \
                static const ::arda::trace::FArdaTraceName Name(FunctionName); \
                return Name; \
            }(__func__))

/**
 * Times the enclosing scope using a stable string label.
 * Nested timers retain explicit parent identifiers in the capture.
 *
 * @code
 * ARDA_NAMED_SCOPE_TIMER("Rendering");
 * {
 *     ARDA_NAMED_SCOPE_TIMER("Raytracer");
 *     {
 *         ARDA_NAMED_SCOPE_TIMER("Gaussian Sampler");
 *     }
 * }
 * @endcode
 */
#define ARDA_NAMED_SCOPE_TIMER(ScopeName) \
    ::arda::trace::FArdaScopeTimer \
        ARDA_PRIVATE_JOIN_TRACE_NAMES(ArdaScopeTimer, __LINE__)( \
            [](const char* Label) -> const ::arda::trace::FArdaTraceName& \
            { \
                static const ::arda::trace::FArdaTraceName TraceName(Label); \
                return TraceName; \
            }(ScopeName))
#else
#define ARDA_SCOPE_TIMER() ((void)0)
#define ARDA_NAMED_SCOPE_TIMER(Name) ((void)0)
#endif
