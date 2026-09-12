add_library(ArdaExampleNodes INTERFACE)
target_include_directories(ArdaExampleNodes INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Public")

if(ARDASHIR_BUILD_RHI_TEST)
    add_subdirectory(RHITest)
endif()

if(ARDASHIR_BUILD_ARDG_EXAMPLE OR ARDASHIR_BUILD_CORNELL_BOX OR ARDASHIR_BUILD_PIXEL_SORT)
    add_subdirectory(Examples)
endif()
