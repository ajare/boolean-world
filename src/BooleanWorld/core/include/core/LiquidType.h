#pragma once

#include <cstdint>
#include <string>

namespace bw {
namespace core {

// The kind of liquid a Union Primitive's authored Liquid level pours into
// the world - see ComputeLiquidLevels. Only one value exists today; the type
// also selects the liquid surface's render material - see
// LiquidMaterialIndex below and core/Defines.h's BW_WATER_MATERIAL_INDEX.
enum class LiquidType : int32_t {
  Water = 0,
};

inline constexpr int32_t LiquidTypeCount = 1;

// The stable name written to and read from YAML, and shown in the editor's
// liquid type picker. Unknown input reads back as Water rather than
// throwing, so a World authored against a newer build still loads.
[[nodiscard]] char const* LiquidTypeName(LiquidType type);
[[nodiscard]] LiquidType LiquidTypeFromName(std::string const& name);

// The reserved MATERIAL_INDEX (core/Defines.h) a liquid surface of this type
// renders with.
[[nodiscard]] uint32_t LiquidMaterialIndex(LiquidType type);

}  // namespace core
}  // namespace bw
