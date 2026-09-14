#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Define platform calling conventions before GLFW supplies its fallback macros.
#include <Windows.h>
#endif

#include "ArdaBackend.h"
#include "ArdaDependencyGraph.h"

#include <EASTL/algorithm.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>

#if defined(ARDA_TEST_WITH_VULKAN)
#include <vulkan/vulkan.h>
#endif
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
