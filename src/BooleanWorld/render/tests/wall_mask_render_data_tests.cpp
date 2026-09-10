#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include <core/Arrangement.h>

#include "WallMaskRenderData.h"
#include "WallNormalMapRenderData.h"
#include "WallRenderVariant.h"

namespace {
void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b) { return std::abs(a - b) < 0.0001f; }

bw::core::WallMaskOverride::BlendParameters blendParameters(float offset) {
  bw::core::WallMaskOverride::BlendParameters parameters{};
  for (size_t i = 0; i < parameters.size(); ++i) {
    parameters[i] = offset + 0.1f * static_cast<float>(i);
  }
  return parameters;
}

void maskIdentityDistinguishesEveryAuthoredField() {
  auto base = bw::core::WallMaskOverride::image(
      "mask/low.png", 0, blendParameters(0.0f));
  auto same = bw::core::WallMaskOverride::image(
      "mask/low.png", 0, blendParameters(0.0f));
  auto differentResource = bw::core::WallMaskOverride::image(
      "mask/high.png", 0, blendParameters(0.0f));
  auto differentChannel = bw::core::WallMaskOverride::image(
      "mask/low.png", 2, blendParameters(0.0f));
  auto differentBlend = bw::core::WallMaskOverride::image(
      "mask/low.png", 0, blendParameters(1.0f));
  auto differentColour = bw::core::WallMaskOverride::image(
      "mask/low.png", 0, blendParameters(0.0f), {0.2f, 0.4f, 0.6f});
  require(maskIdentity(*base.imageData()) ==
              maskIdentity(*same.imageData()),
          "identical masks did not share a bucket identity");
  require(maskIdentity(*base.imageData()) !=
              maskIdentity(*differentResource.imageData()),
          "a different mask resource did not change the bucket identity");
  require(maskIdentity(*base.imageData()) !=
              maskIdentity(*differentChannel.imageData()),
          "a different mask channel did not change the bucket identity");
  require(maskIdentity(*base.imageData()) !=
              maskIdentity(*differentBlend.imageData()),
          "different blend parameters did not change the bucket identity");
  require(maskIdentity(*base.imageData()) !=
              maskIdentity(*differentColour.imageData()),
          "a different blend colour did not change the bucket identity");
}

void combinedIdentityConcatenatesMaskOntoNormalMap() {
  auto mask = bw::core::WallMaskOverride::image(
      "mask/a.png", 1, blendParameters(0.25f));
  auto bare =
      wallImageVariantIdentity("normal-map-v1-abc", std::nullopt);
  auto masked =
      wallImageVariantIdentity("normal-map-v1-abc", *mask.imageData());
  require(bare == "normal-map-v1-abc",
          "an unmasked wall did not keep its normal-map identity");
  require(masked == "normal-map-v1-abc" + maskIdentity(*mask.imageData()),
          "a masked wall did not concatenate its mask identity");
  require(bare != masked,
          "a mask did not change the wall-image bucket identity");
}

void maskSharesNormalMapRepeatAndUvs() {
  auto normalMap = bw::core::WallNormalMapOverride::image(
      "normal/directional.png", 4.0f, 0.75f);
  auto mask = bw::core::WallMaskOverride::image(
      "mask/tile.png", 0, blendParameters(0.0f));
  bw::core::arr::ArrangementWall wall{
      0, -8.0f, 8.0f, 0,
      bw::core::arr::ArrangementWallKind::Border, 16.0f, true,
      normalMap, mask};
  bw::core::arr::ArrangementWallOrientation orientation{
      {-12.0f, -4.0f}, {8.0f, -4.0f}, {0.0f, 1.0f}};

  // The mask has no repeat of its own: the physical UVs are exactly the
  // normal map's repeat (4 tiles across a length-20 wall, 3.2 up its 16-unit
  // height), shared by both images.
  auto uv = CalculateWallPhysicalUv(orientation, wall);
  require(near(uv.u0, 0.0f) && near(uv.u1, 4.0f) &&
              near(uv.minV, 0.0f) && near(uv.maxV, 3.2f),
          "masked wall physical UVs did not use the normal map's repeat");

  // A mask without a normal map owns a single-tile repeat: one tile across,
  // (height / length) tiles up, matching the normal-map formula with repeat 1.
  wall.normalMapOverride = bw::core::WallNormalMapOverride::unset();
  uv = CalculateWallPhysicalUv(orientation, wall);
  require(near(uv.u0, 0.0f) && near(uv.u1, 1.0f) &&
              near(uv.minV, 0.0f) && near(uv.maxV, 0.8f),
          "a mask without a normal map did not use the single-tile repeat");
}

std::string readShader(char const* path) {
  std::ifstream input(path);
  return {(std::istreambuf_iterator<char>(input)), {}};
}

