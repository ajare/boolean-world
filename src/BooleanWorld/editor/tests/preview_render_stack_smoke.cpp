// Not a unit test - creates a real GL context and drives the 3D preview's
// whole render stack headlessly: EditorRenderSystem, then a PreviewRenderScene
// (mpp::Scene + RenderPipeline + the game's WorldRenderer) over a real
// Arrangement built from world-test-1.world.yaml, rendered through the same
// renderScene/getGraphImageRenderTarget path Preview3D.cpp uses.
//
// Every frame it renders also carries a hovered-surface outline, so the raw-GL
// pass that draws one over the pipeline's finished image - a program, a
// framebuffer of its own, and the state it must put back - is exercised here
// rather than only when someone hovers a surface in the editor.
//
// It runs that build-render-destroy cycle repeatedly, which is what makes it
// worth having: GitHub issue #270 asks for repeated preview open/close not to
// leak GPU resources, and mpp::ResourceManager's own counts settling after
// the first cycle is the evidence for that. See PreviewRenderScene.h for why
// teardown order matters.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <SDL3/SDL.h>

#include <mpp/Camera.h>
#include <mpp/ResourceManager.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/Defines.h>
#include <core/LayerBuildStep.h>
#include <core/World.h>
#include <core/YamlSerializer.h>

#include <common/GameDefines.h>

#include "EditorRenderSystem.h"
#include "PlayerView.h"
#include "PreviewRenderScene.h"
#include "ProcMaterialLibrary.h"
#include "ReactiveCamera.h"
#include "SubMaterialThumbnailRenderer.h"

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 480;
constexpr int kCycles = 20;

// Two closed quads squarely in front of the camera, the inner one nested
// inside the outer - the same shape and the same two colours Preview3D.cpp
// hands over for a selected surface and a hovered one, placed where they are
// certain to land in frame so the pixels they draw can be looked for.
std::vector<editor::PreviewOutline> cameraFacingOutline(mpp::Camera& camera) {
  auto origin = camera.getPosition() + camera.getDirection() * 5.0f;
  auto up = camera.getUp();
  auto right = glm::normalize(glm::cross(camera.getDirection(), up));
  std::array<glm::vec3, 4> quad{
      origin - right - up, origin + right - up, origin + right + up,
      origin - right + up};

  auto loop = [](std::array<glm::vec3, 4> const& corners) {
    std::vector<glm::vec3> segments;
    for (size_t index = 0; index < corners.size(); ++index) {
      segments.push_back(corners[index]);
      segments.push_back(corners[(index + 1) % corners.size()]);
    }
    return segments;
  };

  std::array<glm::vec3, 4> inner{
      origin - right * 0.4f - up * 0.4f, origin + right * 0.4f - up * 0.4f,
      origin + right * 0.4f + up * 0.4f, origin - right * 0.4f + up * 0.4f};

  // Red for a selection, yellow for a hover, drawn in that order - exactly
  // what the preview asks for when the pointer is over a second surface.
  return {
      {loop(quad), glm::vec4{1.0f, 0.0f, 0.0f, 1.0f}},
      {loop(inner), glm::vec4{1.0f, 1.0f, 0.0f, 1.0f}}};
}

// How many pixels of the finished image are each outline colour. Nothing the
// world's own materials render reaches saturated red or yellow with no blue at
// all, so these counts are specific to the outline pass having drawn - one
// per colour, which is what proves each outline kept its own.
struct OutlinePixels {
  size_t red{};
  size_t yellow{};
};

OutlinePixels countOutlinePixels(uint32_t textureId) {
  uint32_t frameBuffer{};
  glGenFramebuffers(1, &frameBuffer);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, frameBuffer);
  glFramebufferTexture2D(
      GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0);

  OutlinePixels found;
  if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
    std::vector<float> pixels(size_t(kWidth) * kHeight * 4);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_FLOAT, pixels.data());
    for (size_t index = 0; index + 3 < pixels.size(); index += 4) {
      auto red = pixels[index];
      auto green = pixels[index + 1];
      auto blue = pixels[index + 2];
      if (red > 0.75f && blue < 0.25f) {
        if (green > 0.75f) {
          ++found.yellow;
        } else if (green < 0.25f) {
          ++found.red;
        }
      }
    }
  }

  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &frameBuffer);
  return found;
}

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
  std::vector<std::uint64_t> priorities;
  primitives.reserve(world->getNumPrimitives());
  priorities.reserve(world->getNumPrimitives());
  for (uint32_t i = 0; i < world->getNumPrimitives(); ++i) {
    auto* primitive = world->getPrimitive(i);
    primitives.push_back(primitive);
    priorities.push_back(primitive->getGeneratedPriority());
  }

  bw::core::ArrangementWorldDataGenerator generator;
  generator.generateOrdered(primitives, priorities);
  auto result = std::make_shared<bw::core::ArrangementWorldData>(
      generator.getWorldData(), world->getExtents(),
      float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX), nullptr,
      world->getWedgeGenerationParameters());
  if (result->getWedgeGenerationParameters() !=
      world->getWedgeGenerationParameters() ||
      (world->getWedgeGenerationParameters().enabled &&
       result->getDetail().getWedgeCount() == 0)) {
    throw std::runtime_error(
        "editor preview did not capture or generate the World's Wedges");
  }
  return result;
}

