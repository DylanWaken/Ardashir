file(GLOB_RECURSE ARDA_BACKEND_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
)

if(NOT WIN32)
    list(FILTER ARDA_BACKEND_SOURCES EXCLUDE REGEX "ArdaD3D12BackendDevice\\.cpp$")
endif()
list(FILTER ARDA_BACKEND_SOURCES EXCLUDE REGEX
    "Arda(D3D12|Vulkan)BackendDevice\\.cpp$")
list(FILTER ARDA_BACKEND_SOURCES EXCLUDE REGEX
    "ArdaExternalInterop\\.cpp$")
list(FILTER ARDA_BACKEND_SOURCES EXCLUDE REGEX
    "RHIWrappers/ArdaNvrhi(Conversions|Device)\\.cpp$")

add_library(ArdaBackend STATIC ${ARDA_BACKEND_SOURCES})
add_library(Ardashir::ArdaBackend ALIAS ArdaBackend)

target_compile_features(ArdaBackend PUBLIC cxx_std_17)
target_compile_definitions(ArdaBackend PRIVATE
    $<$<BOOL:${WIN32}>:NOMINMAX>
    $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>
)
if(ARDASHIR_DXC_EXECUTABLE)
    string(REPLACE "\\" "\\\\" ARDA_DEFAULT_DXC_ESCAPED
        "${ARDASHIR_DXC_EXECUTABLE}")
    string(REPLACE "\"" "\\\"" ARDA_DEFAULT_DXC_ESCAPED
        "${ARDA_DEFAULT_DXC_ESCAPED}")
    target_compile_definitions(ArdaBackend PRIVATE
        "ARDA_DEFAULT_DXC_EXECUTABLE=\"${ARDA_DEFAULT_DXC_ESCAPED}\"")
endif()
target_include_directories(ArdaBackend
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)
target_precompile_headers(ArdaBackend PRIVATE Private/ArdaBackendCorePch.h)

target_link_libraries(ArdaBackend PRIVATE ${CMAKE_DL_LIBS})
set(ARDA_LINKED_BACKEND_TARGETS ${ARDASHIR_BACKEND_MODULE_TARGETS})
set(ARDA_LINKED_BACKEND_FUNCTIONS
    ${ARDASHIR_BACKEND_REGISTRATION_FUNCTIONS})
if(ARDASHIR_BACKEND_NVRHI_VULKAN)
    list(APPEND ARDA_LINKED_BACKEND_TARGETS
        Ardashir::ArdaBackendNvrhiVulkan)
    list(APPEND ARDA_LINKED_BACKEND_FUNCTIONS
        RegisterArdaNvrhiVulkanBackend)
endif()
if(WIN32 AND ARDASHIR_BACKEND_NVRHI_D3D12)
    list(APPEND ARDA_LINKED_BACKEND_TARGETS
        Ardashir::ArdaBackendNvrhiD3D12)
    list(APPEND ARDA_LINKED_BACKEND_FUNCTIONS
        RegisterArdaNvrhiD3D12Backend)
endif()
foreach(ARDA_LINKED_BACKEND_TARGET IN LISTS ARDA_LINKED_BACKEND_TARGETS)
    if(NOT TARGET ${ARDA_LINKED_BACKEND_TARGET})
        message(FATAL_ERROR
            "Arda backend target does not exist: ${ARDA_LINKED_BACKEND_TARGET}")
    endif()
    target_link_libraries(ArdaBackend PRIVATE ${ARDA_LINKED_BACKEND_TARGET})
endforeach()

set(ARDA_LINKED_BACKEND_REGISTRATIONS_CONTENT "")
foreach(ARDA_LINKED_BACKEND_FUNCTION IN LISTS ARDA_LINKED_BACKEND_FUNCTIONS)
    if(NOT ARDA_LINKED_BACKEND_FUNCTION MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR
            "Invalid arda::backend registration function: ${ARDA_LINKED_BACKEND_FUNCTION}")
    endif()
    string(APPEND ARDA_LINKED_BACKEND_REGISTRATIONS_CONTENT
        "ARDA_LINKED_BACKEND(${ARDA_LINKED_BACKEND_FUNCTION})\n")
