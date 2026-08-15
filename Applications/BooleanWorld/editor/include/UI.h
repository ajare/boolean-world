#pragma once

#include <functional>
#include <string>

#include <willpower/common/Vector2.h>

#include "Document.h"
#include "Settings.h"

namespace editor {

struct MouseButtonStatus {
  enum Button {
    Left,
    Right
  };

  enum struct State {
    Unavailable,
    Clicked,
    Released,
    Down
  };

  State state[2];
  bool dragging[2];
  ImVec2 startDragPos[2];
  ImVec2 dragDelta[2];
};

void renderWidgets(editor::Document* doc, editor::Settings& settings, bw::core::WorldData const* worldData, double globalTime);

}  // namespace editor