#pragma once
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#include <Windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <vector>
#endif

namespace arda
{
	/** Resolve node assets beside the executable, independently of the renderer and working directory. */
	inline std::filesystem::path GetArdaExampleDirectory()
	{
#ifdef _WIN32
		wchar_t Executable[32768];
		const auto Length = GetModuleFileNameW(nullptr, Executable, 32768);
		if (!Length || Length == 32768)
		{
			throw std::runtime_error("Cannot locate example executable.");
		}
		return std::filesystem::path(Executable).parent_path();
#elif defined(__APPLE__)
		uint32_t Size = 0;
		_NSGetExecutablePath(nullptr, &Size);
		std::vector<char> Executable(Size);
		if (_NSGetExecutablePath(Executable.data(), &Size) != 0)
		{
			throw std::runtime_error("Cannot locate example executable.");
		}
		return std::filesystem::canonical(Executable.data()).parent_path();
#else
		return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif
	}
}
