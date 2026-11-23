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

# Compile each header as its own C++ translation unit using only the supplied dependencies.
function(ardashir_add_header_checks Module Visibility)
    file(GLOB_RECURSE Headers CONFIGURE_DEPENDS
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/${Visibility}"
        "${CMAKE_CURRENT_SOURCE_DIR}/${Visibility}/*.h")
    set(Sources)
    foreach(Header IN LISTS Headers)
        string(MAKE_C_IDENTIFIER "${Header}" HeaderId)
        set(Source "${CMAKE_CURRENT_BINARY_DIR}/${Visibility}Header_${HeaderId}.cpp")
        file(GENERATE OUTPUT "${Source}" CONTENT "#include \"${Header}\"\n")
        list(APPEND Sources "${Source}")
    endforeach()
    set(Target "${Module}${Visibility}Headers")
    add_library(${Target} OBJECT ${Sources})
    target_compile_features(${Target} PRIVATE cxx_std_17)
    target_include_directories(${Target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Public")
    if(Visibility STREQUAL "Private")
        target_include_directories(${Target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
    endif()
    target_link_libraries(${Target} PRIVATE ${ARGN})
    set_target_properties(${Target} PROPERTIES FOLDER "Ardashir/Tests" UNITY_BUILD OFF)
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
