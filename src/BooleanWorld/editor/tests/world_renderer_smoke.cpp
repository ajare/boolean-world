// Real OpenGL integration test. CTest runs the focused Triplanar scenario;
// invoking without a scenario retains the broader manual renderer smoke. Both
// drive EditorRenderSystem + PreviewRenderScene + WorldRenderer and compare
// broad rendered regions rather than exact pixels.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include <GL/glew.h>
#include <SDL3/SDL.h>

#include <common/GameDefines.h>
#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/Defines.h>
#include <core/MeshPrimitive.h>
#include <core/LayerBuildStep.h>
#include <core/World.h>
#include <core/YamlSerializer.h>
#include <mpp/ResourceManager.h>
#include <mpp/RenderSystem.h>
#include <mpp/RenderTexture.h>

#include <PlayerTorchShadows.h>
#include <PortalView.h>
#include <WorldBatch.h>

#include "EditorRenderSystem.h"
#include "PlayerView.h"
#include "PreviewRenderScene.h"
#include "ReactiveCamera.h"

namespace {
constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr char const* kDirectionalNormal = "directional-normal.png";

enum class MapFixture { Unset,
                        Disabled,
                        Image,
                        MixedSharedImage };

struct RenderFixture {
  MapFixture map{MapFixture::Unset};
  float strength{1.0f};
  float repeat{1.0f};
  bw::app::HorizontalMaterials horizontal{
      bw::app::HorizontalMaterials::ThreeDimensional};
  int32_t debugWallTechnique{-1};
  bool emboss{};
  bool wallMask{};
  bool chips{};
  bool wedges{};
  bool lookAtWedges{};
  bool lookAtFloor{};
  bool lookAtCeiling{};
  bool lookAtWallBack{};
  bool wet{};
  bool sloped{};
  bool fragmented{};
  bool triplanar{};
  bool triplanarWallOnly{};
  bool angled{};
  bool continuityJunction{};
  bool portal{};
  bool manyPortalEndpoints{};
  bool threeEndpointPortal{};
  bool stablePortalIdentity{};
  bool portalBackSurface{};
  bool portalBackDecorated{};
  // 0 uses the built-in material; 1 and 2 use identical RGB with alpha 0/1.
  int triplanarAlphaVariant{};
  bw::core::ZoneId zone{bw::core::ZoneId::Euclidean};
  bool bundledNormal{};
  bool wallsVisible{true};
  bool wallCollides{true};
  // 1 floor back, 2 ceiling back, 3 submerged Liquid back.
  int horizontalBack{};
  int detailBack{-1};
  bool detailFront{};
  bool planar{};
  bool phantomWindow{};
  bool phantomReverse{};
  bool phantomDiagnostic{};
  bool phantomObstruction{};
  bool phantomFarWindow{};
};

struct ViewTrace {
  std::array<std::vector<float>, 3> reflection;
  std::array<std::vector<float>, 3> shadow;
  std::array<mpp::ShadowDomainDiagnostics, 3> shadowDiagnostics;
};

std::vector<float> readTorchDepth(mpp::RenderSystem* system) {
  auto target = system->getShadowDomainDepthTarget(
      std::string(bw::app::playerTorchShadowDomain));
  auto* texture = dynamic_cast<mpp::RenderTexture*>(target.get());
  if (!texture || !texture->getDepthTextureId())
    throw std::runtime_error("missing Player Torch shadow cubemap");
  size_t faceSize = texture->getWidth() * texture->getHeight();
  std::vector<float> result(faceSize * 6);
  glBindTexture(GL_TEXTURE_CUBE_MAP, texture->getDepthTextureId());
  for (unsigned face = 0; face < 6; ++face)
    glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, result.data() + face * faceSize);
  glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
  return result;
}

struct ResourceCounts {
  uint32_t resources{}, declared{}, created{}, loaded{};
  bool operator==(ResourceCounts const&) const = default;
};

ResourceCounts resourceCounts(mpp::ResourceManager* manager) {
  ResourceCounts result;
  manager->getResourceCounts(
      result.resources, result.declared, result.created, result.loaded);
  return result;
}

