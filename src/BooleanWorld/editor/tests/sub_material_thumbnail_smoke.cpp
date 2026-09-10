// GPU smoke test for the Selected surface Sub-material thumbnail grid.
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <SDL3/SDL.h>

#include <mpp/Camera.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/Defines.h>
#include <core/MeshPrimitive.h>
#include <core/World.h>

#include "EditorRenderSystem.h"
#include "PreviewRenderScene.h"
#include "ProcMaterialLibrary.h"
#include "SubMaterialThumbnailRenderer.h"

namespace {

std::vector<float> texturePixels(
    std::uint32_t texture, std::uint32_t size) {
  std::uint32_t framebuffer{};
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(
      GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  std::vector<float> pixels(size * size * 4u);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glReadPixels(0, 0, size, size, GL_RGBA, GL_FLOAT, pixels.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &framebuffer);
  return pixels;
}

float textureDifference(
    std::uint32_t first, std::uint32_t second, std::uint32_t size) {
  auto firstPixels = texturePixels(first, size);
  auto secondPixels = texturePixels(second, size);
  double total{};
  for (std::size_t i = 0; i < firstPixels.size(); i += 4) {
    total += std::abs(firstPixels[i] - secondPixels[i]);
    total += std::abs(firstPixels[i + 1] - secondPixels[i + 1]);
    total += std::abs(firstPixels[i + 2] - secondPixels[i + 2]);
  }
  return static_cast<float>(total / (size * size * 3.0));
}

float averageBrightness(std::uint32_t texture, std::uint32_t size) {
  auto pixels = texturePixels(texture, size);
  double total{};
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    total += pixels[i] + pixels[i + 1] + pixels[i + 2];
  }
  return static_cast<float>(total / (size * size * 3.0));
}

}  // namespace

