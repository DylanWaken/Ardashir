#include "FileOperations/ArdaFileOperations.h"
#include "FileOperations/ArdaString.h"

#include <EASTL/algorithm.h>
#include <EASTL/atomic.h>
#include <cctype>
#include <fstream>
#include <iterator>
#include <EASTL/numeric_limits.h>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace arda::fileops
{
	namespace
	{
		bool PhysicalComponentEqual(const std::filesystem::path& Left, const std::filesystem::path& Right)
		{
#if defined(_WIN32)
			const auto Fold = [](const std::filesystem::path& Path)
			{
				eastl::string Value = ToEastl(Path.generic_string());
				eastl::transform(Value.begin(), Value.end(), Value.begin(), [](unsigned char Character)
				    { return static_cast<char>(std::tolower(Character)); });
				return Value;
			};
			return Fold(Left) == Fold(Right);
#else
			return Left == Right;
#endif
		}
	}

	bool IsPathContainedBy(const std::filesystem::path& Candidate,
	    const std::filesystem::path& Root,
	    bool AllowEqual)
	{
		auto CandidateIt = Candidate.begin();
		for (auto RootIt = Root.begin(); RootIt != Root.end(); ++RootIt, ++CandidateIt)
		{
			if (CandidateIt == Candidate.end() || !PhysicalComponentEqual(*CandidateIt, *RootIt))
			{
				return false;
			}
		}
		return AllowEqual || CandidateIt != Candidate.end();
	}

	bool IsDirectChildPath(const std::filesystem::path& Directory, const std::filesystem::path& Path)
	{
		std::error_code Error;
		const auto Root = std::filesystem::absolute(Directory, Error).lexically_normal();
		if (Error)
		{
			return false;
		}
		const auto Candidate = std::filesystem::absolute(Path, Error).lexically_normal();
		return !Error && Candidate.parent_path() == Root;
	}

	bool IsRegularNonEmpty(const std::filesystem::path& Path)
	{
		std::error_code Error;
		return std::filesystem::is_regular_file(Path, Error) && !Error &&
		    std::filesystem::file_size(Path, Error) > 0 && !Error;
	}

	template <class Buffer>
	static EArdaFileReadResult ReadBinaryContents(const std::filesystem::path& Path, Buffer& Contents, size_t MaximumBytes)
	{
		Contents.clear();
		std::ifstream Stream(Path, std::ios::binary | std::ios::ate);
		if (!Stream)
		{
			return EArdaFileReadResult::OpenFailed;
		}
		const std::streamoff Size = Stream.tellg();
		if (Size < 0 || static_cast<uintmax_t>(Size) > MaximumBytes ||
		    Size > eastl::numeric_limits<std::streamsize>::max())
		{
			return EArdaFileReadResult::ReadFailed;
		}
		if (Size == 0)
		{
			return EArdaFileReadResult::Success;
		}
		Contents.resize(static_cast<size_t>(Size));
		Stream.seekg(0);
		Stream.read(reinterpret_cast<char*>(Contents.data()), static_cast<std::streamsize>(Size));
		if (!Stream)
		{
			Contents.clear();
			return EArdaFileReadResult::ReadFailed;
		}
		return EArdaFileReadResult::Success;
	}

	EArdaFileReadResult ReadBinaryFile(const std::filesystem::path& Path, eastl::vector<uint8_t>& Contents)
	{
		return ReadBinaryContents(Path, Contents, eastl::vector<uint8_t>::kMaxSize);
	}

	EArdaFileReadResult ReadBinaryFile(const std::filesystem::path& Path, eastl::string& Contents, size_t MaximumBytes)
	{
		return ReadBinaryContents(Path, Contents, eastl::min<size_t>(MaximumBytes, Contents.max_size()));
	}

	eastl::string ReadText(const std::filesystem::path& Path)
	{
		std::ifstream Stream(Path, std::ios::binary);
		// Standard stream iterators have no EASTL counterpart; convert at this I/O boundary.
		return ToEastl(std::string(std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>()));
	}

	std::filesystem::path TemporaryPath(const std::filesystem::path& Base, const char* Kind)
	{
		static eastl::atomic<uint64_t> TemporaryId{0};
		const uint64_t ProcessId =
#if defined(_WIN32)
		    static_cast<uint64_t>(GetCurrentProcessId());
#else
		    static_cast<uint64_t>(getpid());
#endif
		std::filesystem::path Result = Base;
		const eastl::string Suffix = eastl::string(".arda-") + Kind + "-" + eastl::to_string(ProcessId) + "-" +
		    eastl::to_string(++TemporaryId) + ".tmp";
		Result += Suffix.c_str();
		return Result;
	}

	bool AtomicReplace(const std::filesystem::path& Temporary, const std::filesystem::path& Destination)
	{
#if defined(_WIN32)
		return MoveFileExW(Temporary.c_str(), Destination.c_str(),
		           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
		std::error_code Error;
		std::filesystem::rename(Temporary, Destination, Error);
		return !Error;
#endif
	}

	FArdaTemporaryFiles::~FArdaTemporaryFiles()
	{
		std::error_code Error;
		for (const auto& Path : mPaths)
		{
			std::filesystem::remove(Path, Error);
		}
	}

	bool AtomicWrite(const std::filesystem::path& Path, const eastl::string& Contents)
	{
		const auto Temporary = TemporaryPath(Path, "write");
		const FArdaTemporaryFiles Cleanup{{Temporary}};
		{
			std::ofstream Stream(Temporary, std::ios::binary | std::ios::trunc);
			Stream.write(Contents.data(), static_cast<std::streamsize>(Contents.size()));
			Stream.close();
			if (!Stream)
			{
				return false;
			}
		}
		return AtomicReplace(Temporary, Path);
	}

	bool PublishFilesTransaction(const eastl::vector<eastl::pair<std::filesystem::path, std::filesystem::path>>& Files)
	{
		std::error_code Error;
		// Snapshot existing files without removing them from paths used by active readers.
		eastl::vector<std::filesystem::path> Backups;
		Backups.reserve(Files.size());
		FArdaTemporaryFiles Cleanup;
		Cleanup.mPaths.reserve(Files.size());
		for (const auto& File : Files)
		{
			std::filesystem::path Backup;
			const bool Exists = std::filesystem::exists(File.second, Error);
			if (Error || (Exists && !std::filesystem::is_regular_file(File.second, Error)))
			{
				return false;
			}
			if (Exists)
			{
				Backup = TemporaryPath(File.second, "backup");
				Cleanup.mPaths.push_back(Backup);
				if (!std::filesystem::copy_file(File.second, Backup, Error))
				{
					return false;
				}
			}
			Backups.push_back(Backup);
		}
		for (size_t Index = 0; Index < Files.size(); ++Index)
		{
			if (AtomicReplace(Files[Index].first, Files[Index].second))
			{
				continue;
			}
			while (Index > 0)
			{
				--Index;
				const auto& Destination = Files[Index].second;
				const auto& Backup = Backups[Index];
				if (Backup.empty())
				{
					std::filesystem::remove(Destination, Error);
				}
				else if (!AtomicReplace(Backup, Destination))
				{
					// A reader may deny replacement. Preserve the backup for manual recovery.
					auto Found = eastl::find(Cleanup.mPaths.begin(), Cleanup.mPaths.end(), Backup);
					Cleanup.mPaths.erase(Found);
				}
			}
			return false;
		}
		return true;
	}
}
