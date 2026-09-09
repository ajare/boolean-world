#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <sol/sol.hpp>

#include <core/CoreException.h>

#include "core-lua/StepVariables.h"

namespace bw {
namespace core {

class Layer;
class RunScript;
struct ScriptCoroutineState;

// An execution event handed to the host so it can create a per-script log,
// retain print() output, and distinguish failures without this library knowing
// whether that log is a file, a console pane, or nowhere in a test. A
// RunScript also supplies its optional LayerBuildStep name as log context.
enum class ScriptLogEventType {
  ExecutionStarted,
  Output,
  Error,
};

struct ScriptLogEvent {
  ScriptLogEventType type;
  std::string scriptName;
  std::string message;
  std::string stepName;
};

using ScriptLogSink = std::function<void(ScriptLogEvent const&)>;

// The Lua standard libraries one execution may see. A parameter of execution
// rather than a property of the runtime, so a future gameplay client can ask
// for a different set without weakening what a build script is allowed
// (docs/adr/0040).
enum struct ScriptLibraries : uint32_t {
  None = 0,
  Base = 1u << 0,
  Table = 1u << 1,
  String = 1u << 2,
  Math = 1u << 3,
  Coroutine = 1u << 4,

  // What a LayerBuildStep's script runs with. Narrowing the base functions
  // within it - dropping load, dofile and collectgarbage - and routing print
  // to the host log is the Restricted environment, which follows.
  Build = Base | Table | String | Math,
};

[[nodiscard]] constexpr ScriptLibraries operator|(ScriptLibraries a, ScriptLibraries b) {
  return static_cast<ScriptLibraries>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

[[nodiscard]] constexpr bool contains(ScriptLibraries set, ScriptLibraries library) {
  return (static_cast<uint32_t>(set) & static_cast<uint32_t>(library)) != 0;
}

// A compile or execution failure with the script location kept separately
// from its display message. Syntax errors have no call stack, so their
// traceback is empty; execution errors include Lua's stack traceback.
class ScriptException final : public CoreException {
private:
  uint32_t mLineNumber;
  std::string mTraceback;

public:
  ScriptException(
      std::string message, uint32_t lineNumber, std::string traceback);

  [[nodiscard]] uint32_t getLineNumber() const;

  [[nodiscard]] std::string const& getTraceback() const;
};

// The lifecycle state of an asynchronously-run chunk. A newly started chunk
// is Suspended until its first resume. Complete and Failed coroutines are no
// longer live and are therefore ignored by tick().
enum class ScriptCoroutineStatus {
  Suspended,
  Complete,
  Failed,
  Abandoned,
};

// An opaque, copyable reference to one coroutine. Its implementation is kept
// out of the public API; retaining a handle after completion retains only the
// small status record, not the Lua thread or its environment.
class ScriptCoroutineHandle {
private:
  std::shared_ptr<ScriptCoroutineState> mState;

  explicit ScriptCoroutineHandle(std::shared_ptr<ScriptCoroutineState> state);

  friend class ScriptRuntime;
};

// Holds the one Lua state, compiles scripts given as text, caches the
// compiled chunks by name, and runs them. One exists per host and is handed
// to each RunScript step when that step type is registered, so nothing
// reaches for it through a singleton and a test can own its own.
//
// It resolves nothing itself: script text and Step build variable definitions arrive as
// values, never as a resource name, which keeps it free of the resource system
// and testable without a ResourceManager.
class ScriptRuntime {
private:
  sol::state mLua;

  struct CompiledScript {
    std::string bytecode;

    // Qualified LuaScript resource name to bytecode. These are the root
    // resource's manifest-declared transitive LuaScript dependencies; include()
    // can resolve nothing outside this closed set.
    std::map<std::string, std::string> includedScripts;
  };

  // Compiled roots by the exact name authored by RunScript. Each execution
  // loads fresh functions from these cached forms: executions never share a
  // mutable _ENV or an included module table.
  std::map<std::string, CompiledScript> mChunks;

  // Resource-authored editor/runtime inputs survive a compile failure so the
  // editor can still render and repair a step while its script text is broken.
  std::map<std::string, std::vector<StepVariableDefinition>>
      mStepVariableDefinitions;

  // A failed reload replaces the previously compiled chunk. Keeping the
  // compile failure by name lets every RunScript step that names this script
  // fail on its next rebuild instead of silently running stale code.
  struct CompileFailure {
    std::string message;
    uint32_t lineNumber;
  };
  std::map<std::string, CompileFailure> mCompileFailures;

  // The reverse lookup needed by explicit reload. Tracking steps rather than
  // only Layers preserves the Layer entry when two of its steps name the
  // same script and one is removed or repointed.
  std::map<std::string, std::map<RunScript const*, Layer*>> mScriptSteps;

  void trackStep(
      RunScript const* step, std::string const& name, Layer* layer);
  void untrackStep(RunScript const* step, std::string const& name);

  friend class RunScript;

  ScriptLogSink mLogSink;
  ScriptLogSink mDebugLogSink;
  bool mLogInstructionCounts;
  std::vector<std::shared_ptr<ScriptCoroutineState>> mCoroutines;

  void finishCoroutine(
      std::shared_ptr<ScriptCoroutineState> const& coroutine,
      ScriptCoroutineStatus status);

public:
  // logSink defaults to writing output and errors to the console. debugLogSink
  // is deliberately empty by default, making Lua's dprint() a no-op unless a
  // development host such as the editor opts into its output. Exact
  // instruction counting is also opt-in because its per-instruction hook is
  // intended for development diagnostics rather than the game runtime.
  explicit ScriptRuntime(
      ScriptLogSink logSink = defaultLogSink(),
      ScriptLogSink debugLogSink = {},
      bool logInstructionCounts = false);
  ~ScriptRuntime();

  ScriptRuntime(ScriptRuntime const&) = delete;

  ScriptRuntime& operator=(ScriptRuntime const&) = delete;

  // Compiles text and caches the chunk under name. Loading the same name
  // again replaces the chunk: that is the reload mechanism. A failed reload
  // also replaces any old chunk and is retained so execute() reports it to
  // each naming step. Throws a ScriptException when the text does not
  // compile.
  using IncludedScripts = std::map<std::string, std::string>;

  // includedScripts contains source text keyed by canonical qualified
  // LuaScript resource name (for example, "World/Foo"). include() is present
  // only when this map is non-empty, and every included chunk must return a
  // table. Included chunks are compiled here but executed afresh for each root
  // execution, with one execution-local table cache.
  void load(
      std::string const& name, std::string const& text,
      IncludedScripts const& includedScripts = {},
      std::vector<StepVariableDefinition> stepVariableDefinitions = {});

  // Replaces the cached result, then rebuilds each distinct Layer containing
  // a RunScript step that names name. A compile failure is retained and the
  // same Layers are rebuilt so they expose that failure, then the exception
  // is rethrown for the initiating host action to report.
  void reload(
      std::string const& name, std::string const& text,
      IncludedScripts const& includedScripts = {},
      std::vector<StepVariableDefinition> stepVariableDefinitions = {});

  [[nodiscard]] bool isLoaded(std::string const& name) const;

  // Empty for an internal script or one whose resource declares no Step build variables.
  // The returned definitions are ordered as authored in the manifest.
  [[nodiscard]] std::vector<StepVariableDefinition> const&
  getStepVariableDefinitions(std::string const& name) const;

  // Called with the environment an execution is about to run in, so a caller
  // can put its own bindings in it. The environment is discarded when the
  // execution ends.
  using EnvironmentBinder = std::function<void(sol::environment&)>;

  // Runs the named chunk synchronously to completion in a fresh environment
  // holding the requested libraries, a step.vars table containing the loaded
  // definitions' defaults, plus whatever bind adds. Fresh means
  // nothing a previous execution left behind is visible, and nothing this one
  // leaves behind survives. Throws a CoreException when the chunk is not
  // loaded, or a ScriptException with line and traceback when compilation or
  // execution failed. Execution is bounded by a Lua instruction hook so a
  // non-terminating script fails instead of hanging its host.
  void execute(
      std::string const& name,
      ScriptLibraries libraries,
      EnvironmentBinder const& bind = {},
      std::string const& stepName = {});

  // Creates a suspended execution of the named chunk. Unlike execute(), this
  // environment intentionally remains alive across frames. Gameplay clients
  // must therefore never bind build-time handles into it.
  [[nodiscard]] ScriptCoroutineHandle startCoroutine(
      std::string const& name,
      ScriptLibraries libraries,
      EnvironmentBinder const& bind = {});

  // Advances a live coroutine until its next yield or completion. A script
  // error marks it Failed, releases its Lua resources, and is reported as a
  // ScriptException. Resuming a non-suspended coroutine is an error.
  void resumeCoroutine(ScriptCoroutineHandle const& handle);

  [[nodiscard]] ScriptCoroutineStatus getCoroutineStatus(
      ScriptCoroutineHandle const& handle) const;

  // Stops a live coroutine and immediately releases its Lua thread and
  // environment. Abandoning an already terminal coroutine is harmless.
  void abandonCoroutine(ScriptCoroutineHandle const& handle);

  // Resumes every coroutine that was live at the start of this tick exactly
  // once. All are advanced even if one fails; the first failure is rethrown
  // after the remaining live coroutines have had their turn.
  void tick();

  // The state the chunks and their bindings live in. Exposed for the binding
  // code in this library, which registers its usertypes once against the
  // state rather than per execution; hosts have no reason to touch it.
  [[nodiscard]] sol::state& getState();

  [[nodiscard]] static ScriptLogSink defaultLogSink();
};

}  // namespace core
}  // namespace bw
