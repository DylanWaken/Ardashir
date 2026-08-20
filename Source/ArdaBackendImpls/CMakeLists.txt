set(ARDA_BACKEND_PUBLIC_DIR
    "${PROJECT_SOURCE_DIR}/Source/ArdaBackend/Public")

add_library(ArdaBackendNativeCommon STATIC
    Common/ArdaNativeRHI.cpp
    Common/ArdaNativeRHI.h
    Common/ArdaNativePipelineCache.cpp
    Common/ArdaNativePipelineCache.h)
add_library(Ardashir::ArdaBackendNativeCommon ALIAS ArdaBackendNativeCommon)
target_compile_features(ArdaBackendNativeCommon PUBLIC cxx_std_17)
target_include_directories(ArdaBackendNativeCommon
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${ARDA_BACKEND_PUBLIC_DIR}")
target_link_libraries(ArdaBackendNativeCommon PUBLIC Ardashir::EASTL)
set_target_properties(ArdaBackendNativeCommon PROPERTIES
    FOLDER "Ardashir/Backends")

set(ARDA_NATIVE_LINKED_TARGETS ${ARDASHIR_BACKEND_MODULE_TARGETS})
set(ARDA_NATIVE_LINKED_FUNCTIONS ${ARDASHIR_BACKEND_REGISTRATION_FUNCTIONS})

if(ARDASHIR_BACKEND_VULKAN)
    add_library(ArdaBackendVulkan STATIC
        Vulkan/ArdaVulkanBackend.cpp)
    add_library(Ardashir::ArdaBackendVulkan ALIAS ArdaBackendVulkan)
    target_compile_features(ArdaBackendVulkan PUBLIC cxx_std_17)
    target_include_directories(ArdaBackendVulkan PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${ARDA_BACKEND_PUBLIC_DIR}")
    target_link_libraries(ArdaBackendVulkan
        PUBLIC Ardashir::ArdaBackendNativeCommon
        PRIVATE ${CMAKE_DL_LIBS})
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
    target_compile_features(ArdaBackendD3D12 PUBLIC cxx_std_17)
    target_compile_definitions(ArdaBackendD3D12 PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(ArdaBackendD3D12 PRIVATE
        "${ARDASHIR_D3D12_AGILITY_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${ARDA_BACKEND_PUBLIC_DIR}")
    target_link_libraries(ArdaBackendD3D12
        PUBLIC Ardashir::ArdaBackendNativeCommon
        PRIVATE d3d12 dxgi dxguid)
    set_target_properties(ArdaBackendD3D12 PROPERTIES
        FOLDER "Ardashir/Backends")
    list(APPEND ARDA_NATIVE_LINKED_TARGETS Ardashir::ArdaBackendD3D12)
    list(APPEND ARDA_NATIVE_LINKED_FUNCTIONS RegisterArdaD3D12Backend)
endif()

set(ARDASHIR_BACKEND_MODULE_TARGETS "${ARDA_NATIVE_LINKED_TARGETS}" PARENT_SCOPE)
set(ARDASHIR_BACKEND_REGISTRATION_FUNCTIONS
    "${ARDA_NATIVE_LINKED_FUNCTIONS}" PARENT_SCOPE)
