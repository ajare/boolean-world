#pragma once

#include "core/LiquidType.h"

namespace bw {
namespace core {

// What a liquid is made of, as opposed to what the world puts it in or what
// swims through it. Everything here describes the liquid alone: how a body
// responds to it also depends on that body, and belongs with the body (see
// common/GameDefines.h's BW_PLAYER_DENSITY and BW_PLAYER_SWIM_ACCELERATION).
struct LiquidProperties {
  // Mass per unit volume, taking water as 1. Decides how much of a body's
  // weight the liquid carries: a body floats with its own density over this
  // one of its volume submerged, so a denser liquid floats the same body
  // higher, and a body denser than the liquid does not float at all.
  float density;

  // Linear drag on motion through the liquid, per second, applied in
  // proportion to how much of the body is actually immersed. This is how
  // thick the liquid feels, and it sets three things at once: how gently a
  // floating body drifts back up to its float height (buoyant acceleration
  // over this), how far a fast entry carries before the liquid arrests it
  // (roughly entry speed over this), and what speed a given swimming effort
  // achieves (that effort over this).
  float viscosity;
};

// The properties of the given liquid. Unknown values read back as Water
// rather than throwing, matching LiquidTypeFromName.
[[nodiscard]] LiquidProperties const& GetLiquidProperties(LiquidType type);

}  // namespace core
}  // namespace bw
