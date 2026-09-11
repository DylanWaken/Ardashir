if(NOT WIN32 OR NOT ARDASHIR_ENABLE_CUDA OR NOT ARDASHIR_BUILD_CUDA_KERNELS)
    message(FATAL_ERROR "PixelSort requires Windows, ARDASHIR_ENABLE_CUDA and ARDASHIR_BUILD_CUDA_KERNELS.")
endif()

ardashir_add_cuda_kernels(PixelSortKernels PROFILE pixel_sort
    SOURCES PixelSortKernels.cu DEPENDS PixelSortOperand.h)
add_executable(PixelSort PixelSortMain.cpp PixelSortWindow.cpp PixelSortRenderer.cpp PixelSortOperand.cpp)
target_compile_features(PixelSort PRIVATE cxx_std_17)
target_compile_definitions(PixelSort PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(PixelSort PRIVATE Ardashir::ArdaBackend PixelSortKernels user32 ole32 windowscodecs)
if(ARDASHIR_BACKEND_VULKAN)
    target_link_libraries(PixelSort PRIVATE Vulkan::Headers)
    target_compile_definitions(PixelSort PRIVATE ARDA_PIXEL_SORT_VULKAN)
endif()
set_target_properties(PixelSort PixelSortKernels PROPERTIES FOLDER "Ardashir/Tests/Examples/PixelSort")
# Keep this development example runnable outside the source/build tree. CUDA is
# already native code; DXC is used only for the editable HLSL graphics stages.
get_filename_component(PixelSortDxcDirectory "${ARDASHIR_DXC_EXECUTABLE}" DIRECTORY)
add_custom_target(PixelSortShaders
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PixelSort>/Shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/PixelSort.hlsl"
        "$<TARGET_FILE_DIR:PixelSort>/Shaders/PixelSort.hlsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PixelSort>/ShaderCompiler"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ARDASHIR_DXC_EXECUTABLE}" "${PixelSortDxcDirectory}/dxcompiler.dll" "${PixelSortDxcDirectory}/dxil.dll"
        "$<TARGET_FILE_DIR:PixelSort>/ShaderCompiler"
    VERBATIM)
add_dependencies(PixelSort PixelSortShaders)
set_target_properties(PixelSortShaders PROPERTIES FOLDER "Ardashir/Tests/Examples/PixelSort")
if(ARDASHIR_BACKEND_D3D12)
    ardashir_deploy_d3d12_agility(PixelSort)
endif()
if(ARDASHIR_BUILD_TESTS)
    ardashir_test_validation(PixelSort)
    foreach(Backend IN ITEMS d3d12 vulkan)
        string(TOUPPER "${Backend}" BackendOption)
        if(ARDASHIR_BACKEND_${BackendOption})
            foreach(Mode IN ITEMS auto context graphics)
                add_test(NAME PixelSort.${Backend}.${Mode}
                    COMMAND PixelSort --backend ${Backend} --cuda-mode ${Mode} --hidden --verify --resize-test --frames 6)
                set_tests_properties(PixelSort.${Backend}.${Mode} PROPERTIES
                    LABELS "gpu;cuda;example" SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE)
            endforeach()
        endif()
    endforeach()
endif()
