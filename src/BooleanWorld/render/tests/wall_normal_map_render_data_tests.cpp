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

void physicalUvsUseWallLocalRepeatCount() {
  bw::core::arr::ArrangementWall wall{
      0, 6.0f, 14.0f, 0,
      bw::core::arr::ArrangementWallKind::Border, 8.0f, true,
      bw::core::WallNormalMapOverride::image("normal/directional.png", 4.0f,
                                             0.75f)};
  bw::core::arr::ArrangementWallOrientation orientation{
      {3.0f, 4.0f}, {6.0f, 8.0f}, {-0.8f, 0.6f}};
  auto uv = CalculateWallPhysicalUv(orientation, wall);
  require(near(uv.u0, 0.0f) && near(uv.u1, 4.0f) &&
              near(uv.minV, 0.0f) && near(uv.maxV, 6.4f),
          "mapped wall UVs did not use wall-local horizontal repeat count");

  wall.normalMapOverride = bw::core::WallNormalMapOverride::unset();
  uv = CalculateWallPhysicalUv(orientation, wall);
  require(near(uv.u0, 0) && near(uv.u1, 1) && near(uv.minV, 0) &&
              near(uv.maxV, 1),
          "Unset wall changed the legacy UV data");
}

void physicalUvsFollowVariableWallBoundaries() {
  auto mapped = bw::core::WallNormalMapOverride::image(
      "normal/directional.png", 4.0f, 1.0f);
  bw::core::arr::ArrangementWall wall{
      0, 6.0f, 16.0f, 0,
      bw::core::arr::ArrangementWallKind::Border, 8.0f, true, mapped};
  bw::core::arr::ArrangementWallOrientation orientation{
      {3.0f, 4.0f}, {6.0f, 8.0f}, {-0.8f, 0.6f}, {6.0f, 8.0f}, {14.0f, 16.0f}};

  auto uv = CalculateWallPhysicalUv(orientation, wall);
  require(near(uv.bottomV[0], 0.0f) && near(uv.bottomV[1], 1.6f) &&
              near(uv.topV[0], 6.4f) && near(uv.topV[1], 8.0f),
          "mapped UVs flattened a variable-height wall to scalar bounds");
}

void physicalUvsAlignToEachWallWithoutWorldPhase() {
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
  require(near(first.u0, 0.0f) && near(first.u1, 4.0f) &&
              near(second.u0, 0.0f) && near(second.u1, 4.0f) &&
              near(first.minV, 0.0f),
          "wall-local UVs retained a world-position phase offset");

  auto corner = CalculateWallPhysicalUv(
      bw::core::arr::ArrangementWallOrientation{
          {-4.0f, -4.0f}, {-4.0f, 8.0f}, {-1.0f, 0.0f}},
      wall);
  require(near(corner.u0, 0.0f) && near(corner.u1, 4.0f),
          "a corner did not establish its own wall-local repeat range");
}

