#include "core-lua/CoreLua.h"

#include <core/LayerBuildStep.h>

#include "core-lua/RunScript.h"

namespace bw {
namespace core {

void registerScriptStepTypes(ScriptRuntime& runtime) {
  // A newly added RunScript can execute immediately, before any external
  // resources have been selected or a World has been opened.
  runtime.load(
      defaultLayerBuildStepScriptName, defaultLayerBuildStepScript);
  LayerBuildStep::registerType("RunScript", [&runtime]() { return new RunScript(runtime); });
}

}  // namespace core
}  // namespace bw
