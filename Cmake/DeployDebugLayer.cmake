if(EXISTS "${SOURCE}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SOURCE}" "${DESTINATION}"
        RESULT_VARIABLE RESULT)
    if(RESULT EQUAL 0)
        return()
    endif()
endif()
message(WARNING "Could not deploy the Agility D3D12 debug layer. Tests requiring unavailable validation will skip.")
