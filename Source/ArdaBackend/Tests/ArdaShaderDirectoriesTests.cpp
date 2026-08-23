#include "ArdaBackend.h"
#include "ArdaBackendProvider.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"
#include "ShaderStructs/ArdaShaderDirectoriesPrivate.h"

#include <gtest/gtest.h>

#include <atomic>
#include <fstream>

#ifndef ARDA_BACKEND_TEST_SHADER_DIR
#define ARDA_BACKEND_TEST_SHADER_DIR "."
#endif

namespace
{
    namespace fs = std::filesystem;
    using namespace arda::backend;

    class FTemporaryShaderTree
    {
    public:
        explicit FTemporaryShaderTree(const char* Label)
        {
            static std::atomic<uint64_t> Counter{ 0 };
            mRoot = fs::temp_directory_path() /
                (std::string("ArdaShaderDirectories-") + Label + "-" +
                 std::to_string(++Counter));
            fs::create_directories(mRoot);
        }

        ~FTemporaryShaderTree()
        {
            std::error_code Error;
            fs::remove_all(mRoot, Error);
        }

        fs::path Write(const fs::path& Relative, const char* Contents = "// shader")
        {
            const fs::path Path = mRoot / Relative;
            fs::create_directories(Path.parent_path());
            std::ofstream Stream(Path, std::ios::binary);
            Stream << Contents;
            return fs::canonical(Path);
        }

        fs::path Directory(const fs::path& Relative)
        {
            const fs::path Path = mRoot / Relative;
            fs::create_directories(Path);
            return fs::canonical(Path);
        }

        const fs::path& Root() const { return mRoot; }

    private:
        fs::path mRoot;
    };

    class ArdaShaderDirectories : public testing::Test
    {
    protected:
        void SetUp() override
        {
            ShutdownBackend();
            (void)UnfreezeShaderSourceDirectories();
            ASSERT_TRUE(ClearShaderSourceDirectories());
            const auto Modules = EnumerateBackendModules();
            if (!Modules.empty())
                ASSERT_TRUE(ConfigureBackend(Modules.front().mName.c_str()));
        }

        void TearDown() override
        {
            ShutdownBackend();
            (void)UnfreezeShaderSourceDirectories();
            (void)ClearShaderSourceDirectories();
            FArdaShaderTypeRegistration::ResetForTests();
        }
    };
}

TEST_F(ArdaShaderDirectories, ScansOverlaysAndEnumeratesDeterministically)
{
    FTemporaryShaderTree First("overlay-a");
    FTemporaryShaderTree Second("overlay-b");
    const fs::path Alpha = First.Write("Compute/Alpha.hlsl");
    First.Write("Ignored.txt");
    const fs::path Beta = Second.Write("Compute/Beta.ush");

    ASSERT_TRUE(AddShaderSourceDirectory(Second.Root(), "/"));
    ASSERT_TRUE(AddShaderSourceDirectory(First.Root()));
    ASSERT_TRUE(ScanAndFreezeShaderSourceDirectories());

    const auto Files = EnumerateShaderSourceFiles();
    ASSERT_EQ(Files.size(), 2u);
    EXPECT_EQ(Files[0].mVirtualPath, "/Compute/Alpha.hlsl");
    EXPECT_EQ(Files[0].mPhysicalPath, Alpha);
    EXPECT_EQ(Files[1].mVirtualPath, "/Compute/Beta.ush");
    EXPECT_EQ(Files[1].mPhysicalPath, Beta);

    fs::path Resolved;
    ASSERT_TRUE(ResolveVirtualShaderSource("/Compute/Beta.ush", Resolved));
    EXPECT_EQ(Resolved, Beta);
}

TEST_F(ArdaShaderDirectories, ReportsOverlayCollisionWithBothPhysicalFiles)
{
    FTemporaryShaderTree First("collision-a");
    FTemporaryShaderTree Second("collision-b");
    const fs::path FirstPath = First.Write("Compute/Sort.hlsl");
    const fs::path SecondPath = Second.Write("Compute/Sort.hlsl");

    ASSERT_TRUE(AddShaderSourceDirectory(First.Root()));
    ASSERT_TRUE(AddShaderSourceDirectory(Second.Root()));
    const auto Status = ScanAndFreezeShaderSourceDirectories();
    ASSERT_EQ(Status.mCode, EArdaShaderDirectoryError::DuplicateVirtualPath);
    EXPECT_EQ(Status.mVirtualPath, "/Compute/Sort.hlsl");
    ASSERT_EQ(Status.mPhysicalPaths.size(), 2u);
    EXPECT_NE(Status.mMessage.find(FirstPath.string().c_str()), eastl::string::npos);
    EXPECT_NE(Status.mMessage.find(SecondPath.string().c_str()), eastl::string::npos);
    EXPECT_FALSE(AreShaderSourceDirectoriesFrozen());
    EXPECT_TRUE(EnumerateShaderSourceFiles().empty());
}