// The average colour of the finished image. Coarse on purpose: it is only
// used to answer "did what the world draws actually change", which is the one
// thing a material reassignment has to do.
struct AverageColour {
  float red{};
  float green{};
  float blue{};

  float distanceTo(AverageColour const& other) const {
    auto dr = red - other.red;
    auto dg = green - other.green;
    auto db = blue - other.blue;
    return std::sqrt(dr * dr + dg * dg + db * db);
  }
};

AverageColour averageColour(uint32_t textureId) {
  uint32_t frameBuffer{};
  glGenFramebuffers(1, &frameBuffer);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, frameBuffer);
  glFramebufferTexture2D(
      GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0);

  AverageColour average;
  if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
    std::vector<float> pixels(size_t(kWidth) * kHeight * 4);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_FLOAT, pixels.data());
    double red{}, green{}, blue{};
    for (size_t index = 0; index + 3 < pixels.size(); index += 4) {
      red += pixels[index];
      green += pixels[index + 1];
      blue += pixels[index + 2];
    }
    auto count = double(kWidth) * kHeight;
    average = {float(red / count), float(green / count), float(blue / count)};
  }

  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &frameBuffer);
  return average;
}

// Assigns every surface in the world one Sub-material, the way the editor's
// selected-surface panel assigns one surface a different Sub-material.
void assignEverySurface(bw::core::World* world, std::string const& id) {
  for (uint32_t i = 0; i < world->getNumPrimitives(); ++i) {
    auto* primitive = world->getPrimitive(i);
    auto properties = primitive->getProperties();
    properties.floorMaterial = bw::core::SurfaceMaterialReference::subMaterial(id);
    properties.ceilingMaterial = bw::core::SurfaceMaterialReference::subMaterial(id);
    properties.wallMaterial = bw::core::SurfaceMaterialReference::subMaterial(id);
    primitive->setProperties(properties);
  }
}

// Renders `frames` frames of whatever the scene currently holds and answers
// with the last one's average colour.
AverageColour renderFrames(
    editor::PreviewRenderScene& scene, bw::core::World* world,
    bw::core::ArrangementWorldData const& worldData,
    std::shared_ptr<ReactiveCamera> const& camera, int frames) {
  uint32_t textureId = 0;
  for (int frame = 0; frame < frames; ++frame) {
    textureId = scene.render(
        world, worldData, camera, camera->getPosition(), 1.0f / 60.0f);
  }
  return textureId == 0 ? AverageColour{} : averageColour(textureId);
}

}  // namespace

// Reassigning a surface's Sub-material has to change what that surface draws.
// It is not a uniform-only edit: the mesh bucket a triangle lands in is keyed
// by the Sub-material id in the Arrangement palette, and the buckets are baked
// from the World's material ids when the render scene is built - so the
// snapshot and the scene both have to be rebuilt, which is exactly what
// Preview3D::rebuildPreviewForSurfaceMaterialChange does. Reassigning without
// rebuilding leaves the old material on screen; that was GitHub-less bug
// "selecting a different Sub-material does not update the preview".
int materialReassignmentRedrawsTheWorld(
    editor::EditorRenderSystem& renderSystem, bw::core::World* world) {
  auto camera = std::make_shared<ReactiveCamera>(
      glm::vec3{0.0f, BW_PLAYER_EYE_HEIGHT, 0.0f}, bw::app::cameraYaw(0.0f),
      0.0f, BW_PLAYER_FOV, kWidth / (float)kHeight);
  camera->setClipDistances(0.1f, 1000000.0f);

  auto worldData = buildWorldData(world);
  AverageColour before;
  {
    editor::PreviewRenderScene scene(renderSystem, world, kWidth, kHeight);
    before = renderFrames(scene, world, *worldData, camera, 3);
  }

  // Nothing in world-test-1.world.yaml uses this one, so before the fix its mesh
  // bucket did not even exist: getMeshIndexForMaterialHash would have answered
  // zero and drawn the world in some other material entirely.
  assignEverySurface(world, "builtin.holographic");

  // What the editor now does on a pick: rebuild the Arrangement snapshot, then
  // the render scene built from it.
  auto reassignedWorldData = buildWorldData(world);
  AverageColour after;
  {
    editor::PreviewRenderScene scene(renderSystem, world, kWidth, kHeight);
    after = renderFrames(scene, world, *reassignedWorldData, camera, 3);
  }

  auto distance = before.distanceTo(after);
  printf(
      "reassignment: before=[%.4f %.4f %.4f] after=[%.4f %.4f %.4f] distance=%.4f\n",
      before.red, before.green, before.blue, after.red, after.green,
      after.blue, distance);

  if (distance < 0.01f) {
    printf(
        "FAILED: reassigning every surface's Sub-material did not change what the world draws\n");
    return 1;
  }
  return 0;
}