void writeDirectionalNormal() {
  // A tiny RGB positive-green OpenGL normal image. Keeping the fixture local
  // also proves dynamic application-relative references, rather than a
  // manifest-enumerated colour texture.
  constexpr std::array<unsigned char, 73> png{
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00,
      0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
      0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xfd, 0xd4, 0x9a, 0x73,
      0x00, 0x00, 0x00, 0x10, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63,
      0xf8, 0xdf, 0xd0, 0x00, 0x44, 0x0c, 0x10, 0x0a, 0x00, 0x39, 0xee,
      0x07, 0xfd, 0xc8, 0x84, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00, 0x49,
      0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  std::ofstream output(kDirectionalNormal, std::ios::binary);
  output.write(reinterpret_cast<char const*>(png.data()), png.size());
}

bw::core::ArrangementWorldDataPtr buildWorldData(
    bw::core::World& world, RenderFixture const& fixture) {
  world.createAccelerationGrids(16.0f);
  bw::core::ClosedPolygon ring =
      (fixture.angled || fixture.continuityJunction)
      ? bw::core::ClosedPolygon{
            {{0, -22}}, {{22, 0}}, {{0, 22}}, {{-22, 0}}}
      : fixture.fragmented
          ? bw::core::ClosedPolygon{
                {{-16, -16}}, {{0, -16}}, {{0, 16}}, {{-16, 16}}}
          : bw::core::ClosedPolygon{
                {{-16, -16}}, {{16, -16}}, {{16, 16}}, {{-16, 16}}};
  auto* primitive = bw::core::MeshPrimitive::fromTree(
      bw::core::Primitive::Operation::Union, {{{ring, {}}}});

  if (fixture.map != MapFixture::Unset || fixture.wallMask ||
      !fixture.wallsVisible || !fixture.wallCollides || fixture.phantomWindow) {
    auto proxy = primitive->createEditingProxy();
    auto normalImageReference = (fixture.triplanar || fixture.bundledNormal)
                                    ? "World/TriplanarCompositionNormal"
                                    : kDirectionalNormal;
    auto maskImageReference = fixture.wallMask
                                  ? "World/OreMask"
                                  : kDirectionalNormal;
    size_t ordinal = 0;
    for (auto edge = proxy->getFirstEdgeIndex();
         !proxy->edgeIndexIterationFinished(edge);
         edge = proxy->getNextEdgeIndex(edge), ++ordinal) {
      proxy->setEdgeVisible(edge, fixture.wallsVisible);
      proxy->setEdgeCollisionOverride(edge, fixture.wallCollides);
      if (fixture.phantomWindow && ordinal == 0) {
        proxy->setEdgeVisible(edge, false);
        proxy->setEdgeCollisionOverride(edge, false);
        proxy->setEdgeOtherZone(edge, bw::core::ZoneId::Phantom);
      }
      if (fixture.map != MapFixture::Unset) {
        auto image = bw::core::WallNormalMapOverride::image(
            normalImageReference,
            fixture.map == MapFixture::MixedSharedImage && ordinal == 1
                ? fixture.repeat * 2.0f
                : fixture.repeat,
            fixture.map == MapFixture::MixedSharedImage && ordinal == 2
                ? 2.0f
                : fixture.strength);
        if (fixture.map == MapFixture::Disabled ||
            (fixture.map == MapFixture::MixedSharedImage && ordinal == 3)) {
          proxy->setEdgeNormalMapOverride(
              edge, bw::core::WallNormalMapOverride::disabled());
        } else {
          proxy->setEdgeNormalMapOverride(edge, image);
        }
      }
      if (fixture.wallMask) {
        bw::core::WallMaskOverride::BlendParameters blend{};
        proxy->setEdgeWallMaskOverride(
            edge, bw::core::WallMaskOverride::image(
                      maskImageReference, 0, blend, {0.0f, 1.0f, 0.0f}));
      }
    }
    proxy->commitTo(*primitive);
  }

  auto properties = primitive->getProperties();
  properties.floorZ = 0.0f;
  properties.ceilingZ = 48.0f;
  if (fixture.sloped) {
    properties.floorZ.gradient = {0.25f, 0.125f};
    properties.ceilingZ.gradient = {-0.125f, 0.25f};
  }
  if (fixture.triplanar || fixture.triplanarWallOnly) {
    auto materialName = fixture.continuityJunction
                            ? "World/TriplanarWallContinuityDiagnostic"
                        : fixture.triplanarAlphaVariant == 1
                            ? "World/TriplanarAlphaZeroDiagnostic"
                        : fixture.triplanarAlphaVariant == 2
                            ? "World/TriplanarAlphaOneDiagnostic"
                            : "World/TriplanarFloorTiles";
    auto material = bw::core::SurfaceMaterialReference::triplanar(materialName);
    properties.floorMaterial = fixture.triplanar
                                   ? material
                                   : bw::core::SurfaceMaterialReference::subMaterial(
                                         "migrated.marble.1");
    properties.ceilingMaterial = fixture.triplanar
                                     ? material
                                     : bw::core::SurfaceMaterialReference::subMaterial(
                                           "migrated.marble.1");
    properties.wallMaterial = material;
  } else {
    properties.floorMaterial = bw::core::SurfaceMaterialReference::subMaterial("migrated.marble.1");
    properties.ceilingMaterial = bw::core::SurfaceMaterialReference::subMaterial("migrated.marble.1");
    properties.wallMaterial = bw::core::SurfaceMaterialReference::subMaterial("migrated.marble.1");
  }
  if (fixture.emboss) {
    properties.floorEmbossPresetId = "builtin.emboss.stone";
    properties.ceilingEmbossPresetId = "builtin.emboss.stone";
    properties.wallEmbossPresetId = "builtin.emboss.stone";
  }
  if (fixture.phantomDiagnostic)
    properties.wallMaterial = bw::core::SurfaceMaterialReference::subMaterial("missing.phantom.diagnostic");
  properties.liquidLevel = fixture.wet ? 2.0f : 0.0f;
  primitive->setProperties(properties);
  world.addPrimitive(primitive);
  std::vector<bw::core::Primitive*> primitives{primitive};
  if (fixture.phantomObstruction || fixture.phantomFarWindow) {
    auto* extra = bw::core::MeshPrimitive::fromTree(
        bw::core::Primitive::Operation::Union, {{{ring, {}}}});
    extra->setPosition({0, fixture.phantomObstruction ? -48.0f : 64.0f});
    auto extraProperties = properties;
    extraProperties.wallMaterial = bw::core::SurfaceMaterialReference::subMaterial("migrated.marble.1");
    extra->setProperties(extraProperties);
    if (fixture.phantomFarWindow) {
      auto proxy = extra->createEditingProxy();
      auto edge = proxy->getFirstEdgeIndex();
      proxy->setEdgeVisible(edge, false);
      proxy->setEdgeCollisionOverride(edge, false);
      proxy->setEdgeOtherZone(edge, bw::core::ZoneId::Phantom);
      proxy->commitTo(*extra);
    }
    world.addPrimitive(extra);
    primitives.push_back(extra);
  }
  if (fixture.portalBackSurface) {
    // An elevated floor with invisible walls exposes its underside through the
    // destination aperture. It lies behind the primary eye, but in front of a
    // transformed Portal eye, so using the primary CameraFrame cannot pass.
    bw::core::ClosedPolygon platformRing{
        {{-15, -14}}, {{15, -14}}, {{15, -2}}, {{-15, -2}}};
    auto* platform = bw::core::MeshPrimitive::fromTree(
        bw::core::Primitive::Operation::Union, {{{platformRing, {}}}});
    auto platformProperties = properties;
    platformProperties.floorZ = bw::core::Elevation{10.0f, {0.0f, -1.0f}};
    if (fixture.portalBackDecorated) {
      platformProperties.floorMaterial = bw::core::SurfaceMaterialReference::triplanar(
          "World/TriplanarFloorTiles");
      platformProperties.floorEmbossPresetId = "builtin.emboss.stone";
    }
    platform->setProperties(platformProperties);
    platform->setPriority(1);
    auto proxy = platform->createEditingProxy();
    for (auto edge = proxy->getFirstEdgeIndex();
         !proxy->edgeIndexIterationFinished(edge);
         edge = proxy->getNextEdgeIndex(edge))
      proxy->setEdgeVisible(edge, false);
    proxy->commitTo(*platform);
    world.addPrimitive(platform);
    primitives.push_back(platform);
  }
  if (fixture.continuityJunction) {
    struct JunctionPrimitive {
      bw::core::ClosedPolygon ring;
      float floor;
      float ceiling;
    };
    for (auto const& adjoining : std::array{
             JunctionPrimitive{
                 {{{0, 22}}, {{20, 30}}, {{5, 45}}}, 8.0f, 32.0f},
             JunctionPrimitive{
                 {{{0, 22}}, {{-5, 45}}, {{-20, 30}}}, 16.0f, 56.0f}}) {
      auto* incident = bw::core::MeshPrimitive::fromTree(
          bw::core::Primitive::Operation::Union,
          {{{adjoining.ring, {}}}});
      auto incidentProperties = properties;
      incidentProperties.floorZ = adjoining.floor;
      incidentProperties.ceilingZ = adjoining.ceiling;
      incident->setProperties(incidentProperties);
      world.addPrimitive(incident);
      primitives.push_back(incident);
    }
  }
  if (fixture.fragmented) {
    bw::core::ClosedPolygon rightRing{
        {{0, -16}}, {{16, -16}}, {{16, 16}}, {{0, 16}}};
    auto* right = bw::core::MeshPrimitive::fromTree(
        bw::core::Primitive::Operation::Union, {{{rightRing, {}}}});
    right->setProperties(properties);
    world.addPrimitive(right);
    primitives.push_back(right);
  }
  if (fixture.chips && !fixture.triplanar) {
    bw::core::ClosedPolygon platformRing{
        {{-8, -8}}, {{8, -8}}, {{8, 8}}, {{-8, 8}}};
    auto* platform = bw::core::MeshPrimitive::fromTree(
        bw::core::Primitive::Operation::Union, {{{platformRing, {}}}});
    auto platformProperties = platform->getProperties();
    platformProperties.floorZ = 8.0f;
    platformProperties.ceilingZ = 48.0f;
    platformProperties.floorMaterial = bw::core::SurfaceMaterialReference::subMaterial("migrated.marble.1");
    platformProperties.ceilingMaterial = bw::core::SurfaceMaterialReference::subMaterial("migrated.marble.1");
    platformProperties.wallMaterial = bw::core::SurfaceMaterialReference::subMaterial("migrated.marble.1");
    platform->setProperties(platformProperties);
    platform->setPriority(1);
    world.addPrimitive(platform);
    primitives.push_back(platform);
  }
  if (fixture.wedges) {
    auto wedgeSettings = bw::core::WedgeGenerationParameters{
        true, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f};
    wedgeSettings.quality = 1;
    world.setWedgeGenerationParameters(wedgeSettings);
  }

  bw::core::ArrangementWorldDataGenerator generator;
  if (fixture.chips) {
    generator.setChipParametersResolver([&](std::string const&) {
      return bw::core::ChipGenerationParameters{
          2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 256.0f, 1.0f,
          1.0f, 1.0f, fixture.detailBack >= 0 ? 1.0f : 0.0f};
    });
  }
  generator.generate(primitives);
  std::vector<bw::core::PortalLoopSnapshot> portalLoops;
  if (fixture.portal) {
    auto* layer = world.getActiveLayer();
    if (fixture.manyPortalEndpoints) {
      // Reserve more endpoint buckets than the recursive GPU target budget.
      // The active endpoints below must still bind correctly at higher IDs.
      for (uint32_t i = 0; i < PortalViewSlotCount; ++i) {
        auto id = layer->addPortalLoop(
            {{1000.0f + float(i) * 20.0f, 1000.0f}, 12.0f, 4.0f, 36.0f},
            {{1000.0f + float(i) * 20.0f, -1000.0f}, 12.0f, 4.0f, 36.0f});
        portalLoops.push_back({layer->getId(), *layer->getPortalLoop(id)});
      }
    }
    auto const portalCentres = fixture.threeEndpointPortal
                                   ? std::vector<float>{-8.0f}
                                   : std::vector<float>{-8.0f, 8.0f};
    for (auto centreX : portalCentres) {
      auto loopId = layer->addPortalLoop(
          {{centreX, 16.0f}, 12.0f, 4.0f, 36.0f},
          {{centreX, -16.0f}, 12.0f, 4.0f, 36.0f});
      auto* portalLoop = layer->getPortalLoop(loopId);
      if (fixture.threeEndpointPortal) {
        [[maybe_unused]] auto const thirdEndpointId =
            layer->addPortalEndpointAfter(
                loopId, 1,
                {{8.0f, 16.0f}, 12.0f, 4.0f, 36.0f});
      }
      if (fixture.stablePortalIdentity) {
        auto first = portalLoop->getEndpoints()[0].getAperture();
        auto second = portalLoop->getEndpoints()[1].getAperture();
        *portalLoop = bw::core::PortalLoop{
            loopId, 94,
            {bw::core::PortalEndpoint{93, second},
             bw::core::PortalEndpoint{17, first}},
            {17, 93}};
      }
      portalLoops.push_back({layer->getId(), *portalLoop});
    }
  }
  auto result = std::make_shared<bw::core::ArrangementWorldData>(
      generator.getWorldData(), world.getExtents(),
      float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX), nullptr,
      world.getWedgeGenerationParameters(), false, portalLoops);
  if (fixture.phantomFarWindow && std::ranges::count_if(result->getWalls(),
          [](auto const& wall) { return !wall.visible && wall.sideZones.has_value(); }) != 2)
    throw std::runtime_error("Phantom overlap fixture lost an aperture");
  if (fixture.phantomObstruction && result->getWalls().size() < 8)
    throw std::runtime_error("Phantom near-plane fixture lost its obstruction");
  if (fixture.map == MapFixture::Image) {
    auto mappedWalls = std::ranges::count_if(
        result->getWalls(), [](auto const& wall) {
          return wall.normalMapOverride.imageData() != nullptr;
        });
    if (mappedWalls == 0) {
      throw std::runtime_error(
          "renderer fixture lost its wall normal-map overrides");
    }
    auto frontWalls = std::ranges::count_if(
        result->getWalls(), [&](auto const& wall) {
          auto orientation = bw::core::arr::OrientArrangementWall(
              result->getArrangement(), wall);
          auto midpoint = (orientation.v0 + orientation.v1) * 0.5f;
          return orientation.normal.dot(-midpoint) > 0.0f;
        });
    if (frontWalls == 0) {
      throw std::runtime_error(
          "renderer fixture exposes only wall back faces to its camera");
    }
  }
  if (fixture.chips && fixture.triplanar &&
      result->getDetail().getChipCount() != 0) {
    throw std::runtime_error("Triplanar renderer fixture generated Chips");
  }
  if (fixture.chips && !fixture.triplanar &&
      result->getDetail().getChipCount() == 0) {
    throw std::runtime_error("Sub-material renderer fixture generated no Chips");
  }
  if (fixture.wedges && result->getDetail().getWedgeCount() != 24) {
    throw std::runtime_error(
        "renderer fixture did not generate edge and Corner Border-wall Wedges");
  }
  return result;
}

