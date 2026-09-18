file(GLOB_RECURSE ARDG_EXAMPLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(FILTER ARDG_EXAMPLE_SOURCES EXCLUDE REGEX "/Tests/")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/ArdaARDGExampleConfig.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/ArdaARDGExampleConfig.cpp"
    @ONLY
)
list(APPEND ARDG_EXAMPLE_SOURCES
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/ArdaARDGExampleConfig.cpp")

add_executable(ARDGExample ${ARDG_EXAMPLE_SOURCES})
target_compile_features(ARDGExample PRIVATE cxx_std_17)
target_compile_definitions(ARDGExample PRIVATE
    GLFW_INCLUDE_NONE
)
if(ARDASHIR_BACKEND_VULKAN)
    target_compile_definitions(ARDGExample PRIVATE ARDA_TEST_WITH_VULKAN)
endif()
target_include_directories(ARDGExample PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Private"
    "${CMAKE_CURRENT_SOURCE_DIR}/Public"
)
target_precompile_headers(ARDGExample PRIVATE Private/ArdaARDGExamplePch.h)
target_link_libraries(ARDGExample PRIVATE
    ArdaExampleNodes
    Ardashir::ArdaBackend
    Ardashir::ArdaRenderGraph
    glfw
)

if(WIN32)
    target_compile_definitions(ARDGExample PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(TARGET Vulkan::Headers)
    target_link_libraries(ARDGExample PRIVATE Vulkan::Headers)
elseif(TARGET Vulkan-Headers)
    target_link_libraries(ARDGExample PRIVATE Vulkan-Headers)
endif()

set_target_properties(ARDGExample PROPERTIES FOLDER "Ardashir/Tests/Examples/ARDGExample")

ardashir_test_validation(ARDGExample OPTIONAL)

if(ARDASHIR_BUILD_TESTS)
    add_executable(ARDGExampleValidationTests
        Tests/ArdaTerrainValidationTests.cpp
        Private/ArdaTerrainValidation.cpp)
    target_compile_features(ARDGExampleValidationTests PRIVATE cxx_std_17)
    target_include_directories(ARDGExampleValidationTests PRIVATE Private Public)
    target_link_libraries(ARDGExampleValidationTests PRIVATE
        Ardashir::ArdaRenderGraph GTest::gtest_main)
    set_target_properties(ARDGExampleValidationTests PROPERTIES
        FOLDER "Ardashir/Tests/Examples/ARDGExample")
    gtest_discover_tests(ARDGExampleValidationTests)

    set(ARDG_EXAMPLE_GPU_TESTS)
    if(WIN32 AND ARDASHIR_BACKEND_D3D12)
        add_test(
            NAME ARDGExample.D3D12
            COMMAND ARDGExample --backend d3d12 --frames 3 --hidden
        )
        add_test(
            NAME ARDGExample.D3D12.MultiFrameLifetime
            COMMAND ARDGExample --backend d3d12 --frames 120 --hidden
        )
        add_test(NAME ARDGExample.D3D12.Verify
            COMMAND ARDGExample --backend d3d12 --frames 3 --hidden --verify)
        list(APPEND ARDG_EXAMPLE_GPU_TESTS
            ARDGExample.D3D12
            ARDGExample.D3D12.MultiFrameLifetime
            ARDGExample.D3D12.Verify)
        ardashir_example_validation_test(ARDGExample D3D12 --frames 3 --hidden)
    endif()
    if(ARDASHIR_BACKEND_VULKAN)
        add_test(
            NAME ARDGExample.Vulkan
            COMMAND ARDGExample --backend vulkan --frames 3 --hidden)
        list(APPEND ARDG_EXAMPLE_GPU_TESTS ARDGExample.Vulkan)
        add_test(NAME ARDGExample.Vulkan.Verify
            COMMAND ARDGExample --backend vulkan --frames 3 --hidden --verify)
        list(APPEND ARDG_EXAMPLE_GPU_TESTS ARDGExample.Vulkan.Verify)
        ardashir_example_without_validation_tests(ARDGExample.Vulkan ARDGExample.Vulkan.Verify)
        ardashir_example_validation_test(ARDGExample Vulkan --frames 3 --hidden)
    endif()
    if(ARDG_EXAMPLE_GPU_TESTS)
        set_tests_properties(${ARDG_EXAMPLE_GPU_TESTS} PROPERTIES
            LABELS "gpu"
            SKIP_RETURN_CODE 77
            TIMEOUT 30)
    endif()
endif()

if(WIN32 AND ARDASHIR_BACKEND_D3D12)
    ardashir_deploy_d3d12_agility(ARDGExample)
endif()
