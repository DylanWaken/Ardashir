add_library(ArdaNativeComputeHeaders INTERFACE)
add_library(Ardashir::ArdaNativeComputeHeaders ALIAS ArdaNativeComputeHeaders)
target_include_directories(ArdaNativeComputeHeaders INTERFACE
    "${PROJECT_SOURCE_DIR}/Source/ArdaInfra/ArdaBackendImpls/Cuda/Public"
    "${PROJECT_SOURCE_DIR}/Source/ArdaInfra/ArdaBackendImpls/NVAPI/Public")

add_library(ArdaNativeCompute INTERFACE)
add_library(Ardashir::ArdaNativeCompute ALIAS ArdaNativeCompute)
target_link_libraries(ArdaNativeCompute INTERFACE Ardashir::ArdaBackend)
add_custom_target(ArdaNativeComputePublicHeaders)
set_target_properties(ArdaNativeComputePublicHeaders PROPERTIES FOLDER "Ardashir/Tests")

ardashir_add_module_tests(ArdaNativeCompute ArdaNativeComputeTests)
if(TARGET ArdaNativeComputeTests)
    ardashir_test_validation(ArdaNativeComputeTests)
    target_link_libraries(ArdaNativeComputeTests PRIVATE
        Ardashir::ArdaNativeComputeCuda Ardashir::ArdaNativeComputeCiG Ardashir::ArdaNativeComputeNvapi)
    set(ArdaNvapiTestManifestEntry "")
    if(WIN32 AND ARDASHIR_BACKEND_D3D12)
        ardashir_deploy_d3d12_agility(ArdaNativeComputeTests)
        if(ARDASHIR_DXC_EXECUTABLE)
            set(ArdaNvapiShader "${CMAKE_CURRENT_BINARY_DIR}/ArdaNvapiLaneId.dxil")
            set(ArdaNvapiTestSource "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Shaders/ArdaNvapiLaneId.hlsl")
            set(ArdaNvapiHeaders "${PROJECT_SOURCE_DIR}/ThirdParty/NVAPI")
            ardashir_compile_native_kernel(
                SOURCE "${ArdaNvapiTestSource}" OUTPUT "${ArdaNvapiShader}" COMPILER "${ARDASHIR_DXC_EXECUTABLE}"
                ARGUMENTS -T cs_6_0 -E main -I "${ArdaNvapiHeaders}" -Fo @OUTPUT@ @SOURCE@
                DEPENDS "${ArdaNvapiHeaders}/nvHLSLExtns.h"
                    "${ArdaNvapiHeaders}/nvHLSLExtnsInternal.h" "${ArdaNvapiHeaders}/nvShaderExtnEnums.h"
            )
            string(CONCAT ArdaNvapiTestManifestEntry ",\n"
                "    {\"provider\":\"nvapi\",\"name\":\"lane-id\",\"signature\":\"lane-id-v1\","
                "\"format\":\"nvapi-dxil\",\"entryPoint\":\"main\",\"attributes\":[0,1,8],\"file\":\"${ArdaNvapiShader}\"}")
            target_compile_definitions(ArdaNativeComputeTests PRIVATE
                ARDA_NVAPI_TEST_SHADER="${ArdaNvapiShader}")
        endif()
    endif()
    ardashir_add_native_kernel_library(ArdaNativeTestKernels
        MANIFEST Tests/Shaders/ArdaNativeTestKernels.json.in DEPLOY_TO ArdaNativeComputeTests)
    target_compile_definitions(ArdaNativeComputeTests PRIVATE
        ARDA_NATIVE_TEST_KERNEL_DIR="$<TARGET_FILE_DIR:ArdaNativeComputeTests>")
endif()
