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
            std::uint32_t mThreadId = 0;
            std::uint64_t mGeneration = 0;
            std::string mName;
            std::vector<detail::FArdaBufferedEvent> mEvents;
        };

        struct FArdaTraceState
        {
            std::mutex mMutex;
            std::ofstream mStream;
            std::atomic<bool> mbActive = false;
            std::atomic<std::uint64_t> mGeneration = 0;
            std::atomic<std::uint32_t> mNextNameId = 1;
            std::atomic<std::uint32_t> mNextThreadId = 1;
            std::atomic<std::uint64_t> mNextScopeId = 1;
            std::unordered_map<std::string, std::uint32_t> mNameIds;
            std::unordered_map<std::uint32_t, std::string> mNames;
            std::vector<std::shared_ptr<FArdaThreadBuffer>> mThreadBuffers;
            std::string mError;
            std::uint64_t mOriginNanoseconds = 0;
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
            switch (Event.mType)
            {
            case detail::EArdaBufferedEventType::Scope:
            {
                const detail::EArdaTraceRecordType Type = detail::EArdaTraceRecordType::Scope;
                WriteValue(Stream, Type);
                WriteValue(Stream, Event.mThreadId);
                WriteValue(Stream, Event.mNameId);
                WriteValue(Stream, Event.mPrimaryId);
                WriteValue(Stream, Event.mSecondaryId);
                WriteValue(Stream, Event.mStartNanoseconds);
                WriteValue(Stream, Event.mEndNanoseconds);
                break;
            }
            case detail::EArdaBufferedEventType::Counter:
            {
                const detail::EArdaTraceRecordType Type = detail::EArdaTraceRecordType::Counter;
                WriteValue(Stream, Type);
                WriteValue(Stream, Event.mThreadId);
                WriteValue(Stream, Event.mNameId);
                WriteValue(Stream, Event.mStartNanoseconds);
                WriteValue(Stream, Event.mValue);
                break;
            }
            case detail::EArdaBufferedEventType::Marker:
            {
                const detail::EArdaTraceRecordType Type = detail::EArdaTraceRecordType::Marker;
                WriteValue(Stream, Type);
                WriteValue(Stream, Event.mThreadId);
                WriteValue(Stream, Event.mNameId);
                WriteValue(Stream, Event.mStartNanoseconds);
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
            ThreadBuffer->mThreadId = State.mNextThreadId.fetch_add(1, std::memory_order_relaxed);
            ThreadBuffer->mName = "Thread " + std::to_string(ThreadBuffer->mThreadId);
            ThreadBuffer->mEvents.reserve(EventChunkCapacity);
            return ThreadBuffer;
        }

        bool PrepareThreadBuffer(
            const std::shared_ptr<FArdaThreadBuffer>& ThreadBuffer,
            std::uint64_t Generation)
        {
            if (ThreadBuffer->mGeneration == Generation)
            {
                return true;
            }

            ThreadBuffer->mEvents.clear();
            ThreadBuffer->mGeneration = Generation;

            FArdaTraceState& State = GetState();
            std::lock_guard<std::mutex> Lock(State.mMutex);
            if (!State.mbActive.load(std::memory_order_relaxed)
                || State.mGeneration.load(std::memory_order_relaxed) != Generation)
            {
                return false;
            }

            State.mThreadBuffers.push_back(ThreadBuffer);
            WriteThreadRecord(State.mStream, ThreadBuffer->mThreadId, ThreadBuffer->mName);
            return static_cast<bool>(State.mStream);
        }

        void PublishThreadEvents(FArdaThreadBuffer& ThreadBuffer)
        {
            if (ThreadBuffer.mEvents.empty())
            {
                return;
            }

            FArdaTraceState& State = GetState();
            std::lock_guard<std::mutex> Lock(State.mMutex);
            if (State.mbActive.load(std::memory_order_relaxed)
                && State.mGeneration.load(std::memory_order_relaxed) == ThreadBuffer.mGeneration)
            {
                for (const detail::FArdaBufferedEvent& Event : ThreadBuffer.mEvents)
                {
                    WriteEvent(State.mStream, Event);
                }

                if (!State.mStream)
                {
                    State.mError = "Failed while writing trace events.";
                    State.mbActive.store(false, std::memory_order_release);
                }
            }
            ThreadBuffer.mEvents.clear();
        }

        void BufferEvent(detail::FArdaBufferedEvent Event) noexcept
        {
            FArdaTraceState& State = GetState();
            if (!State.mbActive.load(std::memory_order_acquire))
            {
                return;
            }

            const std::uint64_t Generation =
                State.mGeneration.load(std::memory_order_acquire);
            const std::shared_ptr<FArdaThreadBuffer> ThreadBuffer = GetThreadBuffer();
            if (!PrepareThreadBuffer(ThreadBuffer, Generation))
            {
                return;
            }

            Event.mThreadId = ThreadBuffer->mThreadId;
            ThreadBuffer->mEvents.push_back(Event);
            if (ThreadBuffer->mEvents.size() >= EventChunkCapacity)
            {
                PublishThreadEvents(*ThreadBuffer);
            }
        }

        std::uint32_t RegisterName(const char* Name)
        {
            FArdaTraceState& State = GetState();
            const std::string StableName = Name != nullptr ? Name : "<unnamed>";
            std::lock_guard<std::mutex> Lock(State.mMutex);

            const auto ExistingName = State.mNameIds.find(StableName);
            if (ExistingName != State.mNameIds.end())
            {
                return ExistingName->second;
            }

            const std::uint32_t NameId =
                State.mNextNameId.fetch_add(1, std::memory_order_relaxed);
            State.mNameIds.emplace(StableName, NameId);
            State.mNames.emplace(NameId, StableName);
            if (State.mbActive.load(std::memory_order_relaxed))
            {
                WriteNameRecord(State.mStream, NameId, StableName);
            }
            return NameId;
        }
    }

    FArdaTraceName::FArdaTraceName(const char* Name)
        : mId(RegisterName(Name))
    {
    }

    std::uint32_t FArdaTraceName::GetId() const noexcept
    {
        return mId;
    }

    bool StartTraceCapture(const std::filesystem::path& FilePath)
    {
        FArdaTraceState& State = GetState();
        std::lock_guard<std::mutex> Lock(State.mMutex);
        if (State.mbActive.load(std::memory_order_relaxed))
        {
            State.mError = "A trace capture is already active.";
            return false;
        }

        State.mStream.close();
        State.mStream.clear();
        State.mStream.open(FilePath, std::ios::binary | std::ios::trunc);
        if (!State.mStream)
        {
            State.mError = "Could not open the trace capture file.";
            return false;
        }

        State.mError.clear();
        State.mOriginNanoseconds = detail::GetTraceTimestampNanoseconds();
        const std::uint64_t Generation =
            State.mGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
        (void)Generation;

        WriteBytes(State.mStream, detail::TraceMagic.data(), detail::TraceMagic.size());
        WriteValue(State.mStream, detail::TraceVersion);
        WriteValue(State.mStream, detail::TraceEndianMarker);
        WriteValue(State.mStream, State.mOriginNanoseconds);
        for (const auto& [NameId, Name] : State.mNames)
        {
            WriteNameRecord(State.mStream, NameId, Name);
        }

        if (!State.mStream)
        {
            State.mError = "Could not write the trace capture header.";
            State.mStream.close();
            return false;
        }

        State.mbActive.store(true, std::memory_order_release);
        return true;
    }

    bool StopTraceCapture()
    {
        FArdaTraceState& State = GetState();
        const bool bWasActive = State.mbActive.exchange(false, std::memory_order_acq_rel);
        if (!bWasActive && !State.mStream.is_open())
        {
            std::lock_guard<std::mutex> Lock(State.mMutex);
            if (State.mError.empty())
            {
                State.mError = "No trace capture is active.";
            }
            return false;
        }

        std::lock_guard<std::mutex> Lock(State.mMutex);
        const std::uint64_t Generation = State.mGeneration.load(std::memory_order_relaxed);
        for (const std::shared_ptr<FArdaThreadBuffer>& ThreadBuffer : State.mThreadBuffers)
        {
            if (ThreadBuffer->mGeneration != Generation)
            {
                continue;
            }
            for (const detail::FArdaBufferedEvent& Event : ThreadBuffer->mEvents)
            {
                WriteEvent(State.mStream, Event);
            }
            ThreadBuffer->mEvents.clear();
        }
        State.mThreadBuffers.clear();

        const detail::EArdaTraceRecordType EndType = detail::EArdaTraceRecordType::CaptureEnd;
        WriteValue(State.mStream, EndType);
        State.mStream.flush();
        const bool bSucceeded = static_cast<bool>(State.mStream);
        State.mStream.close();
        if (!bSucceeded && State.mError.empty())
        {
            State.mError = "Failed while finalizing the trace capture.";
        }
        return bSucceeded;
    }

    bool IsTraceCaptureActive() noexcept
    {
        return GetState().mbActive.load(std::memory_order_acquire);
    }

    std::string GetTraceError()
    {
        FArdaTraceState& State = GetState();
        std::lock_guard<std::mutex> Lock(State.mMutex);
        return State.mError;
    }

    void SetCurrentTraceThreadName(const char* ThreadName)
    {
        const std::shared_ptr<FArdaThreadBuffer> ThreadBuffer = GetThreadBuffer();
        ThreadBuffer->mName = ThreadName != nullptr ? ThreadName : "<unnamed thread>";

        FArdaTraceState& State = GetState();
        if (!State.mbActive.load(std::memory_order_acquire))
        {
            return;
        }

        const std::uint64_t Generation = State.mGeneration.load(std::memory_order_acquire);
        if (!PrepareThreadBuffer(ThreadBuffer, Generation))
        {
            return;
        }

        std::lock_guard<std::mutex> Lock(State.mMutex);
        if (State.mbActive.load(std::memory_order_relaxed))
        {
            WriteThreadRecord(State.mStream, ThreadBuffer->mThreadId, ThreadBuffer->mName);
        }
    }

    void RecordTraceCounter(const FArdaTraceName& Name, double Value) noexcept
    {
        if (!IsTraceCaptureActive())
        {
            return;
        }

        detail::FArdaBufferedEvent Event;
        Event.mType = detail::EArdaBufferedEventType::Counter;
        Event.mNameId = Name.GetId();
        Event.mStartNanoseconds = detail::GetTraceTimestampNanoseconds();
        Event.mValue = Value;
        BufferEvent(Event);
    }

    void RecordTraceMarker(const FArdaTraceName& Name) noexcept
    {
        if (!IsTraceCaptureActive())
        {
            return;
        }

        detail::FArdaBufferedEvent Event;
        Event.mType = detail::EArdaBufferedEventType::Marker;
        Event.mNameId = Name.GetId();
        Event.mStartNanoseconds = detail::GetTraceTimestampNanoseconds();
        BufferEvent(Event);
    }

    namespace detail
    {
        std::uint64_t AllocateScopeId() noexcept
        {
            return GetState().mNextScopeId.fetch_add(1, std::memory_order_relaxed);
        }

        void RecordScope(
            std::uint32_t NameId,
            std::uint64_t ScopeId,
            std::uint64_t ParentScopeId,
            std::uint64_t StartNanoseconds,
            std::uint64_t EndNanoseconds) noexcept
        {
            FArdaBufferedEvent Event;
            Event.mType = EArdaBufferedEventType::Scope;
            Event.mNameId = NameId;
            Event.mPrimaryId = ScopeId;
            Event.mSecondaryId = ParentScopeId;
            Event.mStartNanoseconds = StartNanoseconds;
            Event.mEndNanoseconds = EndNanoseconds;
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
