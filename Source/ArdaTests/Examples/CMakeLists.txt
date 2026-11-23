if(ARDASHIR_BUILD_ARDG_EXAMPLE)
    add_subdirectory(ARDGExample)
endif()

if(ARDASHIR_BUILD_CORNELL_BOX)
    add_subdirectory(CornellBox)
    add_subdirectory(CornellDenoising)
endif()

if(ARDASHIR_BUILD_PIXEL_SORT)
    add_subdirectory(PixelSort)
endif()

if(_ardashir_cig_operand_example)
    add_subdirectory(CiGOperand)
endif()
