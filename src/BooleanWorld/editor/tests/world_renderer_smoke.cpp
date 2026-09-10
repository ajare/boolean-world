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
  bool wallMask{};
  bool chips{};
  bool wedges{};
  bool lookAtWedges{};
  bool lookAtFloor{};
  bool lookAtCeiling{};
  bool wet{};
  bool sloped{};
  bool fragmented{};
  bool triplanar{};
  bool angled{};
  bool continuityJunction{};
  // 0 uses the built-in material; 1 and 2 use identical RGB with alpha 0/1.
  int triplanarAlphaVariant{};
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

  if (fixture.map != MapFixture::Unset || fixture.wallMask) {
    auto proxy = primitive->createEditingProxy();
    auto normalImageReference = fixture.triplanar
                                    ? "World/TriplanarCompositionNormal"
                                    : kDirectionalNormal;
    auto maskImageReference = fixture.wallMask
                                  ? "World/OreMask"
                                  : kDirectionalNormal;
    size_t ordinal = 0;
    for (auto edge = proxy->getFirstEdgeIndex();
         !proxy->edgeIndexIterationFinished(edge);
         edge = proxy->getNextEdgeIndex(edge), ++ordinal) {
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
  if (fixture.triplanar) {
    auto materialName = fixture.continuityJunction
                            ? "World/TriplanarWallContinuityDiagnostic"
                        : fixture.triplanarAlphaVariant == 1
                            ? "World/TriplanarAlphaZeroDiagnostic"
                        : fixture.triplanarAlphaVariant == 2
                            ? "World/TriplanarAlphaOneDiagnostic"
                            : "World/TriplanarFloorTiles";
    auto material = bw::core::SurfaceMaterialReference::triplanar(materialName);
    properties.floorMaterial = material;
    properties.ceilingMaterial = material;
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
  properties.liquidLevel = fixture.wet ? 2.0f : 0.0f;
  primitive->setProperties(properties);
  world.addPrimitive(primitive);
  std::vector<bw::core::Primitive*> primitives{primitive};
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
    generator.setChipParametersResolver([](std::string const&) {
      return bw::core::ChipGenerationParameters{
          2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 256.0f, 1.0f};
    });
  }
  generator.generate(primitives);
  auto result = std::make_shared<bw::core::ArrangementWorldData>(
      generator.getWorldData(), world.getExtents(),
      float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX), nullptr,
      world.getWedgeGenerationParameters());
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
  std::string dependencyError;
  auto triplanarDependency = fixture.continuityJunction
                                 ? "World/TriplanarWallContinuityDiagnostic"
                             : fixture.triplanarAlphaVariant == 1
                                 ? "World/TriplanarAlphaZeroDiagnostic"
                             : fixture.triplanarAlphaVariant == 2
                                 ? "World/TriplanarAlphaOneDiagnostic"
                                 : "World/TriplanarFloorTiles";
  auto dependencies = fixture.triplanar
                          ? std::vector<std::string>{triplanarDependency}
                          : std::vector<std::string>{};
  if (fixture.wallMask) {
    dependencies.emplace_back("World/OreMask");
  }
  if (fixture.triplanar && fixture.map != MapFixture::Unset) {
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
  editor::PreviewRenderScene scene(
      renderSystem, &world, kWidth, kHeight, fixture.horizontal);
  auto camera = std::make_shared<ReactiveCamera>(
      glm::vec3{
          0.0f,
          fixture.lookAtCeiling
              ? 8.0f
              : (fixture.lookAtWedges || fixture.lookAtFloor
                     ? 40.0f
                     : BW_PLAYER_EYE_HEIGHT),
          0.0f},
      bw::app::cameraYaw(0.0f),
      fixture.lookAtCeiling ? -45.0f : (fixture.lookAtFloor ? 45.0f : 0.0f),
      BW_PLAYER_FOV,
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

  auto triplanarSlope = render(
      renderSystem,
      {.lookAtFloor = true, .sloped = true, .triplanar = true});
  auto fragmentedTriplanarSlope = render(
      renderSystem,
      {.lookAtFloor = true,
       .sloped = true,
       .fragmented = true,
       .triplanar = true});
  require(regionDifference(
              triplanarSlope, fragmentedTriplanarSlope) < 0.0005,
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
