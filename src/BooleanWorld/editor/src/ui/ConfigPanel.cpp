#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

void renderConfigView(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto world = doc->getWorld();
  auto wdg = static_cast<bw::core::DynamicWorldDataGenerator*>(world->getWorldDataGenerator());

  if (ImGui::TreeNode("Options")) {
    int* configFlags = &settings.configFlags;

    ImGui::SeparatorText("Auto clipping");

    widgets::HelpMarker("Every undoable action regenerates when it commits; these are the paths that are not one.");
    ImGui::CheckboxFlags("On active layer change", configFlags, ED_CLIP_ON_ACTIVE_LAYER_CHANGE);
    ImGui::CheckboxFlags("On undo/redo", configFlags, ED_CLIP_ON_UNDO_REDO);

    ImGui::SeparatorText("Primitive vertices");

    int updateVertices = 0;

    if (wdg->getAlwaysUpdateVertices()) {
      updateVertices = 1;
    } else if (world->getAlwaysUpdateVertices()) {
      updateVertices = 2;
    }

    ImGui::SetNextItemWidth(256);

    if (ImGui::Combo("Position update", &updateVertices, "When not visible on clip\0Always on clip\0Every frame\0\0", 6)) {
      switch (updateVertices) {
        case 0:
          wdg->setAlwaysUpdateVertices(false);
          world->setAlwaysUpdateVertices(false);
          break;

        case 1:
          wdg->setAlwaysUpdateVertices(true);
          world->setAlwaysUpdateVertices(false);
          break;

        case 2:
          wdg->setAlwaysUpdateVertices(false);
          world->setAlwaysUpdateVertices(true);
          break;
      }
    }

    ImGui::TreePop();
    // ImGui::Spacing();
  }

  float generationStartInterval = wdg->getGenerationStartInterval();

  ImGui::SetNextItemWidth(128);
  if (ImGui::InputFloat("Generation start interval", &generationStartInterval)) {
    if (generationStartInterval >= 1.0f) {
      wdg->setGenerationStartInterval(generationStartInterval);
    }
  }

  ImGui::SameLine();

  bool scheduledGenRunning = wdg->isScheduledGenerationRunning();

  if (widgets::ToggleButton("ToggleScheduledGeneration", ICON_FA_ATOM, &scheduledGenRunning)) {
    if (scheduledGenRunning) {
      wdg->startGenerationSchedule(generationStartInterval);
    } else {
      wdg->stopGenerationSchedule();
    }
  }
}

void renderConfigPanel(ViewContext& context) {
  if (ImGui::Begin("Configuration")) {
    renderConfigView(context);
  }

  ImGui::End();
}


}  // namespace editor
