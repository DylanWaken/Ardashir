cmake_minimum_required(VERSION 3.24)
project(ArdaUnrealLibraries LANGUAGES CXX)

set(ARDA_UNREAL_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../ThirdParty/Ardashir" CACHE PATH
    "Clean, independently checked out Ardashir dependency")
if(NOT EXISTS "${ARDA_UNREAL_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "Run Scripts/ArdaBuild.py to fetch Ardashir before configuring.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
add_compile_definitions(EASTL_DEBUG=0 _ITERATOR_DEBUG_LEVEL=0
    EIGEN_MAX_ALIGN_BYTES=16 EIGEN_MAX_STATIC_ALIGN_BYTES=16)
if(MSVC)
    add_compile_options(/GR /EHsc /Zc:__cplusplus /MP8)
endif()

# All integration choices and generated outputs stay outside the dependency.
set(ARDASHIR_BACKEND_VULKAN OFF CACHE BOOL "Use Unreal RHI" FORCE)
set(ARDASHIR_BACKEND_D3D12 OFF CACHE BOOL "Use Unreal RHI" FORCE)
set(ARDASHIR_BUILD_TESTS OFF CACHE BOOL "Build host integration tests separately" FORCE)
set(ARDASHIR_BUILD_RHI_TEST OFF CACHE BOOL "Disable standalone windows" FORCE)
set(ARDASHIR_BUILD_ARDG_EXAMPLE OFF CACHE BOOL "Disable standalone windows" FORCE)
set(ARDASHIR_BUILD_CORNELL_BOX OFF CACHE BOOL "Disable standalone windows" FORCE)
set(ARDASHIR_BUILD_PIXEL_SORT OFF CACHE BOOL "Disable standalone windows" FORCE)
set(ARDASHIR_ENABLE_TRACE OFF CACHE BOOL "Unreal owns tracing" FORCE)
set(ARDASHIR_ENABLE_CUDA OFF CACHE BOOL "Host provider owns CUDA interop" FORCE)
set(ARDASHIR_BUILD_CUDA_KERNELS OFF CACHE BOOL "Test project builds its CUDA kernel" FORCE)
add_subdirectory("${ARDA_UNREAL_SOURCE}" Ardashir EXCLUDE_FROM_ALL)
get_target_property(ARDA_EABASE_INCLUDE EABase INTERFACE_INCLUDE_DIRECTORIES)
file(WRITE "${CMAKE_BINARY_DIR}/ArdaEABaseInclude.txt" "${ARDA_EABASE_INCLUDE}")

# Compile the dependency's own source files into one DLL. Sharing a DLL keeps
# provider, shader and dependency-node registries unique across Unreal modules.
# Source lists and include paths come from upstream targets, without copying or
# editing their source files or putting Ardashir implementation in UE modules.
add_library(ArdaUnrealCore SHARED)
foreach(ARDA_MODULE ArdaBackend ArdaMath ArdaGraph ArdaDependencyGraph ArdaTrace EASTL)
    get_target_property(ARDA_MODULE_SOURCES ${ARDA_MODULE} SOURCES)
    get_target_property(ARDA_MODULE_SOURCE_DIR ${ARDA_MODULE} SOURCE_DIR)
    foreach(ARDA_MODULE_SOURCE IN LISTS ARDA_MODULE_SOURCES)
        if(ARDA_MODULE_SOURCE MATCHES "\\.cpp$")
            if(NOT IS_ABSOLUTE "${ARDA_MODULE_SOURCE}")
                set(ARDA_MODULE_SOURCE "${ARDA_MODULE_SOURCE_DIR}/${ARDA_MODULE_SOURCE}")
            endif()
            target_sources(ArdaUnrealCore PRIVATE "${ARDA_MODULE_SOURCE}")
        endif()
    endforeach()
    get_target_property(ARDA_MODULE_INCLUDES ${ARDA_MODULE} INCLUDE_DIRECTORIES)
    if(ARDA_MODULE_INCLUDES)
        target_include_directories(ArdaUnrealCore PRIVATE ${ARDA_MODULE_INCLUDES})
    endif()
endforeach()
target_include_directories(ArdaUnrealCore PRIVATE
    "${ARDA_UNREAL_SOURCE}/Cmake"
    "${ARDA_UNREAL_SOURCE}/ThirdParty/Eigen"
    ${ARDA_EABASE_INCLUDE})
target_compile_definitions(ArdaUnrealCore PRIVATE
    EASTL_USER_CONFIG_HEADER="ArdaEASTLConfig.h"
    EASTL_STD_ITERATOR_CATEGORY_ENABLED=1
    EASTL_OPENSOURCE=1
    EASTL_DLL=1
    # Automatic symbol exports avoid forcing dllexport emission of EASTL's
    # unused built-in allocator, whose definitions the Arda allocator disables.
    "EASTL_API="
    EIGEN_DEFAULT_TO_ROW_MAJOR
    ARDASHIR_ENABLE_TRACE=0
    NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(ArdaUnrealCore PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# CPU-only executable exercises template code and node callbacks on the other
# side of the actual DLL boundary, without a GPU or Unreal editor process.
add_executable(ArdaUnrealAbiTests EXCLUDE_FROM_ALL "../Tests/ArdaSharedLibraryTests.cpp")
target_link_libraries(ArdaUnrealAbiTests PRIVATE ArdaUnrealCore)
foreach(ARDA_MODULE ArdaBackend ArdaMath ArdaGraph ArdaDependencyGraph)
    get_target_property(ARDA_MODULE_SOURCE_DIR ${ARDA_MODULE} SOURCE_DIR)
    target_include_directories(ArdaUnrealAbiTests PRIVATE "${ARDA_MODULE_SOURCE_DIR}/Public")
endforeach()
target_include_directories(ArdaUnrealAbiTests PRIVATE
    "${ARDA_UNREAL_SOURCE}/Cmake"
    "${ARDA_UNREAL_SOURCE}/ThirdParty/Eigen"
    "${ARDA_UNREAL_SOURCE}/ThirdParty/EASTL/include"
    ${ARDA_EABASE_INCLUDE})
target_compile_definitions(ArdaUnrealAbiTests PRIVATE
    EASTL_USER_CONFIG_HEADER="ArdaEASTLConfig.h"
    EASTL_STD_ITERATOR_CATEGORY_ENABLED=1 EASTL_OPENSOURCE=1 EASTL_DLL=1
    EIGEN_DEFAULT_TO_ROW_MAJOR ARDASHIR_ENABLE_TRACE=0)
set_target_properties(ArdaUnrealAbiTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
