add_library(ArdaBackendCuda STATIC ArdaCudaInterop.cpp)
target_compile_features(ArdaBackendCuda PUBLIC cxx_std_17)
target_link_libraries(ArdaBackendCuda PRIVATE Ardashir::ArdaBackend ${CMAKE_DL_LIBS})
target_compile_definitions(ArdaBackendCuda PRIVATE $<$<BOOL:${WIN32}>:NOMINMAX> $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>)
set_target_properties(ArdaBackendCuda PROPERTIES FOLDER "Ardashir/Backends")

# The RHI also validates CUDA dispatches when no native driver is available.
target_sources(ArdaBackend PRIVATE
    ArdaCudaCommandRecording.cpp
    ArdaCudaGraph.cpp
    ArdaCudaModule.cpp
    ArdaCudaParameters.cpp
    ArdaCudaSequence.cpp
    ArdaCudaTextureBuffer.cpp
    ArdaCudaTiming.cpp
    ArdaRHICuda.cpp)

add_library(ArdaNativeComputeCuda STATIC EXCLUDE_FROM_ALL
    ArdaNativeCudaCompiler.cpp
    ArdaNativeCudaParameters.cpp
    ArdaNativeCudaProvider.cpp)
ardashir_add_native_kernel_library(ArdaModelTransferKernels
    MANIFEST Kernels/ArdaModelTransfers.json EXCLUDE_FROM_ALL)
get_target_property(TransferLibrary ArdaModelTransferKernels ARDA_NATIVE_KERNEL_LIBRARY_FILE)
set(TransferInclude "${CMAKE_CURRENT_BINARY_DIR}/ArdaModelTransferLibrary.inl")
set(EmbedScript "${CMAKE_CURRENT_SOURCE_DIR}/Kernels/EmbedLibrary.cmake")
add_custom_command(OUTPUT "${TransferInclude}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${TransferLibrary}" "-DOUTPUT=${TransferInclude}" -P "${EmbedScript}"
    DEPENDS "${TransferLibrary}" "${EmbedScript}" VERBATIM)
add_custom_target(ArdaModelTransferKernelHeader DEPENDS "${TransferInclude}")
add_dependencies(ArdaModelTransferKernelHeader ArdaModelTransferKernels)
add_dependencies(ArdaNativeComputeCuda ArdaModelTransferKernelHeader)
set_property(TARGET ArdaNativeComputeCuda PROPERTY ARDA_NATIVE_COMPUTE_BUILD_DEPENDENCIES
    ArdaModelTransferKernelHeader)
target_include_directories(ArdaNativeComputeCuda PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
add_library(Ardashir::ArdaNativeComputeCuda ALIAS ArdaNativeComputeCuda)
target_include_directories(ArdaNativeComputeCuda PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public")
target_link_libraries(ArdaNativeComputeCuda PUBLIC Ardashir::ArdaBackend)
target_compile_definitions(ArdaNativeComputeCuda PRIVATE
    $<$<BOOL:${WIN32}>:NOMINMAX> $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>)
set_target_properties(ArdaNativeComputeCuda PROPERTIES FOLDER "Ardashir/NativeCompute")
ardashir_add_header_checks(ArdaNativeComputeCuda Public Ardashir::ArdaBackend)

if(ARDASHIR_ENABLE_CUDA)
    if(NOT EXISTS "${ARDASHIR_CUDA_INCLUDE_DIR}/cuda.h")
        message(FATAL_ERROR "Set ARDASHIR_CUDA_INCLUDE_DIR to the CUDA 12.0+ SDK include directory (CiG capture requires 13.3+).")
    endif()
    target_include_directories(ArdaBackendCuda PRIVATE "${ARDASHIR_CUDA_INCLUDE_DIR}")
    target_compile_definitions(ArdaBackendCuda PUBLIC ARDA_ENABLE_CUDA=1)

    find_package(CUDAToolkit QUIET)
    if(WIN32 AND TARGET CUDA::nvrtc)
        # CUDA 13 separates host DLLs by architecture. CUDA 12 installs them directly in bin.
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
            set(ArdaCudaHostArch arm64)
        else()
            set(ArdaCudaHostArch x64)
        endif()
        file(GLOB ArdaNvrtcDlls
            "${CUDAToolkit_BIN_DIR}/${ArdaCudaHostArch}/nvrtc64_*_0.dll"
            "${CUDAToolkit_BIN_DIR}/nvrtc64_*_0.dll")
        if(ArdaNvrtcDlls)
            list(GET ArdaNvrtcDlls 0 ArdaNvrtcDll)
            get_filename_component(ArdaNvrtcName "${ArdaNvrtcDll}" NAME)
            target_include_directories(ArdaNativeComputeCuda PRIVATE "${CUDAToolkit_INCLUDE_DIRS}")
            target_compile_definitions(ArdaNativeComputeCuda PRIVATE
                ARDA_NATIVE_COMPUTE_HAS_NVRTC=1
                ARDA_NATIVE_NVRTC_DLL_NAME="${ArdaNvrtcName}"
                ARDA_NATIVE_NVRTC_DLL_PATH="${ArdaNvrtcDll}")
        endif()
    elseif(NOT WIN32 AND TARGET CUDA::nvrtc_static)
        target_link_libraries(ArdaNativeComputeCuda PRIVATE CUDA::nvrtc_static)
        target_compile_definitions(ArdaNativeComputeCuda PRIVATE ARDA_NATIVE_COMPUTE_HAS_NVRTC=1)
    elseif(NOT WIN32 AND TARGET CUDA::nvrtc)
        target_link_libraries(ArdaNativeComputeCuda PRIVATE CUDA::nvrtc)
        target_compile_definitions(ArdaNativeComputeCuda PRIVATE ARDA_NATIVE_COMPUTE_HAS_NVRTC=1)
    endif()
endif()
