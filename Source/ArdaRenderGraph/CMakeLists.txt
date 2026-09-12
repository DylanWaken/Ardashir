ardashir_add_module(ArdaRenderGraph Private/ArdaInductorPch.h)
target_link_libraries(ArdaRenderGraph
    PUBLIC Ardashir::ArdaBackend Ardashir::ArdaGraph
    PRIVATE Ardashir::ArdaTrace)

add_custom_target(ArdaDependencyGraphBoundary ALL
    COMMAND "${CMAKE_COMMAND}" "-DARDASHIR_SOURCE_DIR=${PROJECT_SOURCE_DIR}/Source"
        -P "${PROJECT_SOURCE_DIR}/Cmake/VerifyDependencyGraphBoundary.cmake"
    COMMENT "Checking the persistent graph is the only graph implementation"
    VERBATIM)
set_target_properties(ArdaDependencyGraphBoundary PROPERTIES FOLDER "Ardashir/Tests")

# Every public graph header must compile without access to execution internals.
file(GLOB ARDA_GRAPH_PUBLIC_HEADERS CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/Public" "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h")
set(ARDA_GRAPH_HEADER_TARGETS)
foreach(ARDA_GRAPH_PUBLIC_HEADER IN LISTS ARDA_GRAPH_PUBLIC_HEADERS)
    string(MAKE_C_IDENTIFIER "${ARDA_GRAPH_PUBLIC_HEADER}" ARDA_GRAPH_HEADER_ID)
    set(ARDA_GRAPH_HEADER_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/PublicHeader_${ARDA_GRAPH_HEADER_ID}.cpp")
    file(GENERATE OUTPUT "${ARDA_GRAPH_HEADER_SOURCE}" CONTENT "#include \"${ARDA_GRAPH_PUBLIC_HEADER}\"\n")
    set(ARDA_GRAPH_HEADER_TARGET "ArdaDependencyGraphHeader_${ARDA_GRAPH_HEADER_ID}")
    add_library(${ARDA_GRAPH_HEADER_TARGET} OBJECT "${ARDA_GRAPH_HEADER_SOURCE}")
    target_compile_features(${ARDA_GRAPH_HEADER_TARGET} PRIVATE cxx_std_17)
    target_link_libraries(${ARDA_GRAPH_HEADER_TARGET} PRIVATE Ardashir::ArdaRenderGraph)
    set_target_properties(${ARDA_GRAPH_HEADER_TARGET} PROPERTIES FOLDER "Ardashir/Tests/PublicHeaders")
    list(APPEND ARDA_GRAPH_HEADER_TARGETS ${ARDA_GRAPH_HEADER_TARGET})
endforeach()
add_custom_target(ArdaDependencyGraphPublicHeaders ALL DEPENDS ${ARDA_GRAPH_HEADER_TARGETS})
set_target_properties(ArdaDependencyGraphPublicHeaders PROPERTIES FOLDER "Ardashir/Tests")

if(ARDASHIR_BUILD_TESTS)
    add_test(NAME ArdaDependencyGraphBoundary
        COMMAND "${CMAKE_COMMAND}" "-DARDASHIR_SOURCE_DIR=${PROJECT_SOURCE_DIR}/Source"
            -P "${PROJECT_SOURCE_DIR}/Cmake/VerifyDependencyGraphBoundary.cmake")
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