std::vector<float> readColour(uint32_t texture) {
  uint32_t framebuffer{};
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(
      GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  std::vector<float> pixels(size_t(kWidth) * kHeight * 4);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_FLOAT, pixels.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &framebuffer);
  return pixels;
}

// The camera faces a wall through the image centre. Average that stable region
// so a material routed to black cannot satisfy a mere difference comparison.
double centreRegionEnergy(std::vector<float> const& image) {
  double energy = 0.0;
  size_t pixels = 0;
  for (int y = kHeight / 4; y < 3 * kHeight / 4; ++y) {
    for (int x = kWidth / 4; x < 3 * kWidth / 4; ++x) {
      auto offset = (size_t(y) * kWidth + x) * 4;
      energy += image[offset] + image[offset + 1] + image[offset + 2];
      ++pixels;
    }
  }
  return pixels == 0 ? 0.0 : energy / double(pixels);
}

// Compare the broad non-black union of both images. This tolerates
// driver-dependent edge coverage and post-processing while still requiring a
// material-sized region to change.
double regionDifference(
    std::vector<float> const& first, std::vector<float> const& second) {
  double difference = 0.0;
  size_t regionPixels = 0;
  for (size_t i = 0; i + 3 < first.size(); i += 4) {
    auto firstEnergy = first[i] + first[i + 1] + first[i + 2];
    auto secondEnergy = second[i] + second[i + 1] + second[i + 2];
    if (std::max(firstEnergy, secondEnergy) < 0.015f) continue;
    difference += std::abs(first[i] - second[i]) +
                  std::abs(first[i + 1] - second[i + 1]) +
                  std::abs(first[i + 2] - second[i + 2]);
    ++regionPixels;
  }
  return regionPixels == 0 ? 0.0 : difference / double(regionPixels);
}

std::vector<float> render(
    editor::EditorRenderSystem& renderSystem, RenderFixture const& fixture,
    std::array<uint32_t, 3>* surfaceTriangles = nullptr,
    uint32_t* portalPasses = nullptr,
    uint32_t* selectedPortalEndpoints = nullptr,
    std::vector<float>* switchedZoneImage = nullptr,
    ViewTrace* trace = nullptr) {
  std::string dependencyError;
  auto triplanarDependency = fixture.continuityJunction
                                 ? "World/TriplanarWallContinuityDiagnostic"
                             : fixture.triplanarAlphaVariant == 1
                                 ? "World/TriplanarAlphaZeroDiagnostic"
                             : fixture.triplanarAlphaVariant == 2
                                 ? "World/TriplanarAlphaOneDiagnostic"
                                 : "World/TriplanarFloorTiles";
  auto dependencies = fixture.triplanar || fixture.triplanarWallOnly || fixture.portalBackDecorated
                          ? std::vector<std::string>{triplanarDependency}
                          : std::vector<std::string>{};
  if (fixture.wallMask) {
    dependencies.emplace_back("World/OreMask");
  }
  if ((fixture.triplanar || fixture.bundledNormal) && fixture.map != MapFixture::Unset) {
    dependencies.emplace_back("World/TriplanarCompositionNormal");
  } else if (fixture.map == MapFixture::Image ||
             fixture.map == MapFixture::MixedSharedImage) {
    dependencies.emplace_back(kDirectionalNormal);
  }
  if (!renderSystem.loadWorldDependencies(
          dependencies, "World", &dependencyError)) {
    throw std::runtime_error(
        "could not load render fixture dependencies: " + dependencyError);
  }

  bw::core::World world(1.0f, -1.0f);
  auto worldData = buildWorldData(world, fixture);
  mpp::WaterReflectionOptions reflections;
  if (fixture.planar) {
    reflections.technique = mpp::WaterReflectionTechnique::Planar;
    reflections.planarResolution = mpp::PlanarReflectionResolution::Full;
    // Floor fixture: primary eye sees its authored front, mirrored eye sees
    // its back above the clip plane. Wall fixtures retain the upper wall.
    reflections.planarPlanes.push_back({fixture.lookAtFloor ? -8.0f : 20.0f,
                                       mpp::ReflectionPlaneSide::Above});
  }
  bw::app::ShadowOptions shadows;
  // All six faces are read on every Zone transition. A small real cubemap
  // keeps unattended regression runs bounded without changing caster policy.
  if (trace) shadows.faceResolution = 128;
  editor::PreviewRenderScene scene(
      renderSystem, &world, kWidth, kHeight, fixture.horizontal, shadows,
      "Preview3D", true, reflections);
  auto pipeline = renderSystem.renderSystem()->getRenderPipeline("Editor.Preview3D.World");
  auto camera = std::make_shared<ReactiveCamera>(
      glm::vec3{
          0.0f,
          fixture.lookAtCeiling
              ? 8.0f
              : (fixture.lookAtWedges || fixture.lookAtFloor
                     ? 40.0f
                     : BW_PLAYER_EYE_HEIGHT),
          fixture.lookAtWallBack ? -32.0f : 0.0f},
      bw::app::cameraYaw(fixture.lookAtWallBack ? 180.0f : 0.0f),
      fixture.lookAtCeiling ? -45.0f : (fixture.lookAtFloor ? 45.0f : 0.0f),
      BW_PLAYER_FOV,
      kWidth / float(kHeight));
  if (fixture.phantomWindow) {
    camera->setLookAt(fixture.phantomReverse ? glm::vec3{0, 16, 0} : glm::vec3{0, 16, 80},
                     fixture.phantomReverse ? glm::vec3{0, 16, 80} : glm::vec3{0, 16, 0});
  }
  if (fixture.portalBackSurface) camera->setPitch(-12.0f);
  if (fixture.horizontalBack) {
    camera->setPosition({0, fixture.horizontalBack == 2 ? 52.0f :
                               fixture.horizontalBack == 3 ? 1.0f : -4.0f, 0});
    camera->setPitch(fixture.horizontalBack == 2 ? 90.0f : -90.0f);
  }
  if (fixture.detailBack >= 0) {
    auto const& facets = worldData->getDetail().getTriangles();
    auto facet = std::ranges::find_if(facets, [&](auto const& item) {
      return int(item.kind) == fixture.detailBack;
    });
    if (facet == facets.end()) throw std::runtime_error("missing detail render fixture facet");
    glm::vec3 centre{};
    for (auto const& vertex : facet->v)
      centre += glm::vec3(vertex.position[0], vertex.position[2], -vertex.position[1]) / 3.0f;
    auto const& n = facet->v[0].normal;
    glm::vec3 normal{n[0], n[2], -n[1]};
    camera->setLookAt(centre + normal * (fixture.detailFront ? 0.05f : -0.05f), centre,
        std::abs(normal.y) > 0.9f ? glm::vec3{0, 0, 1} : glm::vec3{0, 1, 0});
  }
  camera->setClipDistances(fixture.detailBack >= 0 ? 0.001f : 0.1f, 1000000.0f);
  uint32_t texture{};
  std::array<std::array<uint64_t, 2>, 3> surfaceCounters{};
  PortalViewPlan initialPortalPlan;
  for (int frame = 0; frame < 3; ++frame) {
    if (trace) pipeline->requestGraphImageCapture();
    texture = scene.render(
        &world, *worldData, camera, camera->getPosition(), 1.0f / 60.0f, {},
        -1, fixture.debugWallTechnique,
        frame == 1 ? (fixture.zone == bw::core::ZoneId::Euclidean
                          ? bw::core::ZoneId::NegativeSpace
                          : bw::core::ZoneId::Euclidean)
                   : fixture.zone);
    if (frame == 1 && switchedZoneImage) *switchedZoneImage = readColour(texture);
    if (trace) {
      auto captures = pipeline->takeGraphImageCaptures();
      auto name = fixture.planar ? "PlanarReflection0" : "SceneColourCopy";
      auto image = std::ranges::find_if(captures, [&](auto const& capture) {
        return capture.passName == name && !capture.depth;
      });
      if (image == captures.end() || image->pixels.empty() ||
          pipeline->planarReflectionRuntimeFailed())
        throw std::runtime_error("missing production reflection source");
      for (auto value : image->pixels)
        trace->reflection[frame].push_back(value / 255.0f);
      trace->shadow[frame] = readTorchDepth(renderSystem.renderSystem());
      trace->shadowDiagnostics[frame] = renderSystem.renderSystem()->getShadowDomainDiagnostics(
          std::string(bw::app::playerTorchShadowDomain));
    }
    if (fixture.portal) {
      auto const& plan = scene.portalViewDiagnostics();
      if (frame == 0) initialPortalPlan = plan;
      auto sameEdges = [](auto const& first, auto const& second) {
        if (first.size() != second.size()) return false;
        for (size_t i = 0; i < first.size(); ++i)
          if (first[i].endpoint != second[i].endpoint ||
              first[i].childNode != second[i].childNode ||
              first[i].sourceProjectiveTransform != second[i].sourceProjectiveTransform)
            return false;
        return true;
      };
      if (plan.nodes.size() != initialPortalPlan.nodes.size() ||
          plan.deepestFirst != initialPortalPlan.deepestFirst ||
          !sameEdges(plan.rootChildren, initialPortalPlan.rootChildren))
        throw std::runtime_error("Zone switch changed Portal endpoint routing");
      for (size_t i = 0; i < plan.nodes.size(); ++i) {
        auto const& node = plan.nodes[i];
        auto const& initial = initialPortalPlan.nodes[i];
        if (node.slot != initial.slot || node.cameraPosition != initial.cameraPosition ||
            node.recursionDepth != initial.recursionDepth ||
            !sameEdges(node.children, initial.children))
          throw std::runtime_error("Zone switch changed recursive Portal routing");
      }
      if (fixture.portalBackSurface) {
        // In renderer coordinates the generated floor is y = 10 + z.
        // Its geometric normal points up and toward -z. The primary eye is
        // above that plane, while retained transformed eyes lie below it.
        auto signedFacing = [](glm::vec3 eye) { return eye.y - 10.0f - eye.z; };
        if (signedFacing(camera->getPosition()) <= 0 ||
            !std::ranges::any_of(plan.nodes, [&](auto const& node) {
              return node.recursionDepth > 1 && signedFacing(node.cameraPosition) < 0;
            }))
          throw std::runtime_error("Portal fixture lost opposing per-camera facing");
      }
    }
    for (auto set : {WorldSurfaceSet::Horizontal, WorldSurfaceSet::Liquid, WorldSurfaceSet::Walls}) {
      auto& counters = surfaceCounters[static_cast<size_t>(set)];
      if (frame == 0) counters = scene.surfaceGeometryCounters(set);
      else if (scene.surfaceGeometryCounters(set) != counters)
        throw std::runtime_error("Zone switch rebuilt or uploaded surface geometry");
    }
  }
  if (fixture.phantomWindow) {
    auto before = readColour(texture);
    scene.worldGeometryChanged();
    texture = scene.render(&world, *worldData, camera, camera->getPosition(), 0.0f, {},
        -1, fixture.debugWallTechnique, fixture.zone);
    auto after = readColour(texture);
    if (regionDifference(before, after) >= 0.0005)
      throw std::runtime_error("Phantom snapshot rebuild changed aperture view");
  }
  if (texture == 0)
    throw std::runtime_error("WorldRenderer produced no render texture");
  if (fixture.portal && !scene.renderedPortalView()) {
    throw std::runtime_error(
        "public WorldRenderer scene path did not select or render a Portal view");
  }
  if (portalPasses) *portalPasses = scene.portalRenderedPassCount();
  if (selectedPortalEndpoints) {
    *selectedPortalEndpoints = scene.portalSelectedEndpointCount();
  }
  if (surfaceTriangles) {
    *surfaceTriangles = {
        scene.worldSurfaceTriangleCount(WorldSurfaceSet::Horizontal),
        scene.worldSurfaceTriangleCount(WorldSurfaceSet::Liquid),
        scene.worldSurfaceTriangleCount(WorldSurfaceSet::Walls)};
  }
  return readColour(texture);
}

