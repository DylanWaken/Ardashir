ardashir_add_module(ArdaDL Private/ArdaDLPch.h)
target_link_libraries(ArdaDL PUBLIC Ardashir::ArdaBackend)
ardashir_add_module_tests(ArdaDL ArdaDLTests)
