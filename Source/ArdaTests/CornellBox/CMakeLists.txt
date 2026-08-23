file(GLOB_RECURSE CORNELL_BOX_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/ArdaCornellBoxConfig.cpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/ArdaCornellBoxConfig.cpp"
    @ONLY
)
list(APPEND CORNELL_BOX_SOURCES
    "${CMAKE_CURRENT_BINARY_DIR}/Generated/ArdaCornellBoxConfig.cpp")

add_executable(CornellBox ${CORNELL_BOX_SOURCES})
target_compile_features(CornellBox PRIVATE cxx_std_17)
target_compile_definitions(CornellBox PRIVATE GLFW_INCLUDE_NONE)
if(ARDASHIR_BACKEND_VULKAN)
    target_compile_definitions(CornellBox PRIVATE ARDA_TEST_WITH_VULKAN)
endif()
target_include_directories(CornellBox PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)
target_precompile_headers(CornellBox PRIVATE Private/ArdaCornellBoxPch.h)
target_link_libraries(CornellBox PRIVATE
    Ardashir::ArdaBackend
    Ardashir::ArdaRenderGraph
    glfw
)

if(WIN32)
    target_compile_definitions(CornellBox PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(TARGET Vulkan::Headers)
    target_link_libraries(CornellBox PRIVATE Vulkan::Headers)
elseif(TARGET Vulkan-Headers)
    target_link_libraries(CornellBox PRIVATE Vulkan-Headers)
endif()

set_target_properties(CornellBox PROPERTIES FOLDER "Ardashir/Tests/ARDG")

if(ARDASHIR_BUILD_TESTS)
    set(CORNELL_BOX_GPU_TESTS)
    if(WIN32 AND ARDASHIR_BACKEND_D3D12)
        add_test(
            NAME CornellBox.D3D12
            COMMAND CornellBox --backend d3d12 --frames 2 --hidden
                --width 320 --height 180 --samples-per-dispatch 1
                --max-samples 2 --max-bounces 3
        )
        list(APPEND CORNELL_BOX_GPU_TESTS CornellBox.D3D12)
    endif()
    if(ARDASHIR_BACKEND_VULKAN)
        add_test(
            NAME CornellBox.Vulkan
            COMMAND CornellBox --backend vulkan --frames 2 --hidden
                --width 320 --height 180 --samples-per-dispatch 1
                --max-samples 2 --max-bounces 3
        )
        list(APPEND CORNELL_BOX_GPU_TESTS CornellBox.Vulkan)
    endif()
    if(CORNELL_BOX_GPU_TESTS)
        set_tests_properties(${CORNELL_BOX_GPU_TESTS} PROPERTIES
            LABELS "gpu;raytracing;pathtracing"
            SKIP_RETURN_CODE 77
            TIMEOUT 60)
    endif()
endif()

if(WIN32 AND ARDASHIR_BACKEND_D3D12)
    ardashir_deploy_d3d12_agility(CornellBox)
endif()