TEST_F(ArdaShaderDirectories, RejectsDuplicateExclusiveRoot)
{
    FTemporaryShaderTree First("mapping-a");
    FTemporaryShaderTree Second("mapping-b");
    ASSERT_TRUE(AddShaderSourceDirectoryMapping("/Project", First.Root()));
    const auto Status =
        AddShaderSourceDirectoryMapping("/Project", Second.Root());
    EXPECT_EQ(Status.mCode, EArdaShaderDirectoryError::DuplicateMappingRoot);
    EXPECT_EQ(EnumerateShaderSourceDirectories().size(), 1u);
}

TEST_F(ArdaShaderDirectories, RepeatedIdenticalRegistrationsAreIdempotent)
{
    FTemporaryShaderTree Overlay("idempotent-overlay");
    FTemporaryShaderTree Mapping("idempotent-mapping");

    ASSERT_TRUE(AddShaderSourceDirectory(Overlay.Root(), "/Shared"));
    ASSERT_TRUE(AddShaderSourceDirectory(Overlay.Root(), "/Shared"));
    ASSERT_TRUE(
        AddShaderSourceDirectoryMapping("/Mapped", Mapping.Root()));
    ASSERT_TRUE(
        AddShaderSourceDirectoryMapping("/Mapped", Mapping.Root()));
    EXPECT_EQ(EnumerateShaderSourceDirectories().size(), 2u);
}

TEST_F(ArdaShaderDirectories, RejectsIncompatiblePhysicalRegistrationsAtAdd)
{
    FTemporaryShaderTree Tree("ambiguous-registration");
    ASSERT_TRUE(AddShaderSourceDirectory(Tree.Root(), "/First"));

    const auto Status = AddShaderSourceDirectory(Tree.Root(), "/Second");
    EXPECT_EQ(Status.mCode, EArdaShaderDirectoryError::AmbiguousPhysicalFile);
    ASSERT_EQ(Status.mPhysicalDirectories.size(), 2u);
    EXPECT_EQ(EnumerateShaderSourceDirectories().size(), 1u);
}

TEST_F(ArdaShaderDirectories, RegistryUseGuardRejectsEveryMutation)
{
    FTemporaryShaderTree Tree("registry-guard");
    ASSERT_TRUE(private_api::BeginShaderDirectoryRegistryUse());

    EXPECT_EQ(
        AddShaderSourceDirectory(Tree.Root()).mCode,
        EArdaShaderDirectoryError::RegistryInUse);
    EXPECT_EQ(
        ScanAndFreezeShaderSourceDirectories().mCode,
        EArdaShaderDirectoryError::RegistryInUse);
    EXPECT_EQ(
        UnfreezeShaderSourceDirectories().mCode,
        EArdaShaderDirectoryError::RegistryInUse);
    EXPECT_EQ(
        ClearShaderSourceDirectories().mCode,
        EArdaShaderDirectoryError::RegistryInUse);

    private_api::CompleteShaderDirectoryRegistryUse(false);
    EXPECT_TRUE(AddShaderSourceDirectory(Tree.Root()));
}

TEST_F(ArdaShaderDirectories, NestedExclusiveMappingUsesDeepestRoot)
{
    FTemporaryShaderTree Parent("nested-parent");
    const fs::path Owned = Parent.Write("Nested/Owned.hlsl", "// deepest");
    Parent.Write("Root.hlsl");
    const fs::path Child = Parent.Directory("Nested");

    ASSERT_TRUE(AddShaderSourceDirectoryMapping("/", Parent.Root()));
    ASSERT_TRUE(AddShaderSourceDirectoryMapping("/Nested", Child));
    ASSERT_TRUE(ScanAndFreezeShaderSourceDirectories());

    fs::path Resolved;
    ASSERT_TRUE(ResolveVirtualShaderSource("/Nested/Owned.hlsl", Resolved));
    EXPECT_EQ(Resolved, Owned);
    EXPECT_EQ(EnumerateShaderSourceFiles().size(), 2u);
}