int main() {
  if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  auto* window = SDL_CreateWindow(
      "thumbnail smoke", 128, 128,
      static_cast<SDL_WindowFlags>(SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN));
  if (!window) return 1;
  auto context = SDL_GL_CreateContext(window);
  SDL_GL_MakeCurrent(window, context);
  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) return 1;

  int result{};
  try {
    editor::procMaterialLibrary().load(BW_EDITOR_PROC_MATERIAL_MANIFEST);
    editor::EditorRenderSystem renderSystem(128, 128);
    {
      editor::SubMaterialThumbnailRenderer thumbnails(renderSystem);

      auto triplanarMaterials = editor::discoverLoadedTriplanarMaterials(
          renderSystem.resourceManager());
      std::size_t loadedTriplanarCount{};
      for (auto const& resource : renderSystem.resourceManager()->getResourcesByType("TriplanarMaterial")) {
        if (renderSystem.resourceManager()->isResourceLoaded(resource)) {
          ++loadedTriplanarCount;
        }
      }
      if (triplanarMaterials.size() != loadedTriplanarCount ||
          triplanarMaterials.empty()) {
        std::printf(
            "FAILED: picker discovered %zu of %zu loaded Triplanar materials\n",
            triplanarMaterials.size(), loadedTriplanarCount);
        result = 1;
      }

      std::uint32_t floorTilesTexture{};
      std::uint32_t continuityTexture{};
      for (auto const& material : triplanarMaterials) {
        auto texture = thumbnails.triplanarTexture(material.resourceName);
        if (!texture || !glIsTexture(texture)) {
          std::printf("FAILED: no Triplanar thumbnail for %s\n",
                      material.resourceName.c_str());
          result = 1;
          continue;
        }
        if (!thumbnails.triplanarThumbnailUsesConnectedWallProjection(
                material.resourceName)) {
          std::printf(
              "FAILED: %s thumbnail is not a floor plus connected-wall corner\n",
              material.resourceName.c_str());
          result = 1;
        }
        if (material.resourceName == "World/TriplanarFloorTiles") {
          floorTilesTexture = texture;
        } else if (material.resourceName ==
                   "World/TriplanarWallContinuityDiagnostic") {
          continuityTexture = texture;
        }
      }
      auto imageDifference =
          floorTilesTexture && continuityTexture
              ? textureDifference(
                    floorTilesTexture, continuityTexture,
                    editor::SubMaterialThumbnailRenderer::size)
              : 0.0f;
      if (imageDifference < 0.005f) {
        std::printf(
            "FAILED: selected Triplanar images are not visible in thumbnails "
            "(difference=%.4f)\n",
            imageDifference);
        result = 1;
      }

      std::size_t rendered{};
      for (auto const& catalog : editor::procMaterialLibrary().catalogs()) {
        for (auto const& material : catalog.data.subMaterials) {
          auto texture = thumbnails.texture(material.id);
          if (!texture || !glIsTexture(texture)) {
            std::printf("FAILED: no thumbnail for %s\n", material.id.c_str());
            result = 1;
            break;
          }
          ++rendered;
        }
      }
      if (!rendered) {
        std::printf("FAILED: no Sub-materials were rendered\n");
        result = 1;
      } else {
        std::printf("rendered=%zu\n", rendered);
      }

      auto const& draftMaterial =
          editor::procMaterialLibrary().catalogs().front().data.subMaterials.front();
      auto darkDraft = thumbnails.draftTexture(
          draftMaterial.id, draftMaterial.materialIndex,
          draftMaterial.paramValues, {0.0f, 0.0f, 0.0f});
      auto darkBrightness = darkDraft
                                ? averageBrightness(
                                      darkDraft,
                                      editor::SubMaterialThumbnailRenderer::size)
                                : 0.0f;
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, 128, 128);
      glClearColor(0.125f, 0.25f, 0.5f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      auto brightDraft = thumbnails.draftTexture(
          draftMaterial.id, draftMaterial.materialIndex,
          draftMaterial.paramValues, {1.0f, 1.0f, 1.0f});
      std::array<float, 4> backbufferPixel{};
      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
      glReadBuffer(GL_BACK);
      glReadPixels(
          4, 4, 1, 1, GL_RGBA, GL_FLOAT, backbufferPixel.data());
      auto brightBrightness =
          brightDraft
              ? averageBrightness(
                    brightDraft, editor::SubMaterialThumbnailRenderer::size)
              : 0.0f;
      if (std::abs(backbufferPixel[0] - 0.125f) > 0.01f ||
          std::abs(backbufferPixel[1] - 0.25f) > 0.01f ||
          std::abs(backbufferPixel[2] - 0.5f) > 0.01f) {
        std::printf(
            "FAILED: draft thumbnail leaked into the screen backbuffer "
            "(%.3f, %.3f, %.3f)\n",
            backbufferPixel[0], backbufferPixel[1], backbufferPixel[2]);
        result = 1;
      }
      if (!darkDraft || !brightDraft ||
          std::abs(brightBrightness - darkBrightness) < 0.005f) {
        std::printf(
            "FAILED: live draft thumbnail did not respond to base colour "
            "(dark=%.4f bright=%.4f)\n",
            darkBrightness, brightBrightness);
        result = 1;
      }

      // The thumbnail scene stays alive beside the actual preview. Exercise
      // that coexistence: their unique pipeline and batch names must prevent
      // either renderer from reusing the other's resources.
      auto const& first =
          editor::procMaterialLibrary().catalogs().front().data.subMaterials.front();
      bw::core::World world(1.0f, -1.0f);
      world.createAccelerationGrids(16.0f);
      bw::core::ClosedPolygon ring{
          {{-6.0f, -6.0f}}, {{6.0f, -6.0f}}, {{6.0f, 6.0f}}, {{-6.0f, 6.0f}}};
      auto* primitive = bw::core::MeshPrimitive::fromTree(
          bw::core::Primitive::Operation::Union, {{{ring, {}}}});
      auto properties = primitive->getProperties();
      properties.floorMaterialId = first.id;
      properties.ceilingMaterialId = first.id;
      properties.wallMaterialId = first.id;
      primitive->setProperties(properties);
      world.addPrimitive(primitive);
      std::vector<bw::core::Primitive*> primitives{primitive};
      bw::core::ArrangementWorldDataGenerator generator;
      generator.generate(primitives);
      bw::core::ArrangementWorldData worldData(
          generator.getWorldData(), world.getExtents(),
          float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX), nullptr,
          world.getWedgeGenerationParameters());
      auto camera = std::make_shared<mpp::Camera>(
          glm::vec3{0.0f, 16.0f, 0.0f}, 0.0f, 0.0f, 0.0f, 45.0f, 1.0f);
      camera->setLookAt({0.0f, 16.0f, 0.0f}, {0.0f, 0.0f, 0.0f},
                        {0.0f, 0.0f, -1.0f});
      camera->setClipDistances(0.1f, 1000.0f);
      auto renderLivePreview = [&](bw::core::ArrangementWorldData const& data) {
        editor::PreviewRenderScene preview(renderSystem, &world, 128, 128);
        auto texture = preview.render(
            &world, data, camera, camera->getPosition(), 1.0f / 60.0f);
        return texture ? averageBrightness(texture, 128) : 0.0f;
      };
      auto brightness = renderLivePreview(worldData);
      std::printf("live preview brightness=%.4f\n", brightness);
      if (brightness < 0.01f) {
        std::printf("FAILED: live preview could not coexist with thumbnails\n");
        result = 1;
      }

      auto const& second =
          editor::procMaterialLibrary().catalogs().front().data.subMaterials[1];
      properties.floorMaterialId = second.id;
      properties.ceilingMaterialId = second.id;
      properties.wallMaterialId = second.id;
      primitive->setProperties(properties);
      bw::core::ArrangementWorldDataGenerator changedGenerator;
      changedGenerator.generate(primitives);
      bw::core::ArrangementWorldData changedWorldData(
          changedGenerator.getWorldData(), world.getExtents(),
          float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX), nullptr,
          world.getWedgeGenerationParameters());
      auto rebuiltBrightness = renderLivePreview(changedWorldData);
      std::printf("rebuilt live preview brightness=%.4f\n", rebuiltBrightness);
      if (rebuiltBrightness < 0.01f) {
        std::printf("FAILED: rebuilt live preview is black\n");
        result = 1;
      }
    }
  } catch (std::exception const& exception) {
    std::printf("FAILED: %s\n", exception.what());
    result = 1;
  }

  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return result;
}
