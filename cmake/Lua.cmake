# Vendors PUC-Rio Lua 5.4 (ext/lua) and sol2 (ext/sol2) as build targets.
#
# Upstream Lua ships a Makefile only, and we cannot commit a CMakeLists.txt
# into its submodule tree, so this wrapper lives here instead and lists the
# submodule's sources by path. Lua is compiled as C (never C++): its
# longjmp-based error handling only stays standard-conforming that way, and
# sol2 is responsible for bridging the C++/Lua boundary safely.
#
# sol2 is header-only for our purposes, so it is exposed as a plain INTERFACE
# target rather than pulled in via its own CMakeLists.txt, which defaults to
# fetching and building its own copy of Lua - exactly the vendoring this file
# replaces.

set(BW_LUA_SOURCE_DIR "${BW_ROOT}/ext/lua")
set(BW_SOL2_SOURCE_DIR "${BW_ROOT}/ext/sol2")

if(NOT EXISTS "${BW_LUA_SOURCE_DIR}/lua.h" OR
   NOT EXISTS "${BW_SOL2_SOURCE_DIR}/include/sol/sol.hpp")
    message(FATAL_ERROR
        "Lua or sol2 is empty. Run: git submodule update --init --recursive")
endif()

enable_language(C)

# Every upstream .c file goes into the library except the three that are not
# part of it: lua.c is the standalone "lua" interpreter's main, onelua.c is
# an alternative amalgamated build we do not use, and ltests.c is Lua's
# internal test library (only wired in when LUA_USER_H names it).
file(GLOB BW_LUA_SOURCES CONFIGURE_DEPENDS "${BW_LUA_SOURCE_DIR}/*.c")
list(REMOVE_ITEM BW_LUA_SOURCES
    "${BW_LUA_SOURCE_DIR}/lua.c"
    "${BW_LUA_SOURCE_DIR}/onelua.c"
    "${BW_LUA_SOURCE_DIR}/ltests.c")

add_library(lua_static STATIC ${BW_LUA_SOURCES})
add_library(lua::lua ALIAS lua_static)

target_include_directories(lua_static PUBLIC "${BW_LUA_SOURCE_DIR}")
set_target_properties(lua_static PROPERTIES
    LINKER_LANGUAGE C
    FOLDER "Dependencies")
# Vendored source we do not patch: quiet MSVC's CRT-deprecation warnings
# (fopen, sprintf, ...) that upstream's portable C otherwise triggers.
target_compile_definitions(lua_static PRIVATE _CRT_SECURE_NO_WARNINGS)

add_library(sol2_headers INTERFACE)
add_library(sol2::sol2 ALIAS sol2_headers)
target_include_directories(sol2_headers INTERFACE "${BW_SOL2_SOURCE_DIR}/include")
target_link_libraries(sol2_headers INTERFACE lua::lua)
# sol2 3.x requires C++17 at minimum.
target_compile_features(sol2_headers INTERFACE cxx_std_17)
