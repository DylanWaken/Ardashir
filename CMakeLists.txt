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
option(ARDASHIR_BUILD_NVRHI_TEST "Build the dual-backend NVRHI triangle application" ON)
option(ARDASHIR_BUILD_ARDG_EXAMPLE "Build the dual-backend render-graph terrain application" ON)
option(ARDASHIR_ENABLE_TRACE "Enable Ardashir CPU trace instrumentation" ON)

list(PREPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/Cmake")
include(Platform)
include(Dependencies)

if(ARDASHIR_BUILD_TESTS)
    include(CTest)
    enable_testing()
    include(GoogleTest)
endif()

add_subdirectory(Source/ArdaTrace)
add_subdirectory(Source/ArdaBackend)
add_subdirectory(Source/ArdaRenderGraph)
add_subdirectory(Source/ArdaScene)
add_subdirectory(Source/ArdaGI)
add_subdirectory(Source/ArdaPhys)
add_subdirectory(Source/ArdaDL)

if(ARDASHIR_BUILD_NVRHI_TEST OR ARDASHIR_BUILD_ARDG_EXAMPLE)
    add_subdirectory(Source/ArdaTests)
endif()
