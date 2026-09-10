#include "SubMaterialThumbnailRenderer.h"

#include <array>
#include <vector>

#include <GL/glew.h>

#include <mpp/Camera.h>

#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/Defines.h>
#include <core/MeshPrimitive.h>
#include <core/World.h>

#include "EditorRenderSystem.h"
#include "PreviewRenderScene.h"
#include "ProcMaterialLibrary.h"

namespace editor {
namespace {

constexpr float swatchHalfExtent = 6.0f;
constexpr float swatchSpacing = 32.0f;
constexpr std::size_t swatchesPerRow = 8;
constexpr float cameraHeight = 16.0f;

}  // namespace

SubMaterialThumbnailRenderer::SubMaterialThumbnailRenderer(
    EditorRenderSystem& renderSystem)
    : mwRenderSystem(&renderSystem) {}

SubMaterialThumbnailRenderer::~SubMaterialThumbnailRenderer() {
  mScene.reset();
  clearTextures();
}

void SubMaterialThumbnailRenderer::clearTextures() {
  for (auto const& [id, texture] : mTextures) {
    (void)id;
    if (texture) glDeleteTextures(1, &texture);
  }
  mTextures.clear();
  for (auto const& [name, texture] : mTriplanarTextures) {
    (void)name;
    if (texture) glDeleteTextures(1, &texture);
  }
  mTriplanarTextures.clear();
  if (mDraftTexture) glDeleteTextures(1, &mDraftTexture);
  mDraftTexture = 0;
  mDraftSubMaterialId.clear();
  mDraftParams.clear();
}

void SubMaterialThumbnailRenderer::rebuild() {
  mScene.reset();
  mWorldData.reset();
  mWorld.reset();
  mCentres.clear();
  mTriplanarCameras.clear();
  mTriplanarPaletteIndices.clear();
  clearTextures();

  mWorld = std::make_unique<bw::core::World>(1.0f, -1.0f);
  mWorld->createAccelerationGrids(16.0f);

  std::vector<bw::core::Primitive*> primitives;
  std::size_t ordinal{};
  for (auto const& catalog : procMaterialLibrary().catalogs()) {
    for (auto const& material : catalog.data.subMaterials) {
      auto column = ordinal % swatchesPerRow;
      auto row = ordinal / swatchesPerRow;
      float x = (static_cast<float>(column) - 3.5f) * swatchSpacing;
      float y = (static_cast<float>(row) - 3.5f) * swatchSpacing;
      bw::core::ClosedPolygon ring{
          {{x - swatchHalfExtent, y - swatchHalfExtent}},
          {{x + swatchHalfExtent, y - swatchHalfExtent}},
          {{x + swatchHalfExtent, y + swatchHalfExtent}},
          {{x - swatchHalfExtent, y + swatchHalfExtent}}};
      auto* primitive = bw::core::MeshPrimitive::fromTree(
          bw::core::Primitive::Operation::Union, {{{ring, {}}}});
      auto properties = primitive->getProperties();
      properties.floorZ = 0.0f;
      properties.ceilingZ = 1.0f;
      properties.floorMaterialId = material.id;
      properties.ceilingMaterialId = material.id;
      properties.wallMaterialId = material.id;
      primitive->setProperties(properties);
      mWorld->addPrimitive(primitive);
      primitives.push_back(primitive);
      // Arrangement +Y maps to renderer -Z.
      mCentres.emplace(material.id, glm::vec3{x, 1.0f, -y});
      ++ordinal;
    }
  }

  auto triplanarMaterials = discoverLoadedTriplanarMaterials(
      mwRenderSystem->resourceManager());
  for (auto const& material : triplanarMaterials) {
    auto column = ordinal % swatchesPerRow;
    auto row = ordinal / swatchesPerRow;
    float x = (static_cast<float>(column) - 3.5f) * swatchSpacing;
    float y = (static_cast<float>(row) - 3.5f) * swatchSpacing;
    constexpr float roomHalfExtent = 10.0f;
    bw::core::ClosedPolygon ring{
        {{x - roomHalfExtent, y - roomHalfExtent}},
        {{x + roomHalfExtent, y - roomHalfExtent}},
        {{x + roomHalfExtent, y + roomHalfExtent}},
        {{x - roomHalfExtent, y + roomHalfExtent}}};
    auto* primitive = bw::core::MeshPrimitive::fromTree(
        bw::core::Primitive::Operation::Union, {{{ring, {}}}});
    auto properties = primitive->getProperties();
    properties.floorZ = 0.0f;
    properties.ceilingZ = 12.0f;
    auto reference = bw::core::SurfaceMaterialReference::triplanar(
        material.resourceName);
    properties.floorMaterialId = reference;
    properties.ceilingMaterialId = reference;
    properties.wallMaterialId = reference;
    primitive->setProperties(properties);
    mWorld->addPrimitive(primitive);
    primitives.push_back(primitive);
    mTriplanarPaletteIndices.emplace(
        material.resourceName, static_cast<std::uint16_t>(primitives.size()));

    // Arrangement +Y maps to renderer -Z. The camera sits inside the room and
    // looks toward its lower-left World-plane corner, framing the floor and
    // the two Border walls joined by that corner's vertical edge.
    mTriplanarCameras.emplace(
        material.resourceName,
        TriplanarCamera{{x + 4.0f, 5.0f, -y - 4.0f},
                        {x - 8.0f, 3.0f, -y + 8.0f}});
    ++ordinal;
  }

  bw::core::ArrangementWorldDataGenerator generator;
  generator.setChipParametersResolver([](std::string const& id) {
    auto const* material = procMaterialLibrary().findSubMaterial(id);
    return material ? material->chip : bw::core::ChipGenerationParameters{};
  });
  generator.generate(primitives);
  mWorldData = std::make_shared<bw::core::ArrangementWorldData>(
      generator.getWorldData(), mWorld->getExtents(),
      float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX), nullptr,
      mWorld->getWedgeGenerationParameters());

