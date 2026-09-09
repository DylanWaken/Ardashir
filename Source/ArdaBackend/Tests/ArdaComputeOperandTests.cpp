#include "Compute/ArdaComputeOperand.h"
#include "Compute/ArdaCudaCompiler.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <future>
#include <vector>

namespace
{
    using namespace arda;

    const char* ArdaTestPtx = ".version 8.0\n.target sm_70\n.address_size 64\n"
        ".visible .entry first() { ret; }\n.visible .entry second() { ret; }\n";

    FArdaCudaModuleSource GetArdaTestSource()
    {
        return {"ArdaTest.cu", "#include \"ArdaValue.h\"\nextern \"C\" __global__ void first() {}",
            EArdaCudaSourceLanguage::CudaCpp, {{"ArdaValue.h", "#define ARDA_VALUE 7"}}, {"--std=c++17"}};
    }

    TEST(ArdaCudaModule, OwnsSourceAndCachesSuccessfulCompilationPerArchitecture)
    {
        std::atomic<uint32_t> Compilations{0};
        auto Source = GetArdaTestSource();
        auto Module = FArdaCudaModule::Create(Source,
            [&](const FArdaCudaModuleSource& Input, uint32_t Architecture) -> TArdaRHIResult<eastl::string>
            {
                ++Compilations;
                EXPECT_EQ(Input.mName, "ArdaTest.cu");
                EXPECT_EQ(Input.mCode, GetArdaTestSource().mCode);
                EXPECT_EQ(Input.mHeaders.front().mCode, "#define ARDA_VALUE 7");
                EXPECT_EQ(Input.mCompileOptions.front(), "--std=c++17");
                EXPECT_TRUE(Architecture == 80 || Architecture == 90);
                return {ArdaTestPtx, {}};
            });
        ASSERT_TRUE(Module);
        Source.mCode = "changed";
        EXPECT_EQ(Compilations.load(), 0u);
        EXPECT_EQ(Module.mValue->GetSource().mCode, GetArdaTestSource().mCode);
        std::vector<std::future<TArdaRHIResult<eastl::string>>> Results;
        for (size_t Index = 0; Index < 8; ++Index)
        {
            Results.push_back(std::async(std::launch::async, [Reference = Module.mValue]
            {
                return Reference->GetPtx(80);
            }));
        }
        for (auto& Result : Results)
        {
            auto Ptx = Result.get();
            ASSERT_TRUE(Ptx);
            EXPECT_EQ(Ptx.mValue, ArdaTestPtx);
        }
        EXPECT_EQ(Compilations.load(), 1u);
        ASSERT_TRUE(Module.mValue->GetPtx(90));
        EXPECT_EQ(Compilations.load(), 2u);
        EXPECT_FALSE(Module.mValue->GetPtx(0));
    }

    TEST(ArdaCudaModule, RejectsInvalidSourceAndCompilerContracts)
    {
        const FArdaCudaModuleCompiler Compiler = [](const auto&, uint32_t) -> TArdaRHIResult<eastl::string>
        {
            return {ArdaTestPtx, {}};
        };
        EXPECT_FALSE(FArdaCudaModule::Create(GetArdaTestSource()));
        auto Source = GetArdaTestSource();
        Source.mName.clear();
        EXPECT_FALSE(FArdaCudaModule::Create(Source, Compiler));
        Source = GetArdaTestSource();
        Source.mCode.push_back('\0');
        EXPECT_FALSE(FArdaCudaModule::Create(Source, Compiler));
        Source = GetArdaTestSource();
        Source.mHeaders.push_back(Source.mHeaders.front());
        EXPECT_FALSE(FArdaCudaModule::Create(Source, Compiler));
        Source = GetArdaTestSource();
        Source.mCompileOptions.push_back("");
        EXPECT_FALSE(FArdaCudaModule::Create(Source, Compiler));
        Source = GetArdaTestSource();
        Source.mLanguage = static_cast<EArdaCudaSourceLanguage>(255);
        EXPECT_FALSE(FArdaCudaModule::Create(Source, Compiler));
        Source = {"ArdaTest.ptx", ArdaTestPtx, EArdaCudaSourceLanguage::Ptx};
        EXPECT_FALSE(FArdaCudaModule::Create(Source, Compiler));
        auto Module = FArdaCudaModule::Create(Source);
        ASSERT_TRUE(Module);
        EXPECT_EQ(Module.mValue->GetPtx(80).mValue, ArdaTestPtx);
        Source.mHeaders = {{"ArdaUnused.h", ""}};
        EXPECT_FALSE(FArdaCudaModule::Create(Source));
    }

