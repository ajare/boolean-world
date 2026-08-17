#pragma once

namespace bw::app {

// Controls for the world shader's level of detail, which trades texture
// detail away with distance from the viewer. Debug controls: they are here to
// be dragged about while the game runs, not configured.
struct LodSettings {
  // Off pins every fragment at full detail, as it was before there was a
  // level of detail at all.
  bool enabled{true};

  // Pushes the level of detail towards one end of its range: -1 is no detail
  // anywhere, +1 is full detail everywhere, 0 leaves distance to decide.
  float bias{0.0f};
};

}  // namespace bw::app
