# Register provider libraries before compiling graphs that consume the backend.
add_subdirectory(ArdaBackend)
add_subdirectory(ArdaBackendImpls)
ardashir_finalize_backend_modules()

add_subdirectory(ArdaGraph)
add_subdirectory(ArdaRenderGraph)
