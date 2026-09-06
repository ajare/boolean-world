#include "core-lua/ScriptRuntime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <format>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <string_view>

#include <core/CoreException.h>
#include <core/Layer.h>

namespace bw {
namespace core {

using namespace std;

struct IncludeExecutionState {
  sol::environment* environment = nullptr;
  map<string, string> scripts;
  map<string, sol::table> loaded;
  set<string> loading;
};

struct ScriptCoroutineState {
  ScriptRuntime* owner;
  ScriptCoroutineStatus status = ScriptCoroutineStatus::Suspended;
  string scriptName;
  // These are declared in the order needed for reverse destruction: the
  // thread must remain rooted while references created on its state die.
  optional<sol::thread> thread;
  optional<sol::environment> environment;
  unique_ptr<IncludeExecutionState> includes;
  int yieldedResults = 0;
};

namespace {

// A high enough ceiling for substantial procedural placement, but finite so
// a mistaken infinite loop returns control promptly. Hosts which do not ask
// for an exact count pay for only one hook callback at the ceiling.
constexpr int InstructionBudget = 1'000'000;
constexpr string_view InstructionBudgetMessage =
    "Lua script instruction budget exceeded";

void stopAtInstructionBudget(lua_State* lua, lua_Debug*) {
  // Lua code can catch errors with pcall. Once the budget is exhausted, make
  // the hook fire before every subsequent instruction so catching this first
  // error cannot buy another whole budget (or let a pcall loop run forever).
  lua_sethook(lua, stopAtInstructionBudget, LUA_MASKCOUNT, 1);
  luaL_error(lua, "%s", InstructionBudgetMessage.data());
}

thread_local uint64_t* activeInstructionCount = nullptr;

void countInstructions(lua_State* lua, lua_Debug*) {
  if (!activeInstructionCount) {
    return;
  }
  ++*activeInstructionCount;
  if (*activeInstructionCount >= InstructionBudget) {
    // This hook already fires every instruction, so a caught budget error gets
    // no uncounted grace period before the next instruction fails as well.
    luaL_error(lua, "%s", InstructionBudgetMessage.data());
  }
}

// Restores any hook a future ScriptRuntime client may have installed rather
// than assuming this runtime is the only user of the Lua state.
class InstructionBudgetGuard {
private:
  lua_State* mLua;
  lua_Hook mPreviousHook;
  int mPreviousMask;
  int mPreviousCount;
  uint64_t* mPreviousInstructionCount;

public:
  explicit InstructionBudgetGuard(
      lua_State* lua, uint64_t* instructionCount = nullptr)
      : mLua(lua),
        mPreviousHook(lua_gethook(lua)),
        mPreviousMask(lua_gethookmask(lua)),
        mPreviousCount(lua_gethookcount(lua)),
        mPreviousInstructionCount(activeInstructionCount) {
    activeInstructionCount = instructionCount;
    if (instructionCount) {
      *instructionCount = 0;
      lua_sethook(mLua, countInstructions, LUA_MASKCOUNT, 1);
    } else {
      lua_sethook(
          mLua, stopAtInstructionBudget, LUA_MASKCOUNT, InstructionBudget);
    }
  }