  mScene = std::make_unique<PreviewRenderScene>(
      *mwRenderSystem, mWorld.get(), size, size,
      bw::app::HorizontalMaterials::TwoDimensional,
      bw::app::ShadowOptions{}, "SubMaterialThumbnails", false);
  mLibraryRevision = procMaterialLibrary().revision();
}

std::uint32_t SubMaterialThumbnailRenderer::copyTexture(
    std::uint32_t sourceTexture) const {
  GLint previousTexture{};
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
  std::uint32_t destination{};
  glGenTextures(1, &destination);
  glBindTexture(GL_TEXTURE_2D, destination);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(
      GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA,
      GL_UNSIGNED_BYTE, nullptr);

  GLint previousRead{}, previousDraw{};
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
  std::array<std::uint32_t, 2> framebuffers{};
  glGenFramebuffers(static_cast<GLsizei>(framebuffers.size()), framebuffers.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[0]);
  glFramebufferTexture2D(
      GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
      sourceTexture, 0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[1]);
  glFramebufferTexture2D(
      GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
      destination, 0);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  bool const complete =
      glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
      glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  if (complete) {
    glBlitFramebuffer(
        0, 0, size, size, 0, 0, size, size, GL_COLOR_BUFFER_BIT, GL_LINEAR);
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, previousRead);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previousDraw);
  glBindTexture(GL_TEXTURE_2D, previousTexture);
  glDeleteFramebuffers(static_cast<GLsizei>(framebuffers.size()), framebuffers.data());
  if (!complete) {
    glDeleteTextures(1, &destination);
    return 0;
  }
  return destination;
}

std::uint32_t SubMaterialThumbnailRenderer::renderTexture(
    std::string const& subMaterialId) const {
  auto centre = mCentres.find(subMaterialId);
  if (centre == mCentres.end()) return 0;
  auto position = centre->second + glm::vec3{0.0f, cameraHeight, 0.0f};
  return renderFromCamera(position, centre->second, {0.0f, 0.0f, -1.0f});
}

std::uint32_t SubMaterialThumbnailRenderer::renderTriplanarTexture(
    std::string const& qualifiedResourceName) const {
  auto camera = mTriplanarCameras.find(qualifiedResourceName);
  if (camera == mTriplanarCameras.end()) return 0;
  return renderFromCamera(
      camera->second.position, camera->second.target, {0.0f, 1.0f, 0.0f});
}

std::uint32_t SubMaterialThumbnailRenderer::renderFromCamera(
    glm::vec3 const& position, glm::vec3 const& target,
    glm::vec3 const& up) const {
  if (!mScene || !mWorldData) return 0;

  // MPP's offscreen graph returns to its screen target when it finishes and
  // clears that target when graph presentation is disabled. Thumbnail renders
  // happen while ImGui is only building its draw list, after Main has already
  // cleared the backbuffer, so that clear would otherwise appear as a small
  // live preview in the editor's bottom-left corner. Preserve the editor's
  // clear state and re-establish its clean backbuffer before ImGui draws.
  GLint previousReadFramebuffer{}, previousDrawFramebuffer{};
  GLint previousViewport[4]{};
  GLint previousScissorBox[4]{};
  GLint previousDrawBuffer{};
  GLfloat previousClearColour[4]{};
  GLboolean previousScissor = glIsEnabled(GL_SCISSOR_TEST);
  GLboolean previousColourMask[4]{};
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
  glGetIntegerv(GL_VIEWPORT, previousViewport);
  glGetIntegerv(GL_SCISSOR_BOX, previousScissorBox);
  glGetIntegerv(GL_DRAW_BUFFER, &previousDrawBuffer);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColour);
  glGetBooleanv(GL_COLOR_WRITEMASK, previousColourMask);

  auto restoreBackbuffer = [&] {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(
        previousClearColour[0], previousClearColour[1],
        previousClearColour[2], previousClearColour[3]);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, previousReadFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previousDrawFramebuffer);
    glDrawBuffer(previousDrawBuffer);
    glViewport(
        previousViewport[0], previousViewport[1], previousViewport[2],
        previousViewport[3]);
    glScissor(
        previousScissorBox[0], previousScissorBox[1], previousScissorBox[2],
        previousScissorBox[3]);
    if (previousScissor) glEnable(GL_SCISSOR_TEST);
    glColorMask(
        previousColourMask[0], previousColourMask[1], previousColourMask[2],
        previousColourMask[3]);
  };

  try {
    auto camera = std::make_shared<mpp::Camera>(
        position, 0.0f, 0.0f, 0.0f, 45.0f, 1.0f);
    camera->setLookAt(position, target, up);
    camera->setClipDistances(0.1f, 1000.0f);
    auto source = mScene->render(
        mWorld.get(), *mWorldData, camera, position, 1.0f / 60.0f);
    auto result = source ? copyTexture(source) : 0;
    restoreBackbuffer();
    return result;
  } catch (...) {
    restoreBackbuffer();
    throw;
  }
}