endforeach()
file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ArdaLinkedBackendRegistrations.inl"
    CONTENT "${ARDA_LINKED_BACKEND_REGISTRATIONS_CONTENT}")
target_include_directories(ArdaBackend PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(ArdaBackend
    PUBLIC Ardashir::EASTL
)

set_target_properties(ArdaBackend PROPERTIES FOLDER "Ardashir")

# Compile every public header in isolation without the backend PCH or NVRHI
# include paths. This guards the opaque public/private dependency boundary.
file(GLOB_RECURSE ARDA_BACKEND_PUBLIC_HEADERS CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h")
set(ARDA_BACKEND_HEADER_TARGETS)
foreach(ARDA_PUBLIC_HEADER IN LISTS ARDA_BACKEND_PUBLIC_HEADERS)
    string(MAKE_C_IDENTIFIER "${ARDA_PUBLIC_HEADER}" ARDA_HEADER_ID)
    set(ARDA_HEADER_SOURCE
        "${CMAKE_CURRENT_BINARY_DIR}/PublicHeader_${ARDA_HEADER_ID}.cpp")
    file(GENERATE OUTPUT "${ARDA_HEADER_SOURCE}"
        CONTENT "#include \"${ARDA_PUBLIC_HEADER}\"\n")
    set(ARDA_HEADER_TARGET "ArdaBackendHeader_${ARDA_HEADER_ID}")
    add_library(${ARDA_HEADER_TARGET} OBJECT "${ARDA_HEADER_SOURCE}")
    target_compile_features(${ARDA_HEADER_TARGET} PRIVATE cxx_std_17)
    target_include_directories(${ARDA_HEADER_TARGET} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Public")
    target_link_libraries(${ARDA_HEADER_TARGET} PRIVATE Ardashir::EASTL)
    set_target_properties(${ARDA_HEADER_TARGET} PROPERTIES
        FOLDER "Ardashir/Tests/PublicHeaders")
    list(APPEND ARDA_BACKEND_HEADER_TARGETS ${ARDA_HEADER_TARGET})
endforeach()
add_custom_target(ArdaBackendPublicHeaders ALL
    DEPENDS ${ARDA_BACKEND_HEADER_TARGETS})
set_target_properties(ArdaBackendPublicHeaders PROPERTIES
    FOLDER "Ardashir/Tests")

if(ARDASHIR_BUILD_TESTS)
    set(ARDA_BACKEND_RT_SHADER_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/ArdaRayTracingTest.hlsl")
    set(ARDA_BACKEND_RT_SHADER_OUTPUTS
        "${CMAKE_CURRENT_BINARY_DIR}/ArdaRayTracingTest.dxil"
        "${CMAKE_CURRENT_BINARY_DIR}/ArdaRayTracingTest.spv")
    set(ARDA_BACKEND_STRUCT_SHADER_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/ArdaShaderStructTest.hlsl")
    set(ARDA_BACKEND_STRUCT_SHADER_OUTPUTS
        "${CMAKE_CURRENT_BINARY_DIR}/ArdaShaderStructTest.dxil"
        "${CMAKE_CURRENT_BINARY_DIR}/ArdaShaderStructTest.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/ArdaPipelineStateTestVS.dxil"
        "${CMAKE_CURRENT_BINARY_DIR}/ArdaPipelineStateTestVS.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/ArdaPipelineStateTestPS.dxil"
        "${CMAKE_CURRENT_BINARY_DIR}/ArdaPipelineStateTestPS.spv")
    add_custom_command(
        OUTPUT ${ARDA_BACKEND_RT_SHADER_OUTPUTS}
        COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -T lib_6_3
            -Fo "${CMAKE_CURRENT_BINARY_DIR}/ArdaRayTracingTest.dxil"
            "${ARDA_BACKEND_RT_SHADER_SOURCE}"
        COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -spirv
            -fspv-target-env=vulkan1.2
            -fspv-extension=SPV_KHR_ray_tracing
            -T lib_6_3
            -Fo "${CMAKE_CURRENT_BINARY_DIR}/ArdaRayTracingTest.spv"
            "${ARDA_BACKEND_RT_SHADER_SOURCE}"
        DEPENDS "${ARDA_BACKEND_RT_SHADER_SOURCE}"
        COMMENT "Compiling ArdaBackend ray-tracing test shaders"
        VERBATIM)
    add_custom_command(
        OUTPUT ${ARDA_BACKEND_STRUCT_SHADER_OUTPUTS}
        COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -T cs_6_0
            -E ShaderStructTestCS
            -Fo "${CMAKE_CURRENT_BINARY_DIR}/ArdaShaderStructTest.dxil"
            "${ARDA_BACKEND_STRUCT_SHADER_SOURCE}"
        COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -spirv
            -fspv-target-env=vulkan1.2
            -T cs_6_0
            -E ShaderStructTestCS
            -Fo "${CMAKE_CURRENT_BINARY_DIR}/ArdaShaderStructTest.spv"
            "${ARDA_BACKEND_STRUCT_SHADER_SOURCE}"
        COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -T vs_6_0
            -E PipelineStateTestVS
            -Fo "${CMAKE_CURRENT_BINARY_DIR}/ArdaPipelineStateTestVS.dxil"
            "${ARDA_BACKEND_STRUCT_SHADER_SOURCE}"
        COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -spirv
            -fspv-target-env=vulkan1.2 -T vs_6_0
            -E PipelineStateTestVS
            -Fo "${CMAKE_CURRENT_BINARY_DIR}/ArdaPipelineStateTestVS.spv"
            "${ARDA_BACKEND_STRUCT_SHADER_SOURCE}"
        COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -T ps_6_0
            -E PipelineStateTestPS
            -Fo "${CMAKE_CURRENT_BINARY_DIR}/ArdaPipelineStateTestPS.dxil"
            "${ARDA_BACKEND_STRUCT_SHADER_SOURCE}"
        COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -nologo -spirv
            -fspv-target-env=vulkan1.2 -T ps_6_0
            -E PipelineStateTestPS
            -Fo "${CMAKE_CURRENT_BINARY_DIR}/ArdaPipelineStateTestPS.spv"
            "${ARDA_BACKEND_STRUCT_SHADER_SOURCE}"
        DEPENDS "${ARDA_BACKEND_STRUCT_SHADER_SOURCE}"
        COMMENT "Compiling shader-struct test shaders"
        VERBATIM)
    add_custom_target(ArdaBackendTestShaders
        DEPENDS ${ARDA_BACKEND_RT_SHADER_OUTPUTS}
                ${ARDA_BACKEND_STRUCT_SHADER_OUTPUTS})
    set_target_properties(ArdaBackendTestShaders PROPERTIES
        FOLDER "Ardashir/Tests")

    file(GLOB_RECURSE ARDA_BACKEND_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp"
    )
    list(FILTER ARDA_BACKEND_TEST_SOURCES EXCLUDE REGEX "ArdaRHITests\\.cpp$")
    add_executable(ArdaBackendTests ${ARDA_BACKEND_TEST_SOURCES})
    add_dependencies(ArdaBackendTests ArdaBackendTestShaders)
    target_compile_definitions(ArdaBackendTests PRIVATE
        ARDA_ENABLE_CHECKS
        ARDA_BACKEND_TEST_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}")
    target_include_directories(ArdaBackendTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Private")
    target_link_libraries(ArdaBackendTests PRIVATE Ardashir::ArdaBackend GTest::gtest_main)
    add_custom_command(TARGET ArdaBackendTests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${ARDA_BACKEND_RT_SHADER_OUTPUTS}
            ${ARDA_BACKEND_STRUCT_SHADER_OUTPUTS}
            "$<TARGET_FILE_DIR:ArdaBackendTests>"
        VERBATIM)
    set_target_properties(ArdaBackendTests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaBackendTests)

    add_executable(ArdaRHITests Tests/ArdaRHITests.cpp)
    target_compile_definitions(ArdaRHITests PRIVATE ARDA_ENABLE_CHECKS)
    target_link_libraries(ArdaRHITests PRIVATE Ardashir::ArdaBackend GTest::gtest_main)
    set_target_properties(ArdaRHITests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaRHITests)
endif()
