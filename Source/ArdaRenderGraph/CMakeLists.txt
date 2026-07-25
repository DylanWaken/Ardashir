add_library(ArdaRenderGraph STATIC
    Public/ArdaRenderGraph.h
    Private/ArdaRenderGraph.cpp
    Private/ArdaRenderGraphPch.h
)
add_library(Ardashir::ArdaRenderGraph ALIAS ArdaRenderGraph)

target_compile_features(ArdaRenderGraph PUBLIC cxx_std_17)
target_include_directories(ArdaRenderGraph
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)
target_link_libraries(ArdaRenderGraph PUBLIC nvrhi)
target_precompile_headers(ArdaRenderGraph PRIVATE Private/ArdaRenderGraphPch.h)

set_target_properties(ArdaRenderGraph PROPERTIES FOLDER "Ardashir")

if(ARDASHIR_BUILD_TESTS)
    add_executable(ArdaGraphTests Tests/ArdaGraphTests.cpp)
    target_link_libraries(ArdaGraphTests PRIVATE Ardashir::ArdaRenderGraph GTest::gtest_main)
    set_target_properties(ArdaGraphTests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaGraphTests)
endif()
