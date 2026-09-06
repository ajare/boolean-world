#include "Actions.h"

#include <core/Layer.h>
#include <core-lua/RunScript.h>

namespace editor {

bool setRunScriptScriptName(
    Document*, bw::core::Layer* layer, bw::core::RunScript* step,
    std::string const& scriptName) {
  step->setScriptName(scriptName);
  layer->rebuild();
  return true;
}

bool setRunScriptSeed(
    Document*, bw::core::Layer* layer, bw::core::RunScript* step,
    uint64_t seed) {
  step->setSeed(seed);
  layer->rebuild();
  return true;
}

bool setRunScriptExtraResourceNames(
    Document*, bw::core::Layer* layer, bw::core::RunScript* step,
    std::vector<std::string> const& names) {
  step->setExtraResourceNames(names);
  layer->rebuild();
  return true;
}

bool setRunScriptParameterValue(
    Document*, bw::core::Layer* layer, bw::core::RunScript* step,
    std::string const& name,
    bw::core::ScriptParameterValue const& value) {
  step->setParameterValue(name, value);
  layer->rebuild();
  return true;
}

bool clearRunScriptParameterValue(
    Document*, bw::core::Layer* layer, bw::core::RunScript* step,
    std::string const& name) {
  step->clearParameterValue(name);
  layer->rebuild();
  return true;
}

}  // namespace editor
