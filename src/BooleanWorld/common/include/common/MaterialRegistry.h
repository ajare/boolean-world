#pragma once

#include <array>
#include <string_view>
#include <tuple>

namespace bw {
namespace common {

using MaterialDefinition = std::tuple<std::string_view, uint32_t, std::array<float, 3>>;
using MaterialParameterDefinition = std::tuple<std::string_view, float, float, float>;

// Material name, param count, base colour. Indices 0 and 1 (Marble, Stone)
// are consumed by world_pbr.frag/world_pbr_2d.frag today - see MarbleParams
// and StoneParams in world_pbr.frag. Indices 2-36 name every other material
// evaluateMaterial() switches on there, in the same order, matching the
// display names StatePlayBooleanWorld.cpp's gWorldMaterialNames already
// uses for its debug material-override picker. Their param tables below are
// real and load-bearing for the editor (selectable, tunable, saved per
// Primitive), but not yet read by either shader - each material's shader
// function still uses its own hardcoded constants, same as every material
// did before Marble/Stone were wired up. Base colours are an approximate
// swatch for the editor's picker, not sampled from the shader.
inline constexpr std::array<MaterialDefinition, BW_MATERIAL_COUNT> MaterialNames = {{
    {"Marble", 8, {0.18f, 0.18f, 0.20f}},
    {"Stone", 3, {0.25f, 0.24f, 0.22f}},
    {"Slate", 2, {0.13f, 0.16f, 0.18f}},
    {"Sandstone", 2, {0.55f, 0.33f, 0.17f}},
    {"Limestone", 2, {0.65f, 0.62f, 0.51f}},
    {"Basalt", 2, {0.06f, 0.07f, 0.08f}},
    {"Obsidian", 2, {0.03f, 0.02f, 0.04f}},
    {"Quartz / crystal", 2, {0.72f, 0.82f, 0.88f}},
    {"Ore", 2, {0.30f, 0.28f, 0.24f}},
    {"Rusted iron", 2, {0.35f, 0.15f, 0.05f}},
    {"Galvanized steel", 2, {0.55f, 0.58f, 0.60f}},
    {"Brushed metal", 2, {0.55f, 0.57f, 0.59f}},
    {"Hammered metal", 2, {0.38f, 0.40f, 0.43f}},
    {"Patinated copper", 2, {0.30f, 0.45f, 0.35f}},
    {"Damascene steel", 2, {0.35f, 0.37f, 0.40f}},
    {"Heat-treated metal", 2, {0.40f, 0.30f, 0.35f}},
    {"Wood", 2, {0.38f, 0.19f, 0.07f}},
    {"Bark", 2, {0.15f, 0.07f, 0.03f}},
    {"Bone / ivory", 2, {0.72f, 0.64f, 0.47f}},
    {"Leather", 2, {0.25f, 0.10f, 0.05f}},
    {"Flesh", 2, {0.45f, 0.15f, 0.13f}},
    {"Chitin / shell", 2, {0.08f, 0.05f, 0.11f}},
    {"Coral", 2, {0.65f, 0.22f, 0.15f}},
    {"Arcane crystal", 2, {0.10f, 0.20f, 0.45f}},
    {"Energy stone", 2, {0.10f, 0.25f, 0.55f}},
    {"Alien tissue", 2, {0.20f, 0.30f, 0.15f}},
    {"Magical metal", 2, {0.35f, 0.30f, 0.55f}},
    {"Solid cloud", 2, {0.55f, 0.65f, 0.80f}},
    {"Holographic", 2, {0.35f, 0.40f, 0.50f}},
    {"Corruption", 2, {0.20f, 0.05f, 0.20f}},
    {"Frosted glass", 2, {0.82f, 0.88f, 0.90f}},
    {"Brick", 2, {0.48f, 0.22f, 0.13f}},
    {"Circuit board", 2, {0.10f, 0.20f, 0.12f}},
    {"Banded gneiss", 2, {0.35f, 0.32f, 0.28f}},
    {"Rock", 2, {0.30f, 0.29f, 0.26f}},
    {"Mossy rock", 2, {0.20f, 0.24f, 0.15f}},
    {"Wet rock", 2, {0.20f, 0.20f, 0.19f}},
}};

// Parameter name, min value, max value, default value. Defaults for Marble
// and Stone are the literal constants world_pbr.frag/world_pbr_2d.frag used
// to hardcode at each parameter's bind point - see MarbleParams/StoneParams
// in world_pbr.frag. Every other material follows the same discipline ahead
// of being wired up: "base_scale" is always that material's own worldPos
// scale constant (widened to a uniform [0.3, 3.0] since every extracted
// value already falls inside it), and the second parameter is the single
// most prominent tunable constant local to that material's own *Texture
// function - never one living in a function shared by several materials
// (geologyField, metalField, organicField, supernaturalField), so wiring it
// up later never risks disturbing a material that was not touched.
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
                                                                     {"stone_mix", 0.0f, 1.0f, 0.28f}}},
    // Slate
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.72f},
                                                                     {"rust_mix", 0.0f, 1.0f, 0.35f}}},
    // Sandstone
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.58f},
                                                                     {"grain_scale", 5.0f, 30.0f, 18.0f}}},
    // Limestone
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.66f},
                                                                     {"pore_mix", 0.0f, 1.0f, 0.38f}}},
    // Basalt
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.85f},
                                                                     {"vesicle_mix", 0.0f, 1.0f, 0.72f}}},
    // Obsidian
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.70f},
                                                                     {"inclusion_mix", 0.0f, 1.0f, 0.3f}}},
    // Quartz / crystal
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.62f},
                                                                     {"amethyst_mix", 0.0f, 1.0f, 0.72f}}},
    // Ore
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.72f},
                                                                     {"vein_scale", 1.0f, 10.0f, 4.5f}}},
    // Rusted iron
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.72f},
                                                                     {"rust_scale", 1.0f, 10.0f, 4.5f}}},
    // Galvanized steel
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.66f},
                                                                     {"facet_mix", 0.5f, 3.0f, 1.45f}}},
    // Brushed metal
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.82f},
                                                                     {"scratch_mix", 0.0f, 1.0f, 0.18f}}},
    // Hammered metal
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.72f},
                                                                     {"dent_scale", 1.0f, 10.0f, 4.2f}}},
    // Patinated copper
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.66f},
                                                                     {"exposed_scale", 1.0f, 10.0f, 2.8f}}},
    // Damascene steel
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.72f},
                                                                     {"layer_threshold", 0.0f, 1.0f, 0.32f}}},
    // Heat-treated metal
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.62f},
                                                                     {"oxide_mix", 0.0f, 1.0f, 0.72f}}},
    // Wood
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.54f},
                                                                     {"ring_scale", 5.0f, 30.0f, 18.0f}}},
    // Bark
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.62f},
                                                                     {"ridge_scale", 1.0f, 15.0f, 7.0f}}},
    // Bone / ivory
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.68f},
                                                                     {"pore_scale", 1.0f, 15.0f, 7.0f}}},
    // Leather
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.76f},
                                                                     {"wear_mix", 0.0f, 1.0f, 0.42f}}},
    // Flesh
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.60f},
                                                                     {"vein_mix", 0.0f, 1.0f, 0.72f}}},
    // Chitin / shell
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.67f},
                                                                     {"plate_scale", 1.0f, 10.0f, 3.2f}}},
    // Coral
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.66f},
                                                                     {"pore_mix", 0.0f, 1.0f, 0.80f}}},
    // Arcane crystal
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.64f},
                                                                     {"core_mix", 0.0f, 1.0f, 0.82f}}},
    // Energy stone
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.68f},
                                                                     {"energy_scale", 1.0f, 10.0f, 4.2f}}},
    // Alien tissue
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.67f},
                                                                     {"cell_scale", 1.0f, 10.0f, 3.5f}}},
    // Magical metal
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.72f},
                                                                     {"rune_scale", 0.5f, 5.0f, 1.7f}}},
    // Solid cloud
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.2f, 3.0f, 0.53f},
                                                                     {"density_threshold", 0.0f, 1.0f, 0.30f}}},
    // Holographic
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.75f},
                                                                     {"scan_scale", 5.0f, 60.0f, 35.0f}}},
    // Corruption
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.65f},
                                                                     {"spread_threshold", 0.0f, 1.0f, 0.36f}}},
    // Frosted glass
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 1.4f},
                                                                     {"frost_threshold", 0.0f, 1.0f, 0.22f}}},
    // Brick
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.95f},
                                                                     {"brick_height", 0.1f, 1.0f, 0.42f}}},
    // Circuit board
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 1.15f},
                                                                     {"fine_trace_mix", 0.0f, 1.0f, 0.55f}}},
    // Banded gneiss
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.68f},
                                                                     {"garnet_scale", 5.0f, 20.0f, 13.0f}}},
    // Rock
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.74f},
                                                                     {"mineral_scale", 1.0f, 15.0f, 7.5f}}},
    // Mossy rock
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.72f},
                                                                     {"moss_scale", 5.0f, 30.0f, 16.0f}}},
    // Wet rock
    std::array<MaterialParameterDefinition, BW_MATERIAL_PARAMS_MAX>{{{"base_scale", 0.3f, 3.0f, 0.74f},
                                                                     {"wetness_threshold", 0.0f, 1.0f, 0.24f}}},
};

}  // namespace common
}  // namespace bw