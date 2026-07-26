if(NOT ARDASHIR_DXC_EXECUTABLE)
    message(FATAL_ERROR "ARDASHIR_DXC_EXECUTABLE must be configured by the root project.")
endif()

set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/ArdaTriangle.hlsl")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/Shaders")
set(SHADER_OUTPUTS
    "${SHADER_OUTPUT_DIR}/TriangleVS.dxil"
    "${SHADER_OUTPUT_DIR}/TrianglePS.dxil"
    "${SHADER_OUTPUT_DIR}/TriangleVS.spv"
    "${SHADER_OUTPUT_DIR}/TrianglePS.spv"
)

add_custom_command(
    OUTPUT ${SHADER_OUTPUTS}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUTPUT_DIR}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -T vs_6_0 -E VSMain
        -Fo "${SHADER_OUTPUT_DIR}/TriangleVS.dxil" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -T ps_6_0 -E PSMain
        -Fo "${SHADER_OUTPUT_DIR}/TrianglePS.dxil" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -spirv -fspv-target-env=vulkan1.3
        -T vs_6_0 -E VSMain -Fo "${SHADER_OUTPUT_DIR}/TriangleVS.spv" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -spirv -fspv-target-env=vulkan1.3
        -T ps_6_0 -E PSMain -Fo "${SHADER_OUTPUT_DIR}/TrianglePS.spv" "${SHADER_SOURCE}"
    DEPENDS "${SHADER_SOURCE}"
    COMMENT "Compiling NVRHITest shaders"
    VERBATIM
)

add_custom_target(NVRHITestShaders DEPENDS ${SHADER_OUTPUTS})
set_target_properties(NVRHITestShaders PROPERTIES FOLDER "Ardashir/Tests/NVRHI")

file(GLOB_RECURSE ARDA_NVRHI_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(NVRHITest ${ARDA_NVRHI_TEST_SOURCES})

add_dependencies(NVRHITest NVRHITestShaders)
target_compile_features(NVRHITest PRIVATE cxx_std_17)
target_compile_definitions(NVRHITest PRIVATE GLFW_INCLUDE_NONE)
target_include_directories(NVRHITest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
target_precompile_headers(NVRHITest PRIVATE Private/ArdaNVRHITestPch.h)
target_link_libraries(NVRHITest PRIVATE
    Ardashir::ArdaBackend
    Ardashir::ArdaRenderGraph
    glfw
)

if(WIN32)
    target_compile_definitions(NVRHITest PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(TARGET Vulkan::Headers)
    target_link_libraries(NVRHITest PRIVATE Vulkan::Headers)
elseif(TARGET Vulkan-Headers)
    target_link_libraries(NVRHITest PRIVATE Vulkan-Headers)
endif()

add_custom_command(TARGET NVRHITest POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        ${SHADER_OUTPUTS} "$<TARGET_FILE_DIR:NVRHITest>"
    VERBATIM
)

set_target_properties(NVRHITest PROPERTIES FOLDER "Ardashir/Tests/NVRHI")

if(ARDASHIR_BUILD_TESTS)
    set(NVRHI_GPU_TESTS NVRHITest.Vulkan)
    if(WIN32)
        add_test(
            NAME NVRHITest.D3D12
            COMMAND NVRHITest --backend d3d12 --frames 1 --hidden
        )
        list(APPEND NVRHI_GPU_TESTS NVRHITest.D3D12)
    endif()
    add_test(
        NAME NVRHITest.Vulkan
        COMMAND NVRHITest --backend vulkan --frames 1 --hidden
    )
    set_tests_properties(${NVRHI_GPU_TESTS} PROPERTIES
        LABELS "gpu"
        SKIP_RETURN_CODE 77
        TIMEOUT 30
    )
endif()
