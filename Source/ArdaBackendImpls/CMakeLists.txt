set(ARDA_BACKEND_PUBLIC_DIR
    "${PROJECT_SOURCE_DIR}/Source/ArdaBackend/Public")

# Source compilation is independently optional; PTX launch providers do not load NVRTC.
set(ARDASHIR_NVRTC_INCLUDE_DIR "" CACHE PATH "Optional directory containing nvrtc.h for the CUDA C++ module compiler")
add_library(ArdaCudaCompiler STATIC Cuda/ArdaCudaCompiler.cpp)
add_library(Ardashir::ArdaCudaCompiler ALIAS ArdaCudaCompiler)
target_compile_features(ArdaCudaCompiler PUBLIC cxx_std_17)
target_link_libraries(ArdaCudaCompiler PRIVATE Ardashir::ArdaBackend ${CMAKE_DL_LIBS})
target_include_directories(ArdaCudaCompiler PUBLIC "${ARDA_BACKEND_PUBLIC_DIR}")
target_link_libraries(ArdaCudaCompiler PUBLIC Ardashir::EASTL)
target_compile_definitions(ArdaCudaCompiler PRIVATE $<$<BOOL:${WIN32}>:NOMINMAX> $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>)
set_target_properties(ArdaCudaCompiler PROPERTIES FOLDER "Ardashir/Backends")
if(ARDASHIR_NVRTC_INCLUDE_DIR)
    if(NOT EXISTS "${ARDASHIR_NVRTC_INCLUDE_DIR}/nvrtc.h")
        message(FATAL_ERROR "ARDASHIR_NVRTC_INCLUDE_DIR must contain nvrtc.h.")
    endif()
    target_include_directories(ArdaCudaCompiler PRIVATE "${ARDASHIR_NVRTC_INCLUDE_DIR}")
    target_compile_definitions(ArdaCudaCompiler PRIVATE ARDA_ENABLE_NVRTC=1)
endif()
if(TARGET ArdaBackendTests)
    target_link_libraries(ArdaBackendTests PRIVATE Ardashir::ArdaCudaCompiler)
endif()

add_library(ArdaBackendCuda STATIC Cuda/ArdaCudaInterop.cpp)
target_compile_features(ArdaBackendCuda PUBLIC cxx_std_17)
target_link_libraries(ArdaBackendCuda PRIVATE Ardashir::ArdaBackend ${CMAKE_DL_LIBS})
target_compile_definitions(ArdaBackendCuda PRIVATE $<$<BOOL:${WIN32}>:NOMINMAX> $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>)
set_target_properties(ArdaBackendCuda PROPERTIES FOLDER "Ardashir/Backends")
if(ARDASHIR_ENABLE_CUDA)
    if(NOT EXISTS "${ARDASHIR_CUDA_INCLUDE_DIR}/cuda.h")
        message(FATAL_ERROR "Set ARDASHIR_CUDA_INCLUDE_DIR to the CUDA 12.0+ SDK include directory (CiG capture requires 13.3+).")
    endif()
    target_include_directories(ArdaBackendCuda PRIVATE "${ARDASHIR_CUDA_INCLUDE_DIR}")
    target_compile_definitions(ArdaBackendCuda PUBLIC ARDA_ENABLE_CUDA=1)
endif()

set(ARDA_NATIVE_LINKED_TARGETS ${ARDASHIR_BACKEND_MODULE_TARGETS})
set(ARDA_NATIVE_LINKED_FUNCTIONS ${ARDASHIR_BACKEND_REGISTRATION_FUNCTIONS})

if(ARDASHIR_BACKEND_VULKAN)
    add_library(ArdaBackendVulkan STATIC
        Vulkan/ArdaVulkanBackend.cpp)
    add_library(Ardashir::ArdaBackendVulkan ALIAS ArdaBackendVulkan)
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
