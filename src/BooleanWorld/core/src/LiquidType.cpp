#include "core/LiquidType.h"

#include <array>
#include <utility>

#include "core/Defines.h"

namespace bw {
namespace core {

using namespace std;

namespace {

constexpr array<pair<LiquidType, char const*>, LiquidTypeCount> typeNames{{
    {LiquidType::Water, "Water"},
}};

}  // namespace

char const* LiquidTypeName(LiquidType type) {
  for (auto const& [value, name] : typeNames) {
    if (value == type) {
      return name;
    }
  }
  return "Water";
}

LiquidType LiquidTypeFromName(string const& name) {
  for (auto const& [value, typeName] : typeNames) {
    if (name == typeName) {
      return value;
    }
  }
  return LiquidType::Water;
}

uint32_t LiquidMaterialIndex(LiquidType type) {
  switch (type) {
    case LiquidType::Water:
      return BW_WATER_MATERIAL_INDEX;
  }
  return BW_WATER_MATERIAL_INDEX;
}

}  // namespace core
}  // namespace bw
