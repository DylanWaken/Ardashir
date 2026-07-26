#include "ArdaTracePch.h"

#include "ArdaTrace.h"
#include "ArdaTraceFormat.h"

#include <algorithm>
#include <cstring>

namespace arda::trace
{
    namespace
    {
        constexpr std::size_t EventChunkCapacity = 1024;

        struct FArdaThreadBuffer
        {
            std::uint32_t ThreadId = 0;
            std::uint64_t Generation = 0;
            std::string Name;
            std::vector<detail::FArdaBufferedEvent> Events;
        };

        struct FArdaTraceState
        {
            std::mutex Mutex;
            std::ofstream Stream;
            std::atomic<bool> bActive = false;
            std::atomic<std::uint64_t> Generation = 0;
            std::atomic<std::uint32_t> NextNameId = 1;
            std::atomic<std::uint32_t> NextThreadId = 1;
            std::atomic<std::uint64_t> NextScopeId = 1;
            std::unordered_map<std::string, std::uint32_t> NameIds;
            std::unordered_map<std::uint32_t, std::string> Names;
            std::vector<std::shared_ptr<FArdaThreadBuffer>> ThreadBuffers;
            std::string Error;
            std::uint64_t OriginNanoseconds = 0;
        };

        FArdaTraceState& GetState()
        {
            static FArdaTraceState State;
            return State;
        }

        template <typename ValueType>
        void WriteValue(std::ofstream& Stream, const ValueType& Value)
        {
            Stream.write(
                reinterpret_cast<const char*>(&Value),
                static_cast<std::streamsize>(sizeof(ValueType)));
        }

        void WriteBytes(std::ofstream& Stream, const char* Data, std::size_t Size)
        {
            Stream.write(Data, static_cast<std::streamsize>(Size));
        }

        void WriteNameRecord(
            std::ofstream& Stream,
            std::uint32_t NameId,
            const std::string& Name)
        {
            const detail::EArdaTraceRecordType Type = detail::EArdaTraceRecordType::Name;
            const std::uint32_t Length = static_cast<std::uint32_t>(Name.size());
            WriteValue(Stream, Type);
            WriteValue(Stream, NameId);
            WriteValue(Stream, Length);
            WriteBytes(Stream, Name.data(), Name.size());
        }

        void WriteThreadRecord(
            std::ofstream& Stream,
            std::uint32_t ThreadId,
            const std::string& Name)
        {
            const detail::EArdaTraceRecordType Type = detail::EArdaTraceRecordType::Thread;
            const std::uint32_t Length = static_cast<std::uint32_t>(Name.size());
            WriteValue(Stream, Type);
            WriteValue(Stream, ThreadId);
            WriteValue(Stream, Length);
            WriteBytes(Stream, Name.data(), Name.size());
        }

        void WriteEvent(std::ofstream& Stream, const detail::FArdaBufferedEvent& Event)
        {
            switch (Event.Type)
            {
            case detail::EArdaBufferedEventType::Scope:
            {
                const detail::EArdaTraceRecordType Type = detail::EArdaTraceRecordType::Scope;
                WriteValue(Stream, Type);
                WriteValue(Stream, Event.ThreadId);
                WriteValue(Stream, Event.NameId);
                WriteValue(Stream, Event.PrimaryId);
                WriteValue(Stream, Event.SecondaryId);
                WriteValue(Stream, Event.StartNanoseconds);
                WriteValue(Stream, Event.EndNanoseconds);
                break;
            }
            case detail::EArdaBufferedEventType::Counter:
            {
                const detail::EArdaTraceRecordType Type = detail::EArdaTraceRecordType::Counter;
                WriteValue(Stream, Type);
                WriteValue(Stream, Event.ThreadId);
                WriteValue(Stream, Event.NameId);
                WriteValue(Stream, Event.StartNanoseconds);
                WriteValue(Stream, Event.Value);
                break;
            }
            case detail::EArdaBufferedEventType::Marker:
            {
                const detail::EArdaTraceRecordType Type = detail::EArdaTraceRecordType::Marker;
                WriteValue(Stream, Type);
                WriteValue(Stream, Event.ThreadId);
                WriteValue(Stream, Event.NameId);
                WriteValue(Stream, Event.StartNanoseconds);
                break;
            }
            }
        }

        std::shared_ptr<FArdaThreadBuffer> GetThreadBuffer()
        {
            thread_local std::shared_ptr<FArdaThreadBuffer> ThreadBuffer;
            if (ThreadBuffer != nullptr)
            {
                return ThreadBuffer;
            }

            FArdaTraceState& State = GetState();
            ThreadBuffer = std::make_shared<FArdaThreadBuffer>();
            ThreadBuffer->ThreadId = State.NextThreadId.fetch_add(1, std::memory_order_relaxed);
            ThreadBuffer->Name = "Thread " + std::to_string(ThreadBuffer->ThreadId);
            ThreadBuffer->Events.reserve(EventChunkCapacity);
            return ThreadBuffer;
        }

