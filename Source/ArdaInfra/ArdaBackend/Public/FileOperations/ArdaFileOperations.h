#pragma once

#include <EASTL/vector.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace arda::fileops
{
	enum class EArdaFileReadResult
	{
		Success,
		OpenFailed,
		ReadFailed
	};

	// Both paths must already be absolute and normalized (canonical for symlink checks).
	bool IsPathContainedBy(const std::filesystem::path& Candidate,
	    const std::filesystem::path& Root,
	    bool AllowEqual = false);

	// Resolves relative paths and dot components before comparing the immediate parent.
	// This is a lexical check; callers requiring symlink containment must canonicalize first.
	bool IsDirectChildPath(const std::filesystem::path& Directory, const std::filesystem::path& Path);
	bool IsRegularNonEmpty(const std::filesystem::path& Path);
	EArdaFileReadResult ReadBinaryFile(const std::filesystem::path& Path, eastl::vector<uint8_t>& Contents);
	std::string ReadText(const std::filesystem::path& Path);

	// Produces a process-unique sibling name; it does not create or reserve a file.
	std::filesystem::path TemporaryPath(const std::filesystem::path& Base, const char* Kind);
	bool AtomicReplace(const std::filesystem::path& Temporary, const std::filesystem::path& Destination);
	bool AtomicWrite(const std::filesystem::path& Path, const std::string& Contents);

	// Publishes (staged source, destination) pairs. On failure, restores prior destinations
	// where possible and retains any backup that cannot be restored for manual recovery.
	// Sources and destinations must be distinct, non-overlapping paths. Callers must serialize
	// destination writes. Published staging files are consumed even if a later pair fails.
	bool PublishFilesTransaction(const std::vector<std::pair<std::filesystem::path, std::filesystem::path>>& Files);

	struct FArdaTemporaryFiles
	{
		std::vector<std::filesystem::path> mPaths;
		~FArdaTemporaryFiles();
	};
}
