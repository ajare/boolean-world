#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include <glm/vec3.hpp>

#include <core/WorldData.h>

namespace bw::core {
class World;
}

namespace editor {

class EditorRenderSystem;
class PreviewRenderScene;

// Renders each authored Sub-material on a top-down XZ-plane swatch and keeps
// an independent OpenGL texture for ImGui. The synthetic world contains one
// separated square per Sub-material so the real WorldRenderer bakes every
// catalog entry into a material bucket.
class SubMaterialThumbnailRenderer {
public:
  static constexpr std::uint32_t size = 112;

  explicit SubMaterialThumbnailRenderer(EditorRenderSystem& renderSystem);
  ~SubMaterialThumbnailRenderer();

  SubMaterialThumbnailRenderer(SubMaterialThumbnailRenderer const&) = delete;
  SubMaterialThumbnailRenderer& operator=(
      SubMaterialThumbnailRenderer const&) = delete;

  // Returns zero if rendering is unavailable. Library revisions rebuild the
  // synthetic scene, updating saved entries and adding newly created ones.
  [[nodiscard]] std::uint32_t texture(std::string const& subMaterialId);

private:
  void rebuild();
  void clearTextures();
  std::uint32_t copyTexture(std::uint32_t sourceTexture) const;

  EditorRenderSystem* mwRenderSystem{};
  std::unique_ptr<bw::core::World> mWorld;
  bw::core::ArrangementWorldDataPtr mWorldData;
  std::unique_ptr<PreviewRenderScene> mScene;
  std::map<std::string, glm::vec3> mCentres;
  std::map<std::string, std::uint32_t> mTextures;
  std::uint64_t mLibraryRevision{~std::uint64_t{0}};
};

}  // namespace editor
