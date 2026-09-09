include_guard(GLOBAL)

function(ardashir_add_module Target Pch)
    file(GLOB_RECURSE ModuleSources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Public/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp")
    add_library(${Target} STATIC ${ModuleSources})
    add_library(Ardashir::${Target} ALIAS ${Target})
    target_compile_features(${Target} PUBLIC cxx_std_17)
    target_include_directories(${Target}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
    target_link_libraries(${Target} PUBLIC Ardashir::EASTL)
    target_precompile_headers(${Target} PRIVATE "${Pch}")
    set_target_properties(${Target} PROPERTIES FOLDER "Ardashir")
endfunction()

function(ardashir_add_module_tests Module Target)
    if(NOT ARDASHIR_BUILD_TESTS)
        return()
    endif()
    file(GLOB_RECURSE TestSources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp")
    add_executable(${Target} ${TestSources})
    target_link_libraries(${Target} PRIVATE Ardashir::${Module} GTest::gtest_main)
    set_target_properties(${Target} PROPERTIES FOLDER "Ardashir/Tests")
    gtest_discover_tests(${Target})
endfunction()
