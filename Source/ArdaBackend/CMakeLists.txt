file(GLOB_RECURSE ARDA_BACKEND_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
)

if(NOT WIN32)
    list(FILTER ARDA_BACKEND_SOURCES EXCLUDE REGEX "ArdaD3D12BackendDevice\\.cpp$")
endif()

add_library(ArdaBackend STATIC ${ARDA_BACKEND_SOURCES})
add_library(Ardashir::ArdaBackend ALIAS ArdaBackend)

target_compile_features(ArdaBackend PUBLIC cxx_std_17)
target_compile_definitions(ArdaBackend PRIVATE
    $<$<BOOL:${WIN32}>:NOMINMAX>
    $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>
)
target_include_directories(ArdaBackend
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)
target_precompile_headers(ArdaBackend PRIVATE Private/ArdaBackendPch.h)

target_link_libraries(ArdaBackend PRIVATE
    ${CMAKE_DL_LIBS}
    nvrhi_vk
)
if(WIN32)
    target_link_libraries(ArdaBackend PRIVATE
        nvrhi_d3d12
        d3d12
        dxgi
        dxguid
    )
endif()

# Keep the common NVRHI archive after its static backend archives for GNU ld.
target_link_libraries(ArdaBackend PUBLIC nvrhi)

if(TARGET Vulkan::Headers)
    target_link_libraries(ArdaBackend PRIVATE Vulkan::Headers)
elseif(TARGET Vulkan-Headers)
    target_link_libraries(ArdaBackend PRIVATE Vulkan-Headers)
endif()

set_target_properties(ArdaBackend PROPERTIES FOLDER "Ardashir")

if(ARDASHIR_BUILD_TESTS)
    file(GLOB_RECURSE ARDA_BACKEND_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp"
    )
    add_executable(ArdaBackendTests ${ARDA_BACKEND_TEST_SOURCES})
    target_compile_definitions(ArdaBackendTests PRIVATE ARDA_ENABLE_CHECKS)
    target_link_libraries(ArdaBackendTests PRIVATE Ardashir::ArdaBackend GTest::gtest_main)
    set_target_properties(ArdaBackendTests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaBackendTests)
endif()
