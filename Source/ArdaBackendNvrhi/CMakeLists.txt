if(NOT ARDASHIR_BACKEND_NVRHI_VULKAN AND
   NOT (WIN32 AND ARDASHIR_BACKEND_NVRHI_D3D12))
    return()
endif()

set(ARDA_BACKEND_PRIVATE_DIR
    "${PROJECT_SOURCE_DIR}/Source/ArdaBackend/Private")
set(ARDA_BACKEND_PUBLIC_DIR
    "${PROJECT_SOURCE_DIR}/Source/ArdaBackend/Public")

add_library(ArdaBackendNvrhiCommon STATIC
    "${ARDA_BACKEND_PRIVATE_DIR}/ArdaExternalInterop.cpp"
    "${ARDA_BACKEND_PRIVATE_DIR}/RHIWrappers/ArdaNvrhiConversions.cpp"
    "${ARDA_BACKEND_PRIVATE_DIR}/RHIWrappers/ArdaNvrhiConversions.h"
    "${ARDA_BACKEND_PRIVATE_DIR}/RHIWrappers/ArdaNvrhiDevice.cpp"
    "${ARDA_BACKEND_PRIVATE_DIR}/RHIWrappers/ArdaNvrhiDevice.h")
add_library(Ardashir::ArdaBackendNvrhiCommon ALIAS ArdaBackendNvrhiCommon)
target_compile_features(ArdaBackendNvrhiCommon PUBLIC cxx_std_17)
target_compile_definitions(ArdaBackendNvrhiCommon PRIVATE
    $<$<BOOL:${WIN32}>:NOMINMAX>
    $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>)
if(WIN32 AND ARDASHIR_BACKEND_NVRHI_D3D12)
    target_compile_definitions(ArdaBackendNvrhiCommon PRIVATE
        ARDA_NVRHI_WITH_D3D12)
endif()
target_include_directories(ArdaBackendNvrhiCommon
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Public"
        "${ARDA_BACKEND_PUBLIC_DIR}"
    PRIVATE "${ARDA_BACKEND_PRIVATE_DIR}")
target_precompile_headers(ArdaBackendNvrhiCommon PRIVATE
    "${ARDA_BACKEND_PRIVATE_DIR}/ArdaBackendPch.h")

string(CONCAT ARDA_NVRHI_COMMON_TARGET "nv" "rhi")
string(CONCAT ARDA_NVRHI_VULKAN_TARGET "nv" "rhi_vk")
string(CONCAT ARDA_NVRHI_D3D12_TARGET "nv" "rhi_d3d12")
target_link_libraries(ArdaBackendNvrhiCommon
    PUBLIC Ardashir::EASTL
    PRIVATE ${ARDA_NVRHI_COMMON_TARGET} ${ARDA_NVRHI_VULKAN_TARGET})
if(WIN32 AND ARDASHIR_BACKEND_NVRHI_D3D12)
    target_link_libraries(ArdaBackendNvrhiCommon PRIVATE
        ${ARDA_NVRHI_D3D12_TARGET} d3d12 dxgi dxguid)
endif()
if(TARGET Vulkan::Headers)
    target_link_libraries(ArdaBackendNvrhiCommon PRIVATE Vulkan::Headers)
elseif(TARGET Vulkan-Headers)
    target_link_libraries(ArdaBackendNvrhiCommon PRIVATE Vulkan-Headers)
endif()

if(ARDASHIR_BACKEND_NVRHI_VULKAN)
    add_library(ArdaBackendNvrhiVulkan STATIC
        Private/ArdaNvrhiVulkanBackend.cpp
        "${ARDA_BACKEND_PRIVATE_DIR}/ArdaVulkanBackendDevice.cpp")
    add_library(Ardashir::ArdaBackendNvrhiVulkan ALIAS ArdaBackendNvrhiVulkan)
    target_compile_features(ArdaBackendNvrhiVulkan PUBLIC cxx_std_17)
    target_include_directories(ArdaBackendNvrhiVulkan
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public" "${ARDA_BACKEND_PUBLIC_DIR}"
        PRIVATE
            "${ARDA_BACKEND_PRIVATE_DIR}"
            "${PROJECT_SOURCE_DIR}/ThirdParty/NVRHI/include")
    target_precompile_headers(ArdaBackendNvrhiVulkan PRIVATE
        "${ARDA_BACKEND_PRIVATE_DIR}/ArdaBackendPch.h")
    target_link_libraries(ArdaBackendNvrhiVulkan
        PUBLIC Ardashir::ArdaBackendNvrhiCommon
        PRIVATE ${ARDA_NVRHI_VULKAN_TARGET})
    if(TARGET Vulkan::Headers)
        target_link_libraries(ArdaBackendNvrhiVulkan PRIVATE Vulkan::Headers)
    elseif(TARGET Vulkan-Headers)
        target_link_libraries(ArdaBackendNvrhiVulkan PRIVATE Vulkan-Headers)
    endif()
    set_target_properties(ArdaBackendNvrhiVulkan PROPERTIES
        FOLDER "Ardashir/Backends")
endif()

if(WIN32 AND ARDASHIR_BACKEND_NVRHI_D3D12)
    add_library(ArdaBackendNvrhiD3D12 STATIC
        Private/ArdaNvrhiD3D12Backend.cpp
        "${ARDA_BACKEND_PRIVATE_DIR}/ArdaD3D12BackendDevice.cpp")
    add_library(Ardashir::ArdaBackendNvrhiD3D12 ALIAS ArdaBackendNvrhiD3D12)
    target_compile_features(ArdaBackendNvrhiD3D12 PUBLIC cxx_std_17)
    target_compile_definitions(ArdaBackendNvrhiD3D12 PRIVATE
        ARDA_NVRHI_WITH_D3D12 NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(ArdaBackendNvrhiD3D12
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public" "${ARDA_BACKEND_PUBLIC_DIR}"
        PRIVATE
            "${ARDA_BACKEND_PRIVATE_DIR}"
            "${PROJECT_SOURCE_DIR}/ThirdParty/NVRHI/include")
    target_precompile_headers(ArdaBackendNvrhiD3D12 PRIVATE
        "${ARDA_BACKEND_PRIVATE_DIR}/ArdaBackendPch.h")
    target_link_libraries(ArdaBackendNvrhiD3D12
        PUBLIC Ardashir::ArdaBackendNvrhiCommon
        PRIVATE ${ARDA_NVRHI_D3D12_TARGET} d3d12 dxgi dxguid)
    if(TARGET Vulkan::Headers)
        target_link_libraries(ArdaBackendNvrhiD3D12 PRIVATE Vulkan::Headers)
    elseif(TARGET Vulkan-Headers)
        target_link_libraries(ArdaBackendNvrhiD3D12 PRIVATE Vulkan-Headers)
    endif()
    set_target_properties(ArdaBackendNvrhiD3D12 PROPERTIES
        FOLDER "Ardashir/Backends")
endif()

set_target_properties(ArdaBackendNvrhiCommon PROPERTIES
    FOLDER "Ardashir/Backends")
