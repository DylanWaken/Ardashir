cmake_minimum_required(VERSION 3.24)
project(ArdaCudaCompilerDiscovery LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../ArdaCudaKernels.cmake")
ardashir_enable_cuda_compiler()
if(NOT EXISTS "${ARDASHIR_CUDA_INCLUDE_DIR}/cuda.h")
    message(FATAL_ERROR "CUDA provider include must be a single directory containing cuda.h: ${ARDASHIR_CUDA_INCLUDE_DIR}")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/selected-toolchain.txt"
    "${CMAKE_CUDA_COMPILER}\n${ARDASHIR_CUDA_INCLUDE_DIR}\n")
add_executable(ArdaCudaCompilerDiscovery Probe.cu)
set_target_properties(ArdaCudaCompilerDiscovery PROPERTIES
    CUDA_ARCHITECTURES 80-real CUDA_RUNTIME_LIBRARY Static)
