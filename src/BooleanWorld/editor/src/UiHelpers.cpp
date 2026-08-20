#include <core/Defines.h>
#include <core/Layer.h>

#include "Defines.h"
#include "UiHelpers.h"

extern wp::Vector2 gViewOffset;

namespace editor {
using namespace std;

bool mouseInteractingWithBackground() {
  auto worldPos = getMouseWorldPosition();

  auto const& io = ImGui::GetIO();
  return !io.WantCaptureMouse;
}

wp::Vector2 getMouseWorldPosition() {
  auto mouseScreenPos = ImGui::GetMousePos();

  return {
      (mouseScreenPos.x + gViewOffset.x) - ED_WINDOW_WIDTH / 2.0f,
      ((ED_WINDOW_HEIGHT - mouseScreenPos.y) + gViewOffset.y) - ED_WINDOW_HEIGHT / 2.0f};
}

uint32_t getHoveredPrimitiveIndex(editor::Document* doc, Settings const& settings) {
  if (!mouseInteractingWithBackground()) {
    return ~0u;
  }

  return doc->getHoveredPrimitiveIndex(getMouseWorldPosition(), settings);
}

vector<uint32_t> getHoveredPrimitiveIndices(editor::Document* doc, Settings const& settings) {
  if (!mouseInteractingWithBackground()) {
    return vector<uint32_t>{};
  }

  return doc->getHoveredPrimitiveIndices(getMouseWorldPosition(), settings);
}

uint32_t getHoveredTriggerLineIndex(editor::Document* doc, Settings const& settings) {
  if (!mouseInteractingWithBackground()) {
    return ~0u;
  }

  return doc->getHoveredTriggerLineIndex(getMouseWorldPosition(), settings);
}

uint32_t getHoveredWorldVertexIndex(editor::Document* doc, Settings const& settings, bw::core::WorldData const* worldData) {
  if (!mouseInteractingWithBackground()) {
    return ~0u;
  }

  return (uint32_t)worldData->getNearestVertexIndex(getMouseWorldPosition(), 3);
}

void generateClipping(editor::Document* doc, Settings const& settings, int flag) {
  if (settings.configFlags & flag) {
    doc->getWorld()->generateClipping(true);
  }
}

void regenerateWorldData(editor::Document* doc) {
  if (!doc->isActive()) {
    return;
  }

  doc->getWorld()->generateClipping(true);
}

bool primitiveVisibleForActiveStep(
    bw::core::Layer const& layer,
    bw::core::Primitive const* primitive,
    Settings const& settings) {
  if (settings.showAllStepPrimitives) {
    return true;
  }

  if (primitive->getFlags() & BW_PRIMITIVE_GHOST_FLAG) {
    return true;
  }

  auto owningStepIndex = layer.getOwningStepIndex(primitive);

  return owningStepIndex == ~0u || owningStepIndex <= layer.getActiveStepIndex();
}

void applyStepVisibilityFilter(editor::Document* doc, Settings const& settings) {
  doc->setPrimitiveFilter(
      [&settings](bw::core::Layer const& layer, bw::core::Primitive const* primitive) {
        return primitiveVisibleForActiveStep(layer, primitive, settings);
      });
}

}  // namespace editor