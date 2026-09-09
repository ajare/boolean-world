#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

void renderPrimitiveOrderView(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto const& selection = doc->getSelectedPrimitiveIndices();
  auto world = doc->getWorld();
  auto* activeLayer = world->getActiveLayer();
  auto const activeStepIndex = activeLayer->getActiveStepIndex();
  auto primitives = world->getPrimitivesByPriority();
  primitives.erase(
      remove_if(primitives.begin(), primitives.end(),
                [&](auto const* primitive) {
                  auto const isGhost =
                      primitive->getFlags() & BW_PRIMITIVE_GHOST_FLAG;
                  return !isGhost &&
                         activeLayer->getOwningStepIndex(primitive) !=
                             activeStepIndex;
                }),
      primitives.end());
  auto numPrimitives = (uint32_t)primitives.size();

  for (uint32_t i = 0; i < numPrimitives; ++i) {
    ImGui::PushID(i);

    auto primitive = primitives[i];
    bool isGhost = primitive->getFlags() & BW_PRIMITIVE_GHOST_FLAG;
    bool selected = doc->indexInSelection(primitive->getId());

    if (!isGhost || settings.ghostActive) {
      if (ImGui::Button(ICON_FA_HAND_POINTER)) {
        auto selectId = primitive->getId();
        editor::transact(doc, CommandId::SelectPrimitive,
                         [&] { editor::selectPrimitive(doc, selectId); });
      }
    }

    ImGui::SameLine();

    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1));
    }

    int primitivePriority = (int)primitive->getPriority();
    auto const* label = isGhost ? "Ghost Primitive" : primitive->getName().c_str();
    ImGui::Text("%s :: Priority %d", label, primitivePriority);

    if (!isGhost) {
      int counter = 0;
      ImGui::PushButtonRepeat(false);

      if (i > 0) {
        if (primitives[i - 1]->getPriority() != primitives[i]->getPriority()) {
          ImGui::SameLine();
          if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
            counter--;
          }
        }
      }

      if (i < (numPrimitives - 1)) {
        if (primitives[i]->getPriority() != primitives[i + 1]->getPriority()) {
          ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
          if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
            counter++;
          }
        }
      }

      ImGui::PopButtonRepeat();

      if (counter < 0) {
        auto prevPrim = primitives[i - 1];
        transact(doc, CommandId::SwapPrimitivePriorities, [&] {
          auto prevPriority = prevPrim->getPriority();
          prevPrim->setPriority(primitive->getPriority());
          primitive->setPriority(prevPriority);
        });
      } else if (counter > 0) {
        auto nextPrim = primitives[i + 1];
        transact(doc, CommandId::SwapPrimitivePriorities, [&] {
          auto nextPriority = nextPrim->getPriority();
          nextPrim->setPriority(primitive->getPriority());
          primitive->setPriority(nextPriority);
        });
      }

      if (i != 0) {
        setOperationWidget(doc, primitive, 2);
        if (!dynamic_cast<bw::core::MeshPrimitive*>(primitive)) {
          setFillRuleWidget(doc, primitive, 2);
        }
      }
    }

    if (selected) {
      ImGui::PopStyleColor();
    }

    ImGui::Separator();

    ImGui::PopID();
  }
}