        bool PrepareThreadBuffer(
            const std::shared_ptr<FArdaThreadBuffer>& ThreadBuffer,
            std::uint64_t Generation)
        {
            if (ThreadBuffer->Generation == Generation)
            {
                return true;
            }

            ThreadBuffer->Events.clear();
            ThreadBuffer->Generation = Generation;

            FArdaTraceState& State = GetState();
            std::lock_guard<std::mutex> Lock(State.Mutex);
            if (!State.bActive.load(std::memory_order_relaxed)
                || State.Generation.load(std::memory_order_relaxed) != Generation)
            {
                return false;
            }

            State.ThreadBuffers.push_back(ThreadBuffer);
            WriteThreadRecord(State.Stream, ThreadBuffer->ThreadId, ThreadBuffer->Name);
            return static_cast<bool>(State.Stream);
        }

        void PublishThreadEvents(FArdaThreadBuffer& ThreadBuffer)
        {
            if (ThreadBuffer.Events.empty())
            {
                return;
            }

            FArdaTraceState& State = GetState();
            std::lock_guard<std::mutex> Lock(State.Mutex);
            if (State.bActive.load(std::memory_order_relaxed)
                && State.Generation.load(std::memory_order_relaxed) == ThreadBuffer.Generation)
            {
                for (const detail::FArdaBufferedEvent& Event : ThreadBuffer.Events)
                {
                    WriteEvent(State.Stream, Event);
                }

                if (!State.Stream)
                {
                    State.Error = "Failed while writing trace events.";
                    State.bActive.store(false, std::memory_order_release);
                }
            }
            ThreadBuffer.Events.clear();
        }

        void BufferEvent(detail::FArdaBufferedEvent Event) noexcept
        {
            FArdaTraceState& State = GetState();
            if (!State.bActive.load(std::memory_order_acquire))
            {
                return;
            }

            try
            {
                const std::uint64_t Generation =
                    State.Generation.load(std::memory_order_acquire);
                const std::shared_ptr<FArdaThreadBuffer> ThreadBuffer = GetThreadBuffer();
                if (!PrepareThreadBuffer(ThreadBuffer, Generation))
                {
                    return;
                }

                Event.ThreadId = ThreadBuffer->ThreadId;
                ThreadBuffer->Events.push_back(Event);
                if (ThreadBuffer->Events.size() >= EventChunkCapacity)
                {
                    PublishThreadEvents(*ThreadBuffer);
                }
            }
            catch (...)
            {
                State.bActive.store(false, std::memory_order_release);
            }
        }

        std::uint32_t RegisterName(const char* Name)
        {
            FArdaTraceState& State = GetState();
            const std::string StableName = Name != nullptr ? Name : "<unnamed>";
            std::lock_guard<std::mutex> Lock(State.Mutex);

            const auto ExistingName = State.NameIds.find(StableName);
            if (ExistingName != State.NameIds.end())
            {
                return ExistingName->second;
            }

            const std::uint32_t NameId =
                State.NextNameId.fetch_add(1, std::memory_order_relaxed);
            State.NameIds.emplace(StableName, NameId);
            State.Names.emplace(NameId, StableName);
            if (State.bActive.load(std::memory_order_relaxed))
            {
                WriteNameRecord(State.Stream, NameId, StableName);
            }
            return NameId;
        }
    }

    FArdaTraceName::FArdaTraceName(const char* Name)
        : Id(RegisterName(Name))
    {
    }

    std::uint32_t FArdaTraceName::GetId() const noexcept
    {
        return Id;
    }

    bool StartTraceCapture(const std::filesystem::path& FilePath)
    {
        FArdaTraceState& State = GetState();
        std::lock_guard<std::mutex> Lock(State.Mutex);
        if (State.bActive.load(std::memory_order_relaxed))
        {
            State.Error = "A trace capture is already active.";
            return false;
        }

        State.Stream.close();
        State.Stream.clear();
        State.Stream.open(FilePath, std::ios::binary | std::ios::trunc);
        if (!State.Stream)
        {
            State.Error = "Could not open the trace capture file.";
            return false;
        }

        State.Error.clear();
        State.OriginNanoseconds = detail::GetTraceTimestampNanoseconds();
        const std::uint64_t Generation =
            State.Generation.fetch_add(1, std::memory_order_relaxed) + 1;
        (void)Generation;

        WriteBytes(State.Stream, detail::TraceMagic.data(), detail::TraceMagic.size());
        WriteValue(State.Stream, detail::TraceVersion);
        WriteValue(State.Stream, detail::TraceEndianMarker);
        WriteValue(State.Stream, State.OriginNanoseconds);
        for (const auto& [NameId, Name] : State.Names)
        {
            WriteNameRecord(State.Stream, NameId, Name);
        }

        if (!State.Stream)
        {
            State.Error = "Could not write the trace capture header.";
            State.Stream.close();
            return false;
        }

        State.bActive.store(true, std::memory_order_release);
        return true;
    }

