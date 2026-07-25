if(NOT ARDASHIR_DXC_EXECUTABLE)
    message(FATAL_ERROR "ARDASHIR_DXC_EXECUTABLE must be configured by the root project.")
endif()

set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Triangle.hlsl")
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

add_executable(NVRHITest
    Main.cpp
    Private/Backend.h
    Private/GlfwWindow.cpp
    Private/GlfwWindow.h
    Private/NVRHITestPch.h
    Private/TriangleRenderer.cpp
    Private/TriangleRenderer.h
    Private/VulkanBackend.cpp
    Private/VulkanBackend.h
)

if(WIN32)
    target_sources(NVRHITest PRIVATE
        Private/D3D12Backend.cpp
        Private/D3D12Backend.h
    )
endif()

add_dependencies(NVRHITest NVRHITestShaders)
target_compile_features(NVRHITest PRIVATE cxx_std_17)
target_compile_definitions(NVRHITest PRIVATE
    GLFW_INCLUDE_NONE
    $<$<BOOL:${WIN32}>:ARDASHIR_HAS_D3D12=1>
    $<$<NOT:$<BOOL:${WIN32}>>:ARDASHIR_HAS_D3D12=0>
)
target_include_directories(NVRHITest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
target_precompile_headers(NVRHITest PRIVATE Private/NVRHITestPch.h)
target_link_libraries(NVRHITest PRIVATE
    ${CMAKE_DL_LIBS}
    glfw
    nvrhi_vk
)

if(WIN32)
    target_compile_definitions(NVRHITest PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(NVRHITest PRIVATE
        nvrhi_d3d12
        d3d12
        dxgi
        dxguid
    )
endif()

# Keep the common NVRHI archive after its static backend archives for GNU ld.
target_link_libraries(NVRHITest PRIVATE nvrhi)

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
