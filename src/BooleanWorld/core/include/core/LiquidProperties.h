#pragma once

#include <array>

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

  // The fraction of light absorbed over referenceDepth world units. Zero is
  // perfectly clear at every depth; values at or above one are clamped short
  // of one when converted to extinction so that coefficient stays finite.
  float opacity;
  float referenceDepth;

  // The colour left by absorption. It also biases extinction toward the
  // colours the liquid is not: a blue tint absorbs red faster than blue.
  std::array<float, 3> tint;
};

// The properties of the given liquid. Unknown values read back as Water
// rather than throwing, matching LiquidTypeFromName.
[[nodiscard]] LiquidProperties const& GetLiquidProperties(LiquidType type);

// Converts authored opacity at its reference depth to Beer-Lambert extinction
// coefficients. A neutral tint of {0.5, 0.5, 0.5} preserves the authored
// opacity in every channel; other tints bias absorption toward their missing
// colours.
[[nodiscard]] std::array<float, 3> CalculateLiquidExtinction(
    LiquidProperties const& properties);

// Returns the length of the eye-to-point segment that lies in liquid. Positions
// are x, y, z with y vertical. A surface height at or below its endpoint means
// that endpoint is dry; callers use negative infinity for the dry sentinel. A
// submerged eye supplies the
// governing liquid surface, while the point's own surface still decides
// whether the far end is wet.
[[nodiscard]] float CalculateLiquidPathLength(
    std::array<float, 3> const& eyePosition,
    std::array<float, 3> const& pointPosition,
    float eyeSurfaceHeight,
    float pointSurfaceHeight);

}  // namespace core
}  // namespace bw
