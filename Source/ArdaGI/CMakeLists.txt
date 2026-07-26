file(GLOB_RECURSE ARDA_GI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
)

add_library(ArdaGI STATIC ${ARDA_GI_SOURCES})
add_library(Ardashir::ArdaGI ALIAS ArdaGI)

target_compile_features(ArdaGI PUBLIC cxx_std_17)
target_include_directories(ArdaGI
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)
target_link_libraries(ArdaGI PUBLIC nvrhi)
target_precompile_headers(ArdaGI PRIVATE Private/ArdaGIPch.h)

set_target_properties(ArdaGI PROPERTIES FOLDER "Ardashir")

if(ARDASHIR_BUILD_TESTS)
    file(GLOB_RECURSE ARDA_GI_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp"
    )
    add_executable(ArdaGITests ${ARDA_GI_TEST_SOURCES})
    target_link_libraries(ArdaGITests PRIVATE Ardashir::ArdaGI GTest::gtest_main)
    set_target_properties(ArdaGITests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaGITests)
endif()
