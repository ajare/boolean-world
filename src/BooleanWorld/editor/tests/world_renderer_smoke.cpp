// Manual/opt-in integration test: requires a real OpenGL driver. It drives the
// same EditorRenderSystem + PreviewRenderScene + WorldRenderer stack used by
// the editor, and compares broad rendered regions rather than exact pixels.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
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
#include <core/World.h>
#include <mpp/ResourceManager.h>

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
  bool chips{};
  bool wedges{};
  bool lookAtWedges{};
  bool wet{};
};

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
  bw::core::ClosedPolygon ring{
      {{-16, -16}}, {{16, -16}}, {{16, 16}}, {{-16, 16}}};
  auto* primitive = bw::core::MeshPrimitive::fromTree(
      bw::core::Primitive::Operation::Union, {{{ring, {}}}});

  if (fixture.map != MapFixture::Unset) {
    auto proxy = primitive->createEditingProxy();
    size_t ordinal = 0;
    for (auto edge = proxy->getFirstEdgeIndex();
         !proxy->edgeIndexIterationFinished(edge);
         edge = proxy->getNextEdgeIndex(edge), ++ordinal) {
      auto image = bw::core::WallNormalMapOverride::image(
          kDirectionalNormal,
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
    proxy->commitTo(*primitive);
  }

  auto properties = primitive->getProperties();
  properties.floorZ = 0.0f;
  properties.ceilingZ = 48.0f;
  properties.floorMaterialId = "migrated.marble.1";
  properties.ceilingMaterialId = "migrated.marble.1";
  properties.wallMaterialId = "migrated.marble.1";
  properties.liquidLevel = fixture.wet ? 8.0f : 0.0f;
  primitive->setProperties(properties);
  world.addPrimitive(primitive);
  std::vector<bw::core::Primitive*> primitives{primitive};
  if (fixture.chips) {
    bw::core::ClosedPolygon platformRing{
        {{-8, -8}}, {{8, -8}}, {{8, 8}}, {{-8, 8}}};
    auto* platform = bw::core::MeshPrimitive::fromTree(
        bw::core::Primitive::Operation::Union, {{{platformRing, {}}}});
    auto platformProperties = platform->getProperties();
    platformProperties.floorZ = 8.0f;
    platformProperties.ceilingZ = 48.0f;
    platformProperties.floorMaterialId = "migrated.marble.1";
    platformProperties.ceilingMaterialId = "migrated.marble.1";
    platformProperties.wallMaterialId = "migrated.marble.1";
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
    generator.setChipParametersResolver([](std::string const&) {
      return bw::core::ChipGenerationParameters{
          2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 256.0f, 1.0f};
    });
  }
  generator.generate(primitives);
  auto result = std::make_shared<bw::core::ArrangementWorldData>(
      generator.getWorldData(), world.getExtents(),
      float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX),
      world.getStepThreshold(), nullptr,
      world.getWedgeGenerationParameters());
  if (fixture.chips && result->getDetail().getChipCount() == 0) {
    throw std::runtime_error("renderer fixture generated no Chips");
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
    std::array<uint32_t, 3>* surfaceTriangles = nullptr) {
  bw::core::World world(1.0f, -1.0f);
  auto worldData = buildWorldData(world, fixture);
  editor::PreviewRenderScene scene(
      renderSystem, &world, kWidth, kHeight, fixture.horizontal);
  if (fixture.emboss) {
    bw::core::EmbossData emboss;
    emboss.pattern = bw::core::EmbossPattern::Square;
    emboss.radius = 4.0f;
    emboss.depth = 1.0f;
    scene.updateMaterialDraft(
        "migrated.marble.1", 0,
        {1.1f, 6.0f, 18.0f, 0.15f, 0.25f, 0.65f, 0.2f, 0.5f},
        {0.18f, 0.18f, 0.2f}, emboss);
  }
  auto camera = std::make_shared<ReactiveCamera>(
      glm::vec3{
          0.0f, fixture.lookAtWedges ? 40.0f : BW_PLAYER_EYE_HEIGHT, 0.0f},
      bw::app::cameraYaw(0.0f), 0.0f, BW_PLAYER_FOV,
      kWidth / float(kHeight));
  camera->setClipDistances(0.1f, 1000000.0f);
  uint32_t texture{};
  for (int frame = 0; frame < 3; ++frame) {
    texture = scene.render(
        &world, *worldData, camera, camera->getPosition(), 1.0f / 60.0f, {},
        -1, fixture.debugWallTechnique);
  }
  if (texture == 0)
    throw std::runtime_error("WorldRenderer produced no render texture");
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
}  // namespace

int main() {
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

      std::array<uint32_t, 3> drySurfaceTriangles;
      auto unset = render(renderSystem, {}, &drySurfaceTriangles);
      std::array<uint32_t, 3> wetSurfaceTriangles;
      auto wet = render(renderSystem, {.wet = true}, &wetSurfaceTriangles);
      require(drySurfaceTriangles[0] == wetSurfaceTriangles[0] &&
                  drySurfaceTriangles[1] == 0 &&
                  wetSurfaceTriangles[1] != 0 &&
                  drySurfaceTriangles[2] == wetSurfaceTriangles[2],
              "wet and dry Worlds did not partition Horizontal, Liquid, and Walls");
      require(regionDifference(unset, wet) < 0.0005,
              "separating Liquid changed the transparent-interface appearance");

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

      // Marble contributes its own procedural normal. Add material Embossing
      // through the preview's real draft path and prove that both the earlier
      // image contribution and the later relief remain observable.
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
