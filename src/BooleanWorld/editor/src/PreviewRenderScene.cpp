#include "PreviewRenderScene.h"

#include <exception>

#include <mpp/AmbientOcclusion.h>
#include <mpp/AntiAliasing.h>
#include <mpp/RenderSystem.h>
#include <mpp/RenderTexture.h>

#include <PlayerTorchShadows.h>
#include <VideoOptions.h>
#include <WorldRenderer.h>

#include "EditorRenderSystem.h"

namespace editor {
namespace {

// The named output is always the final offscreen shaded image. Ambient
// occlusion adds three graph images ahead of its composite; generated water
// then appends SceneColourResolved and WaterComposite. See
// StatePlayBooleanWorld::renderWorldThroughTarget, which derives the same
// layout for gameplay. GTAO from depth adds no scene attachments of its own,
// so the MRT-normal variant's higher index does not apply here. An active
// shadow domain inserts one imported graph image before AO output.
constexpr std::uint32_t outputImageIndex = 6u;

// Launcher's own defaults (StatePlayBooleanWorld::DebugDisplay), so the
// preview lights the world exactly as the game does. Exposing these as
// editor-side preview settings is deliberately a later ticket.
constexpr float materialScale = 1.0f;
constexpr float pixelSize = 1.0f / 32.0f;
constexpr SecondaryMaterialOptions secondaryMaterial{};

// Matches StatePlayBooleanWorld::getOrCreateWorldRenderPipeline, minus the
// per-render-scale and anti-aliasing variants the preview has no settings
// for: one named offscreen output with ambient occlusion on, so bloom,
// tonemapping and AO all reach the preview exactly as they reach Launcher.
mpp::RenderPipelineOptions pipelineOptions() {
  mpp::RenderPipelineOptions options;
  options.mode = mpp::RenderPipelineMode::GraphLegacyForward;

  mpp::RenderPipelineOutput output;
  output.name = "World";
  // Liquid is deferred into MPP's post-AO WaterScene, just as it is in
  // gameplay, so the preview presents WaterComposite rather than the opaque
  // AO image it reflects.
  output.image = "WaterComposite";
  output.antiAliasing.msaa = mpp::AntiAliasingSamples::Off;
  output.antiAliasing.fxaa = false;
  options.outputs.push_back(output);
  options.generatedWater = true;
  // The editor has no Launcher video configuration and deliberately keeps the
  // established Screen-space reflection source explicit.
  options.waterReflections.technique =
      mpp::WaterReflectionTechnique::ScreenSpace;

  options.ambientOcclusion.method = mpp::AmbientOcclusionMethod::Gtao;
  options.ambientOcclusion.gtao.normalSource = mpp::GTAONormalSource::Depth;
  // This render system is separate from Launcher’s, but its single preview
  // pipeline joins the same Player Torch domain contract.
  bw::app::joinPlayerTorchShadowDomain(options);
  return options;
}

}  // namespace

PreviewRenderScene::PreviewRenderScene(
    EditorRenderSystem& renderSystem,
    bw::core::World* world,
    std::size_t width,
    std::size_t height,
    bw::app::HorizontalMaterials horizontalMaterials,
    bw::app::ShadowOptions shadowOptions,
    std::string instanceName,
    bool loadWorldDependencies)
    : mwRenderSystem(renderSystem.renderSystem()),
      mShadowOptions(shadowOptions),
      mPipelineName("Editor." + instanceName + ".World"),
      mWidth(width),
      mHeight(height) {
  mScene = mwRenderSystem->createScene("Default");
  mScene->load();

  // A pipeline resolves its domain imports while it is constructed, so seed
  // the proxy Torch before creating the participating pipeline. render()
  // replaces this origin with the current camera/proxy position each frame.
  mwRenderSystem->configureShadowDomain(
      std::string(bw::app::playerTorchShadowDomain),
      bw::app::playerTorchMppShadowOptions(mShadowOptions, glm::vec3{}));

  mPipeline =
      mwRenderSystem->getOrCreateRenderPipeline(mPipelineName, pipelineOptions());
  mPipeline->resize(mWidth, mHeight);

  if (loadWorldDependencies) {
    std::string dependencyError;
    if (!renderSystem.loadWorldDependencies(
            world->getDependentResourceNames(), "World", &dependencyError)) {
      throw std::runtime_error("Could not load World dependencies: " +
                               dependencyError);
    }
  }

  // The same renderer the game uses, not a slimmer copy of it: it already
  // owns one WorldRenderer3d per surface set, and its SubMaterialResolver
  // reads the ProcMaterial catalogs EditorRenderSystem loaded.
  mRenderer = std::make_unique<WorldRenderer>(
      renderSystem.resourceManager(), renderSystem.logger(),
      bw::app::RenderTextureFilter::Linear, horizontalMaterials,
      WorldRenderer::WallUpdatePolicy::EditorEveryUpdate,
      std::vector<WallRenderSurface>{},
      WorldRenderer::WallRenderVariantResolver{}, "World", true,
      "World3d." + instanceName);
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
  // Its framebuffer must go while the pipeline's output texture it attaches
  // is still a valid name, and before anything else here is released.
  mOutline.reset();

  mPipeline.reset();
  mwRenderSystem->removeRenderPipeline(mPipelineName);

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
  // A draft can alter the shader/material used by an opaque caster. The
  // domain's state key catches rebuilt models, but this direct uniform path
  // has no new model resource to compare.
  mwRenderSystem->invalidateShadowDomain(
      std::string(bw::app::playerTorchShadowDomain));
}

void PreviewRenderScene::updateEmbossPresetDraft(
    std::string const& embossPresetId,
    bw::core::EmbossData const& emboss) {
  mRenderer->updateEmbossPresetDraft(embossPresetId, emboss);
  mwRenderSystem->invalidateShadowDomain(
      std::string(bw::app::playerTorchShadowDomain));
}

void PreviewRenderScene::reloadSubMaterialResolver(
    wp::application::resourcesystem::ResourceManager* resourceMgr) {
  mRenderer->reloadSubMaterialResolver(resourceMgr);
}

std::uint32_t PreviewRenderScene::worldSurfaceTriangleCount(
    WorldSurfaceSet surfaceSet) const {
  return mRenderer->getSurfaceTriangleCount(surfaceSet);
}

void PreviewRenderScene::worldGeometryChanged() {
  mRenderer->setWorldChanged();
  // WorldRenderer rebuilds model resources lazily. Mark the domain as well so
  // the point-shadow cache cannot reuse a cubemap from the prior snapshot.
  mwRenderSystem->invalidateShadowDomain(
      std::string(bw::app::playerTorchShadowDomain));
}

std::uint32_t PreviewRenderScene::render(
    bw::core::World* world,
    bw::core::WorldData const& worldData,
    mpp::CameraPtr const& camera,
    glm::vec3 const& cameraPosition,
    float frameTime,
    std::vector<PreviewOutline> const& outlines,
    std::int32_t horizontalMaterialIndexOverride,
    std::int32_t wallMaterialIndexOverride) {
  // The Player proxy is represented by the preview camera. Keep the Torch at
  // that eye position, as the game does, and use the shared release defaults
  // (range, near plane, biases, PCF filtering, and fade semantics).
  mwRenderSystem->configureShadowDomain(
      std::string(bw::app::playerTorchShadowDomain),
      bw::app::playerTorchMppShadowOptions(mShadowOptions, cameraPosition));

  // No highlighted triangle or wall: the preview marks the surface under the
  // pointer by outlining it below, not by tinting the material.
  mRenderer->update(
      world, worldData, cameraPosition, cameraPosition,
      bw::app::PlayerTorchOptions{}, std::nullopt, std::nullopt, std::nullopt,
      std::nullopt, defaultLiquidReflectionMipLevel, true, false,
      horizontalMaterialIndexOverride,
      wallMaterialIndexOverride, materialScale,
      pixelSize, secondaryMaterial, frameTime);

  mScene->setViewport(0, 0, mWidth, mHeight);
  mwRenderSystem->renderScene(
      mScene, camera, {0.0f, 0.0f}, mPipeline->getName());

  auto activeShadowImage =
      mwRenderSystem->getShadowDomainOptions(
                        std::string(bw::app::playerTorchShadowDomain))
          .enabled;
  auto target = mPipeline->getGraphImageRenderTarget(
      {outputImageIndex + (activeShadowImage ? 1u : 0u), 1});
  if (!target) {
    return 0;
  }

  auto textureId = static_cast<mpp::RenderTexture*>(target.get())->getId();

  // After every pass the pipeline runs, straight over the image it resolved.
  if (!outlines.empty() && !mOutlineFailed) {
    if (!mOutline) {
      try {
        mOutline = std::make_unique<PreviewOutlineRenderer>();
      } catch (std::exception const&) {
        // Nothing else here depends on it, so a preview without an outline is
        // better than no preview at all.
        mOutlineFailed = true;
      }
    }
    if (mOutline) {
      mOutline->render(
          textureId, mWidth, mHeight,
          camera->getProjectionTransform() * camera->getViewTransform(),
          outlines);
    }
  }

  return textureId;
}

}  // namespace editor
