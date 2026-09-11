cmake_minimum_required(VERSION 3.24)
project(ArdaCudaExample LANGUAGES CXX)

# Standalone documentation example, independent of ARDASHIR_ENABLE_CUDA.
option(ARDA_EXAMPLE_BUILD_CUDA "Build the ahead-of-time CUDA host helper" ON)
if(NOT ARDA_EXAMPLE_BUILD_CUDA)
    message(STATUS "CUDA example disabled; no CUDA compiler lookup is needed.")
    return()
endif()

if(NOT CMAKE_CUDA_COMPILER)
    unset(CMAKE_CUDA_COMPILER CACHE)
    unset(CMAKE_CUDA_COMPILER)
endif()
# Enabling a required language directly preserves CMake's real toolchain errors.
enable_language(CUDA)
find_package(CUDAToolkit REQUIRED)
set(ARDA_EXAMPLE_ARCHITECTURES "80;86;89;90;100;120" CACHE STRING "Native binary architecture targets")
set(ARDA_EXAMPLE_REAL_TARGETS)
foreach(Arch IN LISTS ARDA_EXAMPLE_ARCHITECTURES)
    if(NOT Arch MATCHES "^[0-9]+[af]?(-real)?$")
        message(FATAL_ERROR "The example requires explicit native architecture targets.")
    endif()
    string(REGEX REPLACE "-real$" "" Arch "${Arch}")
    list(APPEND ARDA_EXAMPLE_REAL_TARGETS "${Arch}-real")
endforeach()

message(STATUS "Build-time CUDA compiler: ${CMAKE_CUDA_COMPILER}")
message(STATUS "Build-time CUDA Toolkit: ${CUDAToolkit_VERSION}")

add_library(ArdaCudaExample STATIC ArdaCudaDirectLaunch.cu)
set_target_properties(ArdaCudaExample PROPERTIES
    CUDA_STANDARD 17
    CUDA_STANDARD_REQUIRED ON
    CUDA_ARCHITECTURES "${ARDA_EXAMPLE_REAL_TARGETS}"
    CUDA_RUNTIME_LIBRARY Static)
# Propagate runtime linkage when a C++ application links this helper library.
target_link_libraries(ArdaCudaExample PUBLIC CUDA::cudart_static)
