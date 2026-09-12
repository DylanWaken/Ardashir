if(NOT DEFINED ARDASHIR_SOURCE_DIR)
    message(FATAL_ERROR "ARDASHIR_SOURCE_DIR is required")
endif()

file(GLOB_RECURSE ARDA_GRAPH_BOUNDARY_FILES LIST_DIRECTORIES FALSE
    "${ARDASHIR_SOURCE_DIR}/*.h" "${ARDASHIR_SOURCE_DIR}/*.hpp"
    "${ARDASHIR_SOURCE_DIR}/*.cpp" "${ARDASHIR_SOURCE_DIR}/*.inl"
    "${ARDASHIR_SOURCE_DIR}/*.cu" "${ARDASHIR_SOURCE_DIR}/*.cuh")
foreach(ARDA_GRAPH_BOUNDARY_FILE IN LISTS ARDA_GRAPH_BOUNDARY_FILES)
    file(READ "${ARDA_GRAPH_BOUNDARY_FILE}" ARDA_GRAPH_BOUNDARY_CONTENT)
    if(ARDA_GRAPH_BOUNDARY_CONTENT MATCHES
       "(FARDG|EARDG|TARDG|ARDG_BEGIN_PARAMETER_STRUCT|ARDG_END_PARAMETER_STRUCT|MakeRenderGraphContext|GetRenderGraphModuleName|AddArdaCudaPass|AddArdaTextureToBufferPass|AddArdaBufferToTexturePass|ArdaRenderGraph(Builder|Compiler|Allocator|Executor|Parameters|Blackboard|Resources|Cuda|Definitions|Pass|Log|State))")
        message(FATAL_ERROR
            "Removed graph API or implementation referenced by ${ARDA_GRAPH_BOUNDARY_FILE}")
    endif()
endforeach()
