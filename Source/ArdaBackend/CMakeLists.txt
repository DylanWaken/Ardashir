file(GLOB_RECURSE ARDA_BACKEND_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
)

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
foreach(ARDA_LINKED_BACKEND_TARGET IN LISTS ARDA_LINKED_BACKEND_TARGETS)
    if(NOT TARGET ${ARDA_LINKED_BACKEND_TARGET})
        message(FATAL_ERROR
            "Arda backend target does not exist: ${ARDA_LINKED_BACKEND_TARGET}")
    endif()
    target_link_libraries(ArdaBackend INTERFACE
        "$<LINK_ONLY:${ARDA_LINKED_BACKEND_TARGET}>")
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

# Compile every public header in isolation without a provider implementation's
# private or public include paths. This guards the module boundary.
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
    file(GLOB_RECURSE ARDA_BACKEND_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp"
    )
    list(FILTER ARDA_BACKEND_TEST_SOURCES EXCLUDE REGEX "ArdaRHITests\\.cpp$")
    add_executable(ArdaBackendTests ${ARDA_BACKEND_TEST_SOURCES})
    target_compile_definitions(ArdaBackendTests PRIVATE
        ARDA_ENABLE_CHECKS
        ARDA_BACKEND_TEST_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/TestShaderCache"
        ARDA_BACKEND_TEST_SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests")
    if(ARDASHIR_BACKEND_VULKAN)
        target_compile_definitions(ArdaBackendTests PRIVATE
            ARDA_TEST_NATIVE_VULKAN)
    endif()
    if(WIN32 AND ARDASHIR_BACKEND_D3D12)
        target_compile_definitions(ArdaBackendTests PRIVATE
            ARDA_TEST_NATIVE_D3D12)
    endif()
    target_include_directories(ArdaBackendTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Private")
    target_link_libraries(ArdaBackendTests PRIVATE Ardashir::ArdaBackend GTest::gtest)
    if(WIN32 AND ARDASHIR_BACKEND_D3D12)
        target_include_directories(ArdaBackendTests PRIVATE
            "${ARDASHIR_D3D12_AGILITY_INCLUDE_DIR}")
        ardashir_deploy_d3d12_agility(ArdaBackendTests)
    endif()
    set_target_properties(ArdaBackendTests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaBackendTests)

    add_executable(ArdaRHITests Tests/ArdaRHITests.cpp)
    target_compile_definitions(ArdaRHITests PRIVATE ARDA_ENABLE_CHECKS)
    target_link_libraries(ArdaRHITests PRIVATE Ardashir::ArdaBackend GTest::gtest_main)
    if(WIN32 AND ARDASHIR_BACKEND_D3D12)
        ardashir_deploy_d3d12_agility(ArdaRHITests)
    endif()
    set_target_properties(ArdaRHITests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaRHITests)
endif()
