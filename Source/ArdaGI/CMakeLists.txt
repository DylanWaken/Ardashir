add_library(ArdaGI STATIC
    Public/ArdaGI.h
    Private/ArdaGI.cpp
    Private/ArdaGIPch.h
)
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
    add_executable(ArdaGITests Tests/ArdaGITests.cpp)
    target_link_libraries(ArdaGITests PRIVATE Ardashir::ArdaGI GTest::gtest_main)
    set_target_properties(ArdaGITests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaGITests)
endif()