optional<string> renderResourceReferenceCombobox(
    char const* label, char const* resourceType, string const& current) {
  auto* renderSystem = editorRenderSystem();
  auto* manager = renderSystem ? renderSystem->resourceManager() : nullptr;
  if (!manager) {
    ImGui::TextDisabled("%s: resource browser unavailable.", label);
    return nullopt;
  }

  auto resources = manager->getResourcesByType(resourceType);
  sort(resources.begin(), resources.end(), [](auto const& left, auto const& right) {
    return left->getQualifiedName() < right->getQualifiedName();
  });

  string display = current.empty() ? string("(none)") : current;
  for (auto const& resource : resources) {
    auto reference = worldResourceReference(*resource);
    if (current == reference || current == resource->getQualifiedName()) {
      display = resource->getQualifiedName();
      break;
    }
  }

  optional<string> selectedReference;
  ImGui::SetNextItemWidth(320.0f);
  if (ImGui::BeginCombo(label, display.c_str())) {
    if (resources.empty()) {
      ImGui::TextDisabled("No %s resources are available.", resourceType);
    }
    for (auto const& resource : resources) {
      auto reference = worldResourceReference(*resource);
      bool selected = current == reference ||
                      current == resource->getQualifiedName();
      if (ImGui::Selectable(resource->getQualifiedName().c_str(), selected)) {
        selectedReference = reference;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return selectedReference;
}

struct RunScriptExtraResourceEditorState {
  vector<string> model;
  vector<array<char, 512>> fields;
  array<char, 512> adding{};
};

struct RunScriptTextParameterEditorState {
  string model;
  string field;
};

int resizeRunScriptTextParameter(
    ImGuiInputTextCallbackData* data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    auto* text = static_cast<string*>(data->UserData);
    text->resize(data->BufTextLen);
    data->Buf = text->data();
  }
  return 0;
}

bool inputRunScriptTextParameter(char const* label, string* text) {
  return ImGui::InputText(
      label, text->data(), text->capacity() + 1,
      ImGuiInputTextFlags_CallbackResize, resizeRunScriptTextParameter, text);
}

void renderRunScriptParameters(
    editor::Document* doc, bw::core::Layer* layer,
    bw::core::RunScript* step) {
  auto const& definitions = step->getRuntime().getParameterDefinitions(
      step->getScriptName());
  if (definitions.empty()) return;

  ImGui::SeparatorText("Params");
  static map<bw::core::RunScript const*,
             map<string, RunScriptTextParameterEditorState>>
      textStates;

  for (auto const& definition : definitions) {
    ImGui::PushID(definition.name.c_str());
    auto value = step->getParameterValue(definition);
    if (!definition.accepts(value)) {
      value = definition.defaultValue;
      ImGui::TextColored(
          ImVec4{1.0f, 0.35f, 0.35f, 1.0f},
          "The serialized value is invalid for the current resource definition.");
    }

    bool changed = false;
    if (definition.type == bw::core::ScriptParameterType::String) {
      auto const& current = get<string>(value);
      if (definition.choices.empty()) {
        auto& state = textStates[step][definition.name];
        if (state.model != current) {
          state.model = current;
          state.field = current;
        }
        ImGui::SetNextItemWidth(300.0f);
        inputRunScriptTextParameter(definition.name.c_str(), &state.field);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          value = state.field;
          changed = true;
        }
      } else {
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::BeginCombo(definition.name.c_str(), current.c_str())) {
          for (auto const& choice : definition.choices) {
            bool const selected = choice == current;
            if (ImGui::Selectable(choice.c_str(), selected)) {
              value = choice;
              changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
      }
    } else if (definition.type ==
               bw::core::ScriptParameterType::Integer) {
      auto integer = get<int64_t>(value);
      ImGui::SetNextItemWidth(300.0f);
      if (ImGui::SliderScalar(
              definition.name.c_str(), ImGuiDataType_S64, &integer,
              &definition.integerMinimum, &definition.integerMaximum)) {
        value = integer;
        changed = true;
      }
    } else if (definition.type == bw::core::ScriptParameterType::Number) {
      auto number = get<double>(value);
      ImGui::SetNextItemWidth(300.0f);
      if (ImGui::SliderScalar(
              definition.name.c_str(), ImGuiDataType_Double, &number,
              &definition.numberMinimum, &definition.numberMaximum, "%.6g")) {
        value = number;
        changed = true;
      }
    } else {
      auto boolean = get<bool>(value);
      if (ImGui::Checkbox(definition.name.c_str(), &boolean)) {
        value = boolean;
        changed = true;
      }
    }

    if (changed) {
      transact(doc, CommandId::SetRunScriptParameterValue, [&] { setRunScriptParameterValue(doc, layer, step, definition.name, value); });
    }

    bool const hasSerializedValue =
        step->getParameterValues().contains(definition.name);
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasSerializedValue);
    if (ImGui::SmallButton("Revert to resource default")) {
      transact(doc, CommandId::ClearRunScriptParameterValue, [&] { clearRunScriptParameterValue(doc, layer, step, definition.name); });
    }
    ImGui::EndDisabled();
    ImGui::PopID();
  }
}

void renderRunScriptView(
    ViewContext& context, bw::core::RunScript* step) {
  auto* doc = context.doc;
  auto* layer = doc->getWorld()->getActiveLayer();

  static map<bw::core::RunScript const*, string> scriptErrors;
  if (auto selected = renderResourceReferenceCombobox(
          "Lua script", "LuaScript", step->getScriptName())) {
    string error;
    if (auto* renderSystem = editorRenderSystem();
        renderSystem && renderSystem->loadLuaScript(*selected, &error)) {
      scriptErrors[step].clear();
      transact(doc, CommandId::SetRunScriptScriptName, [&] { setRunScriptScriptName(doc, layer, step, *selected); });
    } else {
      scriptErrors[step] = error.empty() ? "Could not load the Lua script." : error;
    }
  }

  ImGui::BeginDisabled(!editorRenderSystem());
  if (ImGui::Button("Re-scan resources")) {
    string error;
    if (editorRenderSystem()->rescanLuaScripts(&error)) {
      scriptErrors[step].clear();
    } else {
      scriptErrors[step] =
          error.empty() ? "Could not re-scan Lua script resources." : error;
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(step->getScriptName().empty() || !editorRenderSystem());
  if (ImGui::Button("Reload script")) {
    string error;
    auto reloaded =
        editorRenderSystem()->reloadLuaScript(step->getScriptName(), &error);

    // ScriptRuntime rebuilds every Layer that names this script, replacing
    // its RunScript-owned Primitives. That derived change bypasses the undo
    // action path which normally invalidates the editor Arrangement, so make
    // both the selection and rendered geometry follow the rebuilt output.
    doc->revalidateSelection();
    regenerateWorldData(doc);

    if (reloaded) {
      scriptErrors[step].clear();
    } else {
      scriptErrors[step] = error.empty() ? "Could not reload the Lua script." : error;
    }
  }
  ImGui::EndDisabled();
  if (!scriptErrors[step].empty()) {
    ImGui::TextColored(
        ImVec4{1.0f, 0.35f, 0.35f, 1.0f}, "%s",
        scriptErrors[step].c_str());
  }

  renderRunScriptParameters(doc, layer, step);

  auto seed = step->getSeed();
  ImGui::SetNextItemWidth(220.0f);
  if (ImGui::InputScalar(
          "Seed", ImGuiDataType_U64, &seed, nullptr, nullptr, nullptr,
          ImGuiInputTextFlags_EnterReturnsTrue)) {
    transact(doc, CommandId::SetRunScriptSeed, [&] { setRunScriptSeed(doc, layer, step, seed); });
  }
  ImGui::SameLine();
  if (ImGui::Button("Reroll")) {
    static mt19937_64 randomSeed{random_device{}()};
    transact(doc, CommandId::SetRunScriptSeed, [&] { setRunScriptSeed(doc, layer, step, randomSeed()); });
  }

  ImGui::SeparatorText("Extra resources");
  ImGui::TextWrapped(
      "Resource names used inside the script but not otherwise visible in the World.");
  static map<bw::core::RunScript const*, RunScriptExtraResourceEditorState>
      extraStates;
  auto& extraState = extraStates[step];
  auto const& extraResources = step->getExtraResourceNames();
  if (extraState.model != extraResources) {
    extraState.model = extraResources;
    extraState.fields.clear();
    extraState.fields.resize(extraResources.size());
    for (size_t i = 0; i < extraResources.size(); ++i) {
      snprintf(extraState.fields[i].data(), extraState.fields[i].size(), "%s",
               extraResources[i].c_str());
    }
  }

  bool listChanged = false;
  for (size_t i = 0; i < extraState.fields.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputText(
        "##ExtraResource", extraState.fields[i].data(),
        extraState.fields[i].size());
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      auto names = extraResources;
      names[i] = extraState.fields[i].data();
      transact(doc, CommandId::SetRunScriptExtraResourceNames, [&] { setRunScriptExtraResourceNames(doc, layer, step, names); });
      listChanged = true;
    }
    ImGui::SameLine();
    if (!listChanged && ImGui::Button(ICON_FA_TRASH "##RemoveExtraResource")) {
      auto names = extraResources;
      names.erase(names.begin() + i);
      transact(doc, CommandId::SetRunScriptExtraResourceNames, [&] { setRunScriptExtraResourceNames(doc, layer, step, names); });
      listChanged = true;
    }
    ImGui::PopID();
    if (listChanged) break;
  }

  if (!listChanged) {
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputTextWithHint(
        "##AddExtraResource", "Resource name", extraState.adding.data(),
        extraState.adding.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(extraState.adding.front() == '\0');
    if (ImGui::Button("Add")) {
      auto names = extraResources;
      names.emplace_back(extraState.adding.data());
      extraState.adding.front() = '\0';
      transact(doc, CommandId::SetRunScriptExtraResourceNames, [&] { setRunScriptExtraResourceNames(doc, layer, step, names); });
    }
    ImGui::EndDisabled();
  }

  if (step->hasFailed()) {
    ImGui::SeparatorText("Failure");
    ImGui::TextWrapped("Message: %s", step->getFailureMessage().c_str());
    if (step->getFailureLineNumber()) {
      ImGui::Text("Line: %u", step->getFailureLineNumber());
    }
    if (!step->getFailureTraceback().empty()) {
      ImGui::TextUnformatted("Traceback:");
      ImGui::TextWrapped("%s", step->getFailureTraceback().c_str());
    }
  }
}

void renderTileMapView(ViewContext& context, bw::core::TileMap* tileMap) {
  auto* doc = context.doc;
  auto* layer = doc->getWorld()->getActiveLayer();

  auto const mapSize = tileMap->getMapSize();
  auto const mapLabel = format("{}", mapSize);
  ImGui::SetNextItemWidth(180.0f);
  if (ImGui::BeginCombo("Map size", mapLabel.c_str())) {
    for (auto size : {64u, 128u, 256u}) {
      auto const selected = size == mapSize;
      auto const label = format("{}", size);
      if (ImGui::Selectable(label.c_str(), selected)) {
        transact(doc, CommandId::SetTileMapMapSize, [&] {
          setTileMapMapSize(doc, layer, tileMap, size);
        });
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  auto const cellSize = tileMap->getCellSize();
  auto const cellLabel = format("{}", cellSize);
  ImGui::SetNextItemWidth(180.0f);
  if (ImGui::BeginCombo("Cell size", cellLabel.c_str())) {
    for (auto size : {2u, 4u, 8u, 16u, 32u}) {
      auto const selected = size == cellSize;
      auto const label = format("{}", size);
      if (ImGui::Selectable(label.c_str(), selected)) {
        transact(doc, CommandId::SetTileMapCellSize, [&] {
          setTileMapCellSize(doc, layer, tileMap, size);
        });
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::Text("Grid: %u x %u", tileMap->getWidth(), tileMap->getHeight());
  if (tileMap->isEnabled()) {
    ImGui::TextUnformatted("Click a cell in the World view to toggle it.");
  } else {
    ImGui::TextDisabled("Enable this step to edit its cells.");
  }
}

void renderLayerStepsView(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto world = doc->getWorld();
  auto* layer = world->getActiveLayer();
  auto numSteps = layer->getNumSteps();

  widgets::HelpMarker("Disabling a step and rebuilding removes its Primitives from this Layer; re-enabling restores them. The first step can be disabled but never removed, retyped, or reordered. The active step (radio button) is where Create/Edit Primitive writes.");

  widgets::HelpMarker("When off, the world view only shows Primitives from the active step; Primitives from every other step are hidden and contribute no geometry. When on, Primitives outside the active step render faded.");
  ImGui::SameLine();
  if (ImGui::Checkbox("Show all steps' Primitives##Layer", &settings.showAllStepPrimitives)) {
    // The filter reads the setting live, so what changed here is only which
    // Primitives it now admits: regenerate unconditionally, since a view the
    // user just asked for is not something a config flag should gate.
    world->getWorldDataGenerator()->refreshPrimitiveFilter();
  }

  auto activeStepIndex = layer->getActiveStepIndex();
  bool buildHalted = false;
  static map<bw::core::LayerBuildStep const*,
             pair<string, array<char, 256>>>
      stepNameStates;

  for (uint32_t i = 0; i < numSteps; ++i) {
    ImGui::PushID(i);

    // A step list mutation below invalidates numSteps and every later
    // step's index for the rest of this frame, so this loop stops as soon
    // as one happens; the next frame renders the fresh list.
    bool listChanged = false;

    auto* step = layer->getStep(i);
    bool enabled = step->isEnabled();

    // Not undoable: like World's active Layer, a Layer's active step is
    // ephemeral editor-authoring focus, never serialized.
    if (ImGui::RadioButton("##StepActive", i == activeStepIndex)) {
      layer->setActiveStep(i);
      if (doc->clonePlacementArmed()) cancelClonePlacement(doc);
      doc->disarmMeshDrawTool();
      doc->clearActiveMesh();
      doc->clearSelections();

      // The step filter is anchored on the active step, so moving it changes
      // which ordinary Primitives reach the fold when show-all is off - and
      // unconditionally changes whether a DefinePrefabs step's selected
      // Prefab is now the fold's sole content (Document.cpp), so this always
      // needs telling regardless of that setting.
      world->getWorldDataGenerator()->refreshPrimitiveFilter();
    }
    widgets::HelpMarker("Make this the step Create/Edit Primitive writes into.");
    ImGui::SameLine();

    ImGui::Text("%u :: %s", i, step->getType().c_str());
    if (step->hasFailed()) {
      ImGui::SameLine();
      ImGui::TextColored(
          ImVec4{1.0f, 0.3f, 0.3f, 1.0f}, "FAILED - NOT RUN");
    } else if (buildHalted) {
      ImGui::SameLine();
      ImGui::TextDisabled("NOT RUN");
    }
    ImGui::SameLine();

    if (widgets::ToggleButton("##StepEnabled", "Enabled", &enabled)) {
      transact(doc, CommandId::SetLayerBuildStepEnabled, [&] { setLayerBuildStepEnabled(doc, layer, i, enabled); });
    }

    auto& [nameModel, name] = stepNameStates[step];
    if (nameModel != step->getName()) {
      nameModel = step->getName();
      snprintf(name.data(), name.size(), "%s", nameModel.c_str());
    }
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint(
        "Step name", "Optional script lookup name", name.data(), name.size());
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      transact(doc, CommandId::SetLayerBuildStepName, [&] { setLayerBuildStepName(doc, layer, i, string(name.data())); });
    }

    if (i != 0) {
      ImGui::PushButtonRepeat(false);

      ImGui::SameLine();
      // Moving step 1 up would move it into the reserved index 0, so that
      // arrow is omitted rather than offered and rejected.
      if (i > 1) {
        if (ImGui::ArrowButton("##StepUp", ImGuiDir_Up)) {
          transact(doc, CommandId::MoveLayerBuildStep, [&] { moveLayerBuildStep(doc, layer, i, i - 1); });
          listChanged = true;
        }
        ImGui::SameLine();
      }

      if (!listChanged && i < numSteps - 1) {
        if (ImGui::ArrowButton("##StepDown", ImGuiDir_Down)) {
          transact(doc, CommandId::MoveLayerBuildStep, [&] { moveLayerBuildStep(doc, layer, i, i + 1); });
          listChanged = true;
        }
        ImGui::SameLine();
      }

      ImGui::PopButtonRepeat();

      if (!listChanged && ImGui::Button(ICON_FA_TRASH "##RemoveLayerStep")) {
        transact(doc, CommandId::RemoveLayerBuildStep, [&] { removeLayerBuildStep(doc, layer, i); });
        listChanged = true;
      }
    }

    ImGui::Separator();

    ImGui::PopID();

    buildHalted = buildHalted || step->hasFailed();
    if (listChanged) {
      break;
    }
  }

  auto const stepTypes = bw::core::LayerBuildStep::getRegisteredTypes();
  static string selectedStepType;
  if (find(stepTypes.begin(), stepTypes.end(), selectedStepType) == stepTypes.end() &&
      !stepTypes.empty()) {
    selectedStepType = stepTypes.front();
  }

  ImGui::TextUnformatted("Step type");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(180.0f);
  if (ImGui::BeginCombo("##StepType", selectedStepType.c_str())) {
    for (auto const& type : stepTypes) {
      bool const selected = type == selectedStepType;
      if (ImGui::Selectable(type.c_str(), selected)) {
        selectedStepType = type;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  ImGui::SameLine();
  if (ImGui::Button("Add Step") && !selectedStepType.empty()) {
    transact(doc, CommandId::AddLayerBuildStep, [&] { addLayerBuildStep(doc, layer, selectedStepType); });
  }
}

}  // namespace editor
