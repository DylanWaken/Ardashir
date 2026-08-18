if(NOT ARDASHIR_DXC_EXECUTABLE)
    message(FATAL_ERROR "ARDASHIR_DXC_EXECUTABLE must be configured by the root project.")
endif()

set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/ArdaTriangle.hlsl")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/Shaders")
set(SHADER_OUTPUTS)
if(WIN32 AND ARDASHIR_BACKEND_NVRHI_D3D12)
    set(D3D12_SHADER_OUTPUTS
        "${SHADER_OUTPUT_DIR}/TriangleVS.dxil"
        "${SHADER_OUTPUT_DIR}/TrianglePS.dxil")
    add_custom_command(
        OUTPUT ${D3D12_SHADER_OUTPUTS}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUTPUT_DIR}"
        COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -T vs_6_0 -E VSMain
            -Fo "${SHADER_OUTPUT_DIR}/TriangleVS.dxil" "${SHADER_SOURCE}"
        COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -T ps_6_0 -E PSMain
            -Fo "${SHADER_OUTPUT_DIR}/TrianglePS.dxil" "${SHADER_SOURCE}"
        DEPENDS "${SHADER_SOURCE}"
        COMMENT "Compiling RHITest D3D12 shaders"
        VERBATIM)
    list(APPEND SHADER_OUTPUTS ${D3D12_SHADER_OUTPUTS})
endif()
if(ARDASHIR_BACKEND_NVRHI_VULKAN)
    set(VULKAN_SHADER_OUTPUTS
        "${SHADER_OUTPUT_DIR}/TriangleVS.spv"
        "${SHADER_OUTPUT_DIR}/TrianglePS.spv")
    add_custom_command(
        OUTPUT ${VULKAN_SHADER_OUTPUTS}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUTPUT_DIR}"
        COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -spirv -fspv-target-env=vulkan1.3
            -T vs_6_0 -E VSMain -Fo "${SHADER_OUTPUT_DIR}/TriangleVS.spv" "${SHADER_SOURCE}"
        COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -spirv -fspv-target-env=vulkan1.3
            -T ps_6_0 -E PSMain -Fo "${SHADER_OUTPUT_DIR}/TrianglePS.spv" "${SHADER_SOURCE}"
        DEPENDS "${SHADER_SOURCE}"
        COMMENT "Compiling RHITest Vulkan shaders"
        VERBATIM)
    list(APPEND SHADER_OUTPUTS ${VULKAN_SHADER_OUTPUTS})
endif()

add_custom_target(RHITestShaders DEPENDS ${SHADER_OUTPUTS})
set_target_properties(RHITestShaders PROPERTIES FOLDER "Ardashir/Tests/RHI")

file(GLOB_RECURSE ARDA_RHI_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(RHITest ${ARDA_RHI_TEST_SOURCES})

add_dependencies(RHITest RHITestShaders)
target_compile_features(RHITest PRIVATE cxx_std_17)
target_compile_definitions(RHITest PRIVATE GLFW_INCLUDE_NONE)
if(ARDASHIR_BACKEND_NVRHI_VULKAN)
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

if(SHADER_OUTPUTS)
    add_custom_command(TARGET RHITest POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${SHADER_OUTPUTS} "$<TARGET_FILE_DIR:RHITest>"
        VERBATIM)
endif()

set_target_properties(RHITest PROPERTIES FOLDER "Ardashir/Tests/RHI")

if(ARDASHIR_BUILD_TESTS)
    set(RHI_GPU_TESTS)
    if(WIN32 AND ARDASHIR_BACKEND_NVRHI_D3D12)
        add_test(
            NAME RHITest.D3D12
            COMMAND RHITest --backend d3d12 --frames 1 --hidden
        )
        list(APPEND RHI_GPU_TESTS RHITest.D3D12)
    endif()
    if(ARDASHIR_BACKEND_NVRHI_VULKAN)
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
