#include "Actions.h"

#include <core/Layer.h>
#include <core-lua/RunScript.h>

namespace editor {
namespace {

void rebuildRunScript(Document* document, bw::core::Layer* layer) {
  document->clearSelections();
  layer->rebuild();
}

}  // namespace

bool setRunScriptScriptName(
    Document* document, bw::core::Layer* layer, bw::core::RunScript* step,
    std::string const& scriptName) {
  step->setScriptName(scriptName);
  rebuildRunScript(document, layer);
  return true;
}

bool setRunScriptSeed(
    Document* document, bw::core::Layer* layer, bw::core::RunScript* step,
    uint64_t seed) {
  step->setSeed(seed);
  rebuildRunScript(document, layer);
  return true;
}

bool setRunScriptExtraResourceNames(
    Document* document, bw::core::Layer* layer, bw::core::RunScript* step,
    std::vector<std::string> const& names) {
  step->setExtraResourceNames(names);
  rebuildRunScript(document, layer);
  return true;
}

bool setRunScriptStepVariableValue(
    Document* document, bw::core::Layer* layer, bw::core::RunScript* step,
    std::string const& name,
    bw::core::BuildVariableValue const& value) {
  step->setStepVariableValue(name, value);
  rebuildRunScript(document, layer);
  return true;
}

bool clearRunScriptStepVariableValue(
    Document* document, bw::core::Layer* layer, bw::core::RunScript* step,
    std::string const& name) {
  step->clearStepVariableValue(name);
  rebuildRunScript(document, layer);
  return true;
}

}  // namespace editor
