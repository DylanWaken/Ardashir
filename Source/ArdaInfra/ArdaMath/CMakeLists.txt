ardashir_add_module(ArdaMath Private/ArdaMathPch.h)
target_link_libraries(ArdaMath PUBLIC Eigen3::Eigen)
# Keep direct Eigen matrix declarations consistent with the project's named aliases.
target_compile_definitions(ArdaMath PUBLIC EIGEN_DEFAULT_TO_ROW_MAJOR)
ardashir_add_module_tests(ArdaMath ArdaMathTests)

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/PublicHeader.cpp"
    CONTENT "#include \"ArdaMath.h\"\n")
add_library(ArdaMathPublicHeaders OBJECT "${CMAKE_CURRENT_BINARY_DIR}/PublicHeader.cpp")
target_link_libraries(ArdaMathPublicHeaders PRIVATE Ardashir::ArdaMath)
set_target_properties(ArdaMathPublicHeaders PROPERTIES FOLDER "Ardashir/Tests/PublicHeaders")
