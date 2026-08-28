#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <mpp/Resource.h>
#include <mpp/UniformCollection.h>

// Rendering-only data that distinguishes one wall surface from another without
// changing the Sub-material it resolves.  `identity` is the complete stable
// value identity of the variant; equivalent values must use the same string.
// The payload is deliberately opaque to the world geometry layer, which owns
// neither GPU textures nor shader uniforms.
struct WallRenderVariant {
  std::string identity;
  uint32_t textureIndex{};
  mpp::ResourcePtr texture;
  std::function<void(mpp::UniformCollection&)> setUniforms;

  [[nodiscard]] bool operator==(WallRenderVariant const& other) const {
    return identity == other.identity;
  }
};

// A wall surface selects a Sub-material as usual, then optionally augments its
// render bucket with a surface-specific variant.  Horizontal surfaces never
// use this type, so their batching key remains unchanged.
struct WallRenderSurface {
  std::string subMaterialId;
  std::optional<WallRenderVariant> variant;
};