void shadersShareMaskContract() {
  auto shader3d = readShader(BW_WORLD_PBR_SHADER);
  auto shader2d = readShader(BW_WORLD_PBR_2D_SHADER);
  for (auto const* shader : {&shader3d, &shader2d}) {
    require(shader->find("@@Uniform(int WALL_MASK_ENABLED)") !=
                    std::string::npos &&
                shader->find("@@Uniform(int WALL_MASK_CHANNEL)") !=
                    std::string::npos &&
                shader->find("@@Uniform(float WALL_MASK_BLEND_PARAMS[8])") !=
                    std::string::npos &&
                shader->find("@@Uniform(vec3 MATERIAL_COLOUR)") !=
                    std::string::npos &&
                shader->find("@@Uniform(vec3 WALL_MASK_BLEND_COLOUR)") !=
                    std::string::npos,
            "a world PBR shader lost the wall mask uniforms");
    require(shader->find("@@Texture(sampler2D TEX2)") != std::string::npos,
            "a world PBR shader lost the wall mask sampler");
    require(shader->find("texture(@Texture(TEX2), wallImageUv())") !=
                    std::string::npos &&
                shader->find("WALL_NORMAL_MAP_ASPECT_RATIO") !=
                    std::string::npos,
            "a world PBR shader does not sample the mask with the shared normal-map UVs");
    require(shader->find("blendedMaterialParams[i] = mix(") !=
                    std::string::npos &&
                shader->find("@Uniform(WALL_MASK_BLEND_PARAMS[i])") !=
                    std::string::npos,
            "a world PBR shader does not interpolate the two parameter sets");
    require(shader->find("blendedMaterialColour = mix(") !=
                    std::string::npos &&
                shader->find("@Uniform(WALL_MASK_BLEND_COLOUR)") !=
                    std::string::npos,
            "a world PBR shader does not interpolate the two colour sets");
  }

  auto blend = shader3d.find("blendMaterialParams();");
  auto evaluate = shader3d.find("material = evaluateMaterial");
  auto emboss = shader3d.find("material.normal = embossSurface", evaluate);
  require(blend != std::string::npos && blend < evaluate &&
              evaluate < emboss,
          "3D shader does not interpolate before the single material evaluation and Embossing");

  auto horizontalBlend = shader2d.find("blendMaterialParams();");
  auto horizontalEvaluate = shader2d.find("material = material2d");
  require(horizontalBlend != std::string::npos &&
              horizontalBlend < horizontalEvaluate,
          "2D shader does not interpolate before its material evaluation");
}

void maskOnlyVariantIdentityUsesMaskIdentityAlone() {
  auto mask = bw::core::WallMaskOverride::image(
      "mask/a.png", 0, blendParameters(0.0f));
  auto identity = wallImageVariantIdentity("", *mask.imageData());
  require(identity == maskIdentity(*mask.imageData()),
          "a mask-only wall did not use the mask identity alone");
}

void maskedAndUnmaskedSurfacesHaveDistinctBucketIdentity() {
  auto mask = bw::core::WallMaskOverride::image(
      "mask/a.png", 0, blendParameters(0.0f));
  auto otherMask = bw::core::WallMaskOverride::image(
      "mask/b.png", 3, blendParameters(1.0f));
  WallRenderVariant maskedVariant{
      wallImageVariantIdentity("normal-map-v1-x", *mask.imageData())};
  WallRenderVariant differentlyMaskedVariant{
      wallImageVariantIdentity("normal-map-v1-x", *otherMask.imageData())};
  WallRenderVariant bareVariant{"normal-map-v1-x"};

  auto material = bw::core::SurfaceMaterialReference::subMaterial(
      "same.sub-material");
  WallRenderSurface unmapped{material, std::nullopt};
  WallRenderSurface masked{material, maskedVariant};
  WallRenderSurface differentlyMasked{
      material, differentlyMaskedVariant};
  require(!unmapped.variant && masked.variant && differentlyMasked.variant,
          "mask states cannot select distinct buckets");
  require(masked.variant->identity != differentlyMasked.variant->identity,
          "different masks cannot select distinct buckets");
  require(masked.variant->identity != bareVariant.identity,
          "a mask and a bare normal map cannot select distinct buckets");
}
}  // namespace

int main() {
  try {
    maskIdentityDistinguishesEveryAuthoredField();
    combinedIdentityConcatenatesMaskOntoNormalMap();
    maskSharesNormalMapRepeatAndUvs();
    maskOnlyVariantIdentityUsesMaskIdentityAlone();
    shadersShareMaskContract();
    maskedAndUnmaskedSurfacesHaveDistinctBucketIdentity();
    std::cout << "Wall mask render-data tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
