add_library(ArdaDL STATIC
    Public/ArdaDL.h
    Private/ArdaDL.cpp
    Private/ArdaDLPch.h
)
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
    add_executable(ArdaDLTests Tests/ArdaDLTests.cpp)
    target_link_libraries(ArdaDLTests PRIVATE Ardashir::ArdaDL GTest::gtest_main)
    set_target_properties(ArdaDLTests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaDLTests)
endif()
