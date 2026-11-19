ardashir_add_module(ArdaNativeCompute Public/ArdaNativeCompute.h)
target_link_libraries(ArdaNativeCompute PUBLIC Ardashir::ArdaBackend)

if(ARDASHIR_ENABLE_CUDA)
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
            target_include_directories(ArdaNativeCompute PRIVATE "${CUDAToolkit_INCLUDE_DIRS}")
            target_compile_definitions(ArdaNativeCompute PRIVATE
                ARDA_NATIVE_COMPUTE_HAS_NVRTC=1
                ARDA_NATIVE_NVRTC_DLL_NAME="${ArdaNvrtcName}"
                ARDA_NATIVE_NVRTC_DLL_PATH="${ArdaNvrtcDll}")
        endif()
    elseif(NOT WIN32 AND TARGET CUDA::nvrtc_static)
        target_link_libraries(ArdaNativeCompute PRIVATE CUDA::nvrtc_static)
        target_compile_definitions(ArdaNativeCompute PRIVATE ARDA_NATIVE_COMPUTE_HAS_NVRTC=1)
    elseif(NOT WIN32 AND TARGET CUDA::nvrtc)
        target_link_libraries(ArdaNativeCompute PRIVATE CUDA::nvrtc)
        target_compile_definitions(ArdaNativeCompute PRIVATE ARDA_NATIVE_COMPUTE_HAS_NVRTC=1)
    endif()
endif()

ardashir_add_module_tests(ArdaNativeCompute ArdaNativeComputeTests)
if(TARGET ArdaNativeComputeTests)
    ardashir_test_validation(ArdaNativeComputeTests)
    if(WIN32 AND ARDASHIR_BACKEND_D3D12)
        ardashir_deploy_d3d12_agility(ArdaNativeComputeTests)
        if(ARDASHIR_DXC_EXECUTABLE)
            set(ArdaNvapiShader "${CMAKE_CURRENT_BINARY_DIR}/ArdaNvapiLaneId.dxil")
            set(ArdaNvapiTestSource "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Shaders/ArdaNvapiLaneId.hlsl")
            set(ArdaNvapiHeaders "${PROJECT_SOURCE_DIR}/ThirdParty/NVAPI")
            add_custom_command(OUTPUT "${ArdaNvapiShader}"
                COMMAND "${ARDASHIR_DXC_EXECUTABLE}" -T cs_6_0 -E main -I "${ArdaNvapiHeaders}"
                    -Fo "${ArdaNvapiShader}" "${ArdaNvapiTestSource}"
                DEPENDS "${ArdaNvapiTestSource}" "${ArdaNvapiHeaders}/nvHLSLExtns.h"
                    "${ArdaNvapiHeaders}/nvHLSLExtnsInternal.h" "${ArdaNvapiHeaders}/nvShaderExtnEnums.h"
                VERBATIM)
            add_custom_target(ArdaNvapiTestShader DEPENDS "${ArdaNvapiShader}")
            add_dependencies(ArdaNativeComputeTests ArdaNvapiTestShader)
            target_compile_definitions(ArdaNativeComputeTests PRIVATE ARDA_NVAPI_TEST_SHADER="${ArdaNvapiShader}")
        endif()
    endif()
endif()
