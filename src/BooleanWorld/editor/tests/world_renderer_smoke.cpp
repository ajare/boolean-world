// Manual/opt-in smoke test: requires a real OpenGL driver. It replaces the
// deleted PreviewMaterialProgram smoke test with the render path the editor
// now uses: EditorRenderSystem + PreviewRenderScene/WorldRenderer over a
// small hand-built Arrangement.
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <vector>

#include <GL/glew.h>
#include <SDL3/SDL.h>

#include <common/GameDefines.h>
#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/Defines.h>
#include <core/RectanglePolygon.h>
#include <core/World.h>

#include "EditorRenderSystem.h"
#include "PlayerView.h"
#include "PreviewRenderScene.h"
#include "ReactiveCamera.h"

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;

bw::core::ArrangementWorldDataPtr buildWorldData(bw::core::World& world) {
  world.createAccelerationGrids(16.0f);
  auto* primitive = new bw::core::RectanglePolygon(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f);
  auto properties = primitive->getProperties();
  properties.floorZ = 0.0f;
  properties.ceilingZ = 16.0f;
  properties.floorMaterialId = "migrated.marble.1";
  properties.ceilingMaterialId = "migrated.marble.1";
  properties.wallMaterialId = "migrated.marble.1";
  primitive->setProperties(properties);
  world.addPrimitive(primitive);

  std::vector<bw::core::Primitive*> primitives{primitive};
  bw::core::ArrangementWorldDataGenerator generator;
  generator.generate(primitives);
  return std::make_shared<bw::core::ArrangementWorldData>(
      generator.getWorldData(), world.getExtents(),
      float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX),
      world.getStepThreshold());
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
      bw::core::World world(1.0f, -1.0f);
      auto worldData = buildWorldData(world);
      editor::EditorRenderSystem renderSystem(kWidth, kHeight);
      editor::PreviewRenderScene scene(
          renderSystem, &world, kWidth, kHeight);
      auto camera = std::make_shared<ReactiveCamera>(
          glm::vec3{0.0f, BW_PLAYER_EYE_HEIGHT, 0.0f},
          bw::app::cameraYaw(0.0f), 0.0f, BW_PLAYER_FOV,
          kWidth / float(kHeight));
      camera->setClipDistances(0.1f, 1000000.0f);

      auto texture = scene.render(
          &world, *worldData, camera, camera->getPosition(), 1.0f / 60.0f);
      if (texture == 0) {
        std::printf("FAILED: WorldRenderer produced no render texture\n");
        result = 1;
      }
    } catch (std::exception const& error) {
      std::printf("FAILED: %s\n", error.what());
      result = 1;
    }
  }

  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  if (result == 0) {
    std::printf("WorldRenderer smoke test passed\n");
  }
  return result;
}
