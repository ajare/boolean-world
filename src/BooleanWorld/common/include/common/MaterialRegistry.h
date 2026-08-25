#pragma once

#include <array>
#include <string_view>
#include <tuple>

namespace bw {
namespace common {

using MaterialDefinition = std::tuple<std::string_view, uint32_t, std::array<float, 3>>;
using MaterialParameterDefinition = std::tuple<std::string_view, float, float, float>;

// Material name, param count, base colour
inline constexpr std::array<MaterialDefinition, BW_MATERIAL_COUNT> MaterialNames = {{{"Marble", 8, {0.18f, 0.18f, 0.20f}},
                                                                                     {"Stone", 3, {0.25f, 0.24f, 0.22f}}}};

// Parameter name, min value, max value, default value. Defaults are the
// literal constants world_pbr.frag/world_pbr_2d.frag used to hardcode at
// each parameter's bind point, so a freshly authored Primitive renders
// identically to before these were made tunable - see MarbleParams and
// StoneParams in world_pbr.frag for what each one actually controls. Two
// ranges (veins_fine_scale, base_scale) were widened to fit that extracted
// default; the rest were already wide enough.
inline constexpr std::array MaterialParams{
    // Marble
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"warp_scale", 0.0f, 5.0f, 1.35f},
                                                                     {"veins_scale", 1.0f, 10.0f, 5.0f},
                                                                     {"veins_fine_scale", 5.0f, 20.0f, 12.0f},
                                                                     {"fine_detail_scale", 0.01f, 1.0f, 0.025f},
                                                                     {"light_warm_mix", 0.0f, 1.0f, 0.2f},
                                                                     {"vein_mix", 0.0f, 1.0f, 0.35f},
                                                                     {"cloudiness", 0.0f, 1.0f, 0.42f},
                                                                     {"fbm_scale", 0.01f, 1.0f, 0.72f}}},
    // Stone
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.5f, 3.0f, 0.82f},
                                                                     {"medium_scale", 5.0f, 10.0f, 8.0f},
                                                                     {"stone_mix", 0.0f, 1.0f, 0.28f}}}};

}  // namespace common
}  // namespace bw