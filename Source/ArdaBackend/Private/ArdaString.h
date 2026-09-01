#pragma once

#include <EASTL/string.h>

#include <filesystem>
#include <string>

namespace arda::backend
{
    /** Canonical conversion at the backend's EASTL/std string boundary. */
    [[nodiscard]] inline eastl::string ToEastl(const std::string& Value)
    {
        return eastl::string(Value.data(), Value.size());
    }

    /** Canonical conversion at the backend's EASTL/std string boundary. */
    [[nodiscard]] inline std::string ToStd(const eastl::string& Value)
    {
        return std::string(Value.data(), Value.size());
    }

    /** Converts a native filesystem path to the backend string type. */
    [[nodiscard]] inline eastl::string ToEastlString(
        const std::filesystem::path& Path)
    {
        return ToEastl(Path.string());
    }
}
