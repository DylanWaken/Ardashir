add_executable(ArdaCiGOperandExample EXCLUDE_FROM_ALL main.cpp)
target_link_libraries(ArdaCiGOperandExample PRIVATE Ardashir::ArdaBackend Ardashir::ArdaDependencyGraph)
set_target_properties(ArdaCiGOperandExample PROPERTIES FOLDER "Ardashir/Tests/Examples")

# A single cubin keeps the tutorial's graph key stable and avoids a runtime PTX compiler dependency.
set(ArdaCiGTargets ${CMAKE_CUDA_ARCHITECTURES_NATIVE} ${ARDASHIR_CUDA_ARCHITECTURES} 80)
list(GET ArdaCiGTargets 0 ArdaCiGDefaultArch)
set(ARDASHIR_CIG_EXAMPLE_ARCHITECTURE "${ArdaCiGDefaultArch}" CACHE STRING "Target GPU architecture for the CiG fill example")
if(NOT ARDASHIR_CIG_EXAMPLE_ARCHITECTURE MATCHES "^([1-9][0-9]*)([af]?)(-real)?$")
    message(FATAL_ERROR "Set ARDASHIR_CIG_EXAMPLE_ARCHITECTURE to a native architecture such as 86 or 120.")
endif()
set(ArdaCiGArch "${CMAKE_MATCH_1}${CMAKE_MATCH_2}")
math(EXPR ArdaCiGAttribute "(${CMAKE_MATCH_1} << 1) | 1")
set(ArdaCiGFillCubin "${CMAKE_CURRENT_BINARY_DIR}/ArdaCiGFill-sm${ArdaCiGArch}.cubin")
set(NvccHostArgs)
if(CMAKE_CUDA_HOST_COMPILER)
    list(APPEND NvccHostArgs -ccbin "${CMAKE_CUDA_HOST_COMPILER}")
endif()
ardashir_compile_native_kernel(
    SOURCE ArdaCiGFill.cu OUTPUT "${ArdaCiGFillCubin}" COMPILER "${CMAKE_CUDA_COMPILER}"
    ARGUMENTS --cubin -arch=sm_${ArdaCiGArch} -std=c++17 ${NvccHostArgs} -o @OUTPUT@ @SOURCE@)
ardashir_add_native_kernel_library(ArdaCiGOperandKernels
    MANIFEST ArdaCiGFill.json.in DEPLOY_TO ArdaCiGOperandExample EXCLUDE_FROM_ALL)
set_target_properties(ArdaCiGOperandKernels PROPERTIES FOLDER "Ardashir/Tests/Examples")

if(WIN32 AND ARDASHIR_BACKEND_D3D12)
    ardashir_deploy_d3d12_agility(ArdaCiGOperandExample)
endif()
