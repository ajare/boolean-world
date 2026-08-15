#include "Defines.h"
#include "UiHelpers.h"

extern wp::Vector2 gViewOffset;
extern float gViewZoom;

namespace floored {

bool mouseInteractingWithBackground() {
  auto worldPos = getMouseWorldPosition();

  auto const& io = ImGui::GetIO();
  return !io.WantCaptureMouse;
}

wp::Vector2 getMouseWorldPosition() {
  auto mouseScreenPos = ImGui::GetMousePos();

  return {
      (mouseScreenPos.x + gViewOffset.x) - FE_WINDOW_WIDTH / 2.0f,
      ((FE_WINDOW_HEIGHT - mouseScreenPos.y) + gViewOffset.y) - FE_WINDOW_HEIGHT / 2.0f};
}

}  // namespace floored