#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

void renderWorldView(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto world = doc->getWorld();

  // Name
  string worldName = world->getName();

  ImGui::SetNextItemWidth(192);
  if (widgets::InputText(
          "Name##World", &worldName, ImGuiInputTextFlags_EnterReturnsTrue)) {
    transact(doc, "Set World name", [&] { setWorldName(doc, worldName); });
  }

  // Description
  string worldDesc = world->getDescription();

  if (widgets::InputTextMultiline(
          "Description##World", &worldDesc, ImVec2(512, 96))) {
    // Don't make this transactional as every character change will create an undo state
    // transact(doc, "Set World description",
    //          [&] { setWorldDescription(doc, worldDesc); });
    setWorldDescription(doc, worldDesc);
    doc->setModified(true);
  }

  // Player start angle
  float playerStartAngle = wp::MathsUtils::radians(world->getPlayerStartAngle());

  widgets::HelpMarker("Set the starting angle of the player.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderAngle("PlayerStartAngle##World", &playerStartAngle, 0, 360)) {
    setPlayerStartAngle(doc, wp::MathsUtils::degrees(playerStartAngle));
  }

  if (ImGui::IsItemActivated()) {
    beginTransaction(doc, "", 0.0f);
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

    transact(doc, "Set Primitive Position", [&] { setPlayerStartPosition(doc, position); });
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
    if (transactUndoableActionAtomically(doc, "Set World Wedge settings", [&](Document* doc) { return setWorldWedgeGenerationParameters(doc, wedgeDraft); })) {
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
