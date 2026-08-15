file(GLOB_RECURSE ARDA_SCENE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
)

add_library(ArdaScene STATIC ${ARDA_SCENE_SOURCES})
add_library(Ardashir::ArdaScene ALIAS ArdaScene)

target_compile_features(ArdaScene PUBLIC cxx_std_17)
target_include_directories(ArdaScene
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)
target_link_libraries(ArdaScene PUBLIC Ardashir::ArdaBackend Ardashir::EASTL)
target_precompile_headers(ArdaScene PRIVATE Private/ArdaScenePch.h)

set_target_properties(ArdaScene PROPERTIES FOLDER "Ardashir")

if(ARDASHIR_BUILD_TESTS)
    file(GLOB_RECURSE ARDA_SCENE_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp"
    )
    add_executable(ArdaSceneTests ${ARDA_SCENE_TEST_SOURCES})
    target_link_libraries(ArdaSceneTests PRIVATE Ardashir::ArdaScene GTest::gtest_main)
    set_target_properties(ArdaSceneTests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaSceneTests)
endif()
