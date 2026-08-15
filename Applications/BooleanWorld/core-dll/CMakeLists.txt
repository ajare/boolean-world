# C ABI wrapper around core, loaded by the Python tooling in scripts/ via ctypes.

add_library(core-dll SHARED src/dllmain.cpp)

target_include_directories(core-dll PRIVATE include)

target_compile_definitions(core-dll PRIVATE COREDLL_EXPORTS _WINDOWS _USRDLL)

target_link_libraries(core-dll PRIVATE core vendor::yaml-cpp)

target_compile_features(core-dll PRIVATE cxx_std_20)
target_compile_options(core-dll PRIVATE /W3)
bw_target_defaults(core-dll)
bw_output_dirs(core-dll "bin/x64/$<CONFIG>")
bw_enable_sdl_checks(core-dll)
set_target_properties(core-dll PROPERTIES FOLDER BooleanWorld)

# scripts/*.py load this DLL from the scripts directory (the .vcxproj did the
# same with a copy post-build event).
add_custom_command(TARGET core-dll POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:core-dll>" "${CMAKE_CURRENT_SOURCE_DIR}/../scripts"
    VERBATIM
    COMMENT "Copying core-dll to scripts/ for the Python tooling")
