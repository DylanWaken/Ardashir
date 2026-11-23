set(ARDA_BACKEND_PUBLIC_DIR
    "${PROJECT_SOURCE_DIR}/Source/ArdaInfra/ArdaBackend/Public")

add_subdirectory(Cuda)
add_subdirectory(NVAPI)
add_dependencies(ArdaNativeComputePublicHeaders
    ArdaNativeComputeCudaPublicHeaders ArdaNativeComputeNvapiPublicHeaders)

add_library(ArdaNativeComputeCiG STATIC EXCLUDE_FROM_ALL CiG/ArdaNativeCudaInGraphicsProvider.cpp)
add_library(Ardashir::ArdaNativeComputeCiG ALIAS ArdaNativeComputeCiG)
target_link_libraries(ArdaNativeComputeCiG PUBLIC Ardashir::ArdaNativeComputeCuda)

set_target_properties(ArdaNativeComputeCiG PROPERTIES FOLDER "Ardashir/NativeCompute")

if(ARDASHIR_NATIVE_COMPUTE_CUDA)
    list(APPEND ARDASHIR_NATIVE_COMPUTE_MODULE_TARGETS Ardashir::ArdaNativeComputeCuda)
    list(APPEND ARDASHIR_NATIVE_COMPUTE_REGISTRATION_FUNCTIONS RegisterArdaCudaNativeComputeProvider)
endif()
if(ARDASHIR_NATIVE_COMPUTE_CIG)
    list(APPEND ARDASHIR_NATIVE_COMPUTE_MODULE_TARGETS Ardashir::ArdaNativeComputeCiG)
    list(APPEND ARDASHIR_NATIVE_COMPUTE_REGISTRATION_FUNCTIONS RegisterArdaCiGNativeComputeProvider)
endif()
if(ARDASHIR_NATIVE_COMPUTE_NVAPI)
    list(APPEND ARDASHIR_NATIVE_COMPUTE_MODULE_TARGETS Ardashir::ArdaNativeComputeNvapi)
    list(APPEND ARDASHIR_NATIVE_COMPUTE_REGISTRATION_FUNCTIONS RegisterArdaNvapiNativeComputeProvider)
endif()
set(ARDASHIR_NATIVE_COMPUTE_MODULE_TARGETS "${ARDASHIR_NATIVE_COMPUTE_MODULE_TARGETS}" PARENT_SCOPE)
set(ARDASHIR_NATIVE_COMPUTE_REGISTRATION_FUNCTIONS
    "${ARDASHIR_NATIVE_COMPUTE_REGISTRATION_FUNCTIONS}" PARENT_SCOPE)

set(ARDA_NATIVE_LINKED_TARGETS ${ARDASHIR_BACKEND_MODULE_TARGETS})
set(ARDA_NATIVE_LINKED_FUNCTIONS ${ARDASHIR_BACKEND_REGISTRATION_FUNCTIONS})

if(ARDASHIR_BACKEND_VULKAN OR (WIN32 AND ARDASHIR_BACKEND_D3D12))
    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
endif()

function(ardashir_generate_feature_queries Target Script Metadata Output)
    set(GeneratedDir "${CMAKE_CURRENT_BINARY_DIR}/generated/${Target}")
    set(GeneratedFile "${GeneratedDir}/${Output}")
    add_custom_command(OUTPUT "${GeneratedFile}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${GeneratedDir}"
        COMMAND "${Python3_EXECUTABLE}" "${Script}" "${Metadata}" "${GeneratedFile}"
        DEPENDS "${Script}" "${Metadata}" ${ARGN}
        VERBATIM)
    target_sources(${Target} PRIVATE "${GeneratedFile}")
    target_include_directories(${Target} PRIVATE "${GeneratedDir}")
endfunction()

if(ARDASHIR_BACKEND_VULKAN)
    add_library(ArdaBackendVulkan STATIC
        Vulkan/ArdaVulkanBackend.cpp)
    add_library(Ardashir::ArdaBackendVulkan ALIAS ArdaBackendVulkan)
    ardashir_generate_feature_queries(ArdaBackendVulkan
        "${CMAKE_CURRENT_SOURCE_DIR}/Vulkan/GenerateArdaVulkanFeatures.py"
        "${ARDASHIR_VULKAN_REGISTRY_FILE}"
        ArdaVulkanFeatureQueries.inl
        "${CMAKE_CURRENT_SOURCE_DIR}/Vulkan/VulkanFeatureVocabulary.py")
    target_compile_features(ArdaBackendVulkan PUBLIC cxx_std_17)
    target_include_directories(ArdaBackendVulkan PRIVATE
        "${ARDA_BACKEND_PUBLIC_DIR}")
    target_link_libraries(ArdaBackendVulkan
        PRIVATE Ardashir::ArdaBackend ArdaBackendCuda ${CMAKE_DL_LIBS})
    if(TARGET Vulkan::Headers)
        target_link_libraries(ArdaBackendVulkan PRIVATE Vulkan::Headers)
    elseif(TARGET Vulkan-Headers)
        target_link_libraries(ArdaBackendVulkan PRIVATE Vulkan-Headers)
    else()
        message(FATAL_ERROR "The native Vulkan backend requires Vulkan-Headers.")
    endif()
    set_target_properties(ArdaBackendVulkan PROPERTIES
        FOLDER "Ardashir/Backends")
    list(APPEND ARDA_NATIVE_LINKED_TARGETS Ardashir::ArdaBackendVulkan)
    list(APPEND ARDA_NATIVE_LINKED_FUNCTIONS RegisterArdaVulkanBackend)
endif()

if(WIN32 AND ARDASHIR_BACKEND_D3D12)
    add_library(ArdaBackendD3D12 STATIC
        D3D12/ArdaD3D12Backend.cpp)
    add_library(Ardashir::ArdaBackendD3D12 ALIAS ArdaBackendD3D12)
    ardashir_generate_feature_queries(ArdaBackendD3D12
        "${CMAKE_CURRENT_SOURCE_DIR}/D3D12/GenerateArdaD3D12Features.py"
        "${ARDASHIR_D3D12_AGILITY_INCLUDE_DIR}/d3d12.h"
        ArdaD3D12Features.generated.inl
        "${ARDASHIR_D3D12_AGILITY_INCLUDE_DIR}/dxgiformat.h"
        "${ARDASHIR_D3D12_AGILITY_INCLUDE_DIR}/d3d12video.h")
    target_compile_features(ArdaBackendD3D12 PUBLIC cxx_std_17)
    target_compile_definitions(ArdaBackendD3D12 PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(ArdaBackendD3D12 PRIVATE
        "${ARDASHIR_D3D12_AGILITY_INCLUDE_DIR}"
        "${ARDA_BACKEND_PUBLIC_DIR}")
    target_link_libraries(ArdaBackendD3D12
        PRIVATE Ardashir::ArdaBackend ArdaBackendCuda d3d12 dxgi dxguid)
    set_target_properties(ArdaBackendD3D12 PROPERTIES
        FOLDER "Ardashir/Backends")
    list(APPEND ARDA_NATIVE_LINKED_TARGETS Ardashir::ArdaBackendD3D12)
    list(APPEND ARDA_NATIVE_LINKED_FUNCTIONS RegisterArdaD3D12Backend)
endif()

set(ARDASHIR_BACKEND_MODULE_TARGETS "${ARDA_NATIVE_LINKED_TARGETS}" PARENT_SCOPE)
set(ARDASHIR_BACKEND_REGISTRATION_FUNCTIONS
    "${ARDA_NATIVE_LINKED_FUNCTIONS}" PARENT_SCOPE)
