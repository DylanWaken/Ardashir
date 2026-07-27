file(GLOB_RECURSE ARDA_PHYS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
)

add_library(ArdaPhys STATIC ${ARDA_PHYS_SOURCES})
add_library(Ardashir::ArdaPhys ALIAS ArdaPhys)

target_compile_features(ArdaPhys PUBLIC cxx_std_17)
target_include_directories(ArdaPhys
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)
target_link_libraries(ArdaPhys PUBLIC nvrhi Ardashir::EASTL)
target_precompile_headers(ArdaPhys PRIVATE Private/ArdaPhysPch.h)

set_target_properties(ArdaPhys PROPERTIES FOLDER "Ardashir")

if(ARDASHIR_BUILD_TESTS)
    file(GLOB_RECURSE ARDA_PHYS_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp"
    )
    add_executable(ArdaPhysTests ${ARDA_PHYS_TEST_SOURCES})
    target_link_libraries(ArdaPhysTests PRIVATE Ardashir::ArdaPhys GTest::gtest_main)
    set_target_properties(ArdaPhysTests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaPhysTests)
endif()
