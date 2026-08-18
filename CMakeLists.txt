cmake_minimum_required(VERSION 3.16)

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
option(ARDASHIR_ENABLE_TRACE "Enable Ardashir CPU trace instrumentation" ON)
option(ARDASHIR_BACKEND_NVRHI_VULKAN
    "Link the sample NVRHI Vulkan ArdaBackend module" ON)
option(ARDASHIR_BACKEND_NVRHI_D3D12
    "Link the sample NVRHI D3D12 ArdaBackend module on Windows" ON)
set(ARDASHIR_BACKEND_MODULE_TARGETS "" CACHE STRING
    "Additional semicolon-separated CMake backend library targets")
set(ARDASHIR_BACKEND_REGISTRATION_FUNCTIONS "" CACHE STRING
    "Additional arda::backend registration function names supplied by linked modules")

list(PREPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/Cmake")
include(Platform)
include(Dependencies)

add_custom_target(ArdaRHISourceBoundary ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DARDASHIR_SOURCE_DIR=${PROJECT_SOURCE_DIR}/Source"
        -P "${PROJECT_SOURCE_DIR}/Cmake/VerifyRhiBoundary.cmake"
    COMMENT "Checking opaque RHI source boundary"
    VERBATIM
)
set_target_properties(ArdaRHISourceBoundary PROPERTIES FOLDER "Ardashir/Tests")

if(ARDASHIR_BUILD_TESTS)
    include(CTest)
    enable_testing()
    include(GoogleTest)
    add_test(
        NAME ArdaRHISourceBoundary
        COMMAND "${CMAKE_COMMAND}"
            "-DARDASHIR_SOURCE_DIR=${PROJECT_SOURCE_DIR}/Source"
            -P "${PROJECT_SOURCE_DIR}/Cmake/VerifyRhiBoundary.cmake"
    )
endif()

add_subdirectory(Source/ArdaTrace)
add_subdirectory(Source/ArdaBackendNvrhi)
add_subdirectory(Source/ArdaBackend)
add_subdirectory(Source/ArdaRenderGraph)
add_subdirectory(Source/ArdaScene)
add_subdirectory(Source/ArdaGI)
add_subdirectory(Source/ArdaPhys)
add_subdirectory(Source/ArdaDL)

if(ARDASHIR_BUILD_RHI_TEST OR ARDASHIR_BUILD_ARDG_EXAMPLE)
    add_subdirectory(Source/ArdaTests)
endif()
