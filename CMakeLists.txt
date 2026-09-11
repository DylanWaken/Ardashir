cmake_minimum_required(VERSION 3.24)

project(Ardashir VERSION 0.1.0 LANGUAGES CXX)

message(NOTICE [=[
          __    ____  ____    __    ___  _   _  ____  ____
 ___     /__\  (  _ \(  _ \  /__\  / __)( )_( )(_  _)(  _ \   ___
(___)   /(__)\  )   / )(_) )/(__)\ \__ \ ) _ (  _)(_  )   /  (___)
       (__)(__)(_)\_)(____/(__)(__)(___/(_) (_)(____)(_)\_)
]=])

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(ARDASHIR_BUILD_TESTS "Build the Ardashir module tests" ON)
option(ARDASHIR_BUILD_RHI_TEST "Build the dual-backend opaque RHI triangle application" ON)
option(ARDASHIR_BUILD_ARDG_EXAMPLE "Build the dual-backend render-graph terrain application" ON)
option(ARDASHIR_BUILD_CORNELL_BOX "Build the RDG hardware path-traced Cornell Box application" ON)
option(ARDASHIR_ENABLE_TRACE "Enable Ardashir CPU trace instrumentation" ON)
option(ARDASHIR_ENABLE_CUDA "Enable native CUDA kernel launch providers (optional SDK and runtime driver)" OFF)
option(ARDASHIR_BUILD_CUDA_KERNELS "Compile application CUDA kernel variants with nvcc during the build" ${ARDASHIR_ENABLE_CUDA})
set(_ardashir_pixel_sort_default OFF)
if(WIN32 AND ARDASHIR_BUILD_CUDA_KERNELS)
    set(_ardashir_pixel_sort_default ON)
endif()
option(ARDASHIR_BUILD_PIXEL_SORT "Build the backend-only Windows CUDA pixel-sort example" ${_ardashir_pixel_sort_default})
set(ARDASHIR_CUDA_ARCHITECTURES "80;86;89;90;100;120" CACHE STRING "Explicit native CUDA binary targets; no virtual/PTX targets")
set(ARDASHIR_CUDA_INCLUDE_DIR "" CACHE PATH "CUDA 12.0 or newer include directory (CiG capture requires 13.3+); public RHI headers do not require it")
option(ARDASHIR_BACKEND_VULKAN
    "Link the native Vulkan 1.4 ArdaBackend module" ON)
option(ARDASHIR_BACKEND_D3D12
    "Link the native Direct3D 12 ArdaBackend module on Windows" ON)
set(ARDASHIR_BACKEND_MODULE_TARGETS "" CACHE STRING
    "Additional semicolon-separated CMake backend library targets")
set(ARDASHIR_BACKEND_REGISTRATION_FUNCTIONS "" CACHE STRING
    "Additional arda registration function names supplied by linked modules")

list(PREPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/Cmake")
include(Platform)
include(Dependencies)
include(ArdaModule)
include(ArdaCudaKernels)
if(ARDASHIR_BUILD_CUDA_KERNELS)
    ardashir_enable_cuda_compiler()
endif()

add_custom_target(ArdaRHISourceBoundary ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DARDASHIR_SOURCE_DIR=${PROJECT_SOURCE_DIR}/Source"
        -P "${PROJECT_SOURCE_DIR}/Cmake/VerifyRhiBoundary.cmake"
    COMMENT "Checking opaque RHI source boundary"
    VERBATIM
)
set_target_properties(ArdaRHISourceBoundary PROPERTIES FOLDER "Ardashir/Tests")

add_custom_target(ArdaNamespaceBoundary ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DARDASHIR_SOURCE_DIR=${PROJECT_SOURCE_DIR}/Source"
        -P "${PROJECT_SOURCE_DIR}/Cmake/VerifyNamespaces.cmake"
    COMMENT "Checking flat project namespaces"
    VERBATIM
)
set_target_properties(ArdaNamespaceBoundary PROPERTIES FOLDER "Ardashir/Tests")

if(ARDASHIR_BUILD_TESTS)
    include(CTest)
    enable_testing()
    include(GoogleTest)
    include(TestValidation)
    add_test(
        NAME ArdaRHISourceBoundary
        COMMAND "${CMAKE_COMMAND}"
            "-DARDASHIR_SOURCE_DIR=${PROJECT_SOURCE_DIR}/Source"
            -P "${PROJECT_SOURCE_DIR}/Cmake/VerifyRhiBoundary.cmake"
    )
    add_test(
        NAME ArdaNamespaceBoundary
        COMMAND "${CMAKE_COMMAND}"
            "-DARDASHIR_SOURCE_DIR=${PROJECT_SOURCE_DIR}/Source"
            -P "${PROJECT_SOURCE_DIR}/Cmake/VerifyNamespaces.cmake"
    )
endif()

add_subdirectory(Source/ArdaTrace)
add_subdirectory(Source/ArdaBackend)
add_subdirectory(Source/ArdaBackendImpls)
ardashir_finalize_backend_modules()
add_subdirectory(Source/ArdaRenderGraph)
add_subdirectory(Source/ArdaScene)
add_subdirectory(Source/ArdaGI)
add_subdirectory(Source/ArdaPhys)
add_subdirectory(Source/ArdaDL)

if(ARDASHIR_BUILD_RHI_TEST OR ARDASHIR_BUILD_ARDG_EXAMPLE OR ARDASHIR_BUILD_CORNELL_BOX OR ARDASHIR_BUILD_PIXEL_SORT)
    add_subdirectory(Source/ArdaTests)
endif()
