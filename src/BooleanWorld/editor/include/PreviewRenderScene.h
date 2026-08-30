#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec3.hpp>
#pragma warning(pop)

#include <mpp/Camera.h>
#include <mpp/RenderPipeline.h>
#include <mpp/Scene.h>

#include <VideoOptions.h>
#include <core/Emboss.h>
#include <core/World.h>
#include <core/WorldData.h>

#include "PreviewOutlineRenderer.h"

class WorldRenderer;
enum class WorldSurfaceSet;

namespace wp::application::resourcesystem {
class ResourceManager;
}

namespace mpp {
class RenderSystem;
}  // namespace mpp

namespace editor {

class EditorRenderSystem;

// Everything one open of the 3D preview owns on the GPU: an mpp::Scene, the
// render pipeline that draws it, and the game's own WorldRenderer. Unlike
// EditorRenderSystem, which is bootstrapped once and kept for the process,
// this is built per preview-open and destroyed on close - the editor can open
// and close the preview many times per session, unlike app/'s single map load
// per run, so anything left behind here compounds.
//
// Teardown order is not incidental, and the destructor spells out why: the
// render pipeline has to be released - including RenderSystem's own cached
// copy of it - before the WorldRenderer, or the batch's Model resource is
// never freed and the next open crashes over the stale one. The Scene must
// then outlive the WorldRenderer that was added to it.
class PreviewRenderScene {
public:
  // Builds the whole stack against `renderSystem`, sized in framebuffer
  // pixels. Throws if any part of it fails.
  //
  // `world` is the full World rather than the preview's scoped Primitive
  // list: WorldRenderer3d::addToScene walks every Primitive to seed one
  // uniform collection per material mesh bucket, and seeding buckets for
  // out-of-scope Primitives is harmless.
  PreviewRenderScene(
      EditorRenderSystem& renderSystem,
      bw::core::World* world,
      std::size_t width,
      std::size_t height,
      bw::app::HorizontalMaterials horizontalMaterials =
          bw::app::HorizontalMaterials::TwoDimensional,
      bw::app::ShadowOptions shadowOptions = {});
  ~PreviewRenderScene();

  PreviewRenderScene(PreviewRenderScene const&) = delete;
  PreviewRenderScene& operator=(PreviewRenderScene const&) = delete;

  [[nodiscard]] std::size_t width() const { return mWidth; }
  [[nodiscard]] std::size_t height() const { return mHeight; }

  // No-op when the size has not actually changed.
  void resize(std::size_t width, std::size_t height);

  // Pushes an unsaved editor draft to every existing matching mesh bucket.
  void updateMaterialDraft(
      std::string const& subMaterialId, std::uint32_t materialIndex,
      std::vector<float> const& params,
      std::array<float, 3> const& baseColour,
      bw::core::EmbossData const& emboss);
  void updateEmbossPresetDraft(
      std::string const& embossPresetId,
      bw::core::EmbossData const& emboss);

  // Rebuilds the renderer's cached Sub-material resolver without rebuilding
  // its scene, pipeline, or mesh buckets.
  void reloadSubMaterialResolver(
      wp::application::resourcesystem::ResourceManager* resourceMgr);
  void worldGeometryChanged();

  [[nodiscard]] std::uint32_t worldSurfaceTriangleCount(
      WorldSurfaceSet surfaceSet) const;

  // Rebuilds this frame's world geometry and renders it into the pipeline's
  // offscreen images. Returns the OpenGL texture id of the resolved output
  // image, or zero if the pipeline produced no target.
  //
  // The light is placed at the eye, which is what Launcher does too: the
  // game's own light offset (DebugDisplay::lightDistance) is zero by default.
  //
  // `outlines` are wireframe borders drawn once the world is finished with,
  // over the top of it and with no depth testing - how the preview marks the
  // selected surface and the one under the pointer. Later outlines win where
  // two overlap. Empty draws nothing. Optional material-index overrides use
  // the renderer's diagnostic Technique path without replacing independently
  // authored wall normal-map state.
  [[nodiscard]] std::uint32_t render(
      bw::core::World* world,
      bw::core::WorldData const& worldData,
      mpp::CameraPtr const& camera,
      glm::vec3 const& cameraPosition,
      float frameTime,
      std::vector<PreviewOutline> const& outlines = {},
      std::int32_t horizontalMaterialIndexOverride = -1,
      std::int32_t wallMaterialIndexOverride = -1);

private:
  mpp::RenderSystem* mwRenderSystem{};
  mpp::ScenePtr mScene;
  mpp::RenderPipelinePtr mPipeline;
  std::unique_ptr<WorldRenderer> mRenderer;
  // Built lazily on the first outline: a preview that is never hovered over
  // pays nothing, and a driver that will not compile the program costs the
  // outline rather than the whole preview.
  std::unique_ptr<PreviewOutlineRenderer> mOutline;
  bw::app::ShadowOptions mShadowOptions;
  bool mOutlineFailed{};
  std::size_t mWidth{};
  std::size_t mHeight{};
};

}  // namespace editor
