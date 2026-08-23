#pragma once

#include "ArdaBackend.h"
#include "ArdaRenderGraph.h"

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
