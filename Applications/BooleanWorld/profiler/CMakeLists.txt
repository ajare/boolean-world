# Superluminal-instrumented driver for core. The original .vcxproj defined only
# Release|x64; here it builds in every configuration, which costs nothing.

add_executable(profiler src/Main.cpp)

target_include_directories(profiler PRIVATE include)
target_compile_definitions(profiler PRIVATE _CONSOLE)

target_compile_definitions(profiler PRIVATE YAML_CPP_STATIC_DEFINE)

target_link_libraries(profiler PRIVATE core vendor::yaml-cpp vendor::performanceapi)

target_compile_features(profiler PRIVATE cxx_std_23)
target_compile_options(profiler PRIVATE /W3)
bw_target_defaults(profiler)
bw_output_dirs(profiler "bin/x64/$<CONFIG>")
bw_enable_sdl_checks(profiler)
bw_no_postfix(profiler)
set_target_properties(profiler PROPERTIES FOLDER BooleanWorld)

bw_deploy_runtime_dlls(profiler)
bw_deploy_vendor_dlls(profiler)
bw_deploy_directory(profiler "${CMAKE_CURRENT_SOURCE_DIR}/resources")
