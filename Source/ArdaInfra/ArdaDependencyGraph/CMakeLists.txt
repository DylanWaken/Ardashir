include(ArdaDependencyGraphLayout)
ardashir_validate_dependency_graph_layout("${CMAKE_CURRENT_SOURCE_DIR}")
ardashir_add_module(ArdaDependencyGraph Private/Inductor/Compiler/ArdaInductorPch.h)
target_link_libraries(ArdaDependencyGraph
    PUBLIC Ardashir::ArdaBackend Ardashir::ArdaGraph
    PRIVATE Ardashir::ArdaTrace)

# Verify both sides of the module boundary independently, without a PCH.
foreach(ARDA_GRAPH_VISIBILITY Public Private)
    file(GLOB_RECURSE ARDA_GRAPH_HEADERS CONFIGURE_DEPENDS
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/${ARDA_GRAPH_VISIBILITY}"
        "${CMAKE_CURRENT_SOURCE_DIR}/${ARDA_GRAPH_VISIBILITY}/*.h")
    set(ARDA_GRAPH_HEADER_TARGETS)
    foreach(ARDA_GRAPH_HEADER IN LISTS ARDA_GRAPH_HEADERS)
        # Keep generated object/PDB paths below Windows path limits in nested build trees.
        string(SHA1 ARDA_GRAPH_HEADER_HASH "${ARDA_GRAPH_HEADER}")
        string(SUBSTRING "${ARDA_GRAPH_HEADER_HASH}" 0 12 ARDA_GRAPH_HEADER_ID)
        set(ARDA_GRAPH_HEADER_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/${ARDA_GRAPH_VISIBILITY}Header_${ARDA_GRAPH_HEADER_ID}.cpp")
        file(GENERATE OUTPUT "${ARDA_GRAPH_HEADER_SOURCE}" CONTENT "#include \"${ARDA_GRAPH_HEADER}\"\n")
        set(ARDA_GRAPH_HEADER_TARGET "ArdaDependencyGraph${ARDA_GRAPH_VISIBILITY}Header_${ARDA_GRAPH_HEADER_ID}")
        add_library(${ARDA_GRAPH_HEADER_TARGET} OBJECT "${ARDA_GRAPH_HEADER_SOURCE}")
        target_compile_features(${ARDA_GRAPH_HEADER_TARGET} PRIVATE cxx_std_17)
        target_include_directories(${ARDA_GRAPH_HEADER_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Public")
        if(ARDA_GRAPH_VISIBILITY STREQUAL "Private")
            target_include_directories(${ARDA_GRAPH_HEADER_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
        endif()
        target_link_libraries(${ARDA_GRAPH_HEADER_TARGET} PRIVATE Ardashir::ArdaBackend Ardashir::ArdaGraph)
        set_target_properties(${ARDA_GRAPH_HEADER_TARGET} PROPERTIES FOLDER "Ardashir/Tests/${ARDA_GRAPH_VISIBILITY}Headers")
        list(APPEND ARDA_GRAPH_HEADER_TARGETS ${ARDA_GRAPH_HEADER_TARGET})
    endforeach()
    add_custom_target(ArdaDependencyGraph${ARDA_GRAPH_VISIBILITY}Headers ALL DEPENDS ${ARDA_GRAPH_HEADER_TARGETS})
    set_target_properties(ArdaDependencyGraph${ARDA_GRAPH_VISIBILITY}Headers PROPERTIES FOLDER "Ardashir/Tests")
endforeach()

if(ARDASHIR_BUILD_TESTS)
    target_link_libraries(ArdaBackendTests PRIVATE Ardashir::ArdaDependencyGraph)
    file(GLOB_RECURSE ARDA_DEPENDENCY_GRAPH_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp"
    )
    add_executable(ArdaGraphTests ${ARDA_DEPENDENCY_GRAPH_TEST_SOURCES})
    # Documentation recipes are compiled and exercised by the native graph suite.
    target_sources(ArdaGraphTests PRIVATE
        "${PROJECT_SOURCE_DIR}/Docs/ArdaRDG/Examples/AssembleNodes.cpp"
        "${PROJECT_SOURCE_DIR}/Docs/ArdaRDG/Examples/Nodes/ComputeWriteNode.cpp"
        "${PROJECT_SOURCE_DIR}/Docs/ArdaRDG/Examples/Nodes/MeshTriangleNode.cpp"
        "${PROJECT_SOURCE_DIR}/Docs/ArdaRDG/Examples/Nodes/RasterTriangleNode.cpp"
        "${PROJECT_SOURCE_DIR}/Docs/ArdaRDG/Examples/Nodes/WorkWriteNode.cpp")
    target_compile_definitions(ArdaGraphTests PRIVATE
        ARDA_RDG_RECIPE_SHADER_DIR="${PROJECT_SOURCE_DIR}/Docs/ArdaRDG/Examples/Nodes/Shaders")
    ardashir_test_validation(ArdaGraphTests)
    target_include_directories(ArdaGraphTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
    target_compile_definitions(ArdaGraphTests PRIVATE
        ARDA_ENABLE_CHECKS
        ARDA_BACKEND_TEST_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/TestShaderCache"
        ARDA_BACKEND_TEST_SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/Source/ArdaInfra/ArdaBackend/Tests"
        ARDA_RDG_TEST_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests")
    target_link_libraries(ArdaGraphTests PRIVATE
        Ardashir::ArdaDependencyGraph
        Ardashir::ArdaBackend
        GTest::gtest_main
    )
    if(ARDASHIR_BACKEND_VULKAN)
        target_compile_definitions(ArdaGraphTests PRIVATE ARDA_TEST_NATIVE_VULKAN)
    endif()
    if(WIN32 AND ARDASHIR_BACKEND_D3D12)
        target_compile_definitions(ArdaGraphTests PRIVATE ARDA_TEST_NATIVE_D3D12)
        ardashir_deploy_d3d12_agility(ArdaGraphTests)
    endif()
    set_target_properties(ArdaGraphTests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaGraphTests)
endif()