void require(bool condition, char const* message) {
  if (!condition) throw std::runtime_error(message);
}

void reflectionShadowZonesRender(editor::EditorRenderSystem& renderSystem) {
  auto capture = [&](RenderFixture fixture) {
    ViewTrace trace;
    (void)render(renderSystem, fixture, nullptr, nullptr, nullptr, nullptr, &trace);
    require(trace.shadow[0] == trace.shadow[1] && trace.shadow[0] == trace.shadow[2],
            "Zone treatment leaked into shadow depth");
    require(trace.shadowDiagnostics[0].cacheComplete &&
                trace.shadowDiagnostics[2].regenerationCount ==
                    trace.shadowDiagnostics[0].regenerationCount,
            "Zone switch regenerated shadow geometry/map");
    require(regionDifference(trace.reflection[0], trace.reflection[2]) < 0.0001,
            "returning to a Zone changed reflection rendering");
    return trace;
  };
  for (bool planar : {false, true}) {
    RenderFixture fixture;
    fixture.planar = planar;
    fixture.lookAtWallBack = true;
    auto visible = capture(fixture);
    fixture.zone = bw::core::ZoneId::NegativeSpace;
    auto negativeFirst = capture(fixture);
    require(visible.shadow[0] == negativeFirst.shadow[0],
            "fresh Negative Space shadow pass used camera-facing Zone treatment");
    require(regionDifference(visible.reflection[1], negativeFirst.reflection[0]) < 0.001,
            "reflection Zone depended on the initial scene Zone");
    fixture.zone = bw::core::ZoneId::Euclidean;
    require(regionDifference(visible.reflection[0], visible.reflection[1]) > 0.01,
            "reflection source ignored Zone back-face treatment");
    fixture.wallsVisible = false;
    auto hidden = capture(fixture);
    // Removing every wall also removes the far wall's authored front, which
    // can be seen through the omitted near back in Euclidean.
    require(regionDifference(hidden.reflection[0], hidden.reflection[1]) < 0.001,
            "hidden walls changed reflection colour across Zones");
    require(regionDifference(visible.reflection[1], hidden.reflection[1]) > 0.01,
            "globally hidden wall appeared in Negative Space reflection");
    require(visible.shadow[0] != hidden.shadow[0],
            "omitted Euclidean backs did not remain shadow casters");

    fixture = {};
    fixture.planar = planar;
    auto front = capture(fixture);
    require(regionDifference(front.reflection[0], front.reflection[1]) < 0.001,
            "Zone changed authored wall fronts in reflection");
    fixture.triplanarWallOnly = true;
    auto authoredFront = capture(fixture);
    require(regionDifference(front.reflection[0], authoredFront.reflection[0]) > 0.01,
            "reflection discarded authored wall front material");

    for (auto horizontal : {bw::app::HorizontalMaterials::TwoDimensional,
                            bw::app::HorizontalMaterials::ThreeDimensional}) {
      fixture = {};
      fixture.horizontal = horizontal;
      fixture.planar = planar;
      fixture.lookAtFloor = true;
      auto floor = capture(fixture);
      auto difference = regionDifference(floor.reflection[0], floor.reflection[1]);
      require(planar ? difference > 0.01 : difference < 0.001,
              "reflection did not classify floor facing from its own eye");
      fixture.triplanar = true;
      fixture.emboss = true;
      auto decorated = capture(fixture);
      if (planar) {
        require(regionDifference(floor.reflection[1], decorated.reflection[1]) < 0.001,
                "reflected matte-white back inherited authored surface decoration");
      } else {
        require(regionDifference(floor.reflection[0], decorated.reflection[0]) > 0.01,
                "Screen-space reflection lost authored front material");
      }
    }
  }
}

void portalZonesRender(editor::EditorRenderSystem& renderSystem) {
  using bw::core::ZoneId;
  std::vector<float> switched;
  uint32_t passes{}, endpoints{};
  auto euclidean = render(renderSystem, {.portal = true, .portalBackSurface = true}, nullptr,
                          &passes, &endpoints, &switched);
  auto negative = render(renderSystem,
      {.portal = true, .portalBackSurface = true, .zone = ZoneId::NegativeSpace});
  require(regionDifference(euclidean, negative) > 0.05,
          "recursive Portal cameras did not apply Zone-specific back faces");
  require(passes > endpoints && endpoints >= 2,
          "Zone fixture did not retain recursive Portal views");
  require(regionDifference(switched, negative) < 0.0005,
          "recursive Portal Zone switch missed the next frame");
  auto decoratedBack = render(renderSystem,
      {.portal = true, .portalBackSurface = true, .portalBackDecorated = true,
       .zone = ZoneId::NegativeSpace});
  require(regionDifference(negative, decoratedBack) < 0.0005,
          "Portal back face inherited authored texture or Embossing instead of matte white");
  auto highIds = render(renderSystem,
      {.portal = true, .manyPortalEndpoints = true, .portalBackSurface = true,
       .zone = ZoneId::NegativeSpace});
  require(regionDifference(negative, highIds) < 0.0005,
          "Zone rendering confused endpoint buckets with recursive view slots");
  auto front = render(renderSystem, {.portal = true});
  auto negativeFront = render(renderSystem,
      {.portal = true, .zone = ZoneId::NegativeSpace});
  require(regionDifference(front, negativeFront) < 0.0005,
          "Zone changed authored fronts in recursive Portal views");
}

void detailZonesRender(editor::EditorRenderSystem& renderSystem) {
  using bw::core::ZoneId;
  using bw::core::arr::DetailTriangleKind;
  for (auto horizontal : {bw::app::HorizontalMaterials::ThreeDimensional,
                          bw::app::HorizontalMaterials::TwoDimensional})
  for (auto kind : {DetailTriangleKind::SurfaceRemainder,
                    DetailTriangleKind::HorizontalChipFacet,
                    DetailTriangleKind::VerticalChipFacet,
                    DetailTriangleKind::CornerChipFacet,
                    DetailTriangleKind::WedgeFacet}) {
    RenderFixture fixture;
    fixture.chips = fixture.wedges = true;
    fixture.horizontal = horizontal;
    fixture.detailBack = int(kind);
    fixture.zone = ZoneId::NegativeSpace;
    std::vector<float> switched;
    auto white = render(renderSystem, fixture, nullptr, nullptr, nullptr, &switched);
    fixture.zone = ZoneId::Euclidean;
    auto omitted = render(renderSystem, fixture);
    require(regionDifference(white, omitted) > 0.001,
            "detail facet back did not follow Zone");
    require(regionDifference(switched, omitted) < 0.0005,
            "detail Zone change missed next frame");
    fixture.detailFront = true;
    auto front = render(renderSystem, fixture);
    fixture.zone = ZoneId::NegativeSpace;
    auto negativeFront = render(renderSystem, fixture);
    require(regionDifference(front, negativeFront) < 0.0005,
            "Zone changed authored detail front");
    fixture.detailFront = false;
    fixture.emboss = true;
    auto embossedBack = render(renderSystem, fixture);
    require(regionDifference(white, embossedBack) < 0.0005,
            "detail matte back inherited Embossing");
  }
}

