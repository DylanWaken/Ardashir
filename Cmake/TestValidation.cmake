include_guard(GLOBAL)
option(ARDASHIR_PROVISION_VALIDATION "Build missing Vulkan validation layers from source (prefer SetupGraphicsSDK.py)" OFF)
set(ARDASHIR_VULKAN_VALIDATION_DIR "" CACHE PATH "Directory containing an existing VkLayer_khronos_validation.json")
set(ARDASHIR_VULKAN_VALIDATION_SOURCE_DIR "" CACHE PATH "Optional local Vulkan-ValidationLayers source checkout")
set(ARDASHIR_VALIDATION_ROOT "${PROJECT_BINARY_DIR}/validation-layers")
if(ARDASHIR_ENABLE_GPU_VALIDATION)
    file(MAKE_DIRECTORY "${ARDASHIR_VALIDATION_ROOT}")
    configure_file("${CMAKE_CURRENT_LIST_DIR}/ValidationRequest.cmake.in"
        "${ARDASHIR_VALIDATION_ROOT}/request.cmake" @ONLY)
    # configure_file preserves the timestamp unless setup inputs changed. Normal
    # example launches must not repeat optional SDK work on every configure.
    add_custom_command(OUTPUT "${ARDASHIR_VALIDATION_ROOT}/attempt.stamp"
            "${ARDASHIR_VALIDATION_ROOT}/layer-path.txt"
        COMMAND "${CMAKE_COMMAND}" "-DREQUEST=${ARDASHIR_VALIDATION_ROOT}/request.cmake"
            -P "${CMAKE_CURRENT_LIST_DIR}/ProvisionValidation.cmake"
        # An interrupted or fatal attempt must be retried by the next build.
        COMMAND "${CMAKE_COMMAND}" -E touch "${ARDASHIR_VALIDATION_ROOT}/attempt.stamp"
        DEPENDS "${ARDASHIR_VALIDATION_ROOT}/request.cmake"
            "${CMAKE_CURRENT_LIST_DIR}/ProvisionValidation.cmake"
        COMMENT "Preparing GPU validation layers (source build is opt-in)"
        USES_TERMINAL VERBATIM)
    add_custom_target(ArdaValidationLayers DEPENDS "${ARDASHIR_VALIDATION_ROOT}/attempt.stamp")
    set_target_properties(ArdaValidationLayers PROPERTIES FOLDER "Ardashir/Tests")
endif()

function(ardashir_test_validation Target)
    cmake_parse_arguments(VALIDATION "OPTIONAL" "" "" ${ARGN})
    target_include_directories(${Target} PRIVATE "${PROJECT_SOURCE_DIR}/Source/TestSupport")
    target_compile_definitions(${Target} PRIVATE
        ARDA_TEST_ENABLE_VALIDATION=$<BOOL:${ARDASHIR_ENABLE_GPU_VALIDATION}>)
    if(ARDASHIR_BACKEND_VULKAN)
        target_sources(${Target} PRIVATE "${PROJECT_SOURCE_DIR}/Source/TestSupport/ArdaTestValidation.cpp")
        target_link_libraries(${Target} PRIVATE ${CMAKE_DL_LIBS})
        if(TARGET Vulkan::Headers)
            target_link_libraries(${Target} PRIVATE Vulkan::Headers)
        else()
            target_link_libraries(${Target} PRIVATE Vulkan-Headers)
        endif()
    endif()
    if(NOT ARDASHIR_ENABLE_GPU_VALIDATION)
        return()
    endif()
    # Interactive examples opt into native validation at runtime. Building them
    # must not trigger SDK discovery/downloads; strict GPU test targets retain it.
    if(NOT VALIDATION_OPTIONAL)
        add_dependencies(${Target} ArdaValidationLayers)
    endif()
    target_compile_definitions(${Target} PRIVATE
        "ARDA_TEST_VULKAN_LAYER_PATH_FILE=\"${ARDASHIR_VALIDATION_ROOT}/layer-path.txt\""
        "ARDA_TEST_VULKAN_LAYER_DIR=\"${ARDASHIR_VULKAN_VALIDATION_DIR}\"")
endfunction()

# Keep ordinary example smoke runs independent of native validation availability.
# These separate cases exercise explicit validation requests when supported by the build.
function(ardashir_example_validation_test Target Backend)
    if(NOT ARDASHIR_ENABLE_GPU_VALIDATION)
        return()
    endif()
    string(TOLOWER "${Backend}" BackendArgument)
    add_test(NAME "${Target}.${Backend}.Validation"
        COMMAND ${Target} --backend "${BackendArgument}" --validation ${ARGN})
    set_tests_properties("${Target}.${Backend}.Validation" PROPERTIES
        LABELS "gpu;validation" SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE)
endfunction()

# Hide local and installed Vulkan explicit layers in ordinary example regression
# cases. The process bootstrap respects this path, so default runs cannot rely on validation.
function(ardashir_example_without_validation_tests)
    set(EmptyLayerDirectory "${PROJECT_BINARY_DIR}/without-validation-layers")
    file(MAKE_DIRECTORY "${EmptyLayerDirectory}")
    set_tests_properties(${ARGN} PROPERTIES
        ENVIRONMENT "VK_LAYER_PATH=${EmptyLayerDirectory};VK_ADD_LAYER_PATH=")
endfunction()
