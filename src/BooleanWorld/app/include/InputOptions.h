#pragma once

namespace bw::app {

// Input settings the launcher hands to the game DLL at load time. They apply
// to controlling the player in the 3d world only - ImGui menus and the other
// 2d interfaces read the mouse straight from the platform layer and are
// deliberately left unscaled.
struct InputOptions {
  // Multiplier on raw mouse motion when turning the player's view. The
  // default matches the flat scale the launcher used to ask SDL for, so turn
  // speed is unchanged from when this was fixed in the platform layer.
  float mouseSensitivity{0.3f};
};

}  // namespace bw::app
