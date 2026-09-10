#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include <core/WorldData.h>

namespace bw::core {
class World;
}

namespace editor {

class EditorRenderSystem;
class PreviewRenderScene;

// Renders picker thumbnails through the real WorldRenderer. Sub-materials use
// the established top-down swatch; each Triplanar material uses a fixed room
// corner containing a floor and two connected perpendicular walls, so its
// image and connected-wall Projection-normal behavior are both exercised.
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

  // Returns a rendered three-face corner for a qualified Triplanar Resource
  // name, or zero when rendering is unavailable.
  [[nodiscard]] std::uint32_t triplanarTexture(
      std::string const& qualifiedResourceName);

  // Smoke-test seam proving the synthetic fixture has two same-material
  // Arrangement-connected walls at a shared vertical edge, rather than a flat
  // image swatch.
  [[nodiscard]] bool triplanarThumbnailUsesConnectedWallProjection(
      std::string const& qualifiedResourceName) const;

  // Renders an unsaved parameter/base-colour draft for comparison with the
  // catalog-backed texture above. Reuses the texture until the draft changes.
  [[nodiscard]] std::uint32_t draftTexture(
      std::string const& subMaterialId, std::uint32_t materialIndex,
      std::vector<float> const& params,
      std::array<float, 3> const& baseColour);

private:
  void rebuild();
  void clearTextures();
  std::uint32_t renderTexture(std::string const& subMaterialId) const;
  std::uint32_t renderTriplanarTexture(
      std::string const& qualifiedResourceName) const;
  std::uint32_t renderFromCamera(
      glm::vec3 const& position, glm::vec3 const& target,
      glm::vec3 const& up) const;
  std::uint32_t copyTexture(std::uint32_t sourceTexture) const;

  EditorRenderSystem* mwRenderSystem{};
  std::unique_ptr<bw::core::World> mWorld;
  bw::core::ArrangementWorldDataPtr mWorldData;
  std::unique_ptr<PreviewRenderScene> mScene;
  struct TriplanarCamera {
    glm::vec3 position;
    glm::vec3 target;
  };
  std::map<std::string, glm::vec3> mCentres;
  std::map<std::string, TriplanarCamera> mTriplanarCameras;
  std::map<std::string, std::uint16_t> mTriplanarPaletteIndices;
  std::map<std::string, std::uint32_t> mTextures;
  std::map<std::string, std::uint32_t> mTriplanarTextures;
  std::uint32_t mDraftTexture{};
  std::string mDraftSubMaterialId;
  std::uint32_t mDraftMaterialIndex{};
  std::vector<float> mDraftParams;
  std::array<float, 3> mDraftBaseColour{};
  std::uint64_t mLibraryRevision{~std::uint64_t{0}};
};

}  // namespace editor
