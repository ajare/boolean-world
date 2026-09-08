#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

void renderArrangementFaceView(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto const* worldData = context.worldData;
  if (!worldData) {
    return;
  }
  auto faceIndex = worldData->getContainingFaceIndex(getMouseWorldPosition());
  if (faceIndex == ~0u) {
    ImGui::Text("Exterior");
    return;
  }
  auto const& arrangement = worldData->getArrangement();
  auto const& face = arrangement.faces[faceIndex];
  ImGui::Text("Face: %u", faceIndex);
  ImGui::Text("Primitive: %u", face.primitiveIndex);
  auto properties = arrangement.palette[face.paletteIndex];
  ImGui::Text(
      "Floor / ceiling: %.2f / %.2f",
      properties.floorZ,
      properties.ceilingZ);
  renderPrimitivePropertySet(&properties, false, doc, settings);
}


}  // namespace editor