  ~InstructionBudgetGuard() {
    lua_sethook(mLua, mPreviousHook, mPreviousMask, mPreviousCount);
    activeInstructionCount = mPreviousInstructionCount;
  }
};

uint32_t findLineNumber(string const& scriptName, string_view report) {
  string const marker = scriptName + ":";
  auto position = report.find(marker);
  while (position != string_view::npos) {
    position += marker.size();
    auto end = position;
    while (end < report.size() && isdigit(static_cast<unsigned char>(report[end]))) {
      ++end;
    }

    if (end != position) {
      uint64_t line = 0;
      for (auto digit = position; digit < end; ++digit) {
        line = line * 10 + static_cast<uint64_t>(report[digit] - '0');
        if (line > numeric_limits<uint32_t>::max()) {
          return 0;
        }
      }
      return static_cast<uint32_t>(line);
    }

    position = report.find(marker, position);
  }

  return 0;
}

string firstLine(string_view report) {
  auto const end = report.find('\n');
  return string(report.substr(0, end));
}

// The base functions every execution sees, by name. Lua has no handle on
// "the base functions" as a table - they are plain globals - so handing them
// out means listing them. load, loadfile, dofile and collectgarbage are
// deliberately absent: they let a script load code or drive the collector,
// either of which breaks the determinism a build script depends on
// (docs/adr/0040). print and the opt-in dprint are bound separately, routed
// to host sinks rather than handed out from here.
constexpr array baseNames = {
    "_VERSION", "assert", "error", "getmetatable", "ipairs",
    "next", "pairs", "pcall", "rawequal", "rawget",
    "rawlen", "rawset", "select", "setmetatable", "tonumber",
    "tostring", "type", "warn", "xpcall"};

// A shallow copy, so a script that assigns into math or string changes only
// its own copy. Without this the fresh environment would still leave one
// execution able to reach the next through a shared library table.
sol::table copyLibrary(sol::state_view lua, sol::table const& library) {
  auto result = lua.create_table();
  for (auto const& [key, value] : library) {
    result.set(key, value);
  }
  return result;
}

void addLibrary(
    sol::state_view lua,
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
// tab-separated - and hands the finished line to the host log.
void bindPrint(
    sol::state_view lua, sol::environment& environment,
    ScriptLogSink const& sink, string const& scriptName,
    string const& stepName) {
  environment.set_function(
      "print", [lua, sink, scriptName, stepName](sol::variadic_args args) {
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
        if (sink) {
          sink({ScriptLogEventType::Output, scriptName, move(line), stepName});
        }
      });
}

void bindDebugPrint(
    sol::environment& environment, ScriptLogSink const& sink,
    string const& scriptName, string const& stepName) {
  environment.set_function(
      "dprint", [sink, scriptName, stepName](sol::variadic_args args) {
        if (args.size() != 1 || !args.begin()->is<string>()) {
          throw CoreException("dprint expects exactly one string argument");
        }
        if (sink) {
          sink({ScriptLogEventType::Output, scriptName,
                args.begin()->as<string>(), stepName});
        }
      });
}

sol::environment makeEnvironment(
    sol::state_view lua,
    ScriptLogSink const& logSink,
    ScriptLogSink const& debugLogSink,
    string const& scriptName,
    string const& stepName,
    ScriptLibraries libraries,
    vector<ScriptParameterDefinition> const& parameterDefinitions,
    ScriptRuntime::EnvironmentBinder const& bind) {
  sol::environment environment(lua, sol::create);

  if (contains(libraries, ScriptLibraries::Base)) {
    for (auto const* baseName : baseNames) {
      environment.set(baseName, lua[baseName]);
    }
    bindPrint(lua, environment, logSink, scriptName, stepName);
    bindDebugPrint(environment, debugLogSink, scriptName, stepName);
  }

  addLibrary(lua, environment, libraries, ScriptLibraries::Table, "table");
  addLibrary(lua, environment, libraries, ScriptLibraries::String, "string");
  addLibrary(lua, environment, libraries, ScriptLibraries::Math, "math");
  addLibrary(lua, environment, libraries, ScriptLibraries::Coroutine, "coroutine");

  auto params = lua.create_table();
  for (auto const& definition : parameterDefinitions) {
    visit(
        [&params, &definition](auto const& value) {
          params[definition.name] = value;
        },
        definition.defaultValue);
  }
  environment["params"] = params;

  if (bind) {
    bind(environment);
  }
  return environment;
}

int appendBytecode(lua_State*, void const* bytes, size_t size, void* output) {
  static_cast<string*>(output)->append(static_cast<char const*>(bytes), size);
  return 0;
}

sol::table executeInclude(
    IncludeExecutionState* state, string const& name,
    lua_State* callingState) {
  if (auto cached = state->loaded.find(name); cached != state->loaded.end()) {
    return cached->second;
  }

  auto script = state->scripts.find(name);
  if (script == state->scripts.end()) {
    throw CoreException(format(
        "Lua script include '{}' is not a declared LuaScript dependency", name));
  }
  if (!state->loading.insert(name).second) {
    throw CoreException(format("Cyclic Lua script include of '{}'", name));
  }

  struct LoadingGuard {
    set<string>& loading;
    string const& name;
    ~LoadingGuard() { loading.erase(name); }
  } loadingGuard{state->loading, name};

  sol::state_view lua(callingState);
  auto loaded = lua.load(script->second, "@" + name, sol::load_mode::binary);
  if (!loaded.valid()) {
    throw CoreException(format("Cached Lua include '{}' could not be loaded", name));
  }

  sol::protected_function function = loaded;
  sol::set_environment(*state->environment, function);
  sol::protected_function_result result = function();
  if (!result.valid()) {
    sol::error error = result;
    throw CoreException(format("Lua include '{}' failed: {}", name, error.what()));
  }

  if (result.return_count() != 1) {
    throw CoreException(
        format("Lua include '{}' must return exactly one table", name));
  }
  sol::object value = result.get<sol::object>();
  if (value.get_type() != sol::type::table) {
    throw CoreException(
        format("Lua include '{}' must return exactly one table", name));
  }

  sol::table table = value.as<sol::table>();
  state->loaded.insert_or_assign(name, table);
  return table;
}

unique_ptr<IncludeExecutionState> bindIncludes(
    sol::environment& environment, map<string, string> scripts) {
  auto state = make_unique<IncludeExecutionState>();
  state->environment = &environment;
  state->scripts = move(scripts);
  environment.set_function(
      "include", [state = state.get()](
                     string const& name, sol::this_state callingState) {
        return executeInclude(state, name, callingState.lua_state());
      });
  return state;
}

}  // namespace

ScriptCoroutineHandle::ScriptCoroutineHandle(
    shared_ptr<ScriptCoroutineState> state)
    : mState(move(state)) {
}

ScriptException::ScriptException(
    string message, uint32_t lineNumber, string traceback)
    : CoreException(move(message)),
      mLineNumber(lineNumber),
      mTraceback(move(traceback)) {
}

uint32_t ScriptException::getLineNumber() const {
  return mLineNumber;
}

string const& ScriptException::getTraceback() const {
  return mTraceback;
}

ScriptRuntime::ScriptRuntime(
    ScriptLogSink logSink, ScriptLogSink debugLogSink,
    bool logInstructionCounts)
    : mLogSink(move(logSink)),
      mDebugLogSink(move(debugLogSink)),
      mLogInstructionCounts(logInstructionCounts) {
  // Opened once on the state so the libraries exist to be handed out; which
  // of them an execution actually sees is decided per execution, in
  // execute(), not here.
  mLua.open_libraries(
      sol::lib::base, sol::lib::table, sol::lib::string, sol::lib::math,
      sol::lib::coroutine);
}

ScriptRuntime::~ScriptRuntime() {
  // Handles may outlive their runtime. Release every Lua reference while the
  // state is still alive, leaving those handles as status-only records.
  while (!mCoroutines.empty()) {
    auto coroutine = mCoroutines.back();
    finishCoroutine(coroutine, ScriptCoroutineStatus::Abandoned);
  }
}

void ScriptRuntime::load(
    string const& name, string const& text,
    IncludedScripts const& includedScripts,
    vector<ScriptParameterDefinition> parameterDefinitions) {
  mParameterDefinitions.insert_or_assign(name, move(parameterDefinitions));
  auto compile = [this](string const& sourceName, string const& source) {
    // The leading '@' is Lua's convention for a named source and keeps error
    // locations attached to either the root or the included resource.
    auto chunk = mLua.load(source, "@" + sourceName, sol::load_mode::text);
    if (!chunk.valid()) {
      sol::error error = chunk;
      string const report = error.what();
      throw ScriptException(
          format("Lua script '{}' failed to compile: {}", sourceName,
                 firstLine(report)),
          findLineNumber(sourceName, report), "");
    }

    string bytecode;
    sol::protected_function compiled = chunk;
    compiled.push();
    int const dumpResult =
        lua_dump(mLua.lua_state(), appendBytecode, &bytecode, 0);
    lua_pop(mLua.lua_state(), 1);
    if (dumpResult != 0) {
      throw CoreException(
          format("Lua script '{}' could not be cached", sourceName));
    }
    return bytecode;
  };

  try {
    CompiledScript compiled;
    compiled.bytecode = compile(name, text);
    for (auto const& [includedName, includedText] : includedScripts) {
      compiled.includedScripts.insert_or_assign(
          includedName, compile(includedName, includedText));
    }
    mCompileFailures.erase(name);
    mChunks.insert_or_assign(name, move(compiled));
  } catch (ScriptException const& error) {
    // Broken root or dependency text must not leave the last successfully
    // compiled graph running. Retain one failure under the root name so every
    // naming RunScript reports it during rebuild.
    mChunks.erase(name);
    CompileFailure failure{error.what(), error.getLineNumber()};
    mCompileFailures.insert_or_assign(name, failure);
    throw;
  }
}

void ScriptRuntime::reload(
    string const& name, string const& text,
    IncludedScripts const& includedScripts,
    vector<ScriptParameterDefinition> parameterDefinitions) {
  exception_ptr compileFailure;
  try {
    load(name, text, includedScripts, move(parameterDefinitions));
  } catch (ScriptException const&) {
    // load() atomically replaced the old cache entry with this failure. The
    // dependent Layers must still rebuild so their RunScript panels report
    // it instead of continuing to display stale output.
    compileFailure = current_exception();
  }

  set<Layer*> layers;
  if (auto found = mScriptSteps.find(name); found != mScriptSteps.end()) {
    for (auto const& [step, layer] : found->second) {
      (void)step;
      if (layer) {
        layers.insert(layer);
      }
    }
  }
  for (auto* layer : layers) {
    layer->rebuild();
  }

  if (compileFailure) {
    rethrow_exception(compileFailure);
  }
}

void ScriptRuntime::trackStep(
    RunScript const* step, string const& name, Layer* layer) {
  if (name.empty() || !layer) {
    return;
  }
  mScriptSteps[name].insert_or_assign(step, layer);
}

void ScriptRuntime::untrackStep(RunScript const* step, string const& name) {
  auto found = mScriptSteps.find(name);
  if (found == mScriptSteps.end()) {
    return;
  }

  found->second.erase(step);
  if (found->second.empty()) {
    mScriptSteps.erase(found);
  }
}

bool ScriptRuntime::isLoaded(string const& name) const {
  return mChunks.contains(name);
}

vector<ScriptParameterDefinition> const&
ScriptRuntime::getParameterDefinitions(string const& name) const {
  static vector<ScriptParameterDefinition> const empty;
  auto const found = mParameterDefinitions.find(name);
  return found == mParameterDefinitions.end() ? empty : found->second;
}

void ScriptRuntime::execute(
    string const& name, ScriptLibraries libraries,
    EnvironmentBinder const& bind, string const& stepName) {
  if (mLogSink) {
    mLogSink({ScriptLogEventType::ExecutionStarted, name, "", stepName});
  }

  uint64_t instructionCount = 0;
  bool executionBegan = false;
  bool instructionCountLogged = false;
  auto logInstructionCount = [&]() {
    if (!mLogInstructionCounts || !mLogSink || instructionCountLogged) {
      return;
    }
    instructionCountLogged = true;
    mLogSink(
        {ScriptLogEventType::Output, name,
         format(
             "Lua instructions executed: {} / {}", instructionCount,
             InstructionBudget),
         stepName});
  };

  try {
    if (auto failure = mCompileFailures.find(name);
        failure != mCompileFailures.end()) {
      throw ScriptException(
          failure->second.message, failure->second.lineNumber, "");
    }

    auto chunk = mChunks.find(name);
    if (chunk == mChunks.end()) {
      throw CoreException(format("No Lua script named '{}' has been loaded", name));
    }

    // Instantiate the cached bytecode so this call owns its function and _ENV.
    // A fresh table with no globals behind it means nothing survives execution.
    auto loaded = mLua.load(
        chunk->second.bytecode, "@" + name, sol::load_mode::binary);
    if (!loaded.valid()) {
      throw CoreException(format("Cached Lua script '{}' could not be loaded", name));
    }
    sol::protected_function function = loaded;
    auto environment = makeEnvironment(
        mLua, mLogSink, mDebugLogSink, name, stepName, libraries,
        getParameterDefinitions(name), bind);
    auto includes = bindIncludes(
        environment, chunk->second.includedScripts);
    (void)includes;  // Keeps the execution-local include cache alive.
    sol::set_environment(environment, function);

    sol::protected_function_result result;
    executionBegan = true;
    {
      InstructionBudgetGuard instructionBudget(
          mLua.lua_state(), mLogInstructionCounts ? &instructionCount : nullptr);
      result = function();
    }

    if (!result.valid()) {
      sol::error error = result;
      string const traceback = error.what();
      throw ScriptException(
          format("Lua script '{}' failed: {}", name, firstLine(traceback)),
          findLineNumber(name, traceback), traceback);
    }
    logInstructionCount();
  } catch (exception const& error) {
    if (mLogSink) {
      mLogSink({ScriptLogEventType::Error, name, error.what(), stepName});
    }
    if (executionBegan) {
      logInstructionCount();
    }
    throw;
  }
}

ScriptCoroutineHandle ScriptRuntime::startCoroutine(
    string const& name,
    ScriptLibraries libraries,
    EnvironmentBinder const& bind) {
  if (auto failure = mCompileFailures.find(name);
      failure != mCompileFailures.end()) {
    throw ScriptException(
        failure->second.message, failure->second.lineNumber, "");
  }

  auto chunk = mChunks.find(name);
  if (chunk == mChunks.end()) {
    throw CoreException(format("No Lua script named '{}' has been loaded", name));
  }

  auto state = make_shared<ScriptCoroutineState>();
  state->owner = this;
  state->scriptName = name;
  state->environment.emplace(makeEnvironment(
      mLua, mLogSink, mDebugLogSink, name, "", libraries,
      getParameterDefinitions(name), bind));
  state->includes = bindIncludes(
      *state->environment, chunk->second.includedScripts);
  state->thread.emplace(sol::thread::create(mLua));

  lua_State* coroutine = state->thread->thread_state();
  string const sourceName = "@" + name;
  if (luaL_loadbufferx(
          coroutine, chunk->second.bytecode.data(),
          chunk->second.bytecode.size(),
          sourceName.c_str(), "b") != LUA_OK) {
    throw CoreException(format("Cached Lua script '{}' could not be loaded", name));
  }
  state->environment->push();
  lua_xmove(mLua.lua_state(), coroutine, 1);
  if (!lua_setupvalue(coroutine, 1, 1)) {
    lua_pop(coroutine, 1);
    throw CoreException(format("Lua script '{}' has no environment", name));
  }

  mCoroutines.push_back(state);
  return ScriptCoroutineHandle(move(state));
}

void ScriptRuntime::finishCoroutine(
    shared_ptr<ScriptCoroutineState> const& coroutine,
    ScriptCoroutineStatus status) {
  coroutine->status = status;
  if (coroutine->thread) {
    lua_settop(coroutine->thread->thread_state(), 0);
  }
  coroutine->includes.reset();
  coroutine->environment.reset();
  coroutine->thread.reset();
  erase(mCoroutines, coroutine);
}

void ScriptRuntime::resumeCoroutine(ScriptCoroutineHandle const& handle) {
  auto coroutine = handle.mState;
  if (!coroutine || coroutine->owner != this) {
    throw CoreException("Coroutine handle does not belong to this ScriptRuntime");
  }
  if (coroutine->status != ScriptCoroutineStatus::Suspended ||
      !coroutine->thread) {
    throw CoreException("Only a suspended Lua coroutine can be resumed");
  }

  lua_State* lua = coroutine->thread->thread_state();
  if (coroutine->yieldedResults > 0) {
    lua_pop(lua, coroutine->yieldedResults);
    coroutine->yieldedResults = 0;
  }

  int resultCount = 0;
  int status = LUA_OK;
  {
    InstructionBudgetGuard instructionBudget(lua);
    status = lua_resume(lua, nullptr, 0, &resultCount);
  }

  if (status == LUA_YIELD) {
    coroutine->yieldedResults = resultCount;
    return;
  }
  if (status == LUA_OK) {
    lua_pop(lua, resultCount);
    finishCoroutine(coroutine, ScriptCoroutineStatus::Complete);
    return;
  }

  char const* message = lua_tostring(lua, -1);
  luaL_traceback(lua, lua, message ? message : "unknown Lua error", 1);
  string const traceback = lua_tostring(lua, -1);
  lua_settop(lua, 0);
  finishCoroutine(coroutine, ScriptCoroutineStatus::Failed);
  throw ScriptException(
      format(
          "Lua script '{}' failed: {}", coroutine->scriptName,
          firstLine(traceback)),
      findLineNumber(coroutine->scriptName, traceback), traceback);
}

ScriptCoroutineStatus ScriptRuntime::getCoroutineStatus(
    ScriptCoroutineHandle const& handle) const {
  if (!handle.mState || handle.mState->owner != this) {
    throw CoreException("Coroutine handle does not belong to this ScriptRuntime");
  }
  return handle.mState->status;
}

void ScriptRuntime::abandonCoroutine(ScriptCoroutineHandle const& handle) {
  auto coroutine = handle.mState;
  if (!coroutine || coroutine->owner != this) {
    throw CoreException("Coroutine handle does not belong to this ScriptRuntime");
  }
  if (coroutine->status == ScriptCoroutineStatus::Suspended) {
    finishCoroutine(coroutine, ScriptCoroutineStatus::Abandoned);
  }
}

void ScriptRuntime::tick() {
  auto const liveAtStart = mCoroutines;
  exception_ptr firstFailure;
  for (auto const& coroutine : liveAtStart) {
    if (coroutine->status != ScriptCoroutineStatus::Suspended) {
      continue;
    }
    try {
      resumeCoroutine(ScriptCoroutineHandle(coroutine));
    } catch (ScriptException const&) {
      if (!firstFailure) {
        firstFailure = current_exception();
      }
    }
  }
  if (firstFailure) {
    rethrow_exception(firstFailure);
  }
}

sol::state& ScriptRuntime::getState() {
  return mLua;
}

ScriptLogSink ScriptRuntime::defaultLogSink() {
  return [](ScriptLogEvent const& event) {
    if (event.type == ScriptLogEventType::ExecutionStarted) {
      return;
    }
    auto& output = event.type == ScriptLogEventType::Error ? cerr : cout;
    output << '[' << event.scriptName << "] " << event.message << '\n';
  };
}

}  // namespace core
}  // namespace bw
