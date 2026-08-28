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

void physicalUvsRemainContinuousAcrossSplitsAndResetAtCorners() {
  auto mapped = bw::core::WallNormalMapOverride::image(
      "normal/directional.png", 4.0f, 1.0f);
  bw::core::arr::ArrangementWall wall{
      0, -8.0f, 8.0f, 0,
      bw::core::arr::ArrangementWallKind::Border, 16.0f, true, mapped};
  auto first = CalculateWallPhysicalUv(
      bw::core::arr::ArrangementWallOrientation{
          {-12.0f, -4.0f}, {-4.0f, -4.0f}, {0.0f, 1.0f}},
      wall);
  auto second = CalculateWallPhysicalUv(
      bw::core::arr::ArrangementWallOrientation{
          {-4.0f, -4.0f}, {8.0f, -4.0f}, {0.0f, 1.0f}},
      wall);
  require(near(first.u1, second.u0) && near(first.minV, -2.0f),
          "physical UVs discontinuously restarted on a negative-coordinate split");

  auto corner = CalculateWallPhysicalUv(
      bw::core::arr::ArrangementWallOrientation{
          {-4.0f, -4.0f}, {-4.0f, 8.0f}, {-1.0f, 0.0f}},
      wall);
  require(near(corner.u0, -1.0f) && near(corner.u1, 2.0f),
          "a corner did not establish its own canonical tangent frame");
}

void chippedWallRemainderKeepsPhysicalUvAnchoring() {
  auto mapped = bw::core::WallNormalMapOverride::image(
      "normal/directional.png", 4.0f, 1.0f);
  bw::core::arr::ArrangementWall wall{
      0, -8.0f, 8.0f, 0,
      bw::core::arr::ArrangementWallKind::Border, 16.0f, true, mapped};
  bw::core::arr::ArrangementWallOrientation orientation{
      {-12.0f, -4.0f}, {8.0f, -4.0f}, {0.0f, 1.0f}};
  bw::core::arr::DetailTriangle remainder;
  remainder.kind = bw::core::arr::DetailTriangleKind::SurfaceRemainder;
  remainder.v[0].position = {-8.0f, -4.0f, -4.0f};
  remainder.v[1].position = {4.0f, -4.0f, 6.0f};
  remainder.v[2].position = {0.0f, -4.0f, 8.0f};
  ApplyWallPhysicalUvToRemainder(orientation, wall, remainder);
  require(near(remainder.v[0].uv[0], -2.0f) &&
              near(remainder.v[0].uv[1], -1.0f) &&
              near(remainder.v[1].uv[0], 1.0f) &&
              near(remainder.v[1].uv[1], 1.5f),
          "a Chip's coplanar wall remainder lost absolute physical UV anchoring");

  auto facet = remainder;
  facet.kind = bw::core::arr::DetailTriangleKind::HorizontalChipFacet;
  facet.v[0].uv = {0.25f, 0.75f};
  ApplyWallPhysicalUvToRemainder(orientation, wall, facet);
  require(near(facet.v[0].uv[0], 0.25f) && near(facet.v[0].uv[1], 0.75f),
          "a newly exposed Chip facet inherited wall normal-map UVs");
}

std::string readShader(char const* path) {
  std::ifstream input(path);
  return {(std::istreambuf_iterator<char>(input)), {}};
}

void shadersShareCompositionContract() {
  auto shader3d = readShader(BW_WORLD_PBR_SHADER);
  auto shader2d = readShader(BW_WORLD_PBR_2D_SHADER);
  for (auto const* shader : {&shader3d, &shader2d}) {
    require(shader->find("vec3 applyWallNormalMap") != std::string::npos &&
                shader->find("@Texture(TEX1), @In(TEXCOORDS)") !=
                    std::string::npos &&
                shader->find("if (strength == 0.0)") != std::string::npos &&
                shader->find(
                    "cross(vec3(0.0, 1.0, 0.0), surfaceNormal)") !=
                    std::string::npos,
            "a world PBR shader lost the compatible normal-map/strength/tangent contract");
  }

  auto sample = shader3d.find("vec3 normalDir = applyWallNormalMap");
  auto evaluate = shader3d.find("Material material = evaluateMaterial");
  auto emboss = shader3d.find("material.normal = embossSurface", evaluate);
  require(sample != std::string::npos && sample < evaluate && evaluate < emboss,
          "3D shader does not compose Image before Technique and Embossing");

  auto horizontalApply = shader2d.find(
      "vec3 normal = applyWallNormalMap(shadingNormal)");
  auto horizontalEvaluate = shader2d.find("Material material = material2d");
  require(horizontalApply != std::string::npos &&
              horizontalApply < horizontalEvaluate,
          "2D shader does not bind the compatible dormant map contract");
}

void strengthScalesTangentPlaneBeforeRenormalization() {
  auto tangentInfluence = [](float strength) {
    constexpr float x = 0.6f;
    constexpr float y = 0.2f;
    constexpr float z = 0.7745967f;
    if (strength == 0.0f) return 0.0f;
    auto scaledX = x * strength;
    auto scaledY = y * strength;
    auto length = std::sqrt(
        scaledX * scaledX + scaledY * scaledY + z * z);
    return std::sqrt(scaledX * scaledX + scaledY * scaledY) / length;
  };
  require(near(tangentInfluence(0.0f), 0.0f),
          "strength zero was not flat");
  require(tangentInfluence(2.0f) > tangentInfluence(1.0f),
          "higher valid strength did not increase renormalized tangent-plane influence");
}

void mappedAndUnmappedSurfacesHaveDistinctBucketIdentity() {
  WallRenderSurface unmapped{"same.sub-material", std::nullopt};
  WallRenderSurface disabled{"same.sub-material", std::nullopt};
  WallRenderVariant mappedVariant{"normal-map-v1-directional"};
  WallRenderVariant differentMappedVariant{"normal-map-v1-other-scale"};
  WallRenderSurface mapped{"same.sub-material", mappedVariant};
  WallRenderSurface differentlyMapped{"same.sub-material", differentMappedVariant};
  require(!unmapped.variant && !disabled.variant && mapped.variant &&
              differentlyMapped.variant &&
              mapped.variant->identity != differentlyMapped.variant->identity,
          "different Images cannot select distinct buckets or no-map states cannot batch");
}
}  // namespace

int main() {
  try {
    physicalUvsUseCanonicalProjectionAndWorldElevation();
    physicalUvsRemainContinuousAcrossSplitsAndResetAtCorners();
    chippedWallRemainderKeepsPhysicalUvAnchoring();
    shadersShareCompositionContract();
    strengthScalesTangentPlaneBeforeRenormalization();
    mappedAndUnmappedSurfacesHaveDistinctBucketIdentity();
    std::cout << "Wall normal-map render-data tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
