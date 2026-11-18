# Reuse the existing Cornell renderer, shaders and window loop; only launcher defaults differ.
get_target_property(CORNELL_DENOISING_SOURCES CornellBox SOURCES)
add_executable(CornellDenoising ${CORNELL_DENOISING_SOURCES})
target_compile_features(CornellDenoising PRIVATE cxx_std_17)
target_compile_definitions(CornellDenoising PRIVATE GLFW_INCLUDE_NONE ARDA_CORNELL_REQUIRE_DENOISING)
target_include_directories(CornellDenoising PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../CornellBox/Private"
    "${CMAKE_CURRENT_SOURCE_DIR}/../CornellBox/Public")
target_precompile_headers(CornellDenoising PRIVATE ../CornellBox/Private/ArdaCornellBoxPch.h)
target_link_libraries(CornellDenoising PRIVATE
    ArdaExampleNodes Ardashir::ArdaBackend Ardashir::ArdaDependencyGraph Ardashir::ArdaMath glfw)
if(WIN32)
    target_compile_definitions(CornellDenoising PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(ARDASHIR_BACKEND_VULKAN)
    target_compile_definitions(CornellDenoising PRIVATE ARDA_TEST_WITH_VULKAN)
endif()
if(TARGET Vulkan::Headers)
    target_link_libraries(CornellDenoising PRIVATE Vulkan::Headers)
elseif(TARGET Vulkan-Headers)
    target_link_libraries(CornellDenoising PRIVATE Vulkan-Headers)
endif()
ardashir_test_validation(CornellDenoising OPTIONAL)
if(WIN32 AND ARDASHIR_BACKEND_D3D12)
    ardashir_deploy_d3d12_agility(CornellDenoising)
endif()
set_target_properties(CornellDenoising PROPERTIES FOLDER "Ardashir/Tests/Examples/CornellDenoising")

set(ARDASHIR_CORNELL_DENOISING_PACKAGE "" CACHE FILEPATH "Optional exported 256x256 HDR package for CornellDenoising GPU tests")
if(ARDASHIR_BUILD_TESTS)
    add_test(NAME CornellDenoising.MissingPackage COMMAND CornellDenoising --hidden --frames 1)
    set_tests_properties(CornellDenoising.MissingPackage PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
    if(ARDASHIR_CORNELL_DENOISING_PACKAGE)
        foreach(BACKEND d3d12 vulkan)
            if((BACKEND STREQUAL "d3d12" AND WIN32 AND ARDASHIR_BACKEND_D3D12) OR
               (BACKEND STREQUAL "vulkan" AND ARDASHIR_BACKEND_VULKAN))
                add_test(NAME CornellDenoising.${BACKEND}
                    COMMAND CornellDenoising --backend ${BACKEND} --hidden --frames 3
                        --samples-per-dispatch 2 --max-samples 4 --max-bounces 3
                        --denoise-package "${ARDASHIR_CORNELL_DENOISING_PACKAGE}")
                set_tests_properties(CornellDenoising.${BACKEND} PROPERTIES
                    LABELS "gpu;raytracing;cuda;model" SKIP_RETURN_CODE 77 TIMEOUT 180)
            endif()
        endforeach()
    endif()
endif()
