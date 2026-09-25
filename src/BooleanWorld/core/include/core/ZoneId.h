#pragma once

#include <cstdint>
#include <string_view>

namespace bw::core {

// Reserved persistence identities. Never renumber or derive these from UI text.
// Every World implicitly owns these built-in Zones.
enum class ZoneId : uint32_t {
  Euclidean = 1,
  NegativeSpace = 2,
  Phantom = 3,
};

// Runtime generated-surface strategy (historical wall API name). Geometry and
// shadow participation are unchanged. Euclidean Liquid is the deliberate
// two-sided interface exception to Omitted.
enum class WallBackFaceTreatment : int32_t { Omitted = 0, MatteWhite = 1 };

constexpr WallBackFaceTreatment wallBackFaceTreatment(ZoneId id) {
  switch (id) {
    case ZoneId::Euclidean:
    case ZoneId::Phantom: return WallBackFaceTreatment::Omitted;
    case ZoneId::NegativeSpace: return WallBackFaceTreatment::MatteWhite;
  }
  return WallBackFaceTreatment::Omitted;
}

constexpr bool isKnownZone(ZoneId id) {
  return id == ZoneId::Euclidean || id == ZoneId::NegativeSpace || id == ZoneId::Phantom;
}

constexpr std::string_view zoneSymbol(ZoneId id) {
  switch (id) {
    case ZoneId::Euclidean: return "euclidean";
    case ZoneId::NegativeSpace: return "negative_space";
    case ZoneId::Phantom: return "phantom";
  }
  return {};
}

}  // namespace bw::core
