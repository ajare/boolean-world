#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

void renderContextSensitiveHelp(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto windowFlags = 0;

  if (ImGui::Begin("Context Help")) {
    if (doc->isActive()) {
      auto const& io = ImGui::GetIO();

      auto hoveredPrimitiveIndex = editor::getHoveredPrimitiveIndex(doc, settings);
      auto const& selectedPrimitiveIndices = doc->getSelectedPrimitiveIndices();

      //
      // Left mouse button
      //
      string transformAction;

      int modifierMask = (io.KeyCtrl ? 1 : 0) + (io.KeyShift ? 2 : 0) + (io.KeyAlt ? 4 : 0);
      switch (modifierMask) {
        case 0:
          if (hoveredPrimitiveIndex != ~0u) {
            transformAction = "select primitive";
          }
          break;
        case 1:
          if (hoveredPrimitiveIndex != ~0u) {
            if (selectedPrimitiveIndices.find(hoveredPrimitiveIndex) != selectedPrimitiveIndices.end()) {
              transformAction = "deselect primitive";
            } else {
              transformAction = "select primitive";
            }
          }
          break;
        case 2:
          if ((hoveredPrimitiveIndex != ~0u) || !selectedPrimitiveIndices.empty()) {
            transformAction = "scale primitive(s)";
          }
          break;
        case 3:
          break;
        case 4:
          if ((hoveredPrimitiveIndex != ~0u) || !selectedPrimitiveIndices.empty()) {
            transformAction = "rotate primitive(s)";
          }
          break;
        case 5:
          break;
        case 6:
          if ((hoveredPrimitiveIndex != ~0u) || !selectedPrimitiveIndices.empty()) {
            transformAction = "scale and rotate primitive(s)";
          }
          break;
        case 7:
          break;
      }

      if (transformAction != "") {
        ImGui::Text("LMB: %s", transformAction.c_str());
      }
    }
  }

  ImGui::End();
}


}  // namespace editor
