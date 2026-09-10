#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <mpp/Resource.h>
#include <mpp/UniformCollection.h>

#include <core/SurfaceMaterialReference.h>

// Rendering-only data that distinguishes one wall surface from another without
// changing the Surface material it resolves. `identity` is the complete stable
// value identity of the variant; equivalent values must use the same string.
// The payload is deliberately opaque to the world geometry layer, which owns
// neither GPU textures nor shader uniforms.
struct WallRenderVariant {
  std::string identity;
  // Sampler reflection order changes with shader specialization (for example,
  // enabling point shadows places POINT_SHADOW_MAP before TEX1). Bind by the
  // declared sampler name rather than assuming a fixed texture unit.
  std::string textureSampler;
  mpp::ResourcePtr texture;
  std::function<void(mpp::UniformCollection&)> setUniforms;

  // The wall mask's second sampler (TEX2) and its per-batch uniforms. A
  // wall without a mask leaves these empty: the renderer binds its owned
  // 1x1 zero mask texture and passes the primary parameters as the blend
  // set so the shader contract stays uniform.
  std::string maskTextureSampler;
  mpp::ResourcePtr maskTexture;
  std::function<void(mpp::UniformCollection&)> setMaskUniforms;

  [[nodiscard]] bool operator==(WallRenderVariant const& other) const {
    return identity == other.identity;
  }
};

// A wall surface selects its tagged Surface material, then optionally augments
// its render bucket with a surface-specific image variant. Horizontal surfaces
// never use this type, so their batching key remains unchanged.
struct WallRenderSurface {
  bw::core::SurfaceMaterialReference material;
  std::optional<WallRenderVariant> variant;
  std::string embossPresetId;
};
