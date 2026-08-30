#pragma once

#include <bit>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

#include <core/WallMaskOverride.h>

// Rendering-only identity for a wall mask Image payload. Mirrors the wall
// normal map's identity: the complete authored value - resource, channel, and
// the full blend-parameter array - is encoded so that two masks with any
// differing field select distinct render buckets.
[[nodiscard]] inline std::string maskIdentity(
    bw::core::WallMaskOverride::ImageData const& image) {
  std::ostringstream result;
  result << "mask-v1-";
  for (auto byte : image.resourceName) {
    result << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(static_cast<unsigned char>(byte));
  }
  result << '-' << std::hex << static_cast<unsigned>(image.channel);
  for (auto parameter : image.blendParameters) {
    result << '-' << std::hex << std::bit_cast<uint32_t>(parameter);
  }
  for (auto component : image.blendColour) {
    result << '-' << std::hex << std::bit_cast<uint32_t>(component);
  }
  return result.str();
}

// The complete wall-image bucket identity for a wall carrying a normal map
// and, optionally, a mask. The mask identity's "mask-v1-" prefix cannot occur
// inside the normal-map identity's hex encoding, so plain concatenation is
// unambiguous and keeps the two identities individually recoverable.
[[nodiscard]] inline std::string wallImageVariantIdentity(
    std::string const& normalMapIdentity,
    std::optional<bw::core::WallMaskOverride::ImageData> const& mask) {
  if (!mask) {
    return normalMapIdentity;
  }
  return normalMapIdentity + maskIdentity(*mask);
}
