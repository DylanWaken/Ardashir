if(NOT DEFINED ARDASHIR_SOURCE_DIR)
    message(FATAL_ERROR "ARDASHIR_SOURCE_DIR is required")
endif()

# Scan project-owned sources, including the templates that generate sample code.
# Third-party namespace definitions are outside this source root.
file(GLOB_RECURSE ARDA_NAMESPACE_FILES LIST_DIRECTORIES FALSE
    "${ARDASHIR_SOURCE_DIR}/*.h"
    "${ARDASHIR_SOURCE_DIR}/*.cuh"
    "${ARDASHIR_SOURCE_DIR}/*.cu"
    "${ARDASHIR_SOURCE_DIR}/*.hpp"
    "${ARDASHIR_SOURCE_DIR}/*.cpp"
    "${ARDASHIR_SOURCE_DIR}/*.cpp.in"
    "${ARDASHIR_SOURCE_DIR}/*.hlsl"
    "${ARDASHIR_SOURCE_DIR}/*.hlsli")

set(ARDA_NAMESPACE_ERRORS)
foreach(ARDA_NAMESPACE_FILE IN LISTS ARDA_NAMESPACE_FILES)
    file(STRINGS "${ARDA_NAMESPACE_FILE}" ARDA_NAMESPACE_LINES
        REGEX "^[ \t]*(inline[ \t]+)?namespace[ \t]+[A-Za-z_]")
    foreach(ARDA_NAMESPACE_LINE IN LISTS ARDA_NAMESPACE_LINES)
        string(STRIP "${ARDA_NAMESPACE_LINE}" ARDA_NAMESPACE_LINE)
        # Build-isolated CUDA profiles need distinct global symbols for different
        # architecture/flag products. Public facade types still use arda.
        if(ARDA_NAMESPACE_LINE MATCHES "^namespace[ \t]+(arda_cuda_[A-Za-z_0-9]+|ARDA_CUDA_BUILD_NAMESPACE)([ \t]*\\{|[ \t]*$)")
            continue()
        endif()
        # Aliases for dependency APIs are local conveniences, not Arda API facades.
        if(ARDA_NAMESPACE_LINE MATCHES "^namespace[ \t]+[A-Za-z_][A-Za-z_0-9]*[ \t]*=[ \t]*(std|eastl|vk)::")
            continue()
        endif()
        if(NOT ARDA_NAMESPACE_LINE MATCHES "^namespace[ \t]+arda([ \t]*\\{|[ \t]*$)")
            list(APPEND ARDA_NAMESPACE_ERRORS "${ARDA_NAMESPACE_FILE}: ${ARDA_NAMESPACE_LINE}")
        endif()
    endforeach()
endforeach()

if(ARDA_NAMESPACE_ERRORS)
    list(JOIN ARDA_NAMESPACE_ERRORS "\n  " ARDA_NAMESPACE_REPORT)
    message(FATAL_ERROR "Use the single arda namespace and descriptive names; use anonymous namespaces for file-local helpers:\n  ${ARDA_NAMESPACE_REPORT}")
endif()
