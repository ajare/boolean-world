# core-lua: the Lua scripting library, linked by the editor and the game.
# core stays free of Lua (spec #357); core-dll and the core test targets do
# not link this.

add_library(core-lua STATIC
    src/CoreLua.cpp)

target_link_libraries(core-lua PUBLIC core lua::lua sol2::sol2)

target_compile_features(core-lua PRIVATE cxx_std_23)
target_compile_options(core-lua PRIVATE /W3)
bw_target_defaults(core-lua)
bw_output_dirs(core-lua)
bw_enable_sdl_checks(core-lua)
set_target_properties(core-lua PROPERTIES FOLDER BooleanWorld)

if(BUILD_TESTING)
    add_executable(boolean_world_core_lua_expression_tests
        tests/core_lua_expression_tests.cpp)
    target_link_libraries(boolean_world_core_lua_expression_tests PRIVATE core-lua)
    target_compile_features(boolean_world_core_lua_expression_tests PRIVATE cxx_std_23)
    bw_target_defaults(boolean_world_core_lua_expression_tests)
    bw_output_dirs(boolean_world_core_lua_expression_tests)
    bw_deploy_runtime_dlls(boolean_world_core_lua_expression_tests)
    add_test(NAME boolean_world_core_lua_expression
             COMMAND boolean_world_core_lua_expression_tests)
endif()
