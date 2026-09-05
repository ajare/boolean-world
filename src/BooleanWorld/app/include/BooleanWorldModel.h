#pragma once

#include <applib/Model.h>

#include "Platform.h"
#include "VideoOptions.h"
#include "WorldDataGenerationOptions.h"

struct BooleanWorldModel : public applib::Model {
private:
  bw::app::RenderScale mActiveRenderScale;

  bw::app::AntiAliasing mActiveAntiAliasing;

  bw::app::AmbientOcclusion mAmbientOcclusion;

  bw::app::RenderTextureFilter mRenderTextureFilter;

  bw::app::HorizontalMaterials mHorizontalMaterials;

  bw::app::WaterReflectionTechnique mWaterReflectionTechnique;

  bw::app::PlanarReflectionResolution mPlanarReflectionResolution;

  bw::app::PlayerTorchOptions mPlayerTorchOptions;

  bw::app::ShadowOptions mShadowOptions;

  // Application-run configuration. F4 may override it temporarily; model
  // ownership carries that override across maps without changing Game.yaml.
  bw::app::WorldDataGenerationOptions mWorldDataGenerationOptions;

public:
  BooleanWorldModel(applib::EntityHandlerFactoryFunction handlerFactory,
                    wp::application::resourcesystem::ResourceManager* resourceMgr,
                    bw::app::VideoOptions const& videoOptions = {},
                    bw::app::WorldDataGenerationOptions const&
                        worldDataGenerationOptions = {})
      : applib::Model(handlerFactory, resourceMgr),
        mActiveRenderScale(videoOptions.renderScale),
        mActiveAntiAliasing(videoOptions.antiAliasing),
        mAmbientOcclusion(videoOptions.ambientOcclusion),
        mRenderTextureFilter(videoOptions.renderTextureFilter),
        mHorizontalMaterials(videoOptions.horizontalMaterials),
        mWaterReflectionTechnique(videoOptions.waterReflections.technique),
        mPlanarReflectionResolution(
            videoOptions.waterReflections.planarResolution),
        mPlayerTorchOptions(videoOptions.playerTorch),
        mShadowOptions(videoOptions.shadows),
        mWorldDataGenerationOptions(worldDataGenerationOptions) {
  }

  bw::app::RenderScale getActiveRenderScale() const {
    return mActiveRenderScale;
  }

  void setActiveRenderScale(bw::app::RenderScale renderScale) {
    mActiveRenderScale = renderScale;
  }

  bw::app::AntiAliasing getActiveAntiAliasing() const {
    return mActiveAntiAliasing;
  }

  void setActiveAntiAliasing(bw::app::AntiAliasing antiAliasing) {
    mActiveAntiAliasing = antiAliasing;
  }

  bw::app::AmbientOcclusion getAmbientOcclusion() const {
    return mAmbientOcclusion;
  }

  bw::app::RenderTextureFilter getRenderTextureFilter() const {
    return mRenderTextureFilter;
  }

  bw::app::HorizontalMaterials getHorizontalMaterials() const {
    return mHorizontalMaterials;
  }

  bw::app::WaterReflectionTechnique getWaterReflectionTechnique() const {
    return mWaterReflectionTechnique;
  }

  void setWaterReflectionTechnique(
      bw::app::WaterReflectionTechnique technique) {
    mWaterReflectionTechnique = technique;
  }

  bw::app::PlanarReflectionResolution getPlanarReflectionResolution() const {
    return mPlanarReflectionResolution;
  }

  void setPlanarReflectionResolution(
      bw::app::PlanarReflectionResolution resolution) {
    mPlanarReflectionResolution = resolution;
  }

  bw::app::PlayerTorchOptions const& getPlayerTorchOptions() const {
    return mPlayerTorchOptions;
  }

  bw::app::ShadowOptions const& getShadowOptions() const {
    return mShadowOptions;
  }

  bw::app::WorldDataGenerationMode getGenerationMode() const {
    return mWorldDataGenerationOptions.mode;
  }

  void setGenerationMode(bw::app::WorldDataGenerationMode mode) {
    mWorldDataGenerationOptions.mode = mode;
  }

  float getGenerationStartInterval() const {
    return mWorldDataGenerationOptions.startInterval;
  }

  void setGenerationStartInterval(float interval) {
    mWorldDataGenerationOptions.startInterval = interval;
  }

  bool getAlwaysUpdateGenerationVertices() const {
    return mWorldDataGenerationOptions.alwaysUpdateVertices;
  }

  void setAlwaysUpdateGenerationVertices(bool alwaysUpdate) {
    mWorldDataGenerationOptions.alwaysUpdateVertices = alwaysUpdate;
  }

  bool getAllowGenerationCommitIfVisible() const {
    return mWorldDataGenerationOptions.allowCommitIfVisible;
  }

  void setAllowGenerationCommitIfVisible(bool allowCommit) {
    mWorldDataGenerationOptions.allowCommitIfVisible = allowCommit;
  }
};
