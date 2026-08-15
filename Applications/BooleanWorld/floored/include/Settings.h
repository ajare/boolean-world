#pragma once

#include "imgui.h"

namespace floored {

struct Settings {
  enum struct Style {
    Light,
    Dark,
    Classic
  };

  enum struct EdgeRenderMode {
    None,
    Polygons,
    Graph
  };

  Style style{Style::Dark};
  EdgeRenderMode edgeRenderMode{EdgeRenderMode::None};

  bool renderFaces{true};
  bool renderGraphVertices{false};
  bool renderPrimitives{false};
  bool renderPrimitiveBounds{false};
  bool renderPrimitiveDebug{false};

  // Render colours
  ImColor graphVertexColour{1.0f, 1.0f, 1.0f};
  ImColor primitiveColour{0.35f, 0.35f, 0.35f};
  ImColor primitiveBoundsColour{0.2f, 0.3f, 1.0f};
};

}  // namespace floored