int worldMines3Renders(editor::EditorRenderSystem& renderSystem) {
  std::string dependencyError;
  if (!renderSystem.loadWorldDependencies(
          {"MinesLayer"}, "World", &dependencyError)) {
    throw std::runtime_error(
        "Could not load world-mines-3 dependencies: " + dependencyError);
  }

  auto world = loadWorld(BW_EDITOR_MINES_3_TEST_WORLD);
  auto worldData = buildWorldData(world.get());
  auto surface = worldData->getSurfaceSample({0.0f, 0.0f});
  if (!surface) {
    throw std::runtime_error(
        "world-mines-3 has no grounding surface at the tunnel drop point");
  }
  auto floor = surface->floorElevation;
  auto camera = std::make_shared<ReactiveCamera>(
      glm::vec3{0.0f, floor + BW_PLAYER_EYE_HEIGHT, 0.0f},
      bw::app::cameraYaw(0.0f), 0.0f, BW_PLAYER_FOV,
      kWidth / (float)kHeight);
  camera->setClipDistances(0.1f, 1000000.0f);

  editor::PreviewRenderScene scene(
      renderSystem, world.get(), kWidth, kHeight);
  auto texture = scene.render(
      world.get(), *worldData, camera, camera->getPosition(), 1.0f / 60.0f);
  if (texture == 0) {
    printf("FAILED: world-mines-3 preview rendered no texture\n");
    return 1;
  }
  return 0;
}

int main() {
  bw::core::LayerBuildStep::registerCoreTypes();

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
    auto wedgeSettings = world->getWedgeGenerationParameters();
    wedgeSettings.enabled = true;
    wedgeSettings.minimumReach = 5.0f;
    wedgeSettings.maximumReach = 7.0f;
    world->setWedgeGenerationParameters(wedgeSettings);
    auto worldData = buildWorldData(world.get());
    printf(
        "world: %u primitives, %zu triangles, %zu walls\n",
        world->getNumPrimitives(), worldData->getTriangles().size(),
        worldData->getWalls().size());

    editor::procMaterialLibrary().load(BW_EDITOR_PROC_MATERIAL_MANIFEST);
    editor::EditorRenderSystem renderSystem(kWidth, kHeight);
    // Launcher loads every world program, including overdraw whose fragment
    // shader consumes none of world.vert's varyings. Link that combination
    // too: native flat outputs must not collide with MPP's explicit locations
    // when the linker eliminates unused inputs.
    auto* resources = renderSystem.resourceManager();
    for (auto name : {"WorldProgram", "WorldHorizontal2dProgram",
                      "FragmentOverdrawProgram"}) {
      auto program = resources->getResource(name, "World");
      resources->createResource(program);
      resources->loadResource(program);
      // The application Program wrapper only declares its MPP resource.
      // Force the GPU load rather than leaving shader linking deferred.
      program->getMppResource()->load();
    }
    result |= worldMines3Renders(renderSystem);

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
        auto outline = cameraFacingOutline(*camera);
        for (int frame = 0; frame < 3; ++frame) {
          textureId = scene.render(
              world.get(), *worldData, camera, camera->getPosition(),
              1.0f / 60.0f, outline);
        }

        if (auto error = glGetError(); error != GL_NO_ERROR) {
          printf("FAILED: cycle %d left GL error 0x%04x\n", cycle, error);
          result = 1;
          break;
        }

        if (textureId != 0) {
          auto outlinePixels = countOutlinePixels(textureId);
          printf(
              "cycle %d: outline pixels red=%zu yellow=%zu\n", cycle,
              outlinePixels.red, outlinePixels.yellow);
          if (outlinePixels.red == 0 || outlinePixels.yellow == 0) {
            printf(
                "FAILED: cycle %d did not draw both outlines over the image\n",
                cycle);
            result = 1;
            break;
          }
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

    if (result == 0) {
      editor::SubMaterialThumbnailRenderer thumbnails(renderSystem);
      for (auto const& catalog : editor::procMaterialLibrary().catalogs()) {
        for (auto const& material : catalog.data.subMaterials) {
          if (!thumbnails.texture(material.id)) {
            printf("FAILED: could not render thumbnail %s\n", material.id.c_str());
            result = 1;
            break;
          }
        }
      }
      if (result == 0) {
        result = materialReassignmentRedrawsTheWorld(renderSystem, world.get());
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