void horizontalZonesRender(editor::EditorRenderSystem& renderSystem) {
  using bw::core::ZoneId;
  for (auto horizontal : {bw::app::HorizontalMaterials::ThreeDimensional,
                          bw::app::HorizontalMaterials::TwoDimensional}) {
    for (int surface : {1, 2, 3}) {
      RenderFixture fixture;
      fixture.horizontal = horizontal;
      fixture.wet = surface == 3;
      fixture.horizontalBack = surface;
      fixture.zone = ZoneId::NegativeSpace;
      std::vector<float> switched;
      auto white = render(renderSystem, fixture, nullptr, nullptr, nullptr, &switched);
      fixture.zone = ZoneId::Euclidean;
      auto omitted = render(renderSystem, fixture);
      require(regionDifference(white, omitted) > 0.001,
              "horizontal/Liquid Zone back-face treatment unchanged");
      require(regionDifference(switched, omitted) < 0.0005,
              "horizontal/Liquid Zone change missed next frame");
      if (surface != 3) {
        auto frontFixture = fixture;
        frontFixture.horizontalBack = 0;
        frontFixture.lookAtFloor = surface == 1;
        frontFixture.lookAtCeiling = surface == 2;
        auto front = render(renderSystem, frontFixture);
        frontFixture.zone = ZoneId::NegativeSpace;
        require(regionDifference(front, render(renderSystem, frontFixture)) < 0.0005,
                "Zone changed authored horizontal front");
        fixture.zone = ZoneId::NegativeSpace;
        fixture.emboss = true;
        fixture.triplanar = true;
        auto decorated = render(renderSystem, fixture);
        require(regionDifference(white, decorated) < 0.0005,
                "horizontal matte back inherited authored treatment");
      }
    }
  }
}

void portalRendersThroughPublicSceneAndNamedFinalOutput(
    editor::EditorRenderSystem& renderSystem) {
  uint32_t firstPasses{};
  uint32_t firstSelected{};
  auto image = render(
      renderSystem, {.portal = true}, nullptr, &firstPasses, &firstSelected);
  require(centreRegionEnergy(image) > 0.02,
          "public Portal render-scene path produced no final named-output image");
  auto nonBlack = std::ranges::count_if(
      image, [](float channel) { return channel > 0.08f; });
  require(nonBlack > image.size() / 20,
          "Portal final named output retained only its initialized fallback");
  require(firstSelected >= 2,
          "multiple visible Portal endpoints did not replace their fallback surfaces");
  require(firstPasses > firstSelected &&
              firstPasses <= PortalViewSlotCount,
          "mutually visible Portal branches did not recurse within the fixed GPU pass budget");

  uint32_t secondPasses{};
  uint32_t secondSelected{};
  auto reordered = render(
      renderSystem, {.portal = true, .stablePortalIdentity = true}, nullptr,
      &secondPasses, &secondSelected);
  require(regionDifference(image, reordered) < 0.0005,
          "stable endpoint IDs or reordered storage changed the final Portal image");
  require(secondPasses == firstPasses && secondSelected == firstSelected,
          "the GPU Portal loop changed its deterministic selected endpoint or pass count");

  uint32_t threePasses{};
  uint32_t threeSelected{};
  auto directed = render(
      renderSystem, {.portal = true, .threeEndpointPortal = true}, nullptr,
      &threePasses, &threeSelected);
  require(threeSelected >= 2 && threePasses >= 3 &&
              threePasses <= PortalViewSlotCount,
          "public render-scene did not render bounded A -> B -> C -> A views");
  require(regionDifference(image, directed) > 0.0005,
          "three directed Portal destinations were not visibly distinguished");
}

void minesPortalRenders(editor::EditorRenderSystem& renderSystem) {
  bw::core::LayerBuildStep::registerCoreTypes();
  std::string error;
  if (!renderSystem.loadWorldDependencies({"MinesLayer"}, "World", &error))
    throw std::runtime_error(error);
  auto path = std::filesystem::path(BW_EDITOR_PROC_MATERIAL_MANIFEST).parent_path() /
              "world-mines-3.world.yaml";
  auto reader = std::shared_ptr<bw::core::YamlSerializer>(
      bw::core::YamlSerializer::fromFile(path.string()));
  reader->deserialize();
  bw::core::World world(1.0f, -1.0f);
  bw::core::SerializationWorkData workData{512.0f};
  if (!world.deserialize(reader, workData)) {
    for (auto const& message : world.getDeserializationErrors())
      std::cerr << message << '\n';
    throw std::runtime_error("mines world could not load");
  }
  auto data = world.getWorldData();
  require(data->findPortalLoop(0, 0) && data->findPortalLoop(0, 0)->active,
          "mines Portal loop inactive");
  editor::PreviewRenderScene scene(
      renderSystem, &world, kWidth, kHeight,
      bw::app::HorizontalMaterials::TwoDimensional);
  std::optional<std::array<uint64_t, 2>> publishedCounters;
  std::array<std::vector<float>, 6> approachingImages;
  // Check both the approach and the exact crossing plane.
  for (uint32_t sample = 0; sample < 30; ++sample) {
    auto endpoint = sample % 2;
    auto angleOffset = std::array{0.0f, -30.0f, 30.0f}[(sample % 6) / 2];
    auto distance = std::array{20.0f, 0.2f, 0.05f, 0.001f, 0.0f}[sample / 6];
    auto camera = std::make_shared<ReactiveCamera>(
        endpoint == 0
            ? glm::vec3{-60.0f + distance, -2.0f + BW_PLAYER_EYE_HEIGHT, 0.0f}
            : glm::vec3{-8.0f, -2.0f + BW_PLAYER_EYE_HEIGHT, 28.0f - distance},
        bw::app::cameraYaw((endpoint == 0 ? 270.0f : 180.0f) + angleOffset),
        0.0f, BW_PLAYER_FOV, kWidth / float(kHeight));
    camera->setClipDistances(0.1f, 1000000.0f);
    // Use the same three-frame warm-up as the other real scene tests.
    for (int frame = 0; frame < 3; ++frame) {
      // Keep lighting fixed while checking camera continuity; a Torch on the
      // aperture itself changes which light paths are eligible.
      auto torch = endpoint == 0 ? glm::vec3{-40.0f, BW_PLAYER_EYE_HEIGHT - 2.0f, 0.0f}
                                 : glm::vec3{-8.0f, BW_PLAYER_EYE_HEIGHT - 2.0f, 8.0f};
      auto texture = scene.render(&world, *data, camera, torch,
                                  1.0f / 60.0f, {}, -1, -1);
      if (!scene.renderedPortalView())
        throw std::runtime_error("mines Portal has no rendered view: endpoint " +
            std::to_string(endpoint) + ", distance " + std::to_string(distance));
      if (!publishedCounters) publishedCounters = scene.wallGeometryCounters();
      require(scene.wallGeometryCounters() == *publishedCounters,
              "moving between Portal views rebuilt or uploaded snapshot walls");
      auto image = readColour(texture);
      auto energy = centreRegionEnergy(image);
      if (frame == 2) {
        // A softly lit opaque back face is non-black but has no destination
        // material detail. Measure inside the aperture, away from its border.
        if (distance == 0.001f) approachingImages[sample % 6] = image;
        double discontinuity = 0.0;
        double detail = 0.0;
        size_t samples = 0;
        for (int y = 3 * kHeight / 8; y < 5 * kHeight / 8; ++y) {
          for (int x = 3 * kWidth / 8; x < 5 * kWidth / 8; ++x) {
            auto offset = (size_t(y) * kWidth + x) * 4;
            detail += std::abs(image[offset] - image[offset + 4]);
            if (distance == 0.0f)
              discontinuity += std::abs(image[offset] - approachingImages[sample % 6][offset]);
            ++samples;
          }
        }
        detail /= double(samples);
        if (discontinuity / double(samples) >= 0.02)
          throw std::runtime_error("Portal image jumps at the crossing plane: endpoint " +
              std::to_string(endpoint) + ", difference " +
              std::to_string(discontinuity / double(samples)));
        if (detail < 0.002)
          throw std::runtime_error("mines Portal lacks destination detail: endpoint " +
              std::to_string(endpoint) + ", distance " + std::to_string(distance) +
              ", adjacent-pixel difference " +
              std::to_string(detail));
      }
      if (frame == 2 && energy <= 0.02)
        throw std::runtime_error("mines Portal aperture is black: endpoint " +
            std::to_string(endpoint) + ", frame " + std::to_string(frame) +
            ", energy " + std::to_string(energy));
    }
  }
}

