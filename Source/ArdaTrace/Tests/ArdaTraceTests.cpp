#include "ArdaScopeTimer.h"
#include "ArdaTrace.h"
#include "ArdaTraceReader.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace
{
    std::filesystem::path MakeCapturePath(const char* FileName)
    {
        return std::filesystem::temp_directory_path() / FileName;
    }
}

TEST(ArdaTrace, RecordsNestedScopesCountersAndMarkers)
{
    using namespace arda::trace;

    const std::filesystem::path CapturePath = MakeCapturePath("ArdaTraceRoundTrip.ardatrace");
    ASSERT_TRUE(StartTraceCapture(CapturePath)) << GetTraceError();
    SetCurrentTraceThreadName("Test Thread");
    {
        ARDA_NAMED_SCOPE_TIMER("Rendering");
        ARDA_TRACE_COUNTER("Visible Objects", 42);
        {
            ARDA_NAMED_SCOPE_TIMER("Raytracer");
            ARDA_TRACE_MARKER("Dispatch");
        }
    }
    ASSERT_TRUE(StopTraceCapture()) << GetTraceError();

    FArdaTraceSession Session;
    std::string Error;
    ASSERT_TRUE(ReadTraceCapture(CapturePath, Session, Error)) << Error;
    ASSERT_EQ(Session.Scopes.size(), 2u);
    ASSERT_EQ(Session.Counters.size(), 1u);
    ASSERT_EQ(Session.Markers.size(), 1u);
    EXPECT_EQ(Session.Threads.at(Session.Scopes.front().ThreadId), "Test Thread");

    const FArdaTraceScope* RenderingScope = nullptr;
    const FArdaTraceScope* RaytracerScope = nullptr;
    for (const FArdaTraceScope& Scope : Session.Scopes)
    {
        const std::string& Name = Session.Names.at(Scope.NameId);
        if (Name == "Rendering")
        {
            RenderingScope = &Scope;
        }
        else if (Name == "Raytracer")
        {
            RaytracerScope = &Scope;
        }
    }

    ASSERT_NE(RenderingScope, nullptr);
    ASSERT_NE(RaytracerScope, nullptr);
    EXPECT_EQ(RenderingScope->ParentScopeId, 0u);
    EXPECT_EQ(RaytracerScope->ParentScopeId, RenderingScope->ScopeId);
    EXPECT_GE(RenderingScope->EndNanoseconds, RenderingScope->StartNanoseconds);
    EXPECT_EQ(Session.Names.at(Session.Counters.front().NameId), "Visible Objects");
    EXPECT_DOUBLE_EQ(Session.Counters.front().Value, 42.0);
    EXPECT_EQ(Session.Names.at(Session.Markers.front().NameId), "Dispatch");

    std::filesystem::remove(CapturePath);
}

TEST(ArdaTrace, RejectsTruncatedCapture)
{
    using namespace arda::trace;

    const std::filesystem::path CapturePath = MakeCapturePath("ArdaTraceTruncated.ardatrace");
    {
        std::ofstream Stream(CapturePath, std::ios::binary | std::ios::trunc);
        Stream.write("ARDATRC1", 8);
    }

    FArdaTraceSession Session;
    std::string Error;
    EXPECT_FALSE(ReadTraceCapture(CapturePath, Session, Error));
    EXPECT_FALSE(Error.empty());
    std::filesystem::remove(CapturePath);
}

TEST(ArdaTrace, StreamsFullChunksFromIndependentThreads)
{
    using namespace arda::trace;

    const std::filesystem::path CapturePath = MakeCapturePath("ArdaTraceThreads.ardatrace");
    ASSERT_TRUE(StartTraceCapture(CapturePath)) << GetTraceError();

    const auto RecordScopes = [](const char* ThreadName)
    {
        SetCurrentTraceThreadName(ThreadName);
        static const FArdaTraceName ScopeName("Worker Scope");
        for (int ScopeIndex = 0; ScopeIndex < 1100; ++ScopeIndex)
        {
            FArdaScopeTimer ScopeTimer(ScopeName);
        }
    };

    std::thread FirstThread(RecordScopes, "Worker A");
    std::thread SecondThread(RecordScopes, "Worker B");
    FirstThread.join();
    SecondThread.join();
    ASSERT_TRUE(StopTraceCapture()) << GetTraceError();

    FArdaTraceSession Session;
    std::string Error;
    ASSERT_TRUE(ReadTraceCapture(CapturePath, Session, Error)) << Error;
    EXPECT_EQ(Session.Scopes.size(), 2200u);

    bool bFoundWorkerA = false;
    bool bFoundWorkerB = false;
    for (const auto& [ThreadId, ThreadName] : Session.Threads)
    {
        (void)ThreadId;
        bFoundWorkerA = bFoundWorkerA || ThreadName == "Worker A";
        bFoundWorkerB = bFoundWorkerB || ThreadName == "Worker B";
    }
    EXPECT_TRUE(bFoundWorkerA);
    EXPECT_TRUE(bFoundWorkerB);
    std::filesystem::remove(CapturePath);
}
