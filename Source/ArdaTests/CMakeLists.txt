if(ARDASHIR_BUILD_RHI_TEST)
    add_subdirectory(RHITest)
endif()

if(ARDASHIR_BUILD_ARDG_EXAMPLE OR ARDASHIR_BUILD_CORNELL_BOX OR ARDASHIR_BUILD_PIXEL_SORT)
    add_subdirectory(Examples)
endif()
