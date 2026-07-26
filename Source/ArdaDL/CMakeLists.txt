file(GLOB_RECURSE ARDA_DL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
)

add_library(ArdaDL STATIC ${ARDA_DL_SOURCES})
add_library(Ardashir::ArdaDL ALIAS ArdaDL)

target_compile_features(ArdaDL PUBLIC cxx_std_17)
target_include_directories(ArdaDL
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)
target_link_libraries(ArdaDL PUBLIC nvrhi)
target_precompile_headers(ArdaDL PRIVATE Private/ArdaDLPch.h)

set_target_properties(ArdaDL PROPERTIES FOLDER "Ardashir")

if(ARDASHIR_BUILD_TESTS)
    file(GLOB_RECURSE ARDA_DL_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp"
    )
    add_executable(ArdaDLTests ${ARDA_DL_TEST_SOURCES})
    target_link_libraries(ArdaDLTests PRIVATE Ardashir::ArdaDL GTest::gtest_main)
    set_target_properties(ArdaDLTests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaDLTests)
endif()
