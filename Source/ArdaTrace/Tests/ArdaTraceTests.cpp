#include "ArdaScopeTimer.h"
#include "ArdaTrace.h"
#include "ArdaTraceReader.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <EASTL/string.h>
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
    ASSERT_TRUE(StartTraceCapture(CapturePath)) << GetTraceError().c_str();
    SetCurrentTraceThreadName("Test Thread");
    {
        ARDA_NAMED_SCOPE_TIMER("Rendering");
        ARDA_TRACE_COUNTER("Visible Objects", 42);
        {
            ARDA_NAMED_SCOPE_TIMER("Raytracer");
            ARDA_TRACE_MARKER("Dispatch");
        }
    }
    ASSERT_TRUE(StopTraceCapture()) << GetTraceError().c_str();

    FArdaTraceSession Session;
    eastl::string Error;
    ASSERT_TRUE(ReadTraceCapture(CapturePath, Session, Error)) << Error.c_str();
    ASSERT_EQ(Session.mScopes.size(), 2u);
    ASSERT_EQ(Session.mCounters.size(), 1u);
    ASSERT_EQ(Session.mMarkers.size(), 1u);
    EXPECT_STREQ(
        Session.mThreads.at(Session.mScopes.front().mThreadId).c_str(),
        "Test Thread");

    const FArdaTraceScope* RenderingScope = nullptr;
    const FArdaTraceScope* RaytracerScope = nullptr;
    for (const FArdaTraceScope& Scope : Session.mScopes)
    {
        const eastl::string& Name = Session.mNames.at(Scope.mNameId);
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
    EXPECT_EQ(RenderingScope->mParentScopeId, 0u);
    EXPECT_EQ(RaytracerScope->mParentScopeId, RenderingScope->mScopeId);
    EXPECT_GE(RenderingScope->mEndNanoseconds, RenderingScope->mStartNanoseconds);
    EXPECT_STREQ(
        Session.mNames.at(Session.mCounters.front().mNameId).c_str(),
        "Visible Objects");
    EXPECT_DOUBLE_EQ(Session.mCounters.front().mValue, 42.0);
    EXPECT_STREQ(
        Session.mNames.at(Session.mMarkers.front().mNameId).c_str(),
        "Dispatch");

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
    eastl::string Error;
    EXPECT_FALSE(ReadTraceCapture(CapturePath, Session, Error));
    EXPECT_FALSE(Error.empty());
    std::filesystem::remove(CapturePath);
}

TEST(ArdaTrace, StreamsFullChunksFromIndependentThreads)
{
    using namespace arda::trace;

    const std::filesystem::path CapturePath = MakeCapturePath("ArdaTraceThreads.ardatrace");
    ASSERT_TRUE(StartTraceCapture(CapturePath)) << GetTraceError().c_str();

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
    ASSERT_TRUE(StopTraceCapture()) << GetTraceError().c_str();

    FArdaTraceSession Session;
    eastl::string Error;
    ASSERT_TRUE(ReadTraceCapture(CapturePath, Session, Error)) << Error.c_str();
    EXPECT_EQ(Session.mScopes.size(), 2200u);

    bool bFoundWorkerA = false;
    bool bFoundWorkerB = false;
    for (const auto& [ThreadId, ThreadName] : Session.mThreads)
    {
        (void)ThreadId;
        bFoundWorkerA = bFoundWorkerA || ThreadName == "Worker A";
        bFoundWorkerB = bFoundWorkerB || ThreadName == "Worker B";
    }
    EXPECT_TRUE(bFoundWorkerA);
    EXPECT_TRUE(bFoundWorkerB);
    std::filesystem::remove(CapturePath);
}
