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

	EArdaFileReadResult ReadBinaryFile(const std::filesystem::path& Path, eastl::vector<uint8_t>& Contents)
	{
		Contents.clear();
		std::ifstream Stream(Path, std::ios::binary | std::ios::ate);
		if (!Stream)
		{
			return EArdaFileReadResult::OpenFailed;
		}
		const std::streamoff Size = Stream.tellg();
		if (Size < 0 || static_cast<uintmax_t>(Size) > eastl::numeric_limits<size_t>::max() ||
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
		eastl::vector<eastl::pair<std::filesystem::path, std::filesystem::path>> Backups;
		eastl::vector<std::filesystem::path> Published;
		const auto Rollback = [&]
		{
			for (const auto& Destination : Published)
			{
				std::filesystem::remove(Destination, Error);
			}
			for (auto It = Backups.rbegin(); It != Backups.rend(); ++It)
			{
				// Keep any backup that cannot be restored for later recovery.
				AtomicReplace(It->second, It->first);
			}
			return false;
		};
		for (const auto& File : Files)
		{
			const bool Exists = std::filesystem::exists(File.second, Error);
			if (Error || (Exists && !std::filesystem::is_regular_file(File.second, Error)))
			{
				return Rollback();
			}
			if (Exists)
			{
				const auto Backup = TemporaryPath(File.second, "backup");
				if (!AtomicReplace(File.second, Backup))
				{
					return Rollback();
				}
				Backups.emplace_back(File.second, Backup);
			}
		}
		for (const auto& File : Files)
		{
			if (!AtomicReplace(File.first, File.second))
			{
				return Rollback();
			}
			Published.push_back(File.second);
		}
		for (const auto& Backup : Backups)
		{
			std::filesystem::remove(Backup.second, Error);
		}
		return true;
	}
}
