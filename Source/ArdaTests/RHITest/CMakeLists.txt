file(GLOB_RECURSE ARDA_RHI_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(RHITest ${ARDA_RHI_TEST_SOURCES})

target_compile_features(RHITest PRIVATE cxx_std_17)
target_compile_definitions(RHITest PRIVATE
    GLFW_INCLUDE_NONE
    ARDA_RHI_TEST_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Shaders")
if(ARDASHIR_BACKEND_VULKAN)
    target_compile_definitions(RHITest PRIVATE ARDA_TEST_WITH_VULKAN)
endif()
target_include_directories(RHITest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
target_precompile_headers(RHITest PRIVATE Private/ArdaRHITestPch.h)
target_link_libraries(RHITest PRIVATE
    Ardashir::ArdaBackend
    Ardashir::ArdaRenderGraph
    glfw
)

if(WIN32)
    target_compile_definitions(RHITest PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(TARGET Vulkan::Headers)
    target_link_libraries(RHITest PRIVATE Vulkan::Headers)
elseif(TARGET Vulkan-Headers)
    target_link_libraries(RHITest PRIVATE Vulkan-Headers)
endif()

set_target_properties(RHITest PROPERTIES FOLDER "Ardashir/Tests/RHI")

if(ARDASHIR_BUILD_TESTS)
    set(RHI_GPU_TESTS)
    if(WIN32 AND ARDASHIR_BACKEND_D3D12)
        add_test(
            NAME RHITest.D3D12
            COMMAND RHITest --backend d3d12 --frames 1 --hidden
        )
        list(APPEND RHI_GPU_TESTS RHITest.D3D12)
    endif()
    if(ARDASHIR_BACKEND_VULKAN)
        add_test(
            NAME RHITest.Vulkan
            COMMAND RHITest --backend vulkan --frames 1 --hidden)
        list(APPEND RHI_GPU_TESTS RHITest.Vulkan)
    endif()
    if(RHI_GPU_TESTS)
        set_tests_properties(${RHI_GPU_TESTS} PROPERTIES
            LABELS "gpu"
            SKIP_RETURN_CODE 77
            TIMEOUT 30)
    endif()
endif()

if(WIN32 AND ARDASHIR_BACKEND_D3D12)
    ardashir_deploy_d3d12_agility(RHITest)
endif()