    TEST(ArdaCudaModule, PreservesCompilerDiagnosticsAndRetriesFailures)
    {
        uint32_t Compilations = 0;
        auto Module = FArdaCudaModule::Create(GetArdaTestSource(),
            [&](const auto&, uint32_t) -> TArdaRHIResult<eastl::string>
            {
                ++Compilations;
                if (Compilations == 1)
                {
                    return {{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "ArdaTest.cu: compile error")};
                }
                if (Compilations == 2)
                {
                    return {};
                }
                if (Compilations == 3)
                {
                    eastl::string Invalid = ArdaTestPtx;
                    Invalid.push_back('\0');
                    return {eastl::move(Invalid), {}};
                }
                return {ArdaTestPtx, {}};
            });
        ASSERT_TRUE(Module);
        const auto Failed = Module.mValue->GetPtx(80);
        EXPECT_EQ(Failed.mStatus.mCode, EArdaRHIResult::BackendFailure);
        EXPECT_EQ(Failed.mStatus.mMessage, "ArdaTest.cu: compile error");
        EXPECT_FALSE(Module.mValue->GetPtx(80));
        EXPECT_FALSE(Module.mValue->GetPtx(80));
        EXPECT_TRUE(Module.mValue->GetPtx(80));
        EXPECT_TRUE(Module.mValue->GetPtx(80));
        EXPECT_EQ(Compilations, 4u);
    }

    TEST(ArdaCudaCompiler, MissingLibraryAndBuildSupportRemainOptional)
    {
        EXPECT_EQ(CreateArdaNvrtcCompiler(nullptr).mStatus.mCode, EArdaRHIResult::InvalidArgument);
        EXPECT_EQ(CreateArdaNvrtcCompiler("").mStatus.mCode, EArdaRHIResult::InvalidArgument);
        EXPECT_EQ(CreateArdaNvrtcCompiler("ArdaMissingNvrtcLibrary.invalid").mStatus.mCode, EArdaRHIResult::Unsupported);
    }

    TEST(ArdaCudaCompiler, CompilesIncludesAndOptionsAndPreservesSyntaxDiagnostics)
    {
        const char* LibraryPath = std::getenv("ARDA_TEST_NVRTC_LIBRARY");
        if (!LibraryPath)
        {
            GTEST_SKIP() << "Set ARDA_TEST_NVRTC_LIBRARY to exercise the optional compiler adapter.";
        }
        auto Compiler = CreateArdaNvrtcCompiler(LibraryPath);
        ASSERT_TRUE(Compiler) << Compiler.mStatus.mMessage.c_str();
        FArdaCudaModuleSource Source{"ArdaCompileTest.cu",
            "#include \"ArdaValue.h\"\nextern \"C\" __global__ void write_value(unsigned* Output) "
            "{ Output[0] = ARDA_VALUE + ARDA_BIAS; }", EArdaCudaSourceLanguage::CudaCpp,
            {{"ArdaValue.h", "#define ARDA_VALUE 7"}}, {"--std=c++17", "-DARDA_BIAS=11"}};
        auto Module = FArdaCudaModule::Create(Source, Compiler.mValue);
        ASSERT_TRUE(Module);
        Compiler.mValue = {};
        auto Ptx = Module.mValue->GetPtx(80);
        ASSERT_TRUE(Ptx) << Ptx.mStatus.mMessage.c_str();
        EXPECT_NE(Ptx.mValue.find("write_value"), eastl::string::npos);
        EXPECT_EQ(Ptx.mValue.find('\0'), eastl::string::npos);
        Compiler = CreateArdaNvrtcCompiler(LibraryPath);
        ASSERT_TRUE(Compiler);
        Source.mCode = "invalid CUDA C++ source";
        Module = FArdaCudaModule::Create(Source, Compiler.mValue);
        ASSERT_TRUE(Module);
        Ptx = Module.mValue->GetPtx(80);
        EXPECT_EQ(Ptx.mStatus.mCode, EArdaRHIResult::BackendFailure);
        EXPECT_NE(Ptx.mStatus.mMessage.find("ArdaCompileTest.cu"), eastl::string::npos);
        Source = GetArdaTestSource();
        Source.mCompileOptions.push_back("--gpu-architecture=compute_90");
        Module = FArdaCudaModule::Create(Source, Compiler.mValue);
        ASSERT_TRUE(Module);
        EXPECT_EQ(Module.mValue->GetPtx(80).mStatus.mCode, EArdaRHIResult::InvalidArgument);
    }

}
