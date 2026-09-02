#pragma once

#include "core-lua/ScriptRuntime.h"

namespace bw {
namespace core {

// Registers the step types this library defines - currently RunScript - with
// the shared step Registry, against runtime. The factory closes over runtime,
// so a deserialized step is handed its host's runtime rather than reaching
// for a singleton, and each test can own one (docs/adr/0038).
//
// Like LayerBuildStep::registerCoreTypes(), a host calls this explicitly at
// startup: a static initialiser will not do, because the linker discards one
// from a static library when nothing else references its translation unit.
void registerScriptStepTypes(ScriptRuntime& runtime);

}  // namespace core
}  // namespace bw
