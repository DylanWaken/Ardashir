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

    FArdaCudaCapabilities GetArdaTestCudaCapabilities(uint32_t Architecture = 80)
    {
        FArdaCudaCapabilities Capabilities;
        Capabilities.mLaunchMode = EArdaCudaLaunchMode::ContextSwitch;
        Capabilities.mComputeCapability = Architecture;
        Capabilities.mMaxThreadsPerBlock = 1024;
        Capabilities.mMaxSharedMemoryBytes = 49152;
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            Capabilities.mMaxBlockSize[Axis] = 1024;
            Capabilities.mMaxGridSize[Axis] = 65535;
        }
        return Capabilities;
    }

    FArdaCudaModuleSource GetArdaTestSource()
    {
        return {"ArdaTest.cu", "#include \"ArdaValue.h\"\nextern \"C\" __global__ void first() {}",
            EArdaCudaSourceLanguage::CudaCpp, {{"ArdaValue.h", "#define ARDA_VALUE 7"}}, {"--std=c++17"}};
    }

    class FArdaRegistrationOperand final : public FArdaComputeOperand
    {
    public:
        FArdaRegistrationOperand() : FArdaComputeOperand("test.registration", {}) {}
        using FArdaComputeOperand::RegisterCuda;
        using FArdaComputeOperand::RegisterGraphics;
        using FArdaComputeOperand::GetVariants;
    };

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

    TEST(ArdaComputeOperand, ModularInvocationResolvesOrderedSymbolsAndOnlyUsedModules)
    {
        uint32_t Compilations = 0;
        auto Module = FArdaCudaModule::Create(GetArdaTestSource(),
            [&](const auto&, uint32_t) -> TArdaRHIResult<eastl::string>
            {
                ++Compilations;
                return {ArdaTestPtx, {}};
            });
        auto Unused = FArdaCudaModule::Create(GetArdaTestSource(),
            [](const auto&, uint32_t) -> TArdaRHIResult<eastl::string>
            {
                ADD_FAILURE() << "Unused modules must not compile";
                return {};
            });
        ASSERT_TRUE(Module);
        ASSERT_TRUE(Unused);
        FArdaComputeCudaImplementation Implementation;
        Implementation.mModules = {Module.mValue, Unused.mValue};
        Implementation.mInvoke = [](const FArdaComputeInvocation& Invocation, const FArdaCudaCapabilities& Capabilities)
            -> TArdaRHIResult<eastl::vector<FArdaComputeCudaLaunch>>
        {
            EXPECT_EQ(Capabilities.mComputeCapability, 80u);
            EXPECT_EQ(Invocation.mBindingDimensions[0][1], 33u);
            FArdaComputeCudaLaunch First;
            First.mEntryPoint = "first";
            First.mBlockSize[0] = 32;
            First.mGridSize[0] = 2;
            First.mArguments = {FArdaCudaArgument::Binding(0), FArdaCudaArgument::Value(33u)};
            auto Second = First;
            Second.mEntryPoint = "second";
            return {{First, Second}, {}};
        };
        FArdaComputeInvocation Invocation;
        Invocation.mBindings.resize(1);
        Invocation.mBindingDimensions = {{7, 33}};
        auto Kernels = BuildArdaComputeCudaKernels(Implementation, Invocation, GetArdaTestCudaCapabilities());
        ASSERT_TRUE(Kernels);
        ASSERT_EQ(Kernels.mValue.size(), 2u);
        EXPECT_EQ(Kernels.mValue[0].mEntryPoint, "first");
        EXPECT_EQ(Kernels.mValue[1].mEntryPoint, "second");
        EXPECT_EQ(Kernels.mValue[0].mPtx, ArdaTestPtx);
        EXPECT_EQ(Kernels.mValue[1].mPtx, ArdaTestPtx);
        EXPECT_EQ(Kernels.mValue[0].mGridSize[0], 2u);
        EXPECT_EQ(Kernels.mValue[0].mArguments[0].mBindingIndex, 0u);
        EXPECT_EQ(Kernels.mValue[0].mArguments[1].mValue, FArdaCudaArgument::Value(33u).mValue);
        EXPECT_EQ(Compilations, 1u);
    }

    TEST(ArdaComputeOperand, InvalidLaunchSequencesNeverCompile)
    {
        uint32_t Compilations = 0;
        auto Module = FArdaCudaModule::Create(GetArdaTestSource(),
            [&](const auto&, uint32_t) -> TArdaRHIResult<eastl::string>
            {
                ++Compilations;
                return {ArdaTestPtx, {}};
            });
        ASSERT_TRUE(Module);
        FArdaComputeCudaLaunch Valid;
        Valid.mEntryPoint = "first";
        eastl::vector<FArdaComputeCudaLaunch> Launches;
        FArdaComputeCudaImplementation Implementation{{Module.mValue},
            [&](const auto&, const auto&) -> TArdaRHIResult<eastl::vector<FArdaComputeCudaLaunch>>
            {
                return {Launches, {}};
            }};
        const auto Reject = [&]
        {
            EXPECT_EQ(BuildArdaComputeCudaKernels(Implementation, {}, GetArdaTestCudaCapabilities()).mStatus.mCode,
                EArdaRHIResult::InvalidArgument);
            EXPECT_EQ(Compilations, 0u);
        };
        Reject();
        Launches = {Valid, Valid};
        Launches[1].mModuleIndex = 1;
        Reject();
        Launches = {Valid, Valid};
        Launches[1].mGridSize[1] = 0;
        Reject();
        Launches = {Valid, Valid};
        Launches[1].mBlockSize[0] = 1025;
        Reject();
        Launches = {Valid, Valid};
        Launches[1].mBlockSize[0] = 1024;
        Launches[1].mBlockSize[1] = 2;
        Reject();
        Launches = {Valid, Valid};
        Launches[1].mEntryPoint.clear();
        Reject();
        Launches = {Valid, Valid};
        Launches[1].mArguments = {FArdaCudaArgument::Binding(0)};
        Reject();
        Launches = {Valid, Valid};
        Launches[1].mSharedMemoryBytes = UINT32_MAX;
        Reject();
        Implementation.mInvoke = [](const auto&, const auto&) -> TArdaRHIResult<eastl::vector<FArdaComputeCudaLaunch>>
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Invocation failed")};
        };
        EXPECT_EQ(BuildArdaComputeCudaKernels(Implementation, {}, GetArdaTestCudaCapabilities()).mStatus.mMessage,
            "Invocation failed");
        EXPECT_EQ(Compilations, 0u);
        Implementation.mInvoke = [](const auto&, const auto&) -> TArdaRHIResult<eastl::vector<FArdaComputeCudaLaunch>>
        {
            ADD_FAILURE() << "CUDA-disabled preparation must not invoke user code";
            return {};
        };
        EXPECT_EQ(BuildArdaComputeCudaKernels(Implementation, {}, {}).mStatus.mCode, EArdaRHIResult::Unsupported);
    }

    TEST(ArdaComputeOperand, RejectsMalformedModularRegistrationsWithoutChangingPreference)
    {
        FArdaRegistrationOperand Operand;
        auto Module = FArdaCudaModule::Create({"ArdaTest.ptx", ArdaTestPtx, EArdaCudaSourceLanguage::Ptx});
        ASSERT_TRUE(Module);
        FArdaComputeCudaImplementation Implementation;
        EXPECT_FALSE(Operand.RegisterCuda("invalid", 70, 90, Implementation));
        Implementation.mModules = {Module.mValue};
        EXPECT_FALSE(Operand.RegisterCuda("invalid", 70, 90, Implementation));
        Implementation.mInvoke = [](const auto&, const auto&) -> TArdaRHIResult<eastl::vector<FArdaComputeCudaLaunch>>
        {
            ADD_FAILURE() << "Registration must not invoke kernels";
            return {};
        };
        EXPECT_FALSE(Operand.RegisterCuda("invalid", 90, 70, Implementation));
        EXPECT_TRUE(Operand.RegisterCuda("modular", 70, 90, Implementation));
        EXPECT_FALSE(Operand.RegisterCuda("modular", 70, 90, Implementation));
        EXPECT_FALSE(Operand.RegisterGraphics("modular", [](auto&, const auto&) { return FArdaRHIStatus{}; }));
        Implementation.mModules.push_back({});
        EXPECT_FALSE(Operand.RegisterCuda("invalid", 70, 90, Implementation));
        ASSERT_EQ(Operand.GetVariants().size(), 1u);
        EXPECT_EQ(Operand.GetVariants()[0].mName, "modular");
    }
}