TEST_F(ArdaShaderDirectories, RejectsMismatchedNestedPhysicalMapping)
{
    FTemporaryShaderTree Parent("mismatched-parent");
    const fs::path Child = Parent.Directory("PhysicalChild");
    ASSERT_TRUE(AddShaderSourceDirectoryMapping("/", Parent.Root()));
    EXPECT_EQ(
        AddShaderSourceDirectoryMapping("/VirtualChild", Child).mCode,
        EArdaShaderDirectoryError::AmbiguousPhysicalFile);
}

TEST_F(ArdaShaderDirectories, OverlayStillCollidesWithNestedExclusiveMapping)
{
    FTemporaryShaderTree Overlay("nested-overlay");
    FTemporaryShaderTree Exclusive("nested-exclusive");
    Overlay.Write("Nested/Shared.hlsl");
    Exclusive.Write("Shared.hlsl");

    ASSERT_TRUE(AddShaderSourceDirectory(Overlay.Root()));
    ASSERT_TRUE(
        AddShaderSourceDirectoryMapping("/Nested", Exclusive.Root()));
    EXPECT_EQ(
        ScanAndFreezeShaderSourceDirectories().mCode,
        EArdaShaderDirectoryError::DuplicateVirtualPath);
}

TEST_F(ArdaShaderDirectories, DetectsPortableCaseOnlyCollision)
{
    FTemporaryShaderTree First("case-a");
    FTemporaryShaderTree Second("case-b");
    First.Write("Compute/Sort.hlsl");
    Second.Write("compute/sort.HLSL");

    ASSERT_TRUE(AddShaderSourceDirectory(First.Root()));
    ASSERT_TRUE(AddShaderSourceDirectory(Second.Root()));
    EXPECT_EQ(
        ScanAndFreezeShaderSourceDirectories().mCode,
        EArdaShaderDirectoryError::DuplicateVirtualPath);
}

TEST_F(ArdaShaderDirectories, ValidatesVirtualPathsAndExtensions)
{
    FTemporaryShaderTree Tree("validation");
    Tree.Write("Accepted.hlsli");
    Tree.Write("Ignored.bin");

    EXPECT_EQ(
        AddShaderSourceDirectory(Tree.Root(), "Relative").mCode,
        EArdaShaderDirectoryError::InvalidVirtualPath);
    EXPECT_EQ(
        AddShaderSourceDirectory(Tree.Root(), "/Bad\\Root").mCode,
        EArdaShaderDirectoryError::InvalidVirtualPath);
    EXPECT_EQ(
        AddShaderSourceDirectory(Tree.Root(), "/Bad/../Root").mCode,
        EArdaShaderDirectoryError::InvalidVirtualPath);
    EXPECT_EQ(
        AddShaderSourceDirectory(Tree.Root(), "/Bad Root").mCode,
        EArdaShaderDirectoryError::InvalidVirtualPath);
    ASSERT_TRUE(AddShaderSourceDirectory(Tree.Root()));
    ASSERT_TRUE(ScanAndFreezeShaderSourceDirectories());
    EXPECT_EQ(EnumerateShaderSourceFiles().size(), 1u);

    fs::path Resolved;
    EXPECT_EQ(
        ResolveVirtualShaderSource("/Ignored.bin", Resolved).mCode,
        EArdaShaderDirectoryError::UnsupportedExtension);
    EXPECT_EQ(
        ResolveVirtualShaderSource("/../Accepted.hlsli", Resolved).mCode,
        EArdaShaderDirectoryError::InvalidVirtualPath);
}

TEST_F(ArdaShaderDirectories, RejectsGeneratedNonPortableVirtualPath)
{
    FTemporaryShaderTree Tree("non-portable");
    Tree.Write("Bad Name.hlsl");
    ASSERT_TRUE(AddShaderSourceDirectory(Tree.Root()));
    EXPECT_EQ(
        ScanAndFreezeShaderSourceDirectories().mCode,
        EArdaShaderDirectoryError::InvalidVirtualPath);
}

