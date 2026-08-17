if(NOT ARDASHIR_BACKEND_NVRHI_VULKAN AND
   NOT (WIN32 AND ARDASHIR_BACKEND_NVRHI_D3D12))
    return()
endif()

set(ARDA_BACKEND_PUBLIC_DIR
    "${PROJECT_SOURCE_DIR}/Source/ArdaBackend/Public")

add_library(ArdaBackendNvrhiCommon STATIC
    Private/Interop/ArdaNvrhiExternalInterop.cpp
    Private/RHI/ArdaNvrhiConversions.cpp
    Private/RHI/ArdaNvrhiConversions.h
    Private/RHI/ArdaNvrhiDevice.cpp
    Private/RHI/ArdaNvrhiDevice.h)
add_library(Ardashir::ArdaBackendNvrhiCommon ALIAS ArdaBackendNvrhiCommon)
target_compile_features(ArdaBackendNvrhiCommon PUBLIC cxx_std_17)
target_compile_definitions(ArdaBackendNvrhiCommon PRIVATE
    $<$<BOOL:${WIN32}>:NOMINMAX>
    $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>
    $<$<BOOL:${ARDASHIR_BACKEND_NVRHI_VULKAN}>:ARDA_NVRHI_WITH_VULKAN>)
if(WIN32 AND ARDASHIR_BACKEND_NVRHI_D3D12)
    target_compile_definitions(ArdaBackendNvrhiCommon PRIVATE
        ARDA_NVRHI_WITH_D3D12)
endif()
target_include_directories(ArdaBackendNvrhiCommon
    PRIVATE
        "${ARDA_BACKEND_PUBLIC_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Private")
target_precompile_headers(ArdaBackendNvrhiCommon PRIVATE
    Private/ArdaNvrhiPch.h)

string(CONCAT ARDA_NVRHI_COMMON_TARGET "nv" "rhi")
string(CONCAT ARDA_NVRHI_VULKAN_TARGET "nv" "rhi_vk")
string(CONCAT ARDA_NVRHI_D3D12_TARGET "nv" "rhi_d3d12")
target_link_libraries(ArdaBackendNvrhiCommon
    PUBLIC Ardashir::EASTL
    PRIVATE ${ARDA_NVRHI_COMMON_TARGET})
if(ARDASHIR_BACKEND_NVRHI_VULKAN)
    target_link_libraries(ArdaBackendNvrhiCommon PRIVATE
        ${ARDA_NVRHI_VULKAN_TARGET})
endif()
if(WIN32 AND ARDASHIR_BACKEND_NVRHI_D3D12)
    target_link_libraries(ArdaBackendNvrhiCommon PRIVATE
        ${ARDA_NVRHI_D3D12_TARGET} d3d12 dxgi dxguid)
endif()
if(ARDASHIR_BACKEND_NVRHI_VULKAN)
    if(TARGET Vulkan::Headers)
        target_link_libraries(ArdaBackendNvrhiCommon PRIVATE Vulkan::Headers)
    elseif(TARGET Vulkan-Headers)
        target_link_libraries(ArdaBackendNvrhiCommon PRIVATE Vulkan-Headers)
    endif()
endif()

if(ARDASHIR_BACKEND_NVRHI_VULKAN)
    add_library(ArdaBackendNvrhiVulkan STATIC
        Private/ArdaNvrhiVulkanBackend.cpp
        Private/Devices/ArdaNvrhiVulkanDevice.cpp)
    add_library(Ardashir::ArdaBackendNvrhiVulkan ALIAS ArdaBackendNvrhiVulkan)
    target_compile_features(ArdaBackendNvrhiVulkan PUBLIC cxx_std_17)
    target_compile_definitions(ArdaBackendNvrhiVulkan PRIVATE
        ARDA_NVRHI_WITH_VULKAN)
    target_include_directories(ArdaBackendNvrhiVulkan
        PRIVATE
            "${ARDA_BACKEND_PUBLIC_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/Private"
            "${PROJECT_SOURCE_DIR}/ThirdParty/NVRHI/include")
    target_precompile_headers(ArdaBackendNvrhiVulkan PRIVATE
        Private/ArdaNvrhiPch.h)
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
        Private/Devices/ArdaNvrhiD3D12Device.cpp)
    add_library(Ardashir::ArdaBackendNvrhiD3D12 ALIAS ArdaBackendNvrhiD3D12)
    target_compile_features(ArdaBackendNvrhiD3D12 PUBLIC cxx_std_17)
    target_compile_definitions(ArdaBackendNvrhiD3D12 PRIVATE
        ARDA_NVRHI_WITH_D3D12 NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(ArdaBackendNvrhiD3D12
        PRIVATE
            "${ARDA_BACKEND_PUBLIC_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/Private"
            "${PROJECT_SOURCE_DIR}/ThirdParty/NVRHI/include")
    target_precompile_headers(ArdaBackendNvrhiD3D12 PRIVATE
        Private/ArdaNvrhiPch.h)
    target_link_libraries(ArdaBackendNvrhiD3D12
        PUBLIC Ardashir::ArdaBackendNvrhiCommon
        PRIVATE ${ARDA_NVRHI_D3D12_TARGET} d3d12 dxgi dxguid)
    set_target_properties(ArdaBackendNvrhiD3D12 PROPERTIES
        FOLDER "Ardashir/Backends")
endif()

set_target_properties(ArdaBackendNvrhiCommon PROPERTIES
    FOLDER "Ardashir/Backends")

set(ARDA_NVRHI_LINKED_TARGETS ${ARDASHIR_BACKEND_MODULE_TARGETS})
set(ARDA_NVRHI_LINKED_FUNCTIONS ${ARDASHIR_BACKEND_REGISTRATION_FUNCTIONS})
if(ARDASHIR_BACKEND_NVRHI_VULKAN)
    list(APPEND ARDA_NVRHI_LINKED_TARGETS Ardashir::ArdaBackendNvrhiVulkan)
    list(APPEND ARDA_NVRHI_LINKED_FUNCTIONS RegisterArdaNvrhiVulkanBackend)
endif()
if(WIN32 AND ARDASHIR_BACKEND_NVRHI_D3D12)
    list(APPEND ARDA_NVRHI_LINKED_TARGETS Ardashir::ArdaBackendNvrhiD3D12)
    list(APPEND ARDA_NVRHI_LINKED_FUNCTIONS RegisterArdaNvrhiD3D12Backend)
endif()
set(ARDASHIR_BACKEND_MODULE_TARGETS "${ARDA_NVRHI_LINKED_TARGETS}" PARENT_SCOPE)
set(ARDASHIR_BACKEND_REGISTRATION_FUNCTIONS
    "${ARDA_NVRHI_LINKED_FUNCTIONS}" PARENT_SCOPE)
