cmake_minimum_required(VERSION 3.24)

find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${PACKAGER}"
        --manifest "${MANIFEST}" --base-dir "${BASE_DIR}" --output "${OUTPUT}"
    COMMAND_ERROR_IS_FATAL ANY)
