cmake_minimum_required(VERSION 3.24)
project(ArdaCudaExample LANGUAGES CXX)

# Standalone documentation example, independent of ARDASHIR_ENABLE_CUDA.
option(ARDA_EXAMPLE_BUILD_CUDA "Build the ahead-of-time CUDA host helper" ON)
if(NOT ARDA_EXAMPLE_BUILD_CUDA)
    message(STATUS "CUDA example disabled; no CUDA compiler lookup is needed.")
    return()
endif()

include(CheckLanguage)
check_language(CUDA)
if(NOT CMAKE_CUDA_COMPILER)
    message(FATAL_ERROR
        "Building this example requires a CUDA compiler on the build machine. "
        "For Ninja, select nvcc with CMAKE_CUDA_COMPILER or CUDACXX; "
        "for Visual Studio, select an installed CUDA toolset with -T cuda=<version>. "
        "Use a fresh build directory after changing toolchains, or set "
        "ARDA_EXAMPLE_BUILD_CUDA=OFF. End users do not need nvcc at runtime.")
endif()
enable_language(CUDA)
find_package(CUDAToolkit REQUIRED)

message(STATUS "Build-time CUDA compiler: ${CMAKE_CUDA_COMPILER}")
message(STATUS "Build-time CUDA Toolkit: ${CUDAToolkit_VERSION}")

add_library(ArdaCudaExample STATIC ArdaCudaDirectLaunch.cu)
set_target_properties(ArdaCudaExample PROPERTIES
    CUDA_STANDARD 17
    CUDA_STANDARD_REQUIRED ON
    CUDA_RUNTIME_LIBRARY Static)
# Propagate runtime linkage when a C++ application links this helper library.
target_link_libraries(ArdaCudaExample PUBLIC CUDA::cudart_static)
