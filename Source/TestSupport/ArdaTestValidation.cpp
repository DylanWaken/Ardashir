/** @file ArdaTestValidation.cpp
 * Adds the build-local Vulkan layer to test-process discovery before main.
 * The backend still probes actual availability; a manifest alone is not proof.
 */
#include <cstdlib>
#include <fstream>
#include <string>

namespace
{
	// Shared by direct executable runs and CTest. An explicit VK_LAYER_PATH
	// remains authoritative, including tests that intentionally hide layers.
	const bool ValidationEnvironmentConfigured = []
	{
		if (std::getenv("VK_LAYER_PATH"))
		{
			return true;
		}
		std::ifstream File(ARDA_TEST_VULKAN_LAYER_PATH_FILE);
		std::string Path;
		if (!std::getline(File, Path) || Path.empty())
		{
			return true;
		}
		if (const char* Existing = std::getenv("VK_ADD_LAYER_PATH"); Existing && *Existing)
		{
#if defined(_WIN32)
			Path += ';';
#else
			Path += ':';
#endif
			Path += Existing;
		}
#if defined(_WIN32)
		_putenv_s("VK_ADD_LAYER_PATH", Path.c_str());
#else
		setenv("VK_ADD_LAYER_PATH", Path.c_str(), 1);
#endif
		return true;
	}();
}
