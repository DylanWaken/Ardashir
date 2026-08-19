file(GLOB_RECURSE ARDG_EXAMPLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
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
if(ARDASHIR_BACKEND_NVRHI_VULKAN)
    target_compile_definitions(ARDGExample PRIVATE ARDA_TEST_WITH_VULKAN)
endif()
target_include_directories(ARDGExample PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)
target_precompile_headers(ARDGExample PRIVATE Private/ArdaARDGExamplePch.h)
target_link_libraries(ARDGExample PRIVATE
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

set_target_properties(ARDGExample PROPERTIES FOLDER "Ardashir/Tests/ARDG")

if(ARDASHIR_BUILD_TESTS)
    set(ARDG_EXAMPLE_GPU_TESTS)
    if(WIN32 AND ARDASHIR_BACKEND_NVRHI_D3D12)
        add_test(
            NAME ARDGExample.D3D12
            COMMAND ARDGExample --backend d3d12 --frames 1 --hidden
        )
        list(APPEND ARDG_EXAMPLE_GPU_TESTS ARDGExample.D3D12)
    endif()
    if(ARDASHIR_BACKEND_NVRHI_VULKAN)
        add_test(
            NAME ARDGExample.Vulkan
            COMMAND ARDGExample --backend vulkan --frames 1 --hidden)
        list(APPEND ARDG_EXAMPLE_GPU_TESTS ARDGExample.Vulkan)
    endif()
    if(ARDG_EXAMPLE_GPU_TESTS)
        set_tests_properties(${ARDG_EXAMPLE_GPU_TESTS} PROPERTIES
            LABELS "gpu"
            SKIP_RETURN_CODE 77
            TIMEOUT 30)
    endif()
endif()
