if(ARDASHIR_BUILD_RHI_TEST)
    add_subdirectory(RHITest)
endif()

if(ARDASHIR_BUILD_ARDG_EXAMPLE)
    add_subdirectory(ARDGExample)
endif()

if(ARDASHIR_BUILD_CORNELL_BOX)
    add_subdirectory(CornellBox)
endif()

if(ARDASHIR_BUILD_PIXEL_SORT)
    add_subdirectory(Examples/PixelSort)
endif()
