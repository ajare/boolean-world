#include "PreviewRenderScene.h"

#include <mpp/AmbientOcclusion.h>
#include <mpp/AntiAliasing.h>
#include <mpp/RenderSystem.h>
#include <mpp/RenderTexture.h>

#include <FloorPatternOptions.h>
#include <VideoOptions.h>
#include <WorldRenderer.h>

#include "EditorRenderSystem.h"

namespace editor {
namespace {

// One pipeline name is enough: only one preview can be open at a time, and
// destroying a PreviewRenderScene evicts this name from RenderSystem's own
// pipeline cache again.
constexpr char const* pipelineName = "Editor.Preview3D.World";

// The named output is always the final offscreen shaded image, and ambient
// occlusion adds three graph images ahead of it - see
// StatePlayBooleanWorld::renderWorldThroughTarget, which derives the same
// index from the options it built. GTAO from depth adds no scene attachments
// of its own, so the MRT-normal variant's higher index does not apply here.
constexpr std::uint32_t outputImageIndex = 4u;

// Launcher's own defaults (StatePlayBooleanWorld::DebugDisplay), so the
// preview lights and tiles the world exactly as the game does. Exposing these
// as editor-side preview settings is deliberately a later ticket.
constexpr float materialScale = 1.0f;
constexpr float farGridSize = 0.5f;
constexpr FloorPatternOptions floorPattern{};

// Matches StatePlayBooleanWorld::getOrCreateWorldRenderPipeline, minus the
// per-render-scale and anti-aliasing variants the preview has no settings
// for: one named offscreen output with ambient occlusion on, so bloom,
// tonemapping and AO all reach the preview exactly as they reach Launcher.
mpp::RenderPipelineOptions pipelineOptions() {
  mpp::RenderPipelineOptions options;
  options.mode = mpp::RenderPipelineMode::GraphLegacyForward;

  mpp::RenderPipelineOutput output;
  output.name = "World";
  output.image = "AmbientOcclusionComposite";
  output.antiAliasing.msaa = mpp::AntiAliasingSamples::Off;
  output.antiAliasing.fxaa = false;
  options.outputs.push_back(output);

  options.ambientOcclusion.method = mpp::AmbientOcclusionMethod::Gtao;
  options.ambientOcclusion.gtao.normalSource = mpp::GTAONormalSource::Depth;
  return options;
}

}  // namespace

PreviewRenderScene::PreviewRenderScene(
    EditorRenderSystem& renderSystem,
    bw::core::World const* world,
    std::size_t width,
    std::size_t height)
    : mwRenderSystem(renderSystem.renderSystem()),
      mWidth(width),
      mHeight(height) {
  mScene = mwRenderSystem->createScene("Default");
  mScene->load();

  mPipeline =
      mwRenderSystem->getOrCreateRenderPipeline(pipelineName, pipelineOptions());
  mPipeline->resize(mWidth, mHeight);

  // The same renderer the game uses, not a slimmer copy of it: it already
  // owns one WorldRenderer3d per surface set, and its SubMaterialResolver
  // reads the ProcMaterial catalogs EditorRenderSystem loaded.
  mRenderer = std::make_unique<WorldRenderer>(
      renderSystem.resourceManager(), renderSystem.logger(),
      bw::app::RenderTextureFilter::Linear,
      bw::app::HorizontalMaterials::TwoDimensional);
  mRenderer->create(
      mScene, world, mwRenderSystem, renderSystem.renderResourceManager());
}

PreviewRenderScene::~PreviewRenderScene() {
  // The pipeline goes first, and RenderSystem's cached copy of it with it.
  // While it is alive, something it rendered with keeps a reference to the
  // SceneModel3d alive past WorldRenderer3d's own remove3dModel/reset - and a
  // SceneModel3d holds an mpp acquire() on the batch's Model resource. Destroy
  // the WorldRenderer with that acquire still outstanding and Batch::~Batch
  // sees a non-zero ref count, silently skips its deleteResource, and strands
  // the Model in mpp::ResourceManager's cache; the next open then builds a
  // second batch of the same name over the stale one and crashes.
  //
  // Confirmed in both orderings against mpp's own acquire/release log
  // (mpp::enable_static_log("mpp-resources.log", true)): with the pipeline
  // released first, SceneModel3d releases the Model to 1 and the batch then
  // takes it to 0 and deletes it; with the renderer released first, the batch
  // releases to 1, skips the delete, and SceneModel3d only reaches 0 after.
  mPipeline.reset();
  mwRenderSystem->removeRenderPipeline(pipelineName);

  mRenderer.reset();

  if (mScene) {
    mScene->unload();
    mScene.reset();
  }
}

void PreviewRenderScene::resize(std::size_t width, std::size_t height) {
  if (width == mWidth && height == mHeight) {
    return;
  }

  mPipeline->resize(width, height);
  mWidth = width;
  mHeight = height;
}

void PreviewRenderScene::updateMaterialDraft(
    std::string const& subMaterialId, std::uint32_t materialIndex,
    std::vector<float> const& params,
    std::array<float, 3> const& baseColour) {
  bw::core::MaterialDefinitionData definition;
  definition.params.fill(0.0f);
  for (std::size_t i = 0; i < params.size() && i < definition.params.size(); ++i) {
    definition.params[i] = params[i];
  }
  definition.baseColour = baseColour;
  mRenderer->updateSubMaterialDraft(
      subMaterialId, static_cast<int32_t>(materialIndex), definition);
}

void PreviewRenderScene::reloadSubMaterialResolver(
    wp::application::resourcesystem::ResourceManager* resourceMgr) {
  mRenderer->reloadSubMaterialResolver(resourceMgr);
}

std::uint32_t PreviewRenderScene::render(
    bw::core::World* world,
    bw::core::WorldData const& worldData,
    mpp::CameraPtr const& camera,
    glm::vec3 const& cameraPosition,
    float frameTime) {
  mRenderer->update(
      world, worldData, cameraPosition, cameraPosition, -1, -1, materialScale,
      farGridSize, floorPattern, frameTime);

  mScene->setViewport(0, 0, mWidth, mHeight);
  mwRenderSystem->renderScene(
      mScene, camera, {0.0f, 0.0f}, mPipeline->getName());

  auto target = mPipeline->getGraphImageRenderTarget({outputImageIndex, 1});
  if (!target) {
    return 0;
  }

  return static_cast<mpp::RenderTexture*>(target.get())->getId();
}

}  // namespace editor
