#include <core/Defines.h>
#include <core/Layer.h>

#include "Defines.h"
#include "UiHelpers.h"

extern wp::Vector2 gViewOffset;
extern float gViewZoom;
extern wp::Vector2 gWorldViewScreenOrigin;
extern wp::Vector2 gWorldViewSize;

namespace editor {
using namespace std;

bool mouseInteractingWithBackground() {
  auto worldPos = getMouseWorldPosition();

  auto const& io = ImGui::GetIO();
  if (io.WantCaptureMouse) {
    return false;
  }

  auto mouseScreenPos = ImGui::GetMousePos();
  wp::BoundingBox worldViewBounds(gWorldViewScreenOrigin, gWorldViewSize);
  return worldViewBounds.pointInside(mouseScreenPos.x, mouseScreenPos.y);
}

// The inverse of Render.cpp's worldToScreen(): screen -> canvas-local (undo
// the World window's screen origin) -> world (undo the pan/zoom/y-flip).
wp::Vector2 getMouseWorldPosition() {
  return screenToWorldPosition(ImGui::GetMousePos());
}

wp::Vector2 screenToWorldPosition(ImVec2 const& screenPos) {
  wp::Vector2 canvasPos{
      screenPos.x - gWorldViewScreenOrigin.x,
      screenPos.y - gWorldViewScreenOrigin.y};

  return {
      (canvasPos.x - gWorldViewSize.x / 2.0f) / gViewZoom + gViewOffset.x,
      gViewOffset.y - (canvasPos.y - gWorldViewSize.y / 2.0f) / gViewZoom};
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
  if (!doc->isActive() || !(settings.configFlags & flag)) {
    return;
  }
  doc->getWorld()->generateClipping(true);
}

void regenerateWorldData(editor::Document* doc) {
  if (!doc->isActive()) {
    return;
  }

  doc->getWorld()->generateClipping(true);
}

void applyStepVisibilityFilter(editor::Document* doc, Settings const& settings) {
  doc->setPrimitiveFilter(
      [&settings](bw::core::Layer const& layer, bw::core::Primitive const* primitive) {
        return primitiveParticipatesInEditorFold(layer, primitive, settings);
      });
}

}  // namespace editor