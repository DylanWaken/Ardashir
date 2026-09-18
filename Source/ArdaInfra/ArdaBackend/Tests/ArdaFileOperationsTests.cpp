#include "FileOperations/ArdaFileOperations.h"

#include <gtest/gtest.h>

namespace
{
	namespace fs = std::filesystem;
	namespace fileops = arda::fileops;

	class FArdaFileOperations : public testing::Test
	{
	protected:
		void SetUp() override
		{
			mRoot = fileops::TemporaryPath(fs::temp_directory_path() / "ArdaFileOperations", "tests");
			ASSERT_TRUE(fs::create_directory(mRoot));
		}

		void TearDown() override
		{
			std::error_code Error;
			fs::remove_all(mRoot, Error);
		}

		fs::path mRoot;
	};

	TEST_F(FArdaFileOperations, ReadsBinaryAndEmptyFilesAndClearsMissingFileOutput)
	{
		const std::string Binary("a\0b\xff", 4);
		const auto Path = mRoot / "binary";
		ASSERT_TRUE(fileops::AtomicWrite(Path, Binary));
		eastl::vector<uint8_t> Bytes;
		ASSERT_EQ(fileops::ReadBinaryFile(Path, Bytes), fileops::EArdaFileReadResult::Success);
		ASSERT_EQ(Bytes.size(), Binary.size());
		EXPECT_EQ(std::string(reinterpret_cast<const char*>(Bytes.data()), Bytes.size()), Binary);
		EXPECT_EQ(fileops::ReadText(Path), Binary);
		EXPECT_TRUE(fileops::IsRegularNonEmpty(Path));

		ASSERT_TRUE(fileops::AtomicWrite(Path, ""));
		EXPECT_EQ(fileops::ReadBinaryFile(Path, Bytes), fileops::EArdaFileReadResult::Success);
		EXPECT_TRUE(Bytes.empty());
		EXPECT_FALSE(fileops::IsRegularNonEmpty(Path));
		Bytes.push_back(42);
		EXPECT_EQ(fileops::ReadBinaryFile(mRoot / "missing", Bytes), fileops::EArdaFileReadResult::OpenFailed);
		EXPECT_TRUE(Bytes.empty());
	}

	TEST_F(FArdaFileOperations, PathContainmentUsesComponentsAndNormalizesDirectChildren)
	{
		EXPECT_TRUE(fileops::IsPathContainedBy(mRoot / "nested/file", mRoot));
		EXPECT_FALSE(fileops::IsPathContainedBy(mRoot, mRoot));
		EXPECT_TRUE(fileops::IsPathContainedBy(mRoot, mRoot, true));
		EXPECT_FALSE(fileops::IsPathContainedBy(fs::path(mRoot.string() + "-sibling") / "file", mRoot));
		EXPECT_TRUE(fileops::IsDirectChildPath(mRoot, mRoot / "nested/../file"));
		EXPECT_FALSE(fileops::IsDirectChildPath(mRoot, mRoot / "../outside"));
		EXPECT_FALSE(fileops::IsDirectChildPath(mRoot, mRoot / "nested/file"));
	}

	TEST_F(FArdaFileOperations, AtomicWriteReplacesAndCleansFailedTemporaryFiles)
	{
		const auto Path = mRoot / "artifact";
		ASSERT_TRUE(fileops::AtomicWrite(Path, "old"));
		ASSERT_TRUE(fileops::AtomicWrite(Path, "replacement"));
		EXPECT_EQ(fileops::ReadText(Path), "replacement");
		const auto Directory = mRoot / "directory";
		ASSERT_TRUE(fs::create_directory(Directory));
		EXPECT_FALSE(fileops::AtomicWrite(Directory, "cannot replace a directory"));
		EXPECT_TRUE(fs::is_directory(Directory));
		size_t Count = 0;
		for (const auto& Entry : fs::directory_iterator(mRoot))
		{
			(void)Entry;
			++Count;
		}
		EXPECT_EQ(Count, 2u);
	}

	TEST_F(FArdaFileOperations, TransactionRestoresExistingOutputsWhenLaterPublicationFails)
	{
		const auto First = mRoot / "first";
		const auto Second = mRoot / "second";
		const auto StagedFirst = mRoot / "staged-first";
		ASSERT_TRUE(fileops::AtomicWrite(First, "first original"));
		ASSERT_TRUE(fileops::AtomicWrite(Second, "second original"));
		ASSERT_TRUE(fileops::AtomicWrite(StagedFirst, "first replacement"));
		EXPECT_FALSE(fileops::PublishFilesTransaction({{StagedFirst, First}, {mRoot / "missing-stage", Second}}));
		EXPECT_EQ(fileops::ReadText(First), "first original");
		EXPECT_EQ(fileops::ReadText(Second), "second original");
	}

	TEST_F(FArdaFileOperations, TransactionPublishesAllOutputsAndRemovesBackups)
	{
		const auto First = mRoot / "first";
		const auto Second = mRoot / "second";
		const auto StagedFirst = mRoot / "staged-first";
		const auto StagedSecond = mRoot / "staged-second";
		ASSERT_TRUE(fileops::AtomicWrite(First, "old"));
		ASSERT_TRUE(fileops::AtomicWrite(StagedFirst, "one"));
		ASSERT_TRUE(fileops::AtomicWrite(StagedSecond, "two"));
		ASSERT_TRUE(fileops::PublishFilesTransaction({{StagedFirst, First}, {StagedSecond, Second}}));
		EXPECT_EQ(fileops::ReadText(First), "one");
		EXPECT_EQ(fileops::ReadText(Second), "two");
		size_t Count = 0;
		for (const auto& Entry : fs::directory_iterator(mRoot))
		{
			(void)Entry;
			++Count;
		}
		EXPECT_EQ(Count, 2u);
	}

	TEST_F(FArdaFileOperations, TemporaryNamesAreUniqueAndScopeCleanupRemovesFiles)
	{
		const auto First = fileops::TemporaryPath(mRoot / "artifact", "test");
		const auto Second = fileops::TemporaryPath(mRoot / "artifact", "test");
		EXPECT_NE(First, Second);
		EXPECT_EQ(First.parent_path(), mRoot);
		{
			const fileops::FArdaTemporaryFiles Cleanup{{First, Second}};
			ASSERT_TRUE(fileops::AtomicWrite(First, "one"));
			ASSERT_TRUE(fileops::AtomicWrite(Second, "two"));
		}
		EXPECT_FALSE(fs::exists(First));
		EXPECT_FALSE(fs::exists(Second));
	}

	TEST_F(FArdaFileOperations, TemporaryNamesPreserveNativeUnicodePaths)
	{
		const auto Path = mRoot / fs::u8path(u8"artifact-\u6e32\u67d3");
		const auto Temporary = fileops::TemporaryPath(Path, "unicode");
		EXPECT_EQ(Temporary.native().substr(0, Path.native().size()), Path.native());
		ASSERT_TRUE(fileops::AtomicWrite(Path, "unicode path"));
		EXPECT_EQ(fileops::ReadText(Path), "unicode path");
	}
}