TEST_F(ArdaShaderDirectories, RejectsFileSymlinkEscapingPhysicalRoot)
{
    FTemporaryShaderTree Registered("file-link-root");
    FTemporaryShaderTree External("file-link-external");
    const fs::path Target = External.Write("External.hlsl");
    const fs::path Link = Registered.Root() / "Linked.hlsl";
    std::error_code Error;
    fs::create_symlink(Target, Link, Error);
    if (Error)
        GTEST_SKIP() << "File symlink creation unavailable: " << Error.message();

    ASSERT_TRUE(AddShaderSourceDirectory(Registered.Root()));
    EXPECT_EQ(
        ScanAndFreezeShaderSourceDirectories().mCode,
        EArdaShaderDirectoryError::EscapesPhysicalDirectory);
}

TEST_F(ArdaShaderDirectories, DoesNotFollowDirectorySymlinks)
{
    FTemporaryShaderTree Registered("directory-link-root");
    FTemporaryShaderTree External("directory-link-external");
    Registered.Write("Visible.hlsl");
    External.Write("Hidden.hlsl");
    const fs::path Link = Registered.Root() / "Linked";
    std::error_code Error;
    fs::create_directory_symlink(External.Root(), Link, Error);
    if (Error)
    {
        GTEST_SKIP() << "Directory symlink creation unavailable: "
                     << Error.message();
    }

    ASSERT_TRUE(AddShaderSourceDirectory(Registered.Root()));
    ASSERT_TRUE(ScanAndFreezeShaderSourceDirectories());
    const auto Files = EnumerateShaderSourceFiles();
    ASSERT_EQ(Files.size(), 1u);
    EXPECT_EQ(Files[0].mVirtualPath, "/Visible.hlsl");
}

TEST_F(ArdaShaderDirectories, EnforcesFreezeUnfreezeAndClearLifecycle)
{
    FTemporaryShaderTree First("freeze-a");
    FTemporaryShaderTree Second("freeze-b");
    First.Write("First.usf");
    ASSERT_TRUE(AddShaderSourceDirectory(First.Root()));
    ASSERT_TRUE(ScanAndFreezeShaderSourceDirectories());
    EXPECT_EQ(
        AddShaderSourceDirectory(Second.Root()).mCode,
        EArdaShaderDirectoryError::Frozen);
    EXPECT_EQ(
        ClearShaderSourceDirectories().mCode,
        EArdaShaderDirectoryError::Frozen);

    ASSERT_TRUE(UnfreezeShaderSourceDirectories());
    EXPECT_FALSE(AreShaderSourceDirectoriesFrozen());
    EXPECT_EQ(EnumerateShaderSourceDirectories().size(), 1u);
    EXPECT_TRUE(EnumerateShaderSourceFiles().empty());
    ASSERT_TRUE(ClearShaderSourceDirectories());
    EXPECT_TRUE(EnumerateShaderSourceDirectories().empty());
}

TEST_F(ArdaShaderDirectories, BackendFailsOnCollisionBeforeDeviceCreation)
{
    FTemporaryShaderTree First("backend-collision-a");
    FTemporaryShaderTree Second("backend-collision-b");
    First.Write("Compute/Sort.hlsl");
    Second.Write("Compute/Sort.hlsl");
    ASSERT_TRUE(AddShaderSourceDirectory(First.Root()));
    ASSERT_TRUE(AddShaderSourceDirectory(Second.Root()));

    EXPECT_FALSE(InitializeBackend());
    EXPECT_EQ(GetDevice(), nullptr);
    EXPECT_NE(
        GetBackendError().find("Duplicate virtual shader source"),
        eastl::string::npos);
    EXPECT_FALSE(AreShaderSourceDirectoriesFrozen());
}

TEST_F(ArdaShaderDirectories, CommitFailureDuringInitRollsBackFreeze)
{
    FTemporaryShaderTree Tree("commit-rollback");
    Tree.Write("Present.hlsl");
    ASSERT_TRUE(AddShaderSourceDirectory(Tree.Root(), "/Types"));
    FArdaShaderTypeRegistration Missing(
        "InitMissingVirtual",
        "/Types/Missing.hlsl",
        "InitMissingVirtual",
        "Main",
        arda::rhi::EArdaRHIShaderStage::Compute,
        nullptr);

    EXPECT_FALSE(InitializeBackend());
    EXPECT_EQ(GetDevice(), nullptr);
    EXPECT_FALSE(AreShaderSourceDirectoriesFrozen());
    EXPECT_TRUE(EnumerateShaderSourceFiles().empty());
    EXPECT_EQ(EnumerateShaderSourceDirectories().size(), 1u);
}

