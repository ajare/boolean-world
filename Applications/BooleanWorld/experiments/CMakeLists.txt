# GoogleTest suite for core.

add_executable(experiments src/Main.cpp)

target_include_directories(experiments PRIVATE include)
target_compile_definitions(experiments PRIVATE _CONSOLE)

target_compile_definitions(experiments PRIVATE YAML_CPP_STATIC_DEFINE USINGZ)

target_link_libraries(experiments PRIVATE core ext::yaml-cpp vendor::gtest)

target_compile_features(experiments PRIVATE cxx_std_23)
target_compile_options(experiments PRIVATE /W3)
bw_target_defaults(experiments)
bw_output_dirs(experiments "bin/x64/$<CONFIG>")
bw_enable_sdl_checks(experiments)
bw_no_postfix(experiments)
set_target_properties(experiments PROPERTIES FOLDER BooleanWorld)

bw_deploy_runtime_dlls(experiments)
bw_deploy_vendor_dlls(experiments)
bw_deploy_directory(experiments "${CMAKE_CURRENT_SOURCE_DIR}/resources")

# Main.cpp resolves "../../../../experiments/resources/test-1.yaml" relative to
# the working directory, so the test must run from the executable's directory.
add_test(NAME experiments
         COMMAND experiments
         WORKING_DIRECTORY "$<TARGET_FILE_DIR:experiments>")
