file(GLOB_RECURSE ARDA_TRACE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
)

add_library(ArdaTrace STATIC ${ARDA_TRACE_SOURCES})
add_library(Ardashir::ArdaTrace ALIAS ArdaTrace)

target_compile_features(ArdaTrace PUBLIC cxx_std_17)
target_compile_definitions(ArdaTrace PUBLIC
    ARDASHIR_ENABLE_TRACE=$<BOOL:${ARDASHIR_ENABLE_TRACE}>
)
target_include_directories(ArdaTrace
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)
target_link_libraries(ArdaTrace PUBLIC Ardashir::EASTL)
target_precompile_headers(ArdaTrace PRIVATE Private/ArdaTracePch.h)
set_target_properties(ArdaTrace PROPERTIES FOLDER "Ardashir")

if(ARDASHIR_BUILD_TESTS AND ARDASHIR_ENABLE_TRACE)
    file(GLOB_RECURSE ARDA_TRACE_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp"
    )
    add_executable(ArdaTraceTests ${ARDA_TRACE_TEST_SOURCES})
    target_link_libraries(ArdaTraceTests PRIVATE Ardashir::ArdaTrace GTest::gtest_main)
    set_target_properties(ArdaTraceTests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaTraceTests)
endif()
