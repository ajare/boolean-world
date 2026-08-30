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
}

void SubMaterialThumbnailRenderer::rebuild() {
  mScene.reset();
  mWorldData.reset();
  mWorld.reset();
  mCentres.clear();
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

  bw::core::ArrangementWorldDataGenerator generator;
  generator.setChipParametersResolver([](std::string const& id) {
    auto const* material = procMaterialLibrary().findSubMaterial(id);
    return material ? material->chip : bw::core::ChipGenerationParameters{};
  });
  generator.generate(primitives);
  mWorldData = std::make_shared<bw::core::ArrangementWorldData>(
      generator.getWorldData(), mWorld->getExtents(),
      float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX),
      mWorld->getStepThreshold(), nullptr,
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
  auto centre = mCentres.find(subMaterialId);
  if (centre == mCentres.end()) return 0;

  try {
    auto position = centre->second + glm::vec3{0.0f, cameraHeight, 0.0f};
    auto camera = std::make_shared<mpp::Camera>(
        position, 0.0f, 0.0f, 0.0f, 45.0f, 1.0f);
    camera->setLookAt(position, centre->second, {0.0f, 0.0f, -1.0f});
    camera->setClipDistances(0.1f, 1000.0f);
    auto source = mScene->render(
        mWorld.get(), *mWorldData, camera, position, 1.0f / 60.0f);
    if (!source) return 0;
    auto copied = copyTexture(source);
    mTextures.emplace(subMaterialId, copied);
    return copied;
  } catch (...) {
    mTextures.emplace(subMaterialId, 0u);
    return 0;
  }
}

}  // namespace editor
