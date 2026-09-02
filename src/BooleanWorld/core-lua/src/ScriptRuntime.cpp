#include "core-lua/ScriptRuntime.h"

#include <array>
#include <format>
#include <iostream>

#include <core/CoreException.h>

namespace bw {
namespace core {

using namespace std;

namespace {

// The base functions every execution sees, by name. Lua has no handle on
// "the base functions" as a table - they are plain globals - so handing them
// out means listing them. load, loadfile, dofile and collectgarbage are
// deliberately absent: they let a script load code or drive the collector,
// either of which breaks the determinism a build script depends on
// (docs/adr/0040). print is bound separately, routed to the host's
// PrintSink rather than handed out from here.
constexpr array baseNames = {
    "_VERSION",     "assert", "error",  "getmetatable", "ipairs",
    "next",         "pairs",  "pcall",  "rawequal",     "rawget",
    "rawlen",       "rawset", "select", "setmetatable", "tonumber",
    "tostring",     "type",   "warn",   "xpcall"};

// A shallow copy, so a script that assigns into math or string changes only
// its own copy. Without this the fresh environment would still leave one
// execution able to reach the next through a shared library table.
sol::table copyLibrary(sol::state& lua, sol::table const& library) {
  auto result = lua.create_table();
  for (auto const& [key, value] : library) {
    result.set(key, value);
  }
  return result;
}

void addLibrary(
    sol::state& lua,
    sol::environment& environment,
    ScriptLibraries libraries,
    ScriptLibraries library,
    char const* name) {
  if (!contains(libraries, library)) {
    return;
  }

  environment.set(name, copyLibrary(lua, lua[name]));
}

// Joins arguments the way Lua's own print does - tostring'd and
// tab-separated - and hands the finished line to sink, rather than writing
// anywhere itself.
void bindPrint(sol::state& lua, sol::environment& environment, PrintSink const& sink) {
  environment.set_function("print", [&lua, sink](sol::variadic_args args) {
    sol::function tostringFn = lua["tostring"];
    string line;
    bool first = true;
    for (auto arg : args) {
      if (!first) {
        line += '\t';
      }
      first = false;
      line += tostringFn(arg).get<string>();
    }
    sink(line);
  });
}

}  // namespace

ScriptRuntime::ScriptRuntime(PrintSink printSink)
    : mPrintSink(move(printSink)) {
  // Opened once on the state so the libraries exist to be handed out; which
  // of them an execution actually sees is decided per execution, in
  // execute(), not here.
  mLua.open_libraries(
      sol::lib::base, sol::lib::table, sol::lib::string, sol::lib::math,
      sol::lib::coroutine);
}

void ScriptRuntime::load(string const& name, string const& text) {
  // The leading '@' is Lua's own convention for a named source, and is what
  // makes an error report the script's name rather than its whole text.
  auto chunk = mLua.load(text, "@" + name, sol::load_mode::text);
  if (!chunk.valid()) {
    sol::error error = chunk;
    throw CoreException(
        format("Lua script '{}' failed to compile: {}", name, error.what()));
  }

  mChunks[name] = chunk;
}

bool ScriptRuntime::isLoaded(string const& name) const {
  return mChunks.contains(name);
}

void ScriptRuntime::execute(
    string const& name, ScriptLibraries libraries, EnvironmentBinder const& bind) {
  auto chunk = mChunks.find(name);
  if (chunk == mChunks.end()) {
    throw CoreException(format("No Lua script named '{}' has been loaded", name));
  }

  // A fresh table with no globals behind it: what a script assigns goes here
  // and dies with the execution, and nothing an earlier execution assigned is
  // reachable from this one.
  sol::environment environment(mLua, sol::create);

  if (contains(libraries, ScriptLibraries::Base)) {
    for (auto const* baseName : baseNames) {
      environment.set(baseName, mLua[baseName]);
    }
    bindPrint(mLua, environment, mPrintSink);
  }

  addLibrary(mLua, environment, libraries, ScriptLibraries::Table, "table");
  addLibrary(mLua, environment, libraries, ScriptLibraries::String, "string");
  addLibrary(mLua, environment, libraries, ScriptLibraries::Math, "math");
  addLibrary(mLua, environment, libraries, ScriptLibraries::Coroutine, "coroutine");

  if (bind) {
    bind(environment);
  }

  // The chunk is cached and reused; its environment is replaced before every
  // call, so the one it ran in last time is not what it runs in now.
  sol::set_environment(environment, chunk->second);

  auto result = chunk->second();
  if (!result.valid()) {
    sol::error error = result;
    throw CoreException(format("Lua script '{}' failed: {}", name, error.what()));
  }
}

sol::state& ScriptRuntime::getState() {
  return mLua;
}

PrintSink ScriptRuntime::defaultPrintSink() {
  return [](string const& line) { cout << line << '\n'; };
}

}  // namespace core
}  // namespace bw
