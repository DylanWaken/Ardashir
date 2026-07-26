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
            for (const FArdaTraceScope& Scope : Session.Scopes)
            {
                if (Session.Names.count(Scope.NameId) == 0
                    || Session.Threads.count(Scope.ThreadId) == 0)
                {
                    OutError = "A scope references undefined trace metadata.";
                    return false;
                }
                if (Scope.EndNanoseconds < Scope.StartNanoseconds)
                {
                    OutError = "A scope ends before it begins.";
                    return false;
                }
                if (Scope.ScopeId == 0
                    || !ScopesById.emplace(Scope.ScopeId, &Scope).second)
                {
                    OutError = "The trace capture contains duplicate scope identifiers.";
                    return false;
                }
            }

            for (const FArdaTraceScope& Scope : Session.Scopes)
            {
                std::unordered_set<std::uint64_t> VisitedScopeIds = {Scope.ScopeId};
                const FArdaTraceScope* CurrentScope = &Scope;
                while (CurrentScope->ParentScopeId != 0)
                {
                    const auto Parent = ScopesById.find(CurrentScope->ParentScopeId);
                    if (Parent == ScopesById.end()
                        || Parent->second->ThreadId != Scope.ThreadId
                        || Scope.StartNanoseconds < Parent->second->StartNanoseconds
                        || Scope.EndNanoseconds > Parent->second->EndNanoseconds
                        || !VisitedScopeIds.insert(Parent->second->ScopeId).second)
                    {
                        OutError = "A scope has an invalid parent relationship.";
                        return false;
                    }
                    CurrentScope = Parent->second;
                }
            }

            for (const FArdaTraceCounter& Counter : Session.Counters)
            {
                if (Session.Names.count(Counter.NameId) == 0
                    || Session.Threads.count(Counter.ThreadId) == 0
                    || !std::isfinite(Counter.Value))
                {
                    OutError = "A counter references undefined trace metadata.";
                    return false;
                }
            }

            for (const FArdaTraceMarker& Marker : Session.Markers)
            {
                if (Session.Names.count(Marker.NameId) == 0
                    || Session.Threads.count(Marker.ThreadId) == 0)
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
            || !ReadValue(Stream, Session.OriginNanoseconds))
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
                if (!Session.Names.emplace(NameId, std::move(Name)).second)
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
                Session.Threads[ThreadId] = std::move(Name);
                break;
            }
            case detail::EArdaTraceRecordType::Scope:
            {
                FArdaTraceScope Scope;
                if (!ReadValue(Stream, Scope.ThreadId)
                    || !ReadValue(Stream, Scope.NameId)
                    || !ReadValue(Stream, Scope.ScopeId)
                    || !ReadValue(Stream, Scope.ParentScopeId)
                    || !ReadValue(Stream, Scope.StartNanoseconds)
                    || !ReadValue(Stream, Scope.EndNanoseconds))
                {
                    OutError = "A trace scope record is malformed.";
                    return false;
                }
                Session.Scopes.push_back(Scope);
                break;
            }
            case detail::EArdaTraceRecordType::Counter:
            {
                FArdaTraceCounter Counter;
                if (!ReadValue(Stream, Counter.ThreadId)
                    || !ReadValue(Stream, Counter.NameId)
                    || !ReadValue(Stream, Counter.TimestampNanoseconds)
                    || !ReadValue(Stream, Counter.Value))
                {
                    OutError = "A trace counter record is malformed.";
                    return false;
                }
                Session.Counters.push_back(Counter);
                break;
            }
            case detail::EArdaTraceRecordType::Marker:
            {
                FArdaTraceMarker Marker;
                if (!ReadValue(Stream, Marker.ThreadId)
                    || !ReadValue(Stream, Marker.NameId)
                    || !ReadValue(Stream, Marker.TimestampNanoseconds))
                {
                    OutError = "A trace marker record is malformed.";
                    return false;
                }
                Session.Markers.push_back(Marker);
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
