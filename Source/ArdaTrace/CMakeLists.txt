add_library(ArdaTrace STATIC
    Public/ArdaTrace.h
    Public/ArdaScopeTimer.h
    Public/ArdaTraceReader.h
    Private/ArdaTrace.cpp
    Private/ArdaTraceFormat.h
    Private/ArdaTraceReader.cpp
    Private/ArdaTracePch.h
)
add_library(Ardashir::ArdaTrace ALIAS ArdaTrace)

target_compile_features(ArdaTrace PUBLIC cxx_std_17)
target_compile_definitions(ArdaTrace PUBLIC
    ARDASHIR_ENABLE_TRACE=$<BOOL:${ARDASHIR_ENABLE_TRACE}>
)
target_include_directories(ArdaTrace
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private"
)
target_precompile_headers(ArdaTrace PRIVATE Private/ArdaTracePch.h)
set_target_properties(ArdaTrace PROPERTIES FOLDER "Ardashir")

if(ARDASHIR_BUILD_TESTS AND ARDASHIR_ENABLE_TRACE)
    add_executable(ArdaTraceTests Tests/ArdaTraceTests.cpp)
    target_link_libraries(ArdaTraceTests PRIVATE Ardashir::ArdaTrace GTest::gtest_main)
    set_target_properties(ArdaTraceTests PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(ArdaTraceTests)
endif()
