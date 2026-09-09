ardashir_add_module(ArdaScene Private/ArdaScenePch.h)
target_link_libraries(ArdaScene PUBLIC Ardashir::ArdaBackend)
ardashir_add_module_tests(ArdaScene ArdaSceneTests)
