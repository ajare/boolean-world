#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

namespace {

struct BuildVariableEditorState {
  bool adding = false;
  string addName;
  bw::core::BuildVariableType addType = bw::core::BuildVariableType::Integer;
  map<string, string> names;
  map<string, string> strings;
  map<string, string> stringModels;
  map<string, int64_t> integers;
  map<string, int64_t> integerModels;
  map<string, double> floats;
  map<string, double> floatModels;
  string error;
};

char const* const buildVariableTypes[]{"string", "integer", "float", "boolean"};

bw::core::BuildVariableType buildVariableTypeAt(int index) {
  return static_cast<bw::core::BuildVariableType>(index);
}

bool applyVariableEdit(
    Document* doc, CommandId command, function<bool(Document*)> edit,
    string* error) {
  try {
    if (transactUndoableActionAtomically(doc, command, move(edit))) {
      error->clear();
      return true;
    }
  } catch (exception const& exception) {
    *error = exception.what();
  }
  return false;
}

}  // namespace

void renderBuildVariablesEditor(ViewContext& context, bw::core::Layer* layer) {
  auto* doc = context.doc;
  auto world = doc->getWorld();
  auto const* key = layer ? static_cast<void const*>(layer)
                          : static_cast<void const*>(world.get());
  static map<void const*, BuildVariableEditorState> states;
  auto& state = states[key];

  ImGui::SeparatorText("Variables");
  auto const& local = layer ? layer->getBuildVariables() : world->getBuildVariables();
  auto effective = layer ? layer->getEffectiveBuildVariables() : local;

  for (auto const& [name, effectiveValue] : effective) {
    ImGui::PushID(name.c_str());
    auto localFound = local.find(name);
    bool const inherited = layer && localFound == local.end();
    auto const& value = inherited ? effectiveValue : localFound->second;

    if (inherited) {
      ImGui::TextDisabled("%s", name.c_str());
    } else {
      auto& draftName = state.names[name];
      if (draftName.empty() && name != "") draftName = name;
      ImGui::SetNextItemWidth(150.0f);
      bool const submitted = widgets::InputText(
          "##VariableName", &draftName,
          ImGuiInputTextFlags_EnterReturnsTrue);
      if ((submitted || ImGui::IsItemDeactivatedAfterEdit()) &&
          draftName != name) {
        auto oldName = name;
        auto newName = draftName;
        auto command = layer ? CommandId::RenameLayerBuildVariable
                             : CommandId::RenameWorldBuildVariable;
        if (applyVariableEdit(doc, command, [&](Document*) {
              if (layer) return renameLayerBuildVariable(doc, layer, oldName, newName);
              return renameWorldBuildVariable(doc, oldName, newName); }, &state.error)) {
          ImGui::PopID();
          break;
        }
      }
    }

    ImGui::SameLine();
    if (inherited) {
      ImGui::TextDisabled("%s", bw::core::buildVariableTypeName(
                                    bw::core::buildVariableType(value)));
    } else {
      auto type = bw::core::buildVariableType(value);
      auto selectedType = static_cast<int>(type);
      ImGui::SetNextItemWidth(90.0f);
      if (ImGui::BeginCombo("##VariableType", buildVariableTypes[selectedType])) {
        for (int i = 0; i < 4; ++i) {
          if (ImGui::Selectable(buildVariableTypes[i], i == selectedType)) {
            auto replacement = bw::core::defaultBuildVariableValue(buildVariableTypeAt(i));
            auto command = layer ? CommandId::SetLayerBuildVariable
                                 : CommandId::SetWorldBuildVariable;
            applyVariableEdit(doc, command, [&](Document*) {
              if (layer) return setLayerBuildVariable(doc, layer, name, replacement);
              return setWorldBuildVariable(doc, name, replacement); }, &state.error);
          }
        }
        ImGui::EndCombo();
      }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(inherited);
    bool changed = false;
    auto edited = value;
    if (auto const* text = get_if<string>(&value)) {
      bool const hasModel = state.stringModels.contains(name);
      auto& model = state.stringModels[name];
      auto& draft = state.strings[name];
      if (!hasModel || model != *text) {
        model = *text;
        draft = *text;
      }
      ImGui::SetNextItemWidth(180.0f);
      bool const submitted = widgets::InputText(
          "##VariableValue", &draft,
          ImGuiInputTextFlags_EnterReturnsTrue);
      if (submitted || ImGui::IsItemDeactivatedAfterEdit()) {
        edited = draft;
        changed = edited != value;
      }
    } else if (auto const* integer = get_if<int64_t>(&value)) {
      bool const hasModel = state.integerModels.contains(name);
      auto& model = state.integerModels[name];
      auto& draft = state.integers[name];
      if (!hasModel || model != *integer) {
        model = *integer;
        draft = *integer;
      }
      ImGui::SetNextItemWidth(180.0f);
      bool const submitted = ImGui::InputScalar(
          "##VariableValue", ImGuiDataType_S64, &draft, nullptr, nullptr,
          nullptr, ImGuiInputTextFlags_EnterReturnsTrue);
      if (submitted || ImGui::IsItemDeactivatedAfterEdit()) {
        edited = draft;
        changed = edited != value;
      }
    } else if (auto const* number = get_if<double>(&value)) {
      bool const hasModel = state.floatModels.contains(name);
      auto& model = state.floatModels[name];
      auto& draft = state.floats[name];
      if (!hasModel || model != *number) {
        model = *number;
        draft = *number;
      }
      ImGui::SetNextItemWidth(180.0f);
      bool const submitted = ImGui::InputScalar(
          "##VariableValue", ImGuiDataType_Double, &draft, nullptr, nullptr,
          "%.12g", ImGuiInputTextFlags_EnterReturnsTrue);
      if (submitted || ImGui::IsItemDeactivatedAfterEdit()) {
        edited = draft;
        changed = edited != value;
      }
    } else {
      auto draft = get<bool>(value);
      changed = ImGui::Checkbox("##VariableValue", &draft);
      edited = draft;
    }
    ImGui::EndDisabled();

    if (changed && !inherited) {
      auto command = layer ? CommandId::SetLayerBuildVariable
                           : CommandId::SetWorldBuildVariable;
      applyVariableEdit(doc, command, [&](Document*) {
        if (layer) return setLayerBuildVariable(doc, layer, name, edited);
        return setWorldBuildVariable(doc, name, edited); }, &state.error);
    }

    ImGui::SameLine();
    if (inherited) {
      if (ImGui::SmallButton("Override")) {
        applyVariableEdit(doc, CommandId::SetLayerBuildVariable, [&](Document*) { return setLayerBuildVariable(doc, layer, name, value); }, &state.error);
      }
      ImGui::SameLine();
      ImGui::TextDisabled("World");
    } else {
      if (ImGui::SmallButton(ICON_FA_TRASH "##DeleteVariable")) {
        auto command = layer ? CommandId::RemoveLayerBuildVariable
                             : CommandId::RemoveWorldBuildVariable;
        if (applyVariableEdit(doc, command, [&](Document*) {
              if (layer) return removeLayerBuildVariable(doc, layer, name);
              return removeWorldBuildVariable(doc, name); }, &state.error)) {
          ImGui::PopID();
          break;
        }
      }
      if (layer) {
        ImGui::SameLine();
        ImGui::TextDisabled("Layer");
      }
    }
    ImGui::PopID();
  }

  if (state.adding) {
    ImGui::SetNextItemWidth(150.0f);
    widgets::InputText("Name##AddVariable", &state.addName);
    ImGui::SameLine();
    auto typeIndex = static_cast<int>(state.addType);
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::BeginCombo("Type##AddVariable", buildVariableTypes[typeIndex])) {
      for (int i = 0; i < 4; ++i) {
        if (ImGui::Selectable(buildVariableTypes[i], i == typeIndex))
          state.addType = buildVariableTypeAt(i);
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add##ConfirmVariable")) {
      auto value = bw::core::defaultBuildVariableValue(state.addType);
      auto command = layer ? CommandId::SetLayerBuildVariable
                           : CommandId::SetWorldBuildVariable;
      if (applyVariableEdit(doc, command, [&](Document*) {
            if (layer) return setLayerBuildVariable(doc, layer, state.addName, value);
            return setWorldBuildVariable(doc, state.addName, value); }, &state.error)) {
        state.adding = false;
        state.addName.clear();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##AddVariable")) {
      state.adding = false;
      state.addName.clear();
      state.error.clear();
    }
  } else if (ImGui::Button("Add Variable")) {
    state.adding = true;
    state.addName.clear();
    state.addType = bw::core::BuildVariableType::Integer;
  }

  if (!state.error.empty()) {
    ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "%s", state.error.c_str());
  }
}

void renderWorldView(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto world = doc->getWorld();

  // Name
  string worldName = world->getName();

  ImGui::SetNextItemWidth(192);
  if (widgets::InputText(
          "Name##World", &worldName, ImGuiInputTextFlags_EnterReturnsTrue)) {
    transact(doc, CommandId::SetWorldName, [&] { setWorldName(doc, worldName); });
  }

  // Description
  string worldDesc = world->getDescription();

  if (widgets::InputTextMultiline(
          "Description##World", &worldDesc, ImVec2(512, 96))) {
    // Don't make this transactional as every character change will create an undo state
    // transact(doc, CommandId::SetWorldDescription,
    //          [&] { setWorldDescription(doc, worldDesc); });
    setWorldDescription(doc, worldDesc);
    doc->setModified(true);
  }

  renderBuildVariablesEditor(context);

  // Player start angle
  float playerStartAngle = wp::MathsUtils::radians(world->getPlayerStartAngle());

  widgets::HelpMarker("Set the starting angle of the player.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderAngle("PlayerStartAngle##World", &playerStartAngle, 0, 360)) {
    setPlayerStartAngle(doc, wp::MathsUtils::degrees(playerStartAngle));
  }

  if (ImGui::IsItemActivated()) {
    beginTransaction(doc, CommandId::SetPlayerStartAngle, 0.0f);
  } else if (ImGui::IsItemDeactivatedAfterEdit()) {
    commitUndoableAction(doc, format("Set Player start angle to {}", world->getPlayerStartAngle()));
  } else if (ImGui::IsItemDeactivated()) {
    abandonUndoableAction(doc);
  }

  // Player start position
  wp::Vector2 const& playerStartPos = world->getPlayerStartPosition();

  widgets::HelpMarker("Set the starting position of the player.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  float pPosition[2] = {playerStartPos.x, playerStartPos.y};

  if (ImGui::InputFloat2("PlayerStartPos##World", pPosition)) {
    wp::Vector2 position{pPosition[0], pPosition[1]};

    transact(doc, CommandId::SetPlayerStartPosition, [&] { setPlayerStartPosition(doc, position); });
  }

  // Wedges
  static bw::core::World* wedgeDraftWorld = nullptr;
  static bw::core::WedgeGenerationParameters wedgeDraft;
  static bw::core::WedgeGenerationParameters wedgeSource;
  static bool wedgeDraftModified = false;
  static string wedgeSettingsError;
  auto const& currentWedgeSettings = world->getWedgeGenerationParameters();
  if (wedgeDraftWorld != world.get() ||
      (!wedgeDraftModified && currentWedgeSettings != wedgeSource)) {
    wedgeDraftWorld = world.get();
    wedgeDraft = currentWedgeSettings;
    wedgeSource = currentWedgeSettings;
    wedgeDraftModified = false;
    wedgeSettingsError.clear();
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Border Wedges");
  wedgeDraftModified |=
      ImGui::Checkbox("Enabled##WorldWedges", &wedgeDraft.enabled);
  auto wedgeQuality = int(wedgeDraft.quality);
  if (ImGui::InputInt("Quality##WorldWedges", &wedgeQuality, 1, 1)) {
    wedgeDraft.quality =
        wedgeQuality < 0 ? std::numeric_limits<uint32_t>::max()
                         : uint32_t(wedgeQuality);
    wedgeDraftModified = true;
  }
  wedgeDraftModified |= ImGui::InputFloat(
      "Floor average number per unit distance##WorldWedges",
      &wedgeDraft.floorWedgesPerUnitDistance);
  wedgeDraftModified |= ImGui::InputFloat(
      "Ceiling average number per unit distance##WorldWedges",
      &wedgeDraft.ceilingWedgesPerUnitDistance);
  wedgeDraftModified |= ImGui::InputFloat(
      "Corner probability##WorldWedges",
      &wedgeDraft.cornerWedgeProbability);
  float reach[2]{wedgeDraft.minimumReach, wedgeDraft.maximumReach};
  if (ImGui::InputFloat2("Reach min/max##WorldWedges", reach)) {
    wedgeDraft.minimumReach = reach[0];
    wedgeDraft.maximumReach = reach[1];
    wedgeDraftModified = true;
  }
  float dropDown[2]{
      wedgeDraft.minimumDropDownHeight,
      wedgeDraft.maximumDropDownHeight};
  if (ImGui::InputFloat2("Vertical extent min/max##WorldWedges", dropDown)) {
    wedgeDraft.minimumDropDownHeight = dropDown[0];
    wedgeDraft.maximumDropDownHeight = dropDown[1];
    wedgeDraftModified = true;
  }
  float projection[2]{
      wedgeDraft.minimumProjectionDepth,
      wedgeDraft.maximumProjectionDepth};
  if (ImGui::InputFloat2("Projection min/max##WorldWedges", projection)) {
    wedgeDraft.minimumProjectionDepth = projection[0];
    wedgeDraft.maximumProjectionDepth = projection[1];
    wedgeDraftModified = true;
  }
  float cornerReach[2]{
      wedgeDraft.minimumCornerReach,
      wedgeDraft.maximumCornerReach};
  if (ImGui::InputFloat2(
          "Corner reach min/max##WorldWedges", cornerReach)) {
    wedgeDraft.minimumCornerReach = cornerReach[0];
    wedgeDraft.maximumCornerReach = cornerReach[1];
    wedgeDraftModified = true;
  }
  float cornerVertical[2]{
      wedgeDraft.minimumCornerVerticalExtent,
      wedgeDraft.maximumCornerVerticalExtent};
  if (ImGui::InputFloat2(
          "Corner vertical min/max##WorldWedges", cornerVertical)) {
    wedgeDraft.minimumCornerVerticalExtent = cornerVertical[0];
    wedgeDraft.maximumCornerVerticalExtent = cornerVertical[1];
    wedgeDraftModified = true;
  }
  ImGui::BeginDisabled(!wedgeDraftModified);
  if (ImGui::Button("Apply Wedge settings##WorldWedges")) {
    if (transactUndoableActionAtomically(doc, CommandId::SetWorldWedgeGenerationParameters, [&](Document* doc) { return setWorldWedgeGenerationParameters(doc, wedgeDraft); })) {
      wedgeSource = world->getWedgeGenerationParameters();
      wedgeDraft = wedgeSource;
      wedgeDraftModified = false;
      wedgeSettingsError.clear();
    } else {
      wedgeSettingsError =
          "Wedge dimensions must be finite, positive, and ordered; frequencies and probability must be between zero and one, and quality must be between zero and three.";
    }
  }
  ImGui::EndDisabled();
  if (!wedgeSettingsError.empty()) {
    ImGui::TextColored(ImVec4(1, 0.35f, 0.35f, 1), "%s",
                       wedgeSettingsError.c_str());
  }

  // Layers
  ImGui::Separator();
  widgets::HelpMarker("Selecting a Layer here also makes it the active Layer, so Create/Edit Primitive writes into it.");
  ImGui::SameLine();
  ImGui::Text("Layers");

  auto* activeLayer = world->getActiveLayer();
  for (auto* layer : world->getLayers()) {
    ImGui::PushID((int)layer->getId());

    if (ImGui::Selectable(layer->getName().c_str(), layer == activeLayer)) {
      world->setActiveLayer(layer);
    }

    ImGui::PopID();
  }
}

}  // namespace editor
