#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <core/Arrangement.h>

#include "WallNormalMapRenderData.h"
#include "WallRenderVariant.h"

namespace {
void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b) { return std::abs(a - b) < 0.0001f; }

void physicalUvsUseCanonicalProjectionAndWorldElevation() {
  bw::core::arr::ArrangementWall wall{
      0, 6.0f, 14.0f, 0,
      bw::core::arr::ArrangementWallKind::Border, 8.0f, true,
      bw::core::WallNormalMapOverride::image("normal/directional.png", 4.0f,
                                             0.75f)};
  bw::core::arr::ArrangementWallOrientation orientation{
      {3.0f, 4.0f}, {6.0f, 8.0f}, {-0.8f, 0.6f}};
  auto uv = CalculateWallPhysicalUv(orientation, wall);
  require(near(uv.u0, 1.25f) && near(uv.u1, 2.5f) &&
              near(uv.minV, 1.5f) && near(uv.maxV, 3.5f),
          "mapped wall UVs were not physical canonical-tangent/elevation projections");

  wall.normalMapOverride = bw::core::WallNormalMapOverride::unset();
  uv = CalculateWallPhysicalUv(orientation, wall);
  require(near(uv.u0, 0) && near(uv.u1, 1) && near(uv.minV, 0) &&
              near(uv.maxV, 1),
          "Unset wall changed the legacy UV data");
}

void shaderComposesImageBeforeProceduralMaterialAndEmbossing() {
  std::ifstream input(BW_WORLD_PBR_SHADER);
  std::string shader((std::istreambuf_iterator<char>(input)), {});
  auto sample = shader.find("vec3 tangentNormal = texture");
  auto evaluate = shader.find("Material material = evaluateMaterial");
  auto emboss = shader.find("material.normal = embossSurface", evaluate);
  require(sample != std::string::npos && sample < evaluate && evaluate < emboss,
          "3D shader does not compose the sampled base normal before material evaluation and Embossing");
  require(shader.find("tangentNormal.xy *=") != std::string::npos &&
              shader.find("cross(shadingNormal, vec3(0.0, 1.0, 0.0))") !=
                  std::string::npos,
          "3D shader lost tangent-plane strength or the canonical wall frame");
}

void mappedAndUnmappedSurfacesHaveDistinctBucketIdentity() {
  WallRenderSurface unmapped{"same.sub-material", std::nullopt};
  WallRenderVariant mappedVariant{"normal-map-v1-directional"};
  WallRenderSurface mapped{"same.sub-material", mappedVariant};
  require(!unmapped.variant && mapped.variant &&
              mapped.variant->identity != std::string{},
          "mapped and unmapped walls cannot select distinct variant buckets");
}
}  // namespace

int main() {
  try {
    physicalUvsUseCanonicalProjectionAndWorldElevation();
    shaderComposesImageBeforeProceduralMaterialAndEmbossing();
    mappedAndUnmappedSurfacesHaveDistinctBucketIdentity();
    std::cout << "Wall normal-map render-data tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