void immutableWallsRenderBothSides(editor::EditorRenderSystem& renderSystem) {
  using bw::core::ZoneId;
  std::vector<float> switchedZoneImage;
  auto back = render(renderSystem, {.lookAtWallBack = true, .zone = ZoneId::NegativeSpace},
                     nullptr, nullptr, nullptr, &switchedZoneImage);
  for (int y = kHeight / 4; y < 3 * kHeight / 4; ++y) {
    for (int x = kWidth / 4; x < 3 * kWidth / 4; ++x) {
      auto offset = (size_t(y) * kWidth + x) * 4;
      for (int channel = 0; channel < 3; ++channel)
        require(back[offset + channel] >= 0.999f,
                "Negative Space back is not pure white after shadows and AO");
    }
  }
  auto texturedBack = render(renderSystem, {.lookAtWallBack = true, .triplanar = true,
      .zone = ZoneId::NegativeSpace});
  auto decoratedBack = render(renderSystem,
      {.emboss = true, .wallMask = true, .lookAtWallBack = true,
       .zone = ZoneId::NegativeSpace});
  auto mappedBack = render(renderSystem,
      {.map = MapFixture::Image, .lookAtWallBack = true, .triplanar = true,
       .zone = ZoneId::NegativeSpace});
  auto wetBack = render(renderSystem, {.lookAtWallBack = true, .wet = true,
      .zone = ZoneId::NegativeSpace});
  auto omitted = render(renderSystem, {.lookAtWallBack = true});
  require(regionDifference(switchedZoneImage, omitted) < 0.0005,
          "runtime Zone change did not affect the very next frame");
  require(regionDifference(back, omitted) > 0.001,
          "Euclidean did not omit the Negative Space white wall back");
  for (auto fixture : {RenderFixture{}, RenderFixture{.map = MapFixture::Image, .bundledNormal = true},
                       RenderFixture{.wallMask = true}, RenderFixture{.triplanar = true},
                       RenderFixture{.emboss = true}}) {
    auto front = render(renderSystem, fixture);
    fixture.zone = ZoneId::NegativeSpace;
    require(regionDifference(front, render(renderSystem, fixture)) < 0.0005,
            "Zone changed authored wall front treatment");
    fixture.lookAtWallBack = true;
    require(regionDifference(back, render(renderSystem, fixture)) < 0.0005,
            "authored treatment leaked into Negative Space white back");
    fixture.zone = ZoneId::Euclidean;
    require(regionDifference(omitted, render(renderSystem, fixture)) < 0.0005,
            "authored treatment changed Euclidean wall omission");
  }
  for (auto zone : {ZoneId::Euclidean, ZoneId::NegativeSpace}) {
    for (bool reverse : {false, true}) {
      RenderFixture fixture{.lookAtWallBack = reverse, .zone = zone};
      auto ordinary = render(renderSystem, fixture);
      fixture.wallCollides = false;
      require(regionDifference(ordinary, render(renderSystem, fixture)) < 0.0005,
              "non-colliding wall changed Zone rendering");
      fixture.wallsVisible = false;
      auto hidden = render(renderSystem, fixture);
      fixture.zone = zone == ZoneId::Euclidean ? ZoneId::NegativeSpace : ZoneId::Euclidean;
      require(regionDifference(hidden, render(renderSystem, fixture)) < 0.0005 &&
                  regionDifference(hidden, ordinary) > 0.001,
              "globally hidden walls remained visible or changed with Zone");
    }
  }
  require(regionDifference(back, wetBack) < 0.0005,
          "dry reverse wall used the wet front's Liquid height");
  require(centreRegionEnergy(back) > 0.01, "immutable wall back rendered black");
  require(regionDifference(back, texturedBack) < 0.0005 &&
              regionDifference(back, decoratedBack) < 0.0005 &&
              regionDifference(back, mappedBack) < 0.0005,
          "wall back inherited authored texture, mask, or Embossing");
  // These fixtures also assert that a second and third render never rebuild
  // or upload walls, including Chip facets and both horizontal Programs.
  (void)render(renderSystem, {.chips = true, .wet = true});
  (void)render(renderSystem, {.chips = true, .lookAtWallBack = true, .wet = true});

  auto ordinaryPortals = render(renderSystem, {.portal = true});
  auto highIdPortals = render(renderSystem, {.portal = true, .manyPortalEndpoints = true});
  require(regionDifference(ordinaryPortals, highIdPortals) < 0.0005,
          "stable endpoint bucket IDs were confused with bounded GPU view slots");

  bw::core::World world(1.0f, -1.0f);
  auto data = buildWorldData(world, {.portal = true, .manyPortalEndpoints = true});
  editor::PreviewRenderScene scene(renderSystem, &world, kWidth, kHeight);
  auto draw = [&](glm::vec3 position, float yaw,
                  ZoneId zone = ZoneId::Euclidean) {
    auto camera = std::make_shared<ReactiveCamera>(position, bw::app::cameraYaw(yaw),
        0.0f, BW_PLAYER_FOV, kWidth / float(kHeight));
    camera->setClipDistances(0.1f, 1000000.0f);
    require(scene.render(&world, *data, camera, position, 1.0f / 60.0f,
                         {}, -1, -1, zone) != 0,
            "immutable wall scene produced no image");
  };
  draw({0.0f, BW_PLAYER_EYE_HEIGHT, 0.0f}, 0.0f);
  require(scene.portalRenderedPassCount() > scene.portalSelectedEndpointCount(),
          "immutability regression did not exercise recursive Portal cameras");
  auto baseline = scene.wallGeometryCounters();
  auto triangles = scene.worldSurfaceTriangleCount(WorldSurfaceSet::Walls);
  for (int frame = 0; frame < 16; ++frame) {
    draw({float(frame - 8), BW_PLAYER_EYE_HEIGHT, frame % 2 ? -32.0f : 0.0f},
         float(frame * 90), frame % 2 ? ZoneId::NegativeSpace : ZoneId::Euclidean);
    require(scene.wallGeometryCounters() == baseline,
            "camera movement/Portal visibility changed immutable wall buffers");
  }
  scene.worldGeometryChanged();
  draw({0.0f, BW_PLAYER_EYE_HEIGHT, 0.0f}, 0.0f);
  auto published = scene.wallGeometryCounters();
  require(published[0] == baseline[0] + 1 && published[1] == baseline[1] + 1,
          "world publication must rebuild/upload walls exactly once");
  require(scene.worldSurfaceTriangleCount(WorldSurfaceSet::Walls) == triangles,
          "unchanged snapshot publication changed the wall triangle count");
  draw({0.0f, BW_PLAYER_EYE_HEIGHT, 0.0f}, 270.0f);
  require(scene.wallGeometryCounters() == published,
          "post-publication view rebuilt wall buffers again");
}

void phantomWindowsRender(editor::EditorRenderSystem& renderSystem) {
  using bw::core::ZoneId;
  for (auto horizontal : {bw::app::HorizontalMaterials::ThreeDimensional,
                          bw::app::HorizontalMaterials::TwoDimensional}) {
    auto image = render(renderSystem, {.horizontal = horizontal,
        .zone = ZoneId::Phantom, .phantomWindow = true});
    auto at = [&](int x, int y, int channel) { return image[(y * kWidth + x) * 4 + channel]; };
    require(at(kWidth / 2, kHeight / 2, 0) > 0.001f ||
            at(kWidth / 2, kHeight / 2, 1) > 0.001f,
            "Phantom window did not show its Euclidean interior");
    for (int y = 0; y < kHeight; ++y)
      for (int x : {0, 20, kWidth - 21, kWidth - 1})
        for (int c = 0; c < 3; ++c)
          require(std::abs(at(x, y, c)) < 0.0001f,
                  "world geometry leaked outside Phantom aperture");
    auto reverse = render(renderSystem, {.horizontal = horizontal,
        .zone = ZoneId::Phantom, .phantomWindow = true, .phantomReverse = true});
    for (size_t pixel = 0; pixel < reverse.size(); pixel += 4)
      for (size_t c = 0; c < 3; ++c)
        require(std::abs(reverse[pixel + c]) < 0.0001f, "Phantom reverse side was rendered");
  }
  auto diagnostic = render(renderSystem, {.zone = ZoneId::Phantom,
      .phantomWindow = true, .phantomDiagnostic = true});
  auto obstructed = render(renderSystem, {.zone = ZoneId::Phantom,
      .phantomWindow = true, .phantomDiagnostic = true, .phantomObstruction = true});
  auto overlapping = render(renderSystem, {.zone = ZoneId::Phantom,
      .phantomWindow = true, .phantomDiagnostic = true, .phantomFarWindow = true});
  auto centre = (kHeight / 2 * kWidth + kWidth / 2) * 4;
  for (auto const* image : {&diagnostic, &obstructed, &overlapping})
    require((*image)[centre] > 0.9f && (*image)[centre + 1] < 0.01f && (*image)[centre + 2] > 0.9f,
        "Phantom clipping/nearest-aperture selection lost the diagnostic interior");
  auto counts = resourceCounts(renderSystem.renderResourceManager());
  (void)render(renderSystem, {.zone = ZoneId::Phantom, .phantomWindow = true});
  require(resourceCounts(renderSystem.renderResourceManager()) == counts,
      "Phantom preview teardown leaked aperture resources");
}

