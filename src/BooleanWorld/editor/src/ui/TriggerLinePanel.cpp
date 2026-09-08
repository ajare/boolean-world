#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

void renderCreateTriggerLineView(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto world = doc->getWorld();

  if (ImGui::Button("Create at ghost##CreateTriggerLine")) {
    transact(doc, "Create Trigger Line##CreateTriggerLine", [&] {
      auto ghost = world->getPrimitive(0);
      world->addTriggerLine(new bw::core::WorldTriggerLine(
          ghost->getPosition() - wp::Vector2(100, 0), ghost->getPosition() + wp::Vector2(100, 0)));
    });
  }
}

void renderEditTriggerLineView(ViewContext& context, uint32_t triggerLineIndex) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto world = doc->getWorld();
  auto triggerLine = world->getTriggerLine(triggerLineIndex);

  ImGui::Text("ID: %d", triggerLine->getId());

  ImGui::SeparatorText("Trigger counts");
  ImGui::TextColored(settings.triggerLineRed, "RED: %d", triggerLine->getTriggerCount(bw::core::WorldTriggerLineSide::Red));
  ImGui::TextColored(settings.triggerLineBlue, "BLUE: %d", triggerLine->getTriggerCount(bw::core::WorldTriggerLineSide::Blue));

  // Side
  int selectedSide = (int)triggerLine->getSide();

  widgets::HelpMarker("Which side(s) trigger on crossing.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::Combo("Side##EditTriggerLine", &selectedSide, "Red\0Blue\0Both\0\0", 6)) {
    auto side = (bw::core::WorldTriggerLineSide)selectedSide;
    transact(doc, "Set Trigger Line Side", [&] {
      setTriggerLineSide(doc, triggerLine, side);
    });
  }
}


}  // namespace editor
