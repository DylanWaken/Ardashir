# CPU graphics math is shared by the RHI and application camera code.
add_subdirectory(ArdaMath)

# Register provider libraries before compiling graphs that consume the backend.
add_subdirectory(ArdaBackend)
add_subdirectory(ArdaNativeCompute)
add_subdirectory(ArdaBackendImpls)
ardashir_finalize_backend_modules()
ardashir_finalize_native_compute_modules()

add_subdirectory(ArdaGraph)
add_subdirectory(ArdaDependencyGraph)