    bool StopTraceCapture()
    {
        FArdaTraceState& State = GetState();
        const bool bWasActive = State.bActive.exchange(false, std::memory_order_acq_rel);
        if (!bWasActive && !State.Stream.is_open())
        {
            std::lock_guard<std::mutex> Lock(State.Mutex);
            if (State.Error.empty())
            {
                State.Error = "No trace capture is active.";
            }
            return false;
        }

        std::lock_guard<std::mutex> Lock(State.Mutex);
        const std::uint64_t Generation = State.Generation.load(std::memory_order_relaxed);
        for (const std::shared_ptr<FArdaThreadBuffer>& ThreadBuffer : State.ThreadBuffers)
        {
            if (ThreadBuffer->Generation != Generation)
            {
                continue;
            }
            for (const detail::FArdaBufferedEvent& Event : ThreadBuffer->Events)
            {
                WriteEvent(State.Stream, Event);
            }
            ThreadBuffer->Events.clear();
        }
        State.ThreadBuffers.clear();

        const detail::EArdaTraceRecordType EndType = detail::EArdaTraceRecordType::CaptureEnd;
        WriteValue(State.Stream, EndType);
        State.Stream.flush();
        const bool bSucceeded = static_cast<bool>(State.Stream);
        State.Stream.close();
        if (!bSucceeded && State.Error.empty())
        {
            State.Error = "Failed while finalizing the trace capture.";
        }
        return bSucceeded;
    }

    bool IsTraceCaptureActive() noexcept
    {
        return GetState().bActive.load(std::memory_order_acquire);
    }

    std::string GetTraceError()
    {
        FArdaTraceState& State = GetState();
        std::lock_guard<std::mutex> Lock(State.Mutex);
        return State.Error;
    }

    void SetCurrentTraceThreadName(const char* ThreadName)
    {
        const std::shared_ptr<FArdaThreadBuffer> ThreadBuffer = GetThreadBuffer();
        ThreadBuffer->Name = ThreadName != nullptr ? ThreadName : "<unnamed thread>";

        FArdaTraceState& State = GetState();
        if (!State.bActive.load(std::memory_order_acquire))
        {
            return;
        }

        const std::uint64_t Generation = State.Generation.load(std::memory_order_acquire);
        if (!PrepareThreadBuffer(ThreadBuffer, Generation))
        {
            return;
        }

        std::lock_guard<std::mutex> Lock(State.Mutex);
        if (State.bActive.load(std::memory_order_relaxed))
        {
            WriteThreadRecord(State.Stream, ThreadBuffer->ThreadId, ThreadBuffer->Name);
        }
    }

    void RecordTraceCounter(const FArdaTraceName& Name, double Value) noexcept
    {
        if (!IsTraceCaptureActive())
        {
            return;
        }

        detail::FArdaBufferedEvent Event;
        Event.Type = detail::EArdaBufferedEventType::Counter;
        Event.NameId = Name.GetId();
        Event.StartNanoseconds = detail::GetTraceTimestampNanoseconds();
        Event.Value = Value;
        BufferEvent(Event);
    }

    void RecordTraceMarker(const FArdaTraceName& Name) noexcept
    {
        if (!IsTraceCaptureActive())
        {
            return;
        }

        detail::FArdaBufferedEvent Event;
        Event.Type = detail::EArdaBufferedEventType::Marker;
        Event.NameId = Name.GetId();
        Event.StartNanoseconds = detail::GetTraceTimestampNanoseconds();
        BufferEvent(Event);
    }

    namespace detail
    {
        std::uint64_t AllocateScopeId() noexcept
        {
            return GetState().NextScopeId.fetch_add(1, std::memory_order_relaxed);
        }

        void RecordScope(
            std::uint32_t NameId,
            std::uint64_t ScopeId,
            std::uint64_t ParentScopeId,
            std::uint64_t StartNanoseconds,
            std::uint64_t EndNanoseconds) noexcept
        {
            FArdaBufferedEvent Event;
            Event.Type = EArdaBufferedEventType::Scope;
            Event.NameId = NameId;
            Event.PrimaryId = ScopeId;
            Event.SecondaryId = ParentScopeId;
            Event.StartNanoseconds = StartNanoseconds;
            Event.EndNanoseconds = EndNanoseconds;
            BufferEvent(Event);
        }

        std::uint64_t GetTraceTimestampNanoseconds() noexcept
        {
            const auto Timestamp = std::chrono::steady_clock::now().time_since_epoch();
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Timestamp).count());
        }
    }
}
