#include "ArdaBackend.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <EASTL/string.h>
#include <string>

namespace
{
    ARDA_DEFINE_LOG_CATEGORY_NAMED(LogTest, "TestScope", VeryVerbose);

    struct FCapturedLog
    {
        int mCount = 0;
        char mCategory[64] = {};
        arda::backend::EArdaLogVerbosity mVerbosity =
            arda::backend::EArdaLogVerbosity::Off;
        char mMessage[256] = {};
        char mFile[256] = {};
        std::uint32_t mLine = 0;
        char mFunction[128] = {};
    };

    void CopyText(char* Destination, std::size_t Capacity, const char* Source) noexcept
    {
        std::snprintf(Destination, Capacity, "%s", Source ? Source : "");
    }

    void CaptureLog(
        const arda::backend::FArdaLogRecord& Record,
        void* UserData) noexcept
    {
        auto& capture = *static_cast<FCapturedLog*>(UserData);
        ++capture.mCount;
        CopyText(capture.mCategory, sizeof(capture.mCategory), Record.mCategory);
        capture.mVerbosity = Record.mVerbosity;
        CopyText(capture.mMessage, sizeof(capture.mMessage), Record.mMessage);
        CopyText(capture.mFile, sizeof(capture.mFile), Record.mFile);
        capture.mLine = Record.mLine;
        CopyText(capture.mFunction, sizeof(capture.mFunction), Record.mFunction);
    }

    class FArdaLogTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            arda::backend::SetLogOutput(&CaptureLog, &mCapture);
        }

        void TearDown() override
        {
            arda::backend::ResetLogOutput();
        }

        FCapturedLog mCapture;
    };
}

TEST_F(FArdaLogTest, EmitsFormattedStructuredRecords)
{
    ARDA_LOG(LogTest, Warning, "Resource %s has %d users", "SceneColor", 3);

    EXPECT_EQ(mCapture.mCount, 1);
    EXPECT_STREQ(mCapture.mCategory, "TestScope");
    EXPECT_EQ(mCapture.mVerbosity, arda::backend::EArdaLogVerbosity::Warning);
    EXPECT_STREQ(mCapture.mMessage, "Resource SceneColor has 3 users");
    EXPECT_NE(std::strstr(mCapture.mFile, "ArdaLogTests.cpp"), nullptr);
    EXPECT_GT(mCapture.mLine, 0u);
    EXPECT_NE(mCapture.mFunction[0], '\0');
}

TEST_F(FArdaLogTest, FiltersWithoutEvaluatingLogArguments)
{
    arda::backend::FArdaLogCategory category(
        "FilteredScope",
        arda::backend::EArdaLogVerbosity::Warning);
    int value = 0;

    ARDA_LOG(category, Verbose, "Value %d", ++value);
    EXPECT_EQ(value, 0);
    EXPECT_EQ(mCapture.mCount, 0);

    category.SetMinimumVerbosity(arda::backend::EArdaLogVerbosity::Verbose);
    ARDA_LOG(category, Verbose, "Value %d", ++value);
    EXPECT_EQ(value, 1);
    EXPECT_EQ(mCapture.mCount, 1);
    EXPECT_STREQ(mCapture.mMessage, "Value 1");

    category.SetMinimumVerbosity(arda::backend::EArdaLogVerbosity::Off);
    ARDA_LOG(category, Fatal, "Hidden");
    EXPECT_EQ(mCapture.mCount, 1);
}

TEST_F(FArdaLogTest, HandlesNullFormatStrings)
{
    arda::backend::Logf(
        LogTest,
        arda::backend::EArdaLogVerbosity::Log,
        __FILE__,
        static_cast<std::uint32_t>(__LINE__),
        __func__,
        nullptr);

    ASSERT_EQ(mCapture.mCount, 1);
    EXPECT_STREQ(mCapture.mMessage, "");
}

TEST_F(FArdaLogTest, ExposesVerbosityNames)
{
    using namespace arda::backend;

    EXPECT_STREQ(ToString(EArdaLogVerbosity::VeryVerbose), "VeryVerbose");
    EXPECT_STREQ(ToString(EArdaLogVerbosity::Verbose), "Verbose");
    EXPECT_STREQ(ToString(EArdaLogVerbosity::Log), "Log");
    EXPECT_STREQ(ToString(EArdaLogVerbosity::Display), "Display");
    EXPECT_STREQ(ToString(EArdaLogVerbosity::Warning), "Warning");
    EXPECT_STREQ(ToString(EArdaLogVerbosity::Error), "Error");
    EXPECT_STREQ(ToString(EArdaLogVerbosity::Fatal), "Fatal");
    EXPECT_STREQ(ToString(EArdaLogVerbosity::Off), "Off");
}

TEST(ArdaLog, ResetRestoresDefaultStderrOutput)
{
    arda::backend::ResetLogOutput();
    testing::internal::CaptureStderr();
    ARDA_LOG(LogTest, Display, "Default output");
    const std::string captured = testing::internal::GetCapturedStderr();
    const eastl::string output(captured.data(), captured.size());

    EXPECT_NE(output.find("[TestScope][Display] Default output"), eastl::string::npos);
}
