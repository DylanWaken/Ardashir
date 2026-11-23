include_guard(GLOBAL)

# Declare a build-time compiler invocation whose output a library manifest can consume.
# ARGUMENTS uses @SOURCE@ and @OUTPUT@ for the corresponding absolute paths.
function(ardashir_compile_native_kernel)
    cmake_parse_arguments(PARSE_ARGV 0 K "" "SOURCE;OUTPUT;COMPILER" "ARGUMENTS;DEPENDS")
    if(K_UNPARSED_ARGUMENTS OR K_KEYWORDS_MISSING_VALUES OR NOT K_SOURCE OR NOT K_OUTPUT OR NOT K_COMPILER)
        message(FATAL_ERROR "ardashir_compile_native_kernel requires SOURCE, OUTPUT, COMPILER and compiler ARGUMENTS.")
    endif()
    get_filename_component(SOURCE "${K_SOURCE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(OUTPUT "${K_OUTPUT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    get_filename_component(OutputDirectory "${OUTPUT}" DIRECTORY)
    get_filename_component(OutputName "${OUTPUT}" NAME)
    set(TemporaryOutput "${OutputDirectory}/.tmp-${OutputName}")
    string(FIND "${K_ARGUMENTS}" "@SOURCE@" SourceArgument)
    string(FIND "${K_ARGUMENTS}" "@OUTPUT@" OutputArgument)
    if(SourceArgument EQUAL -1 OR OutputArgument EQUAL -1)
        message(FATAL_ERROR "Compiler arguments for ${OUTPUT} must name @SOURCE@ and @OUTPUT@.")
    endif()
    string(REPLACE "@SOURCE@" "${SOURCE}" Arguments "${K_ARGUMENTS}")
    string(REPLACE "@OUTPUT@" "${TemporaryOutput}" Arguments "${Arguments}")
    add_custom_command(OUTPUT "${OUTPUT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OutputDirectory}"
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${TemporaryOutput}"
        COMMAND "${K_COMPILER}" ${Arguments}
        COMMAND "${CMAKE_COMMAND}" -E rename "${TemporaryOutput}" "${OUTPUT}"
        DEPENDS "${SOURCE}" "${K_COMPILER}" ${K_DEPENDS}
        COMMENT "Compiling native kernel ${K_SOURCE}"
        VERBATIM)
endfunction()

# Manifest file paths are relative to the original manifest. @VAR@ substitutions can
# refer to compiler outputs in the calling scope. DEPLOY_TO stages beside a target.
# EXCLUDE_FROM_ALL leaves packaging and Python discovery to the consuming build.
function(ardashir_add_native_kernel_library Target)
    cmake_parse_arguments(PARSE_ARGV 1 K "EXCLUDE_FROM_ALL" "MANIFEST;DEPLOY_TO" "")
    if(K_UNPARSED_ARGUMENTS OR K_KEYWORDS_MISSING_VALUES OR NOT K_MANIFEST)
        message(FATAL_ERROR "ardashir_add_native_kernel_library requires MANIFEST and accepts DEPLOY_TO and EXCLUDE_FROM_ALL.")
    endif()
    if(K_DEPLOY_TO AND NOT TARGET "${K_DEPLOY_TO}")
        message(FATAL_ERROR "Native kernel deployment target does not exist: ${K_DEPLOY_TO}")
    endif()
    get_filename_component(Manifest "${K_MANIFEST}" ABSOLUTE)
    get_filename_component(ManifestDirectory "${Manifest}" DIRECTORY)
    set(GeneratedDirectory "${CMAKE_CURRENT_BINARY_DIR}/${Target}")
    set(GeneratedManifest "${GeneratedDirectory}/library.json")
    configure_file("${Manifest}" "${GeneratedManifest}" @ONLY)
    file(READ "${GeneratedManifest}" Description)
    string(JSON LibraryId GET "${Description}" library)
    if(NOT LibraryId MATCHES "^[A-Za-z0-9_.-]+$" OR LibraryId STREQUAL "." OR LibraryId STREQUAL "..")
        message(FATAL_ERROR "Invalid native kernel library name: ${LibraryId}")
    endif()
    string(JSON KernelCount LENGTH "${Description}" kernels)
    if(KernelCount LESS 1)
        message(FATAL_ERROR "Native kernel library ${LibraryId} needs at least one kernel.")
    endif()
    math(EXPR LastKernel "${KernelCount} - 1")
    set(Inputs)
    foreach(Index RANGE "${LastKernel}")
        string(JSON Input GET "${Description}" kernels "${Index}" file)
        get_filename_component(Input "${Input}" ABSOLUTE BASE_DIR "${ManifestDirectory}")
        list(APPEND Inputs "${Input}")
    endforeach()

    set(Packager "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Scripts/BuildArdaNativeKernelLibrary.py")
    set(PackageWriter "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Tools/ModelExport/ArdaNativeKernelPackage.py")
    set(Filename "${LibraryId}.ardakernels")
    set(Output "${GeneratedDirectory}/${Filename}")
    if(K_EXCLUDE_FROM_ALL)
        set(PackagingDriver "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RunArdaNativeKernelPackager.cmake")
        set(PythonHints)
        foreach(Hint IN ITEMS Python3_EXECUTABLE Python3_ROOT_DIR CMAKE_DISABLE_FIND_PACKAGE_Python3)
            if(DEFINED ${Hint})
                list(APPEND PythonHints "-D${Hint}=${${Hint}}")
            endif()
        endforeach()
        set(PackageCommand "${CMAKE_COMMAND}" ${PythonHints}
            "-DPACKAGER=${Packager}" "-DMANIFEST=${GeneratedManifest}"
            "-DBASE_DIR=${ManifestDirectory}" "-DOUTPUT=${Output}" -P "${PackagingDriver}")
    else()
        find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
        set(PackageCommand "${Python3_EXECUTABLE}" "${Packager}"
            --manifest "${GeneratedManifest}" --base-dir "${ManifestDirectory}" --output "${Output}")
    endif()
    add_custom_command(OUTPUT "${Output}"
        COMMAND ${PackageCommand}
        DEPENDS "${Packager}" "${PackageWriter}" "${GeneratedManifest}" ${PackagingDriver} ${Inputs}
        COMMENT "Packaging native kernel library ${LibraryId}"
        VERBATIM)
    if(K_DEPLOY_TO)
        add_custom_target("${Target}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${K_DEPLOY_TO}>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${Output}"
                "$<TARGET_FILE_DIR:${K_DEPLOY_TO}>/${Filename}"
            DEPENDS "${Output}"
            VERBATIM)
        add_dependencies("${K_DEPLOY_TO}" "${Target}")
    elseif(K_EXCLUDE_FROM_ALL)
        add_custom_target("${Target}" DEPENDS "${Output}")
    else()
        add_custom_target("${Target}" ALL DEPENDS "${Output}")
    endif()
    set_target_properties("${Target}" PROPERTIES ARDA_NATIVE_KERNEL_LIBRARY_FILE "${Output}")
endfunction()
