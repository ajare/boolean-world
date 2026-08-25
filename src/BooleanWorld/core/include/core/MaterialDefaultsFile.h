#pragma once

#include <cstdint>
#include <string>

namespace bw {
namespace core {

// Optionally overrides bw::common::MaterialParams' min/max/default at
// runtime, read from a "Materials" section at the top level of Game.yaml
// (a sibling of Video/Game/Audio/Input, not nested under Game - Launcher's
// own ProgramOptions parser validates Game's children strictly and does not
// know about this section at all; this reads the same file independently).
// Both the editor and the game load the same file this way, so retuning a
// material's tuning only needs an edit to Game.yaml, not a rebuild.
//
// Materials and parameters are matched by name against bw::common::
// MaterialNames/MaterialParams, not position, so the file only needs to
// mention what it wants to override. A missing file, missing section, or a
// material/parameter absent from it all fall back silently to the compiled-
// in bw::common::MaterialParams value - this is an optional retuning layer,
// not a second mandatory source of truth.
//
// Expected shape:
//   Configuration:
//     Materials:
//       - name: Marble
//         params:
//           - name: warp_scale
//             min: 0.0
//             max: 5.0
//             default: 1.35
//
// Not thread-safe: call once at startup before any other thread reads the
// accessors below.
void loadMaterialDefaultsFile(std::string const& path);

// Reverts every material/parameter to bw::common::MaterialParams. Exists so
// tests do not leak state into one another.
void clearMaterialDefaultOverrides();

// These three answer from the loaded override when materialIndex/paramIndex
// has one, and from bw::common::MaterialParams otherwise - callers do not
// need to check separately. Out-of-range indices answer 0.0f, matching how
// an absent Primitive material already reads as zeroed params elsewhere.
[[nodiscard]] float materialParamMinimum(uint32_t materialIndex, uint32_t paramIndex);
[[nodiscard]] float materialParamMaximum(uint32_t materialIndex, uint32_t paramIndex);
[[nodiscard]] float materialParamDefault(uint32_t materialIndex, uint32_t paramIndex);

}  // namespace core
}  // namespace bw
