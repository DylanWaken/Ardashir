ardashir_add_module(ArdaPhys Private/ArdaPhysPch.h)
target_link_libraries(ArdaPhys PUBLIC Ardashir::ArdaBackend)
ardashir_add_module_tests(ArdaPhys ArdaPhysTests)
