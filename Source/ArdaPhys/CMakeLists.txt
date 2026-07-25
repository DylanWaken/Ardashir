add_library(ArdaPhys STATIC
    Public/ArdaPhys.h
    Private/ArdaPhys.cpp
    Private/ArdaPhysPch.h
)
add_library(Ardashir::ArdaPhys ALIAS ArdaPhys)

target_compile_features(ArdaPhys PUBLIC cxx_std_17)
target_include_directories(ArdaPhys
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)
target_link_libraries(ArdaPhys PUBLIC nvrhi)
target_precompile_headers(ArdaPhys PRIVATE Private/ArdaPhysPch.h)

set_target_properties(ArdaPhys PROPERTIES FOLDER "Ardashir")

if(ARDASHIR_BUILD_TESTS)
    add_executable(ArdaPhysTests Tests/ArdaPhysTests.cpp)
    target_link_libraries(ArdaPhysTests PRIVATE Ardashir::ArdaPhys GTest::gtest_main)
    set_target_properties(ArdaPhysTests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaPhysTests)
endif()
