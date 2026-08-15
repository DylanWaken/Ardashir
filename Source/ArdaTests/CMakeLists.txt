if(ARDASHIR_BUILD_RHI_TEST)
    string(CONCAT ARDA_RHI_TEST_DIRECTORY "NV" "RHI" "Test")
    add_subdirectory("${ARDA_RHI_TEST_DIRECTORY}")
endif()

if(ARDASHIR_BUILD_ARDG_EXAMPLE)
    add_subdirectory(ARDGExample)
endif()
