include("${REQUEST}")
file(MAKE_DIRECTORY "${VALIDATION_ROOT}")
file(WRITE "${VALIDATION_ROOT}/layer-path.txt" "")
# Record an attempt even if optional provisioning fails. Reconfigure to retry.
file(WRITE "${VALIDATION_ROOT}/attempt.stamp" "attempted\n")
if(NOT VULKAN_ENABLED)
    return()
endif()
if(LAYER_DIR)
    if(EXISTS "${LAYER_DIR}/VkLayer_khronos_validation.json")
        file(WRITE "${VALIDATION_ROOT}/layer-path.txt" "${LAYER_DIR}\n")
    else()
        message(WARNING "Vulkan validation manifest is missing from ${LAYER_DIR}; validation tests will skip if the loader cannot find a layer.")
    endif()
    return()
endif()
find_file(LAYER_MANIFEST VkLayer_khronos_validation.json
    PATHS "${VALIDATION_ROOT}/install/bin"
        "${VALIDATION_ROOT}/install/share/vulkan/explicit_layer.d"
        "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/etc/vulkan/explicit_layer.d"
        "$ENV{VULKAN_SDK}/share/vulkan/explicit_layer.d"
        "$ENV{HOME}/.local/share/vulkan/explicit_layer.d"
        /etc/vulkan/explicit_layer.d /usr/share/vulkan/explicit_layer.d
        /usr/local/share/vulkan/explicit_layer.d
        ENV VK_LAYER_PATH ENV VK_ADD_LAYER_PATH
    NO_DEFAULT_PATH)
if(LAYER_MANIFEST)
    get_filename_component(LAYER_DIR "${LAYER_MANIFEST}" DIRECTORY)
    file(WRITE "${VALIDATION_ROOT}/layer-path.txt" "${LAYER_DIR}\n")
    message(STATUS "Using Vulkan validation layers: ${LAYER_DIR}")
    return()
endif()
if(NOT PROVISION OR CROSS_COMPILING)
    message(STATUS "Vulkan validation provisioning is disabled; runtime-dependent tests can skip.")
    return()
endif()

find_program(GIT_EXECUTABLE git)
if(NOT LAYER_SOURCE)
    set(LAYER_SOURCE "${VALIDATION_ROOT}/source")
    if(NOT EXISTS "${LAYER_SOURCE}/CMakeLists.txt")
        if(NOT GIT_EXECUTABLE)
            message(WARNING "Git is unavailable; cannot provision Vulkan validation layers.")
            return()
        endif()
        # Fixed Khronos source revision matching the project's Vulkan 1.4.357 headers.
        set(RESULT 0)
        if(NOT EXISTS "${LAYER_SOURCE}/.git")
            execute_process(COMMAND "${GIT_EXECUTABLE}" clone --no-checkout
                --filter=blob:none https://github.com/KhronosGroup/Vulkan-ValidationLayers.git "${LAYER_SOURCE}"
                RESULT_VARIABLE RESULT TIMEOUT 120
                OUTPUT_FILE "${VALIDATION_ROOT}/download.log" ERROR_FILE "${VALIDATION_ROOT}/download.log")
        endif()
        if("${RESULT}" STREQUAL "0")
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${LAYER_SOURCE}" checkout
                e4786f7ce8f1319215eff0d938f4be4651cbb85d
                RESULT_VARIABLE RESULT TIMEOUT 120
                OUTPUT_FILE "${VALIDATION_ROOT}/checkout.log" ERROR_FILE "${VALIDATION_ROOT}/checkout.log")
        endif()
        if(NOT "${RESULT}" STREQUAL "0")
            message(WARNING "Vulkan validation download failed (${RESULT}); see ${VALIDATION_ROOT}/download.log. Validation-dependent tests can skip.")
            return()
        endif()
    endif()
endif()
set(CONFIGURE_ARGS -G "${GENERATOR}" -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_INSTALL_PREFIX=${VALIDATION_ROOT}/install" -DBUILD_TESTS=OFF -DBUILD_WERROR=OFF
    -DUPDATE_DEPS=ON)
if(UNIX AND NOT APPLE)
    # Headless tests do not require system X11/Wayland development packages.
    list(APPEND CONFIGURE_ARGS -DBUILD_WSI_WAYLAND_SUPPORT=OFF
        -DBUILD_WSI_XCB_SUPPORT=OFF -DBUILD_WSI_XLIB_SUPPORT=OFF)
endif()
if(GENERATOR_PLATFORM)
    list(APPEND CONFIGURE_ARGS -A "${GENERATOR_PLATFORM}")
endif()
if(GENERATOR_TOOLSET)
    list(APPEND CONFIGURE_ARGS -T "${GENERATOR_TOOLSET}")
endif()
if(TOOLCHAIN)
    list(APPEND CONFIGURE_ARGS "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN}")
endif()
if(MAKE_PROGRAM)
    list(APPEND CONFIGURE_ARGS "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
message(STATUS "Building pinned Vulkan validation layers locally; logs: ${VALIDATION_ROOT}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${LAYER_SOURCE}" -B "${VALIDATION_ROOT}/build" ${CONFIGURE_ARGS}
    RESULT_VARIABLE RESULT TIMEOUT 900
    OUTPUT_FILE "${VALIDATION_ROOT}/configure.log" ERROR_FILE "${VALIDATION_ROOT}/configure.log")
if("${RESULT}" STREQUAL "0")
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${VALIDATION_ROOT}/build" --config Release --parallel 4
        RESULT_VARIABLE RESULT TIMEOUT 1800
        OUTPUT_FILE "${VALIDATION_ROOT}/build.log" ERROR_FILE "${VALIDATION_ROOT}/build.log")
endif()
if("${RESULT}" STREQUAL "0")
    execute_process(COMMAND "${CMAKE_COMMAND}" --install "${VALIDATION_ROOT}/build" --config Release
        RESULT_VARIABLE RESULT TIMEOUT 120
        OUTPUT_FILE "${VALIDATION_ROOT}/install.log" ERROR_FILE "${VALIDATION_ROOT}/install.log")
endif()
if(NOT "${RESULT}" STREQUAL "0")
    message(WARNING "Vulkan validation provisioning failed (${RESULT}); see logs in ${VALIDATION_ROOT}. Validation-dependent tests can skip.")
    return()
endif()
if(WIN32)
    set(LAYER_DIR "${VALIDATION_ROOT}/install/bin")
else()
    set(LAYER_DIR "${VALIDATION_ROOT}/install/share/vulkan/explicit_layer.d")
endif()
file(WRITE "${VALIDATION_ROOT}/layer-path.txt" "${LAYER_DIR}\n")
message(STATUS "Installed Vulkan validation layers: ${LAYER_DIR}")