std::uint32_t SubMaterialThumbnailRenderer::texture(
    std::string const& subMaterialId) {
  auto const revision = procMaterialLibrary().revision();
  if (mLibraryRevision != revision) {
    try {
      rebuild();
    } catch (...) {
      // A thumbnail is optional UI; a shader/driver failure must not take the
      // material editor or its text fallback down with it.
      mScene.reset();
      mLibraryRevision = revision;
      return 0;
    }
  }
  if (!mScene || !mWorldData) return 0;
  if (auto found = mTextures.find(subMaterialId); found != mTextures.end()) {
    return found->second;
  }

  try {
    auto copied = renderTexture(subMaterialId);
    mTextures.emplace(subMaterialId, copied);
    return copied;
  } catch (...) {
    mTextures.emplace(subMaterialId, 0u);
    return 0;
  }
}

std::uint32_t SubMaterialThumbnailRenderer::triplanarTexture(
    std::string const& qualifiedResourceName) {
  auto const revision = procMaterialLibrary().revision();
  if (mLibraryRevision != revision) {
    try {
      rebuild();
    } catch (...) {
      mScene.reset();
      mLibraryRevision = revision;
      return 0;
    }
  }
  if (!mScene || !mWorldData) return 0;
  if (auto found = mTriplanarTextures.find(qualifiedResourceName);
      found != mTriplanarTextures.end()) {
    return found->second;
  }

  try {
    auto copied = renderTriplanarTexture(qualifiedResourceName);
    mTriplanarTextures.emplace(qualifiedResourceName, copied);
    return copied;
  } catch (...) {
    mTriplanarTextures.emplace(qualifiedResourceName, 0u);
    return 0;
  }
}

bool SubMaterialThumbnailRenderer::
    triplanarThumbnailUsesConnectedWallProjection(
        std::string const& qualifiedResourceName) const {
  if (!mWorldData) return false;
  auto palette = mTriplanarPaletteIndices.find(qualifiedResourceName);
  if (palette == mTriplanarPaletteIndices.end()) return false;

  auto const& arrangement = mWorldData->getArrangement();
  auto const& walls = mWorldData->getWalls();
  bool hasFloor = false;
  for (auto const& triangle : mWorldData->getTriangles()) {
    if (triangle.face < arrangement.faces.size() &&
        arrangement.faces[triangle.face].paletteIndex == palette->second) {
      hasFloor = true;
      break;
    }
  }
  if (!hasFloor) return false;

  for (std::size_t first = 0; first < walls.size(); ++first) {
    if (walls[first].paletteIndex != palette->second ||
        walls[first].edge >= arrangement.edges.size()) {
      continue;
    }
    auto const& firstEdge = arrangement.edges[walls[first].edge];
    for (std::size_t second = first + 1; second < walls.size(); ++second) {
      if (walls[second].paletteIndex != palette->second ||
          walls[second].edge >= arrangement.edges.size()) {
        continue;
      }
      auto const& secondEdge = arrangement.edges[walls[second].edge];
      for (auto firstVertex : firstEdge.v) {
        for (auto secondVertex : secondEdge.v) {
          if (firstVertex == secondVertex) return true;
        }
      }
    }
  }
  return false;
}

std::uint32_t SubMaterialThumbnailRenderer::draftTexture(
    std::string const& subMaterialId, std::uint32_t materialIndex,
    std::vector<float> const& params,
    std::array<float, 3> const& baseColour) {
  // Cache the catalog version before changing this bucket's live uniforms.
  // It remains the stable left-hand comparison image while the draft moves.
  if (!texture(subMaterialId) || !mScene) return 0;
  if (mDraftTexture && mDraftSubMaterialId == subMaterialId &&
      mDraftMaterialIndex == materialIndex && mDraftParams == params &&
      mDraftBaseColour == baseColour) {
    return mDraftTexture;
  }

  if (mDraftTexture) glDeleteTextures(1, &mDraftTexture);
  mDraftTexture = 0;
  mDraftSubMaterialId = subMaterialId;
  mDraftMaterialIndex = materialIndex;
  mDraftParams = params;
  mDraftBaseColour = baseColour;
  try {
    mScene->updateMaterialDraft(
        subMaterialId, materialIndex, params, baseColour);
    mDraftTexture = renderTexture(subMaterialId);
  } catch (...) {
    // Keep draft failure isolated from the already-copied standard thumbnail.
  }
  return mDraftTexture;
}

}  // namespace editor
