/** @file ArdaTestValidation.cpp
 * Configures Vulkan layers only for example/test processes, before main.
 * The backend still probes actual availability; a manifest alone is not proof.
 */
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
#include <vulkan/vulkan_core.h>

#ifndef ARDA_TEST_VULKAN_LAYER_DIR
#define ARDA_TEST_VULKAN_LAYER_DIR ""
#endif

namespace
{
	void SetEnvironmentDefault(const char* Name, const char* Value)
	{
		if (std::getenv(Name))
		{
			return;
		}
#if defined(_WIN32)
		_putenv_s(Name, Value);
#else
		setenv(Name, Value, 0);
#endif
	}

#if ARDA_TEST_ENABLE_VALIDATION
	bool HasDiscoverableValidationLayer()
	{
#if defined(_WIN32)
		const auto Library = LoadLibraryW(L"vulkan-1.dll");
		const auto EnumerateLayers = Library ? reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
		                                           GetProcAddress(Library, "vkEnumerateInstanceLayerProperties"))
		                                     : nullptr;
#else
		void* Library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
		const auto EnumerateLayers = Library ? reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
		                                           dlsym(Library, "vkEnumerateInstanceLayerProperties"))
		                                     : nullptr;
#endif
		bool Found = false;
		if (EnumerateLayers)
		{
			// Enumeration does not create an instance or enable any layer. Retry
			// if an installation changes between the count and property queries.
			for (int Attempt = 0; Attempt < 3; ++Attempt)
			{
				uint32_t Count = 0;
				if (EnumerateLayers(&Count, nullptr) != VK_SUCCESS || Count == 0)
				{
					break;
				}
				std::vector<VkLayerProperties> Layers(Count);
				const VkResult Result = EnumerateLayers(&Count, Layers.data());
				if (Result != VK_SUCCESS && Result != VK_INCOMPLETE)
				{
					break;
				}
				for (uint32_t Index = 0; Index < Count; ++Index)
				{
					if (std::strcmp(Layers[Index].layerName, "VK_LAYER_KHRONOS_validation") == 0)
					{
						Found = true;
						break;
					}
				}
				if (Found || Result == VK_SUCCESS)
				{
					break;
				}
			}
		}
		if (Library)
		{
#if defined(_WIN32)
			FreeLibrary(Library);
#else
			dlclose(Library);
#endif
		}
		return Found;
	}
#endif

	// Shared by direct executable runs and CTest. An explicit VK_LAYER_PATH
	// remains authoritative, including tests that intentionally hide layers.
	const bool ValidationEnvironmentConfigured = []
	{
		const char* EnableOverlays = std::getenv("ARDASHIR_ENABLE_VULKAN_OVERLAYS");
		if (!EnableOverlays || std::strcmp(EnableOverlays, "1") != 0)
		{
			// These capture overlays may advertise an older Vulkan API than our
			// applications. Use their own manifest switches, preserving explicit
			// choices and leaving validation and unrelated implicit layers intact.
			SetEnvironmentDefault("DISABLE_RTSS_LAYER", "1");
			SetEnvironmentDefault("DISABLE_VULKAN_OBS_CAPTURE", "1");
		}
#if ARDA_TEST_ENABLE_VALIDATION
		if (std::getenv("VK_LAYER_PATH"))
		{
			return true;
		}
		// The configured path is a fallback. A registered SDK or an existing
		// additive path already exposing validation must not be added twice.
		if (HasDiscoverableValidationLayer())
		{
			return true;
		}
		std::ifstream File(ARDA_TEST_VULKAN_LAYER_PATH_FILE);
		std::string Path;
		if (!std::getline(File, Path) || Path.empty())
		{
			// Optional example builds do not run provisioning. An explicitly
			// configured SDK directory still works when its path file is absent.
			Path = ARDA_TEST_VULKAN_LAYER_DIR;
			if (Path.empty())
			{
				return true;
			}
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
#endif
		return true;
	}();
}
