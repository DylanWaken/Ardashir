# Build this directory through the repository root (or RunPixelSort.py). The root
# discovers nvcc while configuring; the launcher prepares Windows developer tools.
# This example uses a Win32 window even when its graphics backend is Vulkan.
if(NOT WIN32)
    message(FATAL_ERROR "PixelSort requires Windows.")
endif()

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
    ArdaExampleNodes Ardashir::ArdaBackend Ardashir::ArdaDependencyGraph user32 ole32 windowscodecs)
# Emit one CUDA ELF per architecture with both exported entry points. The package
# manifest keys encode the same architecture and orientation as the operand variants.
if(ARDASHIR_ENABLE_CUDA AND ARDASHIR_BUILD_CUDA_KERNELS AND
        (ARDASHIR_NATIVE_COMPUTE_CUDA OR ARDASHIR_NATIVE_COMPUTE_CIG))
    set(PixelSortArchitectures ${ARDASHIR_CUDA_ARCHITECTURES})
    if(NOT PixelSortArchitectures)
        message(FATAL_ERROR "PixelSort requires at least one native CUDA architecture target.")
    endif()
    list(TRANSFORM PixelSortArchitectures REPLACE "-real$" "")
    list(REMOVE_DUPLICATES PixelSortArchitectures)
    list(SORT PixelSortArchitectures COMPARE NATURAL ORDER DESCENDING)
    set(PixelSortKernelRecords "")
    set(PixelSortArchRows "")
    set(NvccHostArgs)
    if(CMAKE_CUDA_HOST_COMPILER)
        list(APPEND NvccHostArgs -ccbin "${CMAKE_CUDA_HOST_COMPILER}")
    endif()
    foreach(Arch IN LISTS PixelSortArchitectures)
        if(NOT Arch MATCHES "^([1-9][0-9]*)([af]?)$")
            message(FATAL_ERROR "PixelSort requires explicit native CUDA architecture targets.")
        endif()
        math(EXPR Attribute "${CMAKE_MATCH_1} << 1")
        if(CMAKE_MATCH_2)
            math(EXPR Attribute "${Attribute} | 1")
        endif()
        set(Cubin "${CMAKE_CURRENT_BINARY_DIR}/PixelSortCubins/ArdaPixelSort-sm${Arch}.cubin")
        ardashir_compile_native_kernel(
            SOURCE Private/Nodes/ArdaPixelSortKernels.cu OUTPUT "${Cubin}" COMPILER "${CMAKE_CUDA_COMPILER}"
            ARGUMENTS --cubin -arch=sm_${Arch} -std=c++17 ${NvccHostArgs}
                -I "${CMAKE_CURRENT_SOURCE_DIR}/Public" -o @OUTPUT@ @SOURCE@
            DEPENDS Public/Nodes/ArdaPixelSortKernelParameters.h)
        foreach(Provider IN ITEMS cuda-in-graphics cuda)
            foreach(Orientation IN ITEMS Rows Columns)
                string(APPEND PixelSortKernelRecords
                    "    {\"provider\":\"${Provider}\",\"name\":\"${Orientation}.sm${Arch}\","
                    "\"signature\":\"rgba8-radix-v1\",\"format\":\"cubin\","
                    "\"entryPoint\":\"ArdaPixelSort${Orientation}\",\"attributes\":[${Attribute}],\"file\":\"${Cubin}\"},\n")
            endforeach()
        endforeach()
        string(APPEND PixelSortArchRows "    \"${Arch}\",\n")
    endforeach()
    string(REGEX REPLACE ",\n$" "\n" PixelSortKernelRecords "${PixelSortKernelRecords}")
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ArdaPixelSortArchitectures.inl"
        CONTENT "${PixelSortArchRows}")
    ardashir_add_native_kernel_library(PixelSortKernels
        MANIFEST Private/Nodes/ArdaPixelSortKernels.json.in DEPLOY_TO PixelSort)
    target_include_directories(PixelSort PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(PixelSort PRIVATE ARDA_PIXEL_SORT_NATIVE_KERNELS)
    set_target_properties(PixelSortKernels PROPERTIES FOLDER "Ardashir/Tests/Examples/PixelSort")
endif()
if(ARDASHIR_BACKEND_VULKAN)
    # Headers describe the Win32 surface ABI. PixelSortWindow loads the system's
    # Vulkan loader dynamically, so the application does not link a loader library.
    target_link_libraries(PixelSort PRIVATE Vulkan::Headers)
    target_compile_definitions(PixelSort PRIVATE ARDA_PIXEL_SORT_VULKAN)
endif()
set_target_properties(PixelSort PROPERTIES FOLDER "Ardashir/Tests/Examples/PixelSort")
# Deploy graphics assets beside the executable, independent of cwd.
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
            if(NOT TARGET PixelSortKernels)
                add_test(NAME PixelSort.${Backend}.Shader
                    COMMAND PixelSort --backend ${Backend} --hidden --verify --resize-test --frames 6)
                set_tests_properties(PixelSort.${Backend}.Shader PROPERTIES
                    LABELS "gpu;example" SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE)
                if(Backend STREQUAL "vulkan")
                    ardashir_example_without_validation_tests(PixelSort.vulkan.Shader)
                endif()
                ardashir_example_validation_test(PixelSort ${Backend} --hidden --verify --resize-test --frames 6)
                continue()
            endif()
            foreach(Mode IN ITEMS auto context graphics)
                add_test(NAME PixelSort.${Backend}.${Mode}
                    COMMAND PixelSort --backend ${Backend} --cuda-mode ${Mode}
                        --require-cuda --hidden --verify --resize-test --frames 6)
                set_tests_properties(PixelSort.${Backend}.${Mode} PROPERTIES
                    LABELS "gpu;cuda;example" SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE)
            endforeach()
            add_test(NAME PixelSort.${Backend}.Replay
                COMMAND PixelSort --backend ${Backend} --cuda-mode auto
                    --require-cuda --hidden --verify --width 197 --height 321 --frames 8)
            set_tests_properties(PixelSort.${Backend}.Replay PROPERTIES
                LABELS "gpu;cuda;example" SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE)

            # Exercise submission/replay without the CPU oracle's per-frame wait.
            # Process shutdown retires GPU work and checks deferred validation errors.
            add_test(NAME PixelSort.${Backend}.AsyncReplay
                COMMAND PixelSort --backend ${Backend} --cuda-mode auto
                    --require-cuda --hidden --width 197 --height 321 --frames 12)
            set_tests_properties(PixelSort.${Backend}.AsyncReplay PROPERTIES
                LABELS "gpu;cuda;example" SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE)
            if(Backend STREQUAL "vulkan")
                ardashir_example_without_validation_tests(PixelSort.vulkan.auto PixelSort.vulkan.context
                    PixelSort.vulkan.graphics PixelSort.vulkan.Replay PixelSort.vulkan.AsyncReplay)
            endif()
            ardashir_example_validation_test(PixelSort ${Backend} --cuda-mode auto
                --require-cuda --hidden --verify --resize-test --frames 6)
            if(ARDASHIR_ENABLE_GPU_VALIDATION)
                add_test(NAME PixelSort.${Backend}.AsyncReplay.Validation
                    COMMAND PixelSort --backend ${Backend} --cuda-mode auto
                        --require-cuda --hidden --validation --width 197 --height 321 --frames 12)
                set_tests_properties(PixelSort.${Backend}.AsyncReplay.Validation PROPERTIES
                    LABELS "gpu;cuda;example;validation" SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE)
            endif()
        endif()
    endforeach()
endif()
