ardashir_add_module(ArdaGI Private/ArdaGIPch.h)
target_link_libraries(ArdaGI PUBLIC Ardashir::ArdaBackend)
ardashir_add_module_tests(ArdaGI ArdaGITests)
