#pragma once

#include <string>
#include <cstdint>

#include "imgui.h"

// What is left to choose about auto-clipping. Everything that goes through an
// undoable action regenerates unconditionally when it commits (see
// Undo.cpp), so only the paths that are not one of those are a policy.
#define ED_CLIP_ON_ACTIVE_LAYER_CHANGE 0x0008
#define ED_CLIP_ON_UNDO_REDO 0x0020

namespace editor {

struct Settings {
  enum struct Style {
    Light,
    Dark,
    Classic
  };

  Style style{Style::Dark};

  float gridSize{32};

  bool showGrid{false};

  bool renderAnimatedPrimitives{true};
  bool renderTriangulation{false};
  bool renderWorldBorder{true};
  bool renderPrimitiveBorders{true};
  bool renderPrimitiveBounds{false};
  bool renderScaleInfluenceZones{true};
  bool renderAngleInfluenceZones{true};
  bool renderOrbitAngleInfluenceZones{true};
  bool renderOrbitDistanceInfluenceZones{true};
  bool renderTimeUpdateDistance{true};
  bool renderInfluenceEyes{false};
  bool renderPlayerView{true};
  bool renderMiniMap{false};
  bool renderTriggerLines{true};
  bool renderArrangementVertices{false};

  // When false, the world view only renders Primitives from the active
  // Layer's active LayerBuildStep and earlier ones; later steps' Primitives
  // are hidden entirely. Either way, non-active-step Primitives among those
  // shown render faded (see Render.cpp).
  bool showAllStepPrimitives{true};

  bool ghostActive{true};
  bool expertMode{false};

  bool showDebugPanel{false};
  bool showContextSensitiveHelpPanel{false};

  int configFlags{ED_CLIP_ON_ACTIVE_LAYER_CHANGE |
                  ED_CLIP_ON_UNDO_REDO};

  uint8_t activeLayer{0};

  // Render colours
  ImColor gridColour{0.35f, 0.35f, 0.35f, 0.5f};
  ImColor backgroundColour{0.2f, 0.3f, 1.0f, 1.0f};
  ImColor triangulationColour{0.9f, 0.6f, 0.1f, 1.0f};
  ImColor borderColour{1.0f, 0.7f, 0.2f, 1.0f};
  ImColor vertexColour{0.8f, 0.5f, 0.0f, 1.0f};
  ImColor staticBoundsColour{0.0f, 1.0f, 0.0f, 1.0f};
  ImColor animatedBoundsColour{0.0f, 1.0f, 1.0f, 1.0f};
  ImColor primitiveColour{0.5f, 0.5f, 0.5f, 1.0f};
  ImColor selectedPrimitiveColour{1.0f, 1.0f, 0.0f, 1.0f};
  ImColor hoveredPrimitiveColour{1.0f, 0.1f, 0.1f, 1.0f};
  ImColor ghostPrimitiveColour{0.0f, 1.0f, 1.0f, 1.0f};
  ImColor triggerLineColour{0.8f, 0.8f, 0.0f, 1.0f};
  ImColor influenceEyeColour{0.9f, 0.8f, 0.7f, 1.0f};
  ImColor transformOriginColour{0.7f, 0.9f, 0.7f, 1.0f};
  ImColor influenceColours[4] = {
      {1.0f, 0.3f, 0.3f, 1.0f},
      {0.3f, 1.0f, 0.3f, 1.0f},
      {0.3f, 0.3f, 1.0f, 1.0f},
      {1.0f, 0.3f, 1.0f, 1.0f}};
  ImColor timeUpdateDistColour{1.0f, 1.0f, 0.3f, 1.0f};
  ImColor playerProxyColour{1.0f, 0.5f, 0.0f, 1.0f};

  float triggerLineSelectionDistance{5.0f};
  float triggerLineHandleRadius{5.0f};
  float vertexRadius{5.0f};

  ImColor triggerLineRed{1.0f, 0.2f, 0.2f, 1.0f};
  ImColor triggerLineBlue{0.2f, 0.2f, 1.0f, 1.0f};
};

}  // namespace editor
