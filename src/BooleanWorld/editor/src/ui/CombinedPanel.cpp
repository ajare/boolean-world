#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

void renderCombinedPanel(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto const* worldData = context.worldData;
  auto globalTime = context.globalTime;
  if (!doc->isActive()) {
    return;
  }

  if (ImGui::Begin("Editing")) {
    auto windowFlags = 0;

    // While the 3D preview holds the world viewport this panel carries its
    // surface authoring and nothing else: every editing header below acts on
    // structure the preview cannot see change, which is why they are frozen
    // for as long as it is open. Showing them greyed out would be noise.
    if (preview3DIsOpen()) {
      renderPreview3DSelectedSurface();
      ImGui::End();
      return;
    }

    if (ImGui::CollapsingHeader("World", nullptr, windowFlags)) {
      renderWorldView(context);
    }

    if (ImGui::CollapsingHeader("Layer", nullptr, windowFlags)) {
      renderLayerStepsView(context);
    }

    auto* activeLayer = doc->getWorld()->getActiveLayer();
    auto* defineTileMaps =
        dynamic_cast<bw::core::DefineTileMaps*>(activeLayer->getActiveStep());
    if (defineTileMaps) {
      if (ImGui::CollapsingHeader("TileMaps", nullptr, windowFlags)) {
        renderDefineTileMapsView(context, defineTileMaps);
      }
    } else if (auto* definePrefabs = dynamic_cast<bw::core::DefinePrefabs*>(activeLayer->getActiveStep())) {
      if (ImGui::CollapsingHeader("Prefabs", nullptr, windowFlags)) {
        renderPrefabsView(context, definePrefabs);
      }
      if (auto* prefab = definePrefabs->getSelectedPrefab()) {
        auto const label = format(
            "Prefab: {}###SelectedPrefab", prefab->getName());
        if (ImGui::CollapsingHeader(
                label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
          renderSelectedPrefabView(context, definePrefabs, prefab);
        }
      }
    } else if (auto* prefabField = dynamic_cast<bw::core::PrefabField*>(activeLayer->getActiveStep())) {
      if (ImGui::CollapsingHeader("Prefabs", nullptr, windowFlags)) {
        renderPrefabFieldView(context, prefabField);
      }
    } else if (auto* runScript = dynamic_cast<bw::core::RunScript*>(activeLayer->getActiveStep())) {
      if (ImGui::CollapsingHeader("Script", nullptr, windowFlags)) {
        renderRunScriptView(context, runScript);
      }
    }

    if (!defineTileMaps) {
      if (settings.mode == Settings::Mode::Primitive) {
        // Below Layer: creating a Primitive writes into the active Layer's active
        // step, so the choice of where comes before the making of what, and
        // editing one comes after both.
        if (ImGui::CollapsingHeader("Create Primitive", nullptr, windowFlags)) {
          renderCreatePrimitiveView(context);
        }

        // The header follows the view: no header where the view would have
        // nothing under it.
        if (hasEditablePrimitiveSelection(doc)) {
          if (ImGui::CollapsingHeader("Edit Primitive", nullptr, windowFlags)) {
            renderEditPrimitiveView(context);
          }
        }
      } else if (ImGui::CollapsingHeader("Mesh", nullptr, windowFlags)) {
        renderMeshView(context);
      }

      if (ImGui::CollapsingHeader("Clip Order", nullptr, windowFlags)) {
        renderPrimitiveOrderView(context);
      }

      if (ImGui::CollapsingHeader("Create Trigger Line", nullptr, windowFlags)) {
        renderCreateTriggerLineView(context);
      }

      auto selectedTriggerLineIndex = doc->getSelectedTriggerLineIndex();
      if (selectedTriggerLineIndex != ~0u) {
        if (ImGui::CollapsingHeader("Edit Trigger Line", nullptr, windowFlags)) {
          renderEditTriggerLineView(context, selectedTriggerLineIndex);
        }
      }

      if (ImGui::CollapsingHeader("Region under cursor", nullptr, windowFlags)) {
        renderArrangementFaceView(context);
      }
    }

    if (!getActionHistory().empty()) {
      if (ImGui::CollapsingHeader("History", nullptr, windowFlags)) {
        renderHistoryView(context);
      }
    }

    if (ImGui::CollapsingHeader("Configuration", nullptr, windowFlags)) {
      renderConfigView(context);
    }
  }

  ImGui::End();
}

}  // namespace editor
