#pragma once

#include <array>
#include <string_view>

namespace bw {
namespace common {

// Technique indices are compiled into the procedural shader dispatch. This
// table is its matching display-name map; ProcMaterial owns all authorable
// Technique schemas and Sub-material data.
inline constexpr std::array<std::string_view, BW_MATERIAL_COUNT> TechniqueNames{{
    "Plain grey",
    "Marble",
    "Stone",
    "Slate",
    "Sandstone",
    "Limestone",
    "Basalt",
    "Obsidian",
    "Quartz / crystal",
    "Ore",
    "Rusted iron",
    "Galvanized steel",
    "Brushed metal",
    "Hammered metal",
    "Patinated copper",
    "Damascene steel",
    "Heat-treated metal",
    "Wood",
    "Bark",
    "Bone / ivory",
    "Leather",
    "Flesh",
    "Chitin / shell",
    "Coral",
    "Arcane crystal",
    "Energy stone",
    "Alien tissue",
    "Magical metal",
    "Solid cloud",
    "Holographic",
    "Corruption",
    "Frosted glass",
    "Brick",
    "Circuit board",
    "Banded gneiss",
    "Rock",
    "Mossy rock",
    "Wet rock",
    "Wood2",
}};

}  // namespace common
}  // namespace bw
