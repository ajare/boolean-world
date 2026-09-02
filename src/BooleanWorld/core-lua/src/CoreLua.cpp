#include "core-lua/CoreLua.h"

#include <core/LayerBuildStep.h>

#include "core-lua/RunScript.h"

namespace bw {
namespace core {

void registerScriptStepTypes(ScriptRuntime& runtime) {
  LayerBuildStep::registerType("RunScript", [&runtime]() { return new RunScript(runtime); });
}

}  // namespace core
}  // namespace bw
