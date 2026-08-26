// Not a unit test - creates a real GL context and drives the 3D preview's
// whole render stack headlessly: EditorRenderSystem, then a PreviewRenderScene
// (mpp::Scene + RenderPipeline + the game's WorldRenderer) over a real
// Arrangement built from world-test-1.yaml, rendered through the same
// renderScene/getGraphImageRenderTarget path Preview3D.cpp uses.
//
// It runs that build-render-destroy cycle repeatedly, which is what makes it
// worth having: GitHub issue #270 asks for repeated preview open/close not to
// leak GPU resources, and mpp::ResourceManager's own counts settling after
// the first cycle is the evidence for that. See PreviewRenderScene.h for why
// teardown order matters.
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <SDL3/SDL.h>

#include <mpp/ResourceManager.h>

#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/Defines.h>
#include <core/World.h>
#include <core/YamlSerializer.h>

#include <common/GameDefines.h>

#include "EditorRenderSystem.h"
#include "PlayerView.h"
#include "PreviewRenderScene.h"
#include "ReactiveCamera.h"

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 480;
constexpr int kCycles = 20;

struct ResourceCounts {
  uint32_t resources{}, declared{}, created{}, loaded{};

  bool operator==(ResourceCounts const& other) const {
    return resources == other.resources && declared == other.declared &&
           created == other.created && loaded == other.loaded;
  }
};

ResourceCounts read(mpp::ResourceManager* resourceMgr) {
  ResourceCounts counts;
  resourceMgr->getResourceCounts(
      counts.resources, counts.declared, counts.created, counts.loaded);
  return counts;
}

std::unique_ptr<bw::core::World> loadWorld(std::string const& path) {
  std::ifstream stream(path);
  if (!stream) {
    throw std::runtime_error("Could not open test world: " + path);
  }
  std::ostringstream text;
  text << stream.rdbuf();

  auto serializer = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString(text.str()));
  serializer->deserialize();

  auto world = std::make_unique<bw::core::World>(1.0f, -1.0f);
  auto workData = bw::core::SerializationWorkData{512.0f};
  if (!world->deserialize(serializer, workData)) {
    throw std::runtime_error("Could not deserialize test world: " + path);
  }
  return world;
}

// The same Arrangement build openPreview3D performs, over every Primitive in
// the world rather than a selection-scoped subset.
bw::core::ArrangementWorldDataPtr buildWorldData(bw::core::World* world) {
  std::vector<bw::core::Primitive*> primitives;
  primitives.reserve(world->getNumPrimitives());
  for (uint32_t i = 0; i < world->getNumPrimitives(); ++i) {
    primitives.push_back(world->getPrimitive(i));
  }

  bw::core::ArrangementWorldDataGenerator generator;
  generator.generate(primitives);
  return std::make_shared<bw::core::ArrangementWorldData>(
      generator.getWorldData(), world->getExtents(),
      float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX),
      world->getStepThreshold());
}

}  // namespace

int main() {
  // Unbuffered: a driver-level crash mid-cycle must not swallow the progress
  // printed up to that point, which is the only clue to where it happened.
  setvbuf(stdout, nullptr, _IONBF, 0);

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    printf("SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  auto* window = SDL_CreateWindow(
      "preview-render-stack-smoke", kWidth, kHeight,
      (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN));
  if (!window) {
    printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
    return 1;
  }
  auto context = SDL_GL_CreateContext(window);
  SDL_GL_MakeCurrent(window, context);

  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) {
    printf("glewInit failed\n");
    return 1;
  }

  int result = 0;
  try {
    auto world = loadWorld(BW_EDITOR_PREVIEW_TEST_WORLD);
    auto worldData = buildWorldData(world.get());
    printf(
        "world: %u primitives, %zu triangles, %zu walls\n",
        world->getNumPrimitives(), worldData->getTriangles().size(),
        worldData->getWalls().size());

    editor::EditorRenderSystem renderSystem(kWidth, kHeight);

    ResourceCounts baseline;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
      auto camera = std::make_shared<ReactiveCamera>(
          glm::vec3{0.0f, BW_PLAYER_EYE_HEIGHT, 0.0f}, bw::app::cameraYaw(0.0f),
          0.0f, BW_PLAYER_FOV, kWidth / (float)kHeight);
      camera->setClipDistances(0.1f, 1000000.0f);

      {
        editor::PreviewRenderScene scene(
            renderSystem, world.get(), kWidth, kHeight);

        // More than one frame, so the wall provider's per-frame rebuild is
        // exercised rather than only its first pass.
        uint32_t textureId = 0;
        for (int frame = 0; frame < 3; ++frame) {
          textureId = scene.render(
              world.get(), *worldData, camera, camera->getPosition(),
              1.0f / 60.0f);
        }

        if (textureId == 0) {
          printf("FAILED: cycle %d produced no output texture\n", cycle);
          result = 1;
          break;
        }
      }

      auto counts = read(renderSystem.renderResourceManager());
      printf(
          "cycle %d: resources=%u declared=%u created=%u loaded=%u\n", cycle,
          counts.resources, counts.declared, counts.created, counts.loaded);

      // The first cycle is what establishes the steady state: it is allowed
      // to add whatever the world's materials need. Every cycle after it must
      // give back exactly what it took.
      if (cycle == 0) {
        baseline = counts;
      } else if (!(counts == baseline)) {
        printf(
            "FAILED: resource counts drifted from the first cycle's "
            "resources=%u declared=%u created=%u loaded=%u\n",
            baseline.resources, baseline.declared, baseline.created,
            baseline.loaded);
        renderSystem.renderResourceManager()->dumpResources(
            "preview-render-stack-smoke-resources.csv");
        result = 1;
        break;
      }
    }
  } catch (std::exception const& ex) {
    printf("FAILED: exception: %s\n", ex.what());
    result = 1;
  }

  auto error = glGetError();
  if (error != GL_NO_ERROR) {
    printf("GL error after teardown: 0x%x\n", error);
    result = 1;
  }

  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();

  if (result == 0) {
    printf(
        "PASSED: %d preview open/render/close cycles with flat GPU resource "
        "counts\n",
        kCycles);
  }
  return result;
}