TEST_F(ArdaShaderDirectories, VirtualCommitBeforeFreezeIsActionable)
{
    FTemporaryShaderTree Tree("commit-not-frozen");
    Tree.Write("Present.hlsl");
    ASSERT_TRUE(AddShaderSourceDirectory(Tree.Root(), "/Types"));
    FArdaShaderTypeRegistration Present(
        "NotFrozenVirtual",
        "/Types/Present.hlsl",
        "NotFrozenVirtual",
        "Main",
        arda::rhi::EArdaRHIShaderStage::Compute,
        nullptr);

    const auto Status = FArdaShaderTypeRegistration::CommitAll();
    EXPECT_EQ(
        Status.mCode,
        EArdaShaderRegistrationError::DirectoryRegistryNotFrozen);
    EXPECT_NE(
        Status.mMessage.find("scanned and frozen"),
        eastl::string::npos);
}

TEST_F(ArdaShaderDirectories, ValidatesVirtualShaderTypeAgainstManifest)
{
    FTemporaryShaderTree Tree("shader-type");
    Tree.Write("Present.hlsl");
    ASSERT_TRUE(AddShaderSourceDirectory(Tree.Root(), "/Types"));
    ASSERT_TRUE(ScanAndFreezeShaderSourceDirectories());

    FArdaShaderTypeRegistration Present(
        "VirtualPresent",
        "/Types/Present.hlsl",
        "VirtualPresent",
        "Main",
        arda::rhi::EArdaRHIShaderStage::Compute,
        nullptr);
    EXPECT_TRUE(FArdaShaderTypeRegistration::CommitAll());

    {
        FArdaShaderTypeRegistration Missing(
            "VirtualMissing",
            "/Types/Missing.hlsl",
            "VirtualMissing",
            "Main",
            arda::rhi::EArdaRHIShaderStage::Compute,
            nullptr);
        const auto Status = FArdaShaderTypeRegistration::CommitAll();
        EXPECT_EQ(
            Status.mCode,
            EArdaShaderRegistrationError::MissingVirtualSource);
        EXPECT_NE(Status.mMessage.find("VirtualMissing"), eastl::string::npos);
        EXPECT_NE(
            Status.mMessage.find("/Types/Missing.hlsl"),
            eastl::string::npos);
    }
}

TEST_F(ArdaShaderDirectories, ArtifactFailureReportsVirtualAndPhysicalSource)
{
    FTemporaryShaderTree Tree("map-diagnostic");
    const fs::path Source = Tree.Write("Diagnostic.hlsl");
    ASSERT_TRUE(AddShaderSourceDirectory(Tree.Root(), "/Diagnostics"));
    FArdaShaderTypeRegistration Registration(
        "VirtualDiagnostic",
        "/Diagnostics/Diagnostic.hlsl",
        "DefinitelyMissingVirtualDiagnosticArtifact",
        "Main",
        arda::rhi::EArdaRHIShaderStage::Compute,
        nullptr);

    FArdaBackendConfiguration Configuration;
    Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
    ASSERT_TRUE(ConfigureBackend(Configuration));
    if (!InitializeBackend())
        GTEST_SKIP() << GetBackendError().c_str();

    EXPECT_EQ(
        AddShaderSourceDirectory(Tree.Root(), "/Other").mCode,
        EArdaShaderDirectoryError::BackendInitialized);
    EXPECT_EQ(
        UnfreezeShaderSourceDirectories().mCode,
        EArdaShaderDirectoryError::BackendInitialized);
    EXPECT_EQ(
        ClearShaderSourceDirectories().mCode,
        EArdaShaderDirectoryError::BackendInitialized);

    FArdaGlobalShaderMap Map;
    EXPECT_FALSE(Map.Initialize(
        GetDevice(),
        fs::path(ARDA_BACKEND_TEST_SHADER_DIR)));
    const auto Diagnostics = Map.GetDiagnostics();
    ASSERT_FALSE(Diagnostics.empty());
    const auto& Diagnostic = Diagnostics.back();
    EXPECT_EQ(Diagnostic.mCode, EArdaGlobalShaderMapError::BytecodeMissing);
    EXPECT_EQ(
        Diagnostic.mVirtualSource,
        "/Diagnostics/Diagnostic.hlsl");
    EXPECT_EQ(Diagnostic.mPhysicalSource, Source.string().c_str());
}
