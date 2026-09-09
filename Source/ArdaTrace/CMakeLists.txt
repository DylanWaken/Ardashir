ardashir_add_module(ArdaTrace Private/ArdaTracePch.h)
target_compile_definitions(ArdaTrace PUBLIC
    ARDASHIR_ENABLE_TRACE=$<BOOL:${ARDASHIR_ENABLE_TRACE}>)
if(ARDASHIR_ENABLE_TRACE)
    ardashir_add_module_tests(ArdaTrace ArdaTraceTests)
endif()
