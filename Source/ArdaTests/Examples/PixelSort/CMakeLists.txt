# Build this directory through the repository root (or RunPixelSort.py). The root
# discovers nvcc while configuring; the launcher prepares Windows developer tools.
# This example uses a Win32 window even when its graphics backend is Vulkan.
if(NOT WIN32 OR NOT ARDASHIR_ENABLE_CUDA OR NOT ARDASHIR_BUILD_CUDA_KERNELS)
    message(FATAL_ERROR "PixelSort requires Windows, ARDASHIR_ENABLE_CUDA and ARDASHIR_BUILD_CUDA_KERNELS.")
endif()

# Step 1: compile the .cu file into a static library of native GPU code.
# PROFILE pixel_sort generates ARDA_CUDA_BUILD_NAMESPACE=arda_cuda_pixel_sort
# and GetBuildInfo() in ArdaCudaBuildInfo.h. DEPENDS includes the shared schema
# in the generated build identity; edit that schema once for both C++ and CUDA.
# With ARCHITECTURES omitted, this target inherits ARDASHIR_CUDA_ARCHITECTURES.
# The helper emits native (-real) images and links static cudart: no runtime nvcc/PTX.
#
# For a different per-kernel architecture/flag policy, add another named target
# with a UNIQUE PROFILE, e.g. ARCHITECTURES 89 120, DEFINITIONS MY_TUNING=1,
# or OPTIONS --maxrregcount=64. Each profile gets its own namespace/manifest.
# Link that target and call its binding export from BindKernelVariants explicitly.
ardashir_add_cuda_kernels(PixelSortKernels PROFILE pixel_sort
    SOURCES Private/Nodes/ArdaPixelSortKernels.cu DEPENDS Public/Nodes/ArdaPixelSortOperand.h)
target_include_directories(PixelSortKernels PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Public")
# Step 2: compile the renderer and node library with the host C++ compiler.
# The graph owns GPU execution; the window uses Win32 and capture uses WIC.
add_executable(PixelSort PixelSortMain.cpp PixelSortWindow.cpp PixelSortRenderer.cpp
    Private/Nodes/ArdaPixelSortOperand.cpp
    Private/Nodes/ArdaPixelSortUploadFrameNode.cpp
    Private/Nodes/ArdaPixelSortNoiseNode.cpp
    Private/Nodes/ArdaPixelSortSortNode.cpp
    Private/Nodes/ArdaPixelSortPresentNode.cpp
    Private/Nodes/ArdaPixelSortReadbackNode.cpp
)
target_include_directories(PixelSort PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Public")
target_compile_features(PixelSort PRIVATE cxx_std_17)
target_compile_definitions(PixelSort PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(PixelSort PRIVATE
    ArdaExampleNodes Ardashir::ArdaBackend Ardashir::ArdaRenderGraph PixelSortKernels user32 ole32 windowscodecs)
if(ARDASHIR_BACKEND_VULKAN)
    # Headers describe the Win32 surface ABI. PixelSortWindow loads the system's
    # Vulkan loader dynamically, so the application does not link a loader library.
    target_link_libraries(PixelSort PRIVATE Vulkan::Headers)
    target_compile_definitions(PixelSort PRIVATE ARDA_PIXEL_SORT_VULKAN)
endif()
set_target_properties(PixelSort PixelSortKernels PROPERTIES FOLDER "Ardashir/Tests/Examples/PixelSort")
# Step 3: deploy the graphics assets beside the executable, independent of cwd.
# Keep this development example runnable outside the source/build tree. CUDA is
# already native code; DXC is used only for the editable HLSL graphics stages.
get_filename_component(PixelSortDxcDirectory "${ARDASHIR_DXC_EXECUTABLE}" DIRECTORY)
add_custom_target(PixelSortShaders
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PixelSort>/Nodes/Shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/Private/Nodes/Shaders/ArdaPixelSort.hlsl"
        "$<TARGET_FILE_DIR:PixelSort>/Nodes/Shaders/ArdaPixelSort.hlsl"
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
ardashir_test_validation(PixelSort OPTIONAL)

if(ARDASHIR_BUILD_TESTS)
    # Opt-in development validation and exact CPU/GPU comparisons. Each test
    # resizes a real hidden window six times to cover both variants and edge tiles.
    # Exit 77 means the selected backend/mode is unavailable, rather than a sort failure.
    foreach(Backend IN ITEMS d3d12 vulkan)
        string(TOUPPER "${Backend}" BackendOption)
        if(ARDASHIR_BACKEND_${BackendOption})
            foreach(Mode IN ITEMS auto context graphics)
                add_test(NAME PixelSort.${Backend}.${Mode}
                    COMMAND PixelSort --backend ${Backend} --cuda-mode ${Mode} --hidden --verify --resize-test --frames 6)
                set_tests_properties(PixelSort.${Backend}.${Mode} PROPERTIES
                    LABELS "gpu;cuda;example" SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE)
            endforeach()
            add_test(NAME PixelSort.${Backend}.Replay
                COMMAND PixelSort --backend ${Backend} --cuda-mode auto
                    --hidden --verify --width 197 --height 321 --frames 8)
            set_tests_properties(PixelSort.${Backend}.Replay PROPERTIES
                LABELS "gpu;cuda;example" SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE)

            # Exercise submission/replay without the CPU oracle's per-frame wait.
            # Process shutdown retires GPU work and checks deferred validation errors.
            add_test(NAME PixelSort.${Backend}.AsyncReplay
                COMMAND PixelSort --backend ${Backend} --cuda-mode auto
                    --hidden --width 197 --height 321 --frames 12)
            set_tests_properties(PixelSort.${Backend}.AsyncReplay PROPERTIES
                LABELS "gpu;cuda;example" SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE)
            if(Backend STREQUAL "vulkan")
                ardashir_example_without_validation_tests(PixelSort.vulkan.auto PixelSort.vulkan.context
                    PixelSort.vulkan.graphics PixelSort.vulkan.Replay PixelSort.vulkan.AsyncReplay)
            endif()
            ardashir_example_validation_test(PixelSort ${Backend} --cuda-mode auto
                --hidden --verify --resize-test --frames 6)
            if(ARDASHIR_ENABLE_GPU_VALIDATION)
                add_test(NAME PixelSort.${Backend}.AsyncReplay.Validation
                    COMMAND PixelSort --backend ${Backend} --cuda-mode auto
                        --hidden --validation --width 197 --height 321 --frames 12)
                set_tests_properties(PixelSort.${Backend}.AsyncReplay.Validation PROPERTIES
                    LABELS "gpu;cuda;example;validation" SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE)
            endif()
        endif()
    endforeach()
endif()
