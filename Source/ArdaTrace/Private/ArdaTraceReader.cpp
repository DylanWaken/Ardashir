#include "ArdaTracePch.h"

#include "ArdaTraceFormat.h"
#include "ArdaTraceReader.h"

#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace arda::trace
{
    namespace
    {
        constexpr std::uint32_t MaximumStringLength = 1024 * 1024;

        template <typename ValueType>
        bool ReadValue(std::ifstream& Stream, ValueType& OutValue)
        {
            return static_cast<bool>(Stream.read(
                reinterpret_cast<char*>(&OutValue),
                static_cast<std::streamsize>(sizeof(ValueType))));
        }

        bool ReadString(std::ifstream& Stream, std::string& OutValue)
        {
            std::uint32_t Length = 0;
            if (!ReadValue(Stream, Length) || Length > MaximumStringLength)
            {
                return false;
            }

            OutValue.resize(Length);
            return Length == 0
                || static_cast<bool>(Stream.read(OutValue.data(), Length));
        }

        bool ValidateReferences(const FArdaTraceSession& Session, std::string& OutError)
        {
            std::unordered_map<std::uint64_t, const FArdaTraceScope*> ScopesById;
            for (const FArdaTraceScope& Scope : Session.mScopes)
            {
                if (Session.mNames.count(Scope.mNameId) == 0
                    || Session.mThreads.count(Scope.mThreadId) == 0)
                {
                    OutError = "A scope references undefined trace metadata.";
                    return false;
                }
                if (Scope.mEndNanoseconds < Scope.mStartNanoseconds)
                {
                    OutError = "A scope ends before it begins.";
                    return false;
                }
                if (Scope.mScopeId == 0
                    || !ScopesById.emplace(Scope.mScopeId, &Scope).second)
                {
                    OutError = "The trace capture contains duplicate scope identifiers.";
                    return false;
                }
            }

            for (const FArdaTraceScope& Scope : Session.mScopes)
            {
                std::unordered_set<std::uint64_t> VisitedScopeIds = {Scope.mScopeId};
                const FArdaTraceScope* CurrentScope = &Scope;
                while (CurrentScope->mParentScopeId != 0)
                {
                    const auto Parent = ScopesById.find(CurrentScope->mParentScopeId);
                    if (Parent == ScopesById.end()
                        || Parent->second->mThreadId != Scope.mThreadId
                        || Scope.mStartNanoseconds < Parent->second->mStartNanoseconds
                        || Scope.mEndNanoseconds > Parent->second->mEndNanoseconds
                        || !VisitedScopeIds.insert(Parent->second->mScopeId).second)
                    {
                        OutError = "A scope has an invalid parent relationship.";
                        return false;
                    }
                    CurrentScope = Parent->second;
                }
            }

            for (const FArdaTraceCounter& Counter : Session.mCounters)
            {
                if (Session.mNames.count(Counter.mNameId) == 0
                    || Session.mThreads.count(Counter.mThreadId) == 0
                    || !std::isfinite(Counter.mValue))
                {
                    OutError = "A counter references undefined trace metadata.";
                    return false;
                }
            }

            for (const FArdaTraceMarker& Marker : Session.mMarkers)
            {
                if (Session.mNames.count(Marker.mNameId) == 0
                    || Session.mThreads.count(Marker.mThreadId) == 0)
                {
                    OutError = "A marker references undefined trace metadata.";
                    return false;
                }
            }
            return true;
        }
    }

    bool ReadTraceCapture(
        const std::filesystem::path& FilePath,
        FArdaTraceSession& OutSession,
        std::string& OutError)
    {
        std::ifstream Stream(FilePath, std::ios::binary);
        if (!Stream)
        {
            OutError = "Could not open the trace capture file.";
            return false;
        }

        std::array<char, detail::TraceMagic.size()> Magic = {};
        std::uint32_t Version = 0;
        std::uint32_t EndianMarker = 0;
        FArdaTraceSession Session;
        if (!Stream.read(Magic.data(), static_cast<std::streamsize>(Magic.size()))
            || !ReadValue(Stream, Version)
            || !ReadValue(Stream, EndianMarker)
            || !ReadValue(Stream, Session.mOriginNanoseconds))
        {
            OutError = "The trace capture header is truncated.";
            return false;
        }
        if (Magic != detail::TraceMagic)
        {
            OutError = "The file is not an Arda trace capture.";
            return false;
        }
        if (Version != detail::TraceVersion)
        {
            OutError = "The trace capture version is unsupported.";
            return false;
        }
        if (EndianMarker != detail::TraceEndianMarker)
        {
            OutError = "The trace capture byte order is unsupported.";
            return false;
        }

        bool bFoundCaptureEnd = false;
        while (!bFoundCaptureEnd)
        {
            detail::EArdaTraceRecordType Type;
            if (!ReadValue(Stream, Type))
            {
                OutError = "The trace capture ended before its completion record.";
                return false;
            }

            switch (Type)
            {
            case detail::EArdaTraceRecordType::Name:
            {
                std::uint32_t NameId = 0;
                std::string Name;
                if (!ReadValue(Stream, NameId) || !ReadString(Stream, Name))
                {
                    OutError = "A trace name record is malformed.";
                    return false;
                }
                if (!Session.mNames.emplace(NameId, std::move(Name)).second)
                {
                    OutError = "The trace capture contains a duplicate name identifier.";
                    return false;
                }
                break;
            }
            case detail::EArdaTraceRecordType::Thread:
            {
                std::uint32_t ThreadId = 0;
                std::string Name;
                if (!ReadValue(Stream, ThreadId) || !ReadString(Stream, Name))
                {
                    OutError = "A trace thread record is malformed.";
                    return false;
                }
                Session.mThreads[ThreadId] = std::move(Name);
                break;
            }
            case detail::EArdaTraceRecordType::Scope:
            {
                FArdaTraceScope Scope;
                if (!ReadValue(Stream, Scope.mThreadId)
                    || !ReadValue(Stream, Scope.mNameId)
                    || !ReadValue(Stream, Scope.mScopeId)
                    || !ReadValue(Stream, Scope.mParentScopeId)
                    || !ReadValue(Stream, Scope.mStartNanoseconds)
                    || !ReadValue(Stream, Scope.mEndNanoseconds))
                {
                    OutError = "A trace scope record is malformed.";
                    return false;
                }
                Session.mScopes.push_back(Scope);
                break;
            }
            case detail::EArdaTraceRecordType::Counter:
            {
                FArdaTraceCounter Counter;
                if (!ReadValue(Stream, Counter.mThreadId)
                    || !ReadValue(Stream, Counter.mNameId)
                    || !ReadValue(Stream, Counter.mTimestampNanoseconds)
                    || !ReadValue(Stream, Counter.mValue))
                {
                    OutError = "A trace counter record is malformed.";
                    return false;
                }
                Session.mCounters.push_back(Counter);
                break;
            }
            case detail::EArdaTraceRecordType::Marker:
            {
                FArdaTraceMarker Marker;
                if (!ReadValue(Stream, Marker.mThreadId)
                    || !ReadValue(Stream, Marker.mNameId)
                    || !ReadValue(Stream, Marker.mTimestampNanoseconds))
                {
                    OutError = "A trace marker record is malformed.";
                    return false;
                }
                Session.mMarkers.push_back(Marker);
                break;
            }
            case detail::EArdaTraceRecordType::CaptureEnd:
                bFoundCaptureEnd = true;
                break;
            default:
                OutError = "The trace capture contains an unknown record type.";
                return false;
            }
        }

        if (!ValidateReferences(Session, OutError))
        {
            return false;
        }
        char TrailingByte = 0;
        if (Stream.read(&TrailingByte, 1))
        {
            OutError = "The trace capture contains data after its completion record.";
            return false;
        }

        OutSession = std::move(Session);
        OutError.clear();
        return true;
    }
}