void portalLightIsClippedToTheRenderedApertureProjection(
    editor::EditorRenderSystem& renderSystem) {
  // Compile and exercise the production Portal-capable world programs first.
  // The focused two-sample draw below then isolates the aperture predicate
  // from procedural material variation and recursive Portal imagery.
  (void)render(renderSystem, {.portal = true});

  constexpr char const* vertexSource = R"(
#version 130
void main()
{
    vec2 vertices[3] = vec2[3](
        vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
}
)";
  constexpr char const* fragmentSource = R"(
#version 130
out vec4 colour;
uniform vec3 lightPosition;
uniform vec3 apertureCentre;
uniform vec3 apertureTangent;
uniform vec3 apertureFront;
uniform vec3 apertureBounds;

float apertureGate(vec3 receiverPosition)
{
    vec3 front = normalize(apertureFront);
    float receiverSide = dot(receiverPosition - apertureCentre, front);
    float lightSide = dot(lightPosition - apertureCentre, front);
    if (receiverSide <= 0.0001 || lightSide >= -0.0001) return 0.0;
    float denominator = lightSide - receiverSide;
    if (abs(denominator) <= 0.0001) return 0.0;
    float alongRay = -receiverSide / denominator;
    if (alongRay < 0.0 || alongRay > 1.0) return 0.0;
    vec3 intersection = receiverPosition +
        alongRay * (lightPosition - receiverPosition);
    float across = abs(dot(
        intersection - apertureCentre, normalize(apertureTangent)));
    return across <= apertureBounds.x &&
           intersection.y >= apertureBounds.y &&
           intersection.y <= apertureBounds.z ? 1.0 : 0.0;
}

void main()
{
    // Pixel zero maps through the resolved rectangle. Pixel one is the
    // immediately adjacent receiver whose plane hit lies just past its side.
    vec3 receiver = gl_FragCoord.x < 1.0
        ? vec3(8.0, 2.0, 0.0)
        : vec3(8.0, 2.0, -2.1);
    colour = vec4(apertureGate(receiver), 0.0, 0.0, 1.0);
}
)";

  auto compile = [](GLenum type, char const* source) {
    auto shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled{};
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
      glDeleteShader(shader);
      throw std::runtime_error("Portal light rendered-test shader failed");
    }
    return shader;
  };

  auto vertex = compile(GL_VERTEX_SHADER, vertexSource);
  auto fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);
  auto program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  GLint linked{};
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  if (!linked) {
    glDeleteProgram(program);
    throw std::runtime_error("Portal light rendered-test program failed");
  }

  GLint previousFramebuffer{}, previousProgram{}, previousViewport[4]{};
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
  glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
  glGetIntegerv(GL_VIEWPORT, previousViewport);
  GLuint texture{}, framebuffer{}, vertexArray{};
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(
      GL_TEXTURE_2D, 0, GL_RGBA32F, 2, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  glGenVertexArrays(1, &vertexArray);
  glBindVertexArray(vertexArray);
  glViewport(0, 0, 2, 1);
  glUseProgram(program);
  glUniform3f(glGetUniformLocation(program, "lightPosition"), 12.0f, 2.0f, 0.0f);
  glUniform3f(glGetUniformLocation(program, "apertureCentre"), 10.0f, 0.0f, 0.0f);
  glUniform3f(glGetUniformLocation(program, "apertureTangent"), 0.0f, 0.0f, -1.0f);
  glUniform3f(glGetUniformLocation(program, "apertureFront"), -1.0f, 0.0f, 0.0f);
  glUniform3f(glGetUniformLocation(program, "apertureBounds"), 1.0f, 0.0f, 4.0f);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  std::array<float, 8> pixels{};
  glReadPixels(0, 0, 2, 1, GL_RGBA, GL_FLOAT, pixels.data());

  glBindVertexArray(0);
  glDeleteVertexArrays(1, &vertexArray);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(1, &texture);
  glDeleteProgram(program);
  glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
  glUseProgram(previousProgram);
  glViewport(
      previousViewport[0], previousViewport[1],
      previousViewport[2], previousViewport[3]);

  require(pixels[0] > 0.99f,
          "rendered receiver inside the aperture projection was unlit");
  require(pixels[4] < 0.01f,
          "rendered receiver immediately outside the aperture projection was lit");
}

void triplanarMaterialsRenderThroughTheRealWorldPrograms(
    editor::EditorRenderSystem& renderSystem) {
  auto proceduralFloor = render(
      renderSystem, {.lookAtFloor = true});
  auto triplanarFloor = render(
      renderSystem, {.lookAtFloor = true, .triplanar = true});
  auto proceduralCeiling = render(
      renderSystem, {.lookAtCeiling = true});
  auto triplanarCeiling = render(
      renderSystem, {.lookAtCeiling = true, .triplanar = true});
  auto proceduralWall = render(renderSystem, {});
  auto triplanarWall = render(renderSystem, {.triplanar = true});
  auto wallOnlyTriplanar = render(
      renderSystem, {.triplanarWallOnly = true});
  auto angledProcedural = render(renderSystem, {.angled = true});
  auto angledTriplanar = render(
      renderSystem, {.triplanar = true, .angled = true});

  require(regionDifference(proceduralFloor, triplanarFloor) > 0.0005,
          "Triplanar albedo did not reach an isolated floor");
  require(regionDifference(proceduralCeiling, triplanarCeiling) > 0.0005,
          "Triplanar albedo did not reach an isolated ceiling");
  require(regionDifference(proceduralWall, triplanarWall) > 0.0005 &&
              regionDifference(angledProcedural, angledTriplanar) > 0.0005,
          "Triplanar albedo did not reach vertical walls at arbitrary angles");
  require(centreRegionEnergy(wallOnlyTriplanar) > 0.015,
          "an independently assigned Triplanar wall rendered black");

  auto triplanarSlope = render(
      renderSystem,
      {.lookAtFloor = true, .sloped = true, .triplanar = true});
  auto fragmentedTriplanarSlope = render(
      renderSystem,
      {.lookAtFloor = true,
       .sloped = true,
       .fragmented = true,
       .triplanar = true});
  // Separate triangle topology can perturb lit/shadowed edge coverage. Keep
  // enough tolerance for that while still rejecting a material-scale or phase
  // change across the broad comparison region.
  require(regionDifference(
              triplanarSlope, fragmentedTriplanarSlope) < 0.02,
          "Triplanar World scale or origin changed across fragments");
  auto triplanarFloor2d = render(
      renderSystem,
      {.horizontal = bw::app::HorizontalMaterials::TwoDimensional,
       .lookAtFloor = true,
       .triplanar = true});
  require(!triplanarFloor2d.empty(),
          "2D world Program did not render a Triplanar floor");
}

void triplanarComposesWithEstablishedSurfaceFeatures(
    editor::EditorRenderSystem& renderSystem) {
  auto baseline = render(
      renderSystem, {.triplanar = true, .angled = true});
  auto mapped = render(
      renderSystem,
      {.map = MapFixture::Image, .triplanar = true, .angled = true});
  auto mappedAndEmbossed = render(
      renderSystem,
      {.map = MapFixture::Image,
       .emboss = true,
       .triplanar = true,
       .angled = true});
  require(!mapped.empty() && !mappedAndEmbossed.empty(),
          "Triplanar normal-map/Emboss composition did not reach the real renderer");

  auto maskedTriplanar = render(
      renderSystem,
      {.wallMask = true, .triplanar = true, .angled = true});
  require(regionDifference(baseline, maskedTriplanar) < 0.0005,
          "authored wall mask affected a Triplanar material");
  auto subMaterial = render(renderSystem, {});
  auto maskedSubMaterial = render(renderSystem, {.wallMask = true});
  require(regionDifference(subMaterial, maskedSubMaterial) > 0.0005,
          "switching to a Sub-material did not restore the authored wall mask");

  std::array<uint32_t, 3> baselineTriangles;
  (void)render(renderSystem, {.triplanar = true}, &baselineTriangles);
  std::array<uint32_t, 3> chipTriangles;
  auto chipConfigured = render(
      renderSystem, {.chips = true, .triplanar = true}, &chipTriangles);
  require(!chipConfigured.empty() && chipTriangles == baselineTriangles,
          "Chip configuration changed Triplanar render geometry");

  auto alphaZero = render(
      renderSystem,
      {.lookAtFloor = true, .triplanar = true, .triplanarAlphaVariant = 1});
  auto alphaOne = render(
      renderSystem,
      {.lookAtFloor = true, .triplanar = true, .triplanarAlphaVariant = 2});
  require(regionDifference(alphaZero, alphaOne) < 0.0005,
          "RGBA albedo alpha changed Triplanar colour or opacity");

  for (auto horizontal : {bw::app::HorizontalMaterials::ThreeDimensional,
                          bw::app::HorizontalMaterials::TwoDimensional}) {
    auto slopedSurface = render(
        renderSystem,
        {.horizontal = horizontal,
         .lookAtFloor = true,
         .sloped = true,
         .triplanar = true});
    require(!slopedSurface.empty(),
            "sloped Triplanar surface did not render in a horizontal mode");
  }
}

void triplanarWallContinuityRendersThroughTheRealWorldProgram(
    editor::EditorRenderSystem& renderSystem) {
  // Three arbitrary-angle regions meet at the diamond's north Arrangement
  // vertex with floor/ceiling spans 0..48, 8..32, and 16..56. OreMask is an
  // asymmetric 2:1 diagnostic image, so the real Program exercises unequal
  // and multi-wall continuity without a symmetric texture hiding swaps.
  std::array<uint32_t, 3> proceduralTriangles;
  auto procedural = render(
      renderSystem, {.continuityJunction = true}, &proceduralTriangles);
  std::array<uint32_t, 3> triplanarTriangles;
  auto triplanar = render(
      renderSystem, {.triplanar = true, .continuityJunction = true},
      &triplanarTriangles);
  require(regionDifference(procedural, triplanar) > 0.0005,
          "asymmetric Triplanar diagnostic did not render at the unequal multi-wall junction");
  require(triplanarTriangles[2] > proceduralTriangles[2],
          "unequal junction spans did not reach the real renderer as split wall triangles");
}
}  // namespace

