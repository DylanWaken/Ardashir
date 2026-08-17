include_guard(GLOBAL)

include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Declare project-wide fetched dependencies before making any of them available.
# This keeps Ardashir in control if a dependency also uses FetchContent.
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Disable GLFW documentation" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Disable GLFW examples" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Disable GLFW tests" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "Disable GLFW install rules" FORCE)
if(UNIX AND NOT APPLE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "Build GLFW without Wayland dependencies" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "Build GLFW with X11 support" FORCE)
endif()

FetchContent_Declare(
    ardashir_glfw
    GIT_REPOSITORY "https://github.com/glfw/glfw.git"
    GIT_TAG "7b6aead9fb88b3623e3b3725ebb42670cbe4c579"
)

if(WIN32)
    set(_ardashir_dxc_url
        "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2602.24/dxc_2026_05_27.zip")
    set(_ardashir_dxc_hash
        "SHA256=cf658aacf070d3045e31b8f1f8a696c2945f37c1095019481ef7c513368db3b4")
    set(_ardashir_dxc_relative_path "bin/x64/dxc.exe")
elseif(UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(_ardashir_dxc_url
        "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/linux_dxc_2025_07_14.x86_64.tar.gz")
    set(_ardashir_dxc_hash
        "SHA256=f2213da1fc99dc8778c8823078e16ba97c7f80f86a1d4520ab1adf4b462bc48c")
    set(_ardashir_dxc_relative_path "bin/dxc")
else()
    message(FATAL_ERROR "The automatic DXC toolchain supports Windows x64 and Linux x86_64.")
endif()

FetchContent_Declare(
    ardashir_dxc
    URL "${_ardashir_dxc_url}"
    URL_HASH "${_ardashir_dxc_hash}"
)

FetchContent_MakeAvailable(ardashir_glfw ardashir_dxc)

set(ARDASHIR_DXC_EXECUTABLE
    "${ardashir_dxc_SOURCE_DIR}/${_ardashir_dxc_relative_path}"
    CACHE INTERNAL "Path to the pinned DirectX Shader Compiler")
if(NOT EXISTS "${ARDASHIR_DXC_EXECUTABLE}")
    message(FATAL_ERROR
        "The downloaded DXC archive does not contain ${_ardashir_dxc_relative_path}.")
endif()

# EASTL is the project-wide container and algorithm library. Its allocator is
# configured through ArdaEASTLConfig.h so consumers do not need EA's extended
# global operator-new overloads.
set(EASTL_BUILD_BENCHMARK OFF CACHE BOOL "Disable EASTL benchmarks" FORCE)
set(EASTL_BUILD_TESTS OFF CACHE BOOL "Disable EASTL tests" FORCE)
set(EASTL_STD_ITERATOR_CATEGORY_ENABLED ON CACHE BOOL
    "Allow EASTL iterators to interoperate with standard-library facilities" FORCE)
# EASTL currently pins an EABase revision whose project declares CMake 3.1.
# CMake 4 requires a newer compatibility floor; 3.10 also avoids CMake's
# pending-removal deprecation warning.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
add_subdirectory(
    "${PROJECT_SOURCE_DIR}/ThirdParty/EASTL"
    "${PROJECT_BINARY_DIR}/ThirdParty/EASTL"
    EXCLUDE_FROM_ALL)
add_library(Ardashir::EASTL ALIAS EASTL)
target_include_directories(EASTL PUBLIC "${PROJECT_SOURCE_DIR}/Cmake")
target_compile_definitions(EASTL PUBLIC
    EASTL_USER_CONFIG_HEADER="ArdaEASTLConfig.h")
target_compile_options(EASTL PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/wd4100>)
set_target_properties(EASTL EABase PROPERTIES FOLDER "ThirdParty/EASTL")

# Dear ImGui does not provide its own CMake project. Define reusable core and
# GLFW platform-backend targets from the checked-out submodule.
set(_ardashir_imgui_dir "${PROJECT_SOURCE_DIR}/ThirdParty/ImGui")
if(NOT EXISTS "${_ardashir_imgui_dir}/imgui.cpp")
    message(FATAL_ERROR
        "Dear ImGui is missing. Run: git submodule update --init --recursive")
endif()

add_library(ArdashirImGui STATIC
    "${_ardashir_imgui_dir}/imgui.cpp"
    "${_ardashir_imgui_dir}/imgui_draw.cpp"
    "${_ardashir_imgui_dir}/imgui_tables.cpp"
    "${_ardashir_imgui_dir}/imgui_widgets.cpp")
add_library(Ardashir::ImGui ALIAS ArdashirImGui)
target_include_directories(ArdashirImGui PUBLIC "${_ardashir_imgui_dir}")
target_compile_features(ArdashirImGui PUBLIC cxx_std_17)
set_target_properties(ArdashirImGui PROPERTIES FOLDER "ThirdParty/ImGui")

add_library(ArdashirImGuiGlfw STATIC
    "${_ardashir_imgui_dir}/backends/imgui_impl_glfw.cpp")
add_library(Ardashir::ImGuiGlfw ALIAS ArdashirImGuiGlfw)
target_include_directories(ArdashirImGuiGlfw
    PUBLIC
        "${_ardashir_imgui_dir}"
        "${_ardashir_imgui_dir}/backends")
target_compile_definitions(ArdashirImGuiGlfw PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(ArdashirImGuiGlfw PUBLIC Ardashir::ImGui glfw)
target_compile_features(ArdashirImGuiGlfw PUBLIC cxx_std_17)
set_target_properties(ArdashirImGuiGlfw PROPERTIES FOLDER "ThirdParty/ImGui")

# NVRHI is a sample backend dependency, not an ArdaBackend ABI dependency.
if(ARDASHIR_BACKEND_NVRHI_VULKAN OR
   (WIN32 AND ARDASHIR_BACKEND_NVRHI_D3D12))
    set(NVRHI_INSTALL OFF CACHE BOOL "Disable NVRHI install rules" FORCE)
    # The shared NVRHI external-device adapter uses Vulkan as its portable base.
    set(NVRHI_WITH_VULKAN ON
        CACHE BOOL "Build the NVRHI Vulkan backend" FORCE)
    if(WIN32)
        set(NVRHI_WITH_DX12 ${ARDASHIR_BACKEND_NVRHI_D3D12}
            CACHE BOOL "Build the NVRHI D3D12 backend" FORCE)
    endif()
    add_subdirectory(
        "${PROJECT_SOURCE_DIR}/ThirdParty/NVRHI"
        "${PROJECT_BINARY_DIR}/ThirdParty/NVRHI")
endif()

if(ARDASHIR_BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE BOOL "Disable GoogleTest install rules" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "Use the shared MSVC runtime" FORCE)
    add_subdirectory(
        "${PROJECT_SOURCE_DIR}/ThirdParty/GoogleTest"
        "${PROJECT_BINARY_DIR}/ThirdParty/GoogleTest"
        EXCLUDE_FROM_ALL)
endif()
