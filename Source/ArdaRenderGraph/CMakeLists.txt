ardashir_add_module(ArdaRenderGraph Private/ArdaRenderGraphPch.h)
target_link_libraries(ArdaRenderGraph
    PUBLIC Ardashir::ArdaBackend
    PRIVATE Ardashir::ArdaTrace)

if(ARDASHIR_BUILD_TESTS)
    target_link_libraries(ArdaBackendTests PRIVATE Ardashir::ArdaRenderGraph)
    file(GLOB_RECURSE ARDA_RENDER_GRAPH_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp"
    )
    add_executable(ArdaGraphTests ${ARDA_RENDER_GRAPH_TEST_SOURCES})
    ardashir_test_validation(ArdaGraphTests)
    target_include_directories(ArdaGraphTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
    target_compile_definitions(ArdaGraphTests PRIVATE
        ARDA_ENABLE_CHECKS
        ARDA_BACKEND_TEST_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/TestShaderCache"
        ARDA_BACKEND_TEST_SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/Source/ArdaBackend/Tests"
        ARDA_RDG_TEST_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests")
    target_link_libraries(ArdaGraphTests PRIVATE
        Ardashir::ArdaRenderGraph
        Ardashir::ArdaBackend
        GTest::gtest_main
    )
    if(WIN32 AND ARDASHIR_BACKEND_D3D12)
        ardashir_deploy_d3d12_agility(ArdaGraphTests)
    endif()
    set_target_properties(ArdaGraphTests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaGraphTests)
endif()