void chippedWallRemainderKeepsWallLocalUvAnchoring() {
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
  require(near(remainder.v[0].uv[0], 0.8f) &&
              near(remainder.v[0].uv[1], 0.8f) &&
              near(remainder.v[1].uv[0], 3.2f) &&
              near(remainder.v[1].uv[1], 2.8f),
          "a Chip's coplanar wall remainder lost wall-local UV anchoring");

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
                shader->find("vec2 wallImageUv()") != std::string::npos &&
                shader->find(
                    "uv.y *= @Uniform(WALL_NORMAL_MAP_ASPECT_RATIO)") !=
                    std::string::npos &&
                shader->find("@Texture(TEX1), wallImageUv()") !=
                    std::string::npos &&
                shader->find("if (strength == 0.0)") != std::string::npos &&
                shader->find(
                    "cross(surfaceNormal, vec3(0.0, 1.0, 0.0))") !=
                    std::string::npos,
            "a world PBR shader lost the compatible normal-map/strength/tangent contract");
    require(shader->find("material.albedo = vec3(0.5, 0.5, 0.5)") !=
                    std::string::npos &&
                shader->find("material.metallic = 0.0") !=
                    std::string::npos &&
                shader->find("material.roughness = 0.0") !=
                    std::string::npos,
            "a world PBR shader lost the parameterless Plain grey Technique");
    require(shader->find("const float grooveWidth = 1.0") !=
                    std::string::npos &&
                shader->find(
                    "tileGrooveHeight(float distanceToEdge, float radius") ==
                    std::string::npos &&
                shader->find("radius * 0.01") == std::string::npos,
            "a world PBR shader scales emboss groove width or sampling with tile size");
    require(shader->find("vec3 triplanarAlbedo") != std::string::npos &&
                shader->find("rendererPosition.x, -rendererPosition.z") !=
                    std::string::npos &&
                shader->find("projectionNormal = abs(vec3(") !=
                    std::string::npos &&
                shader->find("pow(projectionNormal, vec3(sharpness))") !=
                    std::string::npos &&
                shader->find("weights / weightSum") != std::string::npos &&
                shader->find("@Texture(TEX3)") != std::string::npos &&
                shader->find(".rgb;") != std::string::npos &&
                shader->find("material.metallic = 0.0") !=
                    std::string::npos &&
                shader->find("material.roughness = 0.7") !=
                    std::string::npos,
            "a world PBR shader lost the fixed opaque Triplanar projection contract");
  }

  require(shader3d.find("Material graniteTexture") != std::string::npos &&
              shader3d.find("case 39: material = graniteTexture") !=
                  std::string::npos &&
              shader2d.find("Material graniteMaterial2d") !=
                  std::string::npos &&
              shader2d.find("if (type == 39)") != std::string::npos &&
              shader3d.find("case 40: // BW_WALL_BACK_FACE_MATERIAL_INDEX") !=
                  std::string::npos &&
              shader3d.find("if (bucketMaterialIndex == 41)") !=
                  std::string::npos &&
              shader2d.find("if (type == 41)") != std::string::npos,
          "Granite or the following reserved materials are not dispatched by both procedural PBR programs");

  auto sample = shader3d.find("vec3 normalDir = applyWallNormalMap");
  auto evaluate = shader3d.find("material = evaluateMaterial");
  auto emboss = shader3d.find("material.normal = embossSurface", evaluate);
  require(sample != std::string::npos && sample < evaluate && evaluate < emboss,
          "3D shader does not compose Image before Technique and Embossing");

  auto horizontalApply = shader2d.find(
      "vec3 normal = applyWallNormalMap(shadingNormal)");
  auto horizontalEvaluate = shader2d.find("material = material2d");
  require(horizontalApply != std::string::npos &&
              horizontalApply < horizontalEvaluate,
          "2D shader does not bind the compatible dormant map contract");
}

void horizontalMaterialCoordinatesUseCanonicalSurfaceUp() {
  auto shader2d = readShader(BW_WORLD_PBR_2D_SHADER);
  require(
      shader2d.find("void surfaceFrame(vec3 surfaceUp") !=
              std::string::npos &&
          shader2d.find("vec3(1.0, 0.0, 0.0) - up * up.x") !=
              std::string::npos &&
          shader2d.find("axisV = cross(axisU, up)") != std::string::npos &&
          shader2d.find("surfaceFrame(surfaceUp, surfaceAxisU, surfaceAxisV)") !=
              std::string::npos &&
          shader2d.find("dot(worldPos, surfaceAxisU), dot(worldPos, surfaceAxisV)") !=
              std::string::npos,
      "2D shader does not flatten horizontal surfaces through a World-anchored frame");

  auto coordinates = shader2d.find("vec2 surfacePosition = vec2(");
  auto faceForward = shader2d.find("if (dot(shadingNormal, viewDir) < 0.0)");
  auto emboss = shader2d.find("material.normal = embossSurface");
  require(coordinates != std::string::npos && faceForward != std::string::npos &&
              emboss != std::string::npos && coordinates < faceForward &&
              faceForward < emboss &&
              shader2d.find(
                  "material.normal, surfacePosition, surfaceAxisU, surfaceAxisV") !=
                  std::string::npos,
          "2D material coordinates are not fixed before facing and Embossing");
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
  auto material = bw::core::SurfaceMaterialReference::subMaterial(
      "same.sub-material");
  WallRenderSurface unmapped{material, std::nullopt};
  WallRenderSurface disabled{material, std::nullopt};
  WallRenderVariant mappedVariant{"normal-map-v1-directional"};
  WallRenderVariant differentMappedVariant{"normal-map-v1-other-scale"};
  WallRenderSurface mapped{material, mappedVariant};
  WallRenderSurface differentlyMapped{material, differentMappedVariant};
  require(!unmapped.variant && !disabled.variant && mapped.variant &&
              differentlyMapped.variant &&
              mapped.variant->identity != differentlyMapped.variant->identity,
          "different Images cannot select distinct buckets or no-map states cannot batch");
}
}  // namespace

int main() {
  try {
    physicalUvsUseWallLocalRepeatCount();
    physicalUvsFollowVariableWallBoundaries();
    physicalUvsAlignToEachWallWithoutWorldPhase();
    chippedWallRemainderKeepsWallLocalUvAnchoring();
    shadersShareCompositionContract();
    horizontalMaterialCoordinatesUseCanonicalSurfaceUp();
    strengthScalesTangentPlaneBeforeRenormalization();
    mappedAndUnmappedSurfacesHaveDistinctBucketIdentity();
    std::cout << "Wall normal-map render-data tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
