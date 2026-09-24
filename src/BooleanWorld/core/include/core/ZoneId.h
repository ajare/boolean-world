#pragma once

#include <cstdint>
#include <string_view>

namespace bw::core {

// Reserved persistence identities. Never renumber or derive these from UI text.
// Every World implicitly owns these built-in Zones.
enum class ZoneId : uint32_t {
  Euclidean = 1,
  NegativeSpace = 2,
};

constexpr bool isKnownZone(ZoneId id) {
  return id == ZoneId::Euclidean || id == ZoneId::NegativeSpace;
}

constexpr std::string_view zoneSymbol(ZoneId id) {
  switch (id) {
    case ZoneId::Euclidean: return "euclidean";
    case ZoneId::NegativeSpace: return "negative_space";
  }
  return {};
}

}  // namespace bw::core
