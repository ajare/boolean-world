// Manual/opt-in smoke test: requires a real OpenGL driver. It replaces the
// deleted PreviewMaterialProgram smoke test with the render path the editor
// now uses: EditorRenderSystem + PreviewRenderScene/WorldRenderer over a
// small hand-built Arrangement.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <vector>

#include <GL/glew.h>
#include <SDL3/SDL.h>

#include <common/GameDefines.h>
#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/Defines.h>
#include <core/MeshPrimitive.h>
#include <core/World.h>

#include "EditorRenderSystem.h"
#include "PlayerView.h"
#include "PreviewRenderScene.h"
#include "ReactiveCamera.h"

namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;

constexpr char const* kDirectionalNormal = "directional-normal.png";

void writeDirectionalNormal() {
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
    bw::core::World& world, bool mapped) {
  world.createAccelerationGrids(16.0f);
  bw::core::ClosedPolygon ring{
      {{-16, -16}}, {{16, -16}}, {{16, 16}}, {{-16, 16}}};
  auto* primitive = bw::core::MeshPrimitive::fromTree(
      bw::core::Primitive::Operation::Union, {{{ring, {}}}});
  if (mapped) {
    auto proxy = primitive->createEditingProxy();
    auto image = bw::core::WallNormalMapOverride::image(
        kDirectionalNormal, 8.0f, 1.0f);
    auto mappedEdge = proxy->getFirstEdgeIndex();
    float highestMidpoint = -std::numeric_limits<float>::infinity();
    for (auto edge = proxy->getFirstEdgeIndex();
         !proxy->edgeIndexIterationFinished(edge);
         edge = proxy->getNextEdgeIndex(edge)) {
      auto const& value = proxy->getEdge(edge);
      auto midpoint =
          (proxy->getVertex(value.getFirstVertex()).getPosition() +
           proxy->getVertex(value.getSecondVertex()).getPosition()) /
          2.0f;
      if (midpoint.y > highestMidpoint) {
        highestMidpoint = midpoint.y;
        mappedEdge = edge;
      }
    }
    proxy->setEdgeNormalMapOverride(mappedEdge, image);
    proxy->commitTo(*primitive);
  }
  auto properties = primitive->getProperties();
  properties.floorZ = 0.0f;
  properties.ceilingZ = 48.0f;
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
      auto render = [&](bool mapped) {
        bw::core::World world(1.0f, -1.0f);
        auto worldData = buildWorldData(world, mapped);
        editor::PreviewRenderScene scene(
            renderSystem, &world, kWidth, kHeight,
            bw::app::HorizontalMaterials::ThreeDimensional);
        auto camera = std::make_shared<ReactiveCamera>(
            glm::vec3{0.0f, BW_PLAYER_EYE_HEIGHT, 0.0f},
            bw::app::cameraYaw(0.0f), 0.0f, BW_PLAYER_FOV,
            kWidth / float(kHeight));
        camera->setClipDistances(0.1f, 1000000.0f);
        auto texture = scene.render(
            &world, *worldData, camera, camera->getPosition(), 1.0f / 60.0f);
        if (texture == 0)
          throw std::runtime_error("WorldRenderer produced no render texture");
        return readColour(texture);
      };
      auto unmapped = render(false);
      auto mapped = render(true);
      double difference = 0.0;
      for (size_t i = 0; i < mapped.size(); i += 4) {
        difference += std::abs(mapped[i] - unmapped[i]) +
                      std::abs(mapped[i + 1] - unmapped[i + 1]) +
                      std::abs(mapped[i + 2] - unmapped[i + 2]);
      }
      difference /= double(kWidth) * kHeight;
      if (difference < 0.0001) {
        std::printf("FAILED: directional mapped wall did not visibly differ from Unset\n");
        result = 1;
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
  if (result == 0) {
    std::printf("WorldRenderer smoke test passed\n");
  }
  return result;
}
