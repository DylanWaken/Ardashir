file(GLOB_RECURSE ARDA_RENDER_GRAPH_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
)

add_library(ArdaRenderGraph STATIC ${ARDA_RENDER_GRAPH_SOURCES})
add_library(Ardashir::ArdaRenderGraph ALIAS ArdaRenderGraph)

target_compile_features(ArdaRenderGraph PUBLIC cxx_std_17)
target_include_directories(ArdaRenderGraph
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)
target_link_libraries(ArdaRenderGraph
    PUBLIC nvrhi Ardashir::ArdaBackend Ardashir::EASTL
    PRIVATE Ardashir::ArdaTrace
)
target_precompile_headers(ArdaRenderGraph PRIVATE Private/ArdaRenderGraphPch.h)

set_target_properties(ArdaRenderGraph PROPERTIES FOLDER "Ardashir")

if(ARDASHIR_BUILD_TESTS)
    file(GLOB_RECURSE ARDA_RENDER_GRAPH_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp"
    )
    add_executable(ArdaGraphTests ${ARDA_RENDER_GRAPH_TEST_SOURCES})
    target_include_directories(ArdaGraphTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
    target_compile_definitions(ArdaGraphTests PRIVATE ARDA_ENABLE_CHECKS)
    target_link_libraries(ArdaGraphTests PRIVATE
        Ardashir::ArdaRenderGraph
        Ardashir::ArdaBackend
        GTest::gtest_main
    )
    set_target_properties(ArdaGraphTests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaGraphTests)
endif()
