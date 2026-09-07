include_guard(GLOBAL)
option(ARDASHIR_PROVISION_VALIDATION "Try to install missing test validation layers in the build tree" ON)
set(ARDASHIR_VULKAN_VALIDATION_DIR "" CACHE PATH "Directory containing an existing VkLayer_khronos_validation.json")
set(ARDASHIR_VULKAN_VALIDATION_SOURCE_DIR "" CACHE PATH "Optional local Vulkan-ValidationLayers source checkout")
set(ARDASHIR_VALIDATION_ROOT "${PROJECT_BINARY_DIR}/validation-layers")
file(MAKE_DIRECTORY "${ARDASHIR_VALIDATION_ROOT}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/ValidationRequest.cmake.in"
    "${ARDASHIR_VALIDATION_ROOT}/request.cmake" @ONLY)
# A configure is a fresh opportunity to retry a previously failed installation.
file(TOUCH "${ARDASHIR_VALIDATION_ROOT}/request.cmake")
add_custom_command(OUTPUT "${ARDASHIR_VALIDATION_ROOT}/attempt.stamp"
    COMMAND "${CMAKE_COMMAND}" "-DREQUEST=${ARDASHIR_VALIDATION_ROOT}/request.cmake"
        -P "${CMAKE_CURRENT_LIST_DIR}/ProvisionValidation.cmake"
    DEPENDS "${ARDASHIR_VALIDATION_ROOT}/request.cmake"
        "${CMAKE_CURRENT_LIST_DIR}/ProvisionValidation.cmake"
    COMMENT "Provisioning optional GPU validation layers (failures do not block tests)"
    VERBATIM)
add_custom_target(ArdaValidationLayers DEPENDS "${ARDASHIR_VALIDATION_ROOT}/attempt.stamp")
set_target_properties(ArdaValidationLayers PROPERTIES FOLDER "Ardashir/Tests")

function(ardashir_test_validation Target)
    add_dependencies(${Target} ArdaValidationLayers)
    target_sources(${Target} PRIVATE "${PROJECT_SOURCE_DIR}/Source/TestSupport/ArdaTestValidation.cpp")
    target_include_directories(${Target} PRIVATE "${PROJECT_SOURCE_DIR}/Source/TestSupport")
    target_compile_definitions(${Target} PRIVATE
        "ARDA_TEST_VULKAN_LAYER_PATH_FILE=\"${ARDASHIR_VALIDATION_ROOT}/layer-path.txt\"")
endfunction()
