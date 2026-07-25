include_guard(GLOBAL)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "Ardashir supports x64 targets only. Configure a 64-bit toolchain "
        "(for Visual Studio, use -A x64).")
endif()

if(CMAKE_VS_PLATFORM_NAME)
    set(_ardashir_target_architecture "${CMAKE_VS_PLATFORM_NAME}")
else()
    set(_ardashir_target_architecture "${CMAKE_SYSTEM_PROCESSOR}")
endif()

string(TOLOWER "${_ardashir_target_architecture}" _ardashir_target_architecture)
if(NOT _ardashir_target_architecture MATCHES "^(amd64|x64|x86_64)$")
    message(FATAL_ERROR
        "Ardashir supports the x64 architecture only; "
        "the selected target is '${_ardashir_target_architecture}'.")
endif()

message(STATUS "Ardashir target architecture: x64")