int main(int argc, char** argv) {
#if defined(__linux__)
  // GLEW is built with GLX support, so do not let SDL prefer Wayland and
  // create an EGL context that glewInit cannot use. Preserve explicit test
  // environment overrides for driver-specific testing.
  if (!SDL_getenv("SDL_VIDEODRIVER") &&
      !SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11")) {
    std::printf("Could not select SDL's X11 video driver for GLX\n");
    return 1;
  }
#endif
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::printf("SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  auto* window = SDL_CreateWindow(
      "world-renderer-smoke", kWidth, kHeight,
      SDL_WindowFlags(SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN));
  if (!window) {
    std::printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  auto context = SDL_GL_CreateContext(window);
  SDL_GL_MakeCurrent(window, context);
  glewExperimental = GL_TRUE;

  int result = 0;
  if (glewInit() != GLEW_OK) {
    std::printf("glewInit failed\n");
    result = 1;
  } else {
    try {
      writeDirectionalNormal();
      editor::EditorRenderSystem renderSystem(kWidth, kHeight);

      auto scenario = argc == 2 ? std::string(argv[1]) : std::string{};
      if (scenario == "triplanar") {
        triplanarMaterialsRenderThroughTheRealWorldPrograms(renderSystem);
      } else if (scenario == "triplanar-composition") {
        triplanarComposesWithEstablishedSurfaceFeatures(renderSystem);
      } else if (scenario == "triplanar-continuity") {
        triplanarWallContinuityRendersThroughTheRealWorldProgram(renderSystem);
      } else if (scenario == "portal") {
        portalRendersThroughPublicSceneAndNamedFinalOutput(renderSystem);
      } else if (scenario == "mines-portal") {
        minesPortalRenders(renderSystem);
      } else if (scenario == "reflection-shadow-zones") {
        reflectionShadowZonesRender(renderSystem);
      } else if (scenario == "portal-zones") {
        portalZonesRender(renderSystem);
      } else if (scenario == "detail-zones") {
        detailZonesRender(renderSystem);
      } else if (scenario == "phantom") {
        phantomWindowsRender(renderSystem);
      } else if (scenario == "horizontal-zones") {
        horizontalZonesRender(renderSystem);
      } else if (scenario == "immutable-walls") {
        immutableWallsRenderBothSides(renderSystem);
      } else if (scenario == "portal-light") {
        portalLightIsClippedToTheRenderedApertureProjection(renderSystem);
      } else {
        std::array<uint32_t, 3> drySurfaceTriangles;
      auto unset = render(renderSystem, {}, &drySurfaceTriangles);
      auto flatFloor = render(renderSystem, {.lookAtFloor = true});
      auto sloped = render(
          renderSystem, {.lookAtFloor = true, .sloped = true});
      require(regionDifference(flatFloor, sloped) > 0.0005,
              "evaluated sloped geometry rendered like the flat baseline");

      // Exercise the fast path itself. A horizontal floor and its reflected
      // ceiling have the historic X/Z orientation, while splitting one sloped
      // plane into separate Arrangement faces must neither restart its field
      // nor alter its in-plane material scale.
      auto flatFloor2d = render(
          renderSystem,
          {.horizontal = bw::app::HorizontalMaterials::TwoDimensional,
           .lookAtFloor = true});
      auto flatCeiling2d = render(
          renderSystem,
          {.horizontal = bw::app::HorizontalMaterials::TwoDimensional,
           .lookAtCeiling = true});
      require(regionDifference(flatFloor2d, flatCeiling2d) < 0.0005,
              "2D ceiling material did not retain the floor orientation");
      auto sloped2d = render(
          renderSystem,
          {.horizontal = bw::app::HorizontalMaterials::TwoDimensional,
           .lookAtFloor = true,
           .sloped = true});
      auto fragmentedSlope2d = render(
          renderSystem,
          {.horizontal = bw::app::HorizontalMaterials::TwoDimensional,
           .lookAtFloor = true,
           .sloped = true,
           .fragmented = true});
      require(regionDifference(sloped2d, fragmentedSlope2d) < 0.0005,
              "2D Surface frame changed across fragments of one sloped plane");

      std::array<uint32_t, 3> wetSurfaceTriangles;
      auto wet = render(renderSystem, {.wet = true}, &wetSurfaceTriangles);
      require(drySurfaceTriangles[0] == wetSurfaceTriangles[0] &&
                  drySurfaceTriangles[1] == 0 &&
                  wetSurfaceTriangles[1] != 0 &&
                  drySurfaceTriangles[2] == wetSurfaceTriangles[2],
              "wet and dry Worlds did not partition Horizontal, Liquid, and Walls");
      std::array<uint32_t, 3> wetSlopeSurfaceTriangles;
      auto wetSlope = render(
          renderSystem, {.wet = true, .sloped = true},
          &wetSlopeSurfaceTriangles);
      require(!wetSlope.empty() && wetSlopeSurfaceTriangles[1] != 0,
              "a sloped basin produced no clipped Liquid render geometry");
      // This overhead fixture intentionally has no high-contrast reflection
      // source. Its Liquid F0 response can therefore be below a broad-image
      // difference threshold; topology and WaterScene selection are covered
      // by the Liquid SSR contract test instead.

      auto disabled = render(
          renderSystem, {.map = MapFixture::Disabled});
      auto flat = render(
          renderSystem, {.map = MapFixture::Image, .strength = 0.0f});
      auto authored = render(
          renderSystem, {.map = MapFixture::Image, .strength = 1.0f});
      auto stronger = render(
          renderSystem, {.map = MapFixture::Image, .strength = 2.0f});
      // Chips and Wedges share the real opaque mesh consumed by both the lit
      // and point-shadow passes. Compare otherwise-identical chipped scenes.
      auto wedgeBaseline = render(
          renderSystem, {.chips = true, .lookAtWedges = true});
      auto wedges = render(
          renderSystem,
          {.chips = true, .wedges = true, .lookAtWedges = true});

      auto disabledDifference = regionDifference(unset, disabled);
      auto flatDifference = regionDifference(unset, flat);
      auto authoredDifference = regionDifference(unset, authored);
      auto strongerDifference = regionDifference(unset, stronger);
      std::printf(
          "regions: disabled=%.6f flat=%.6f authored=%.6f stronger=%.6f\n",
          disabledDifference, flatDifference, authoredDifference,
          strongerDifference);
      require(disabledDifference < 0.0005,
              "Disabled did not render through the ordinary unmapped path");
      require(flatDifference < 0.0005,
              "strength zero did not produce a flat image contribution");
      require(authoredDifference > 0.0005,
              "authored-strength normal map did not visibly affect the wall");
      require(strongerDifference > 0.0005 &&
                  regionDifference(authored, stronger) > 0.0005,
              "higher strength did not visibly change the renormalized map");
      require(regionDifference(wedgeBaseline, wedges) > 0.0005,
              "Wedges did not reach the ordinary lit renderer path");

      // A global material-index/Technique diagnostic changes only procedural
      // evaluation. The independently authored wall image must remain active.
      auto debugUnset = render(
          renderSystem, {.debugWallTechnique = 3});
      auto debugMapped = render(
          renderSystem,
          {.map = MapFixture::Image, .debugWallTechnique = 3});
      require(regionDifference(debugUnset, debugMapped) > 0.0005,
              "debug Technique override suppressed the wall normal map");

      // Marble contributes its own procedural normal. Add preset Embossing
      // and prove that both the earlier image contribution and the later
      // relief remain observable.
      auto embossedUnset = render(
          renderSystem, {.emboss = true});
      auto embossedMapped = render(
          renderSystem, {.map = MapFixture::Image, .emboss = true});
      auto embossedImageDifference =
          regionDifference(embossedUnset, embossedMapped);
      auto embossDifference = regionDifference(authored, embossedMapped);
      std::printf(
          "composition: image=%.6f emboss=%.6f\n", embossedImageDifference,
          embossDifference);
      require(embossedImageDifference > 0.0005,
              "Technique or Embossing replaced the earlier Image normal");
      require(embossDifference > 0.000001,
              "Image normal replaced the later Embossing contribution");

      // Compiles/renders the parallel 2D horizontal PBR program while mapped
      // walls continue through the 3D program. Horizontal buckets always bind
      // the compatible map contract disabled.
      auto horizontal2d = render(
          renderSystem,
          {.map = MapFixture::MixedSharedImage,
           .horizontal = bw::app::HorizontalMaterials::TwoDimensional});
      require(!horizontal2d.empty(),
              "2D horizontal mode did not render with the shared contract");

      // Four walls share one Sub-material and one image reference while using
      // distinct scale/strength/Disabled configurations. Repeated real preview
      // teardown must return all CPU/GPU-facing mpp resources to one baseline.
      std::optional<ResourceCounts> baseline;
      for (int cycle = 0; cycle < 4; ++cycle) {
        (void)render(
            renderSystem,
            {.map = MapFixture::MixedSharedImage,
             .horizontal = cycle % 2 == 0
                               ? bw::app::HorizontalMaterials::ThreeDimensional
                               : bw::app::HorizontalMaterials::TwoDimensional});
        auto counts = resourceCounts(renderSystem.renderResourceManager());
        if (!baseline) {
          baseline = counts;
        } else {
          require(counts == *baseline,
                  "preview teardown leaked normal-map CPU/GPU resources");
        }
      }
      }

      std::filesystem::remove(kDirectionalNormal);
    } catch (std::exception const& error) {
      std::printf("FAILED: %s\n", error.what());
      result = 1;
    }
  }

  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  if (result == 0) std::printf("WorldRenderer smoke test passed\n");
  return result;
}
