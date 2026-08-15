if(NOT ARDASHIR_DXC_EXECUTABLE)
    message(FATAL_ERROR "ARDASHIR_DXC_EXECUTABLE must be configured by the root project.")
endif()

set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/ArdaTerrain.hlsl")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/Shaders")
set(VULKAN_DXC_FLAGS
    -spirv
    -fspv-target-env=vulkan1.3
    -fvk-t-shift 0 0
    -fvk-s-shift 128 0
    -fvk-b-shift 256 0
    -fvk-u-shift 384 0
)
set(SHADER_OUTPUTS
    "${SHADER_OUTPUT_DIR}/TerrainGenerateCS.dxil"
    "${SHADER_OUTPUT_DIR}/TerrainErodeCS.dxil"
    "${SHADER_OUTPUT_DIR}/TerrainTriangulateCS.dxil"
    "${SHADER_OUTPUT_DIR}/TerrainVS.dxil"
    "${SHADER_OUTPUT_DIR}/TerrainPS.dxil"
    "${SHADER_OUTPUT_DIR}/TerrainOverlayVS.dxil"
    "${SHADER_OUTPUT_DIR}/TerrainOverlayPS.dxil"
    "${SHADER_OUTPUT_DIR}/TerrainGenerateCS.spv"
    "${SHADER_OUTPUT_DIR}/TerrainErodeCS.spv"
    "${SHADER_OUTPUT_DIR}/TerrainTriangulateCS.spv"
    "${SHADER_OUTPUT_DIR}/TerrainVS.spv"
    "${SHADER_OUTPUT_DIR}/TerrainPS.spv"
    "${SHADER_OUTPUT_DIR}/TerrainOverlayVS.spv"
    "${SHADER_OUTPUT_DIR}/TerrainOverlayPS.spv"
)

add_custom_command(
    OUTPUT ${SHADER_OUTPUTS}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_OUTPUT_DIR}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -T cs_6_0
        -E GenerateNoiseHeightmapCS -Fo "${SHADER_OUTPUT_DIR}/TerrainGenerateCS.dxil" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -T cs_6_0
        -E ErodeHeightmapCS -Fo "${SHADER_OUTPUT_DIR}/TerrainErodeCS.dxil" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -T cs_6_0
        -E TriangulateTerrainCS -Fo "${SHADER_OUTPUT_DIR}/TerrainTriangulateCS.dxil" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -T vs_6_0
        -E TerrainVS -Fo "${SHADER_OUTPUT_DIR}/TerrainVS.dxil" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -T ps_6_0
        -E TerrainPS -Fo "${SHADER_OUTPUT_DIR}/TerrainPS.dxil" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -T vs_6_0
        -E TerrainOverlayVS -Fo "${SHADER_OUTPUT_DIR}/TerrainOverlayVS.dxil" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -T ps_6_0
        -E TerrainOverlayPS -Fo "${SHADER_OUTPUT_DIR}/TerrainOverlayPS.dxil" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo ${VULKAN_DXC_FLAGS}
        -T cs_6_0 -E GenerateNoiseHeightmapCS -Fo "${SHADER_OUTPUT_DIR}/TerrainGenerateCS.spv" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo ${VULKAN_DXC_FLAGS}
        -T cs_6_0 -E ErodeHeightmapCS -Fo "${SHADER_OUTPUT_DIR}/TerrainErodeCS.spv" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo ${VULKAN_DXC_FLAGS}
        -T cs_6_0 -E TriangulateTerrainCS -Fo "${SHADER_OUTPUT_DIR}/TerrainTriangulateCS.spv" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo ${VULKAN_DXC_FLAGS}
        -T vs_6_0 -E TerrainVS -Fo "${SHADER_OUTPUT_DIR}/TerrainVS.spv" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo ${VULKAN_DXC_FLAGS}
        -T ps_6_0 -E TerrainPS -Fo "${SHADER_OUTPUT_DIR}/TerrainPS.spv" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo ${VULKAN_DXC_FLAGS}
        -T vs_6_0 -E TerrainOverlayVS -Fo "${SHADER_OUTPUT_DIR}/TerrainOverlayVS.spv" "${SHADER_SOURCE}"
    COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo ${VULKAN_DXC_FLAGS}
        -T ps_6_0 -E TerrainOverlayPS -Fo "${SHADER_OUTPUT_DIR}/TerrainOverlayPS.spv" "${SHADER_SOURCE}"
    DEPENDS "${SHADER_SOURCE}"
    COMMENT "Compiling ARDG terrain example shaders"
    VERBATIM
)

add_custom_target(ARDGExampleShaders DEPENDS ${SHADER_OUTPUTS})
set_target_properties(ARDGExampleShaders PROPERTIES FOLDER "Ardashir/Tests/ARDG")

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
add_dependencies(ARDGExample ARDGExampleShaders)
target_compile_features(ARDGExample PRIVATE cxx_std_17)
target_compile_definitions(ARDGExample PRIVATE
    GLFW_INCLUDE_NONE
)
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

add_custom_command(TARGET ARDGExample POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        ${SHADER_OUTPUTS} "$<TARGET_FILE_DIR:ARDGExample>"
    VERBATIM
)

set_target_properties(ARDGExample PROPERTIES FOLDER "Ardashir/Tests/ARDG")

if(ARDASHIR_BUILD_TESTS)
    set(ARDG_EXAMPLE_GPU_TESTS ARDGExample.Vulkan)
    if(WIN32)
        add_test(
            NAME ARDGExample.D3D12
            COMMAND ARDGExample --backend d3d12 --frames 1 --hidden
        )
        list(APPEND ARDG_EXAMPLE_GPU_TESTS ARDGExample.D3D12)
    endif()
    add_test(
        NAME ARDGExample.Vulkan
        COMMAND ARDGExample --backend vulkan --frames 1 --hidden
    )
    set_tests_properties(${ARDG_EXAMPLE_GPU_TESTS} PROPERTIES
        LABELS "gpu"
        SKIP_RETURN_CODE 77
        TIMEOUT 30
    )
endif()
