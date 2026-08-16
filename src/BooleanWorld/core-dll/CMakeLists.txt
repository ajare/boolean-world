# C ABI wrapper around core, loaded by the Python tooling via ctypes.

add_library(core-dll SHARED src/dllmain.cpp)

target_include_directories(core-dll PRIVATE include)

target_compile_definitions(core-dll PRIVATE COREDLL_EXPORTS _WINDOWS _USRDLL)

target_link_libraries(core-dll PRIVATE core ext::yaml-cpp)

target_compile_features(core-dll PRIVATE cxx_std_20)
target_compile_options(core-dll PRIVATE /W3)
bw_target_defaults(core-dll)
bw_output_dirs(core-dll)
bw_enable_sdl_checks(core-dll)
set_target_properties(core-dll PROPERTIES FOLDER BooleanWorld)

bw_deploy_runtime_dlls(core-dll)
bw_deploy_vendor_dlls(core-dll)
