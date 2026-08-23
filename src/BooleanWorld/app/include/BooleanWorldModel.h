#pragma once

#include <applib/Model.h>

#include "Platform.h"
#include "VideoOptions.h"

struct BooleanWorldModel : public applib::Model {
private:
  bw::app::RenderScale mActiveRenderScale;

  bw::app::AntiAliasing mActiveAntiAliasing;

  bw::app::AmbientOcclusion mAmbientOcclusion;

  bw::app::RenderTextureFilter mRenderTextureFilter;

  bw::app::HorizontalMaterials mHorizontalMaterials;

public:
  BooleanWorldModel(applib::EntityHandlerFactoryFunction handlerFactory,
                    wp::application::resourcesystem::ResourceManager* resourceMgr,
                    bw::app::VideoOptions const& videoOptions = {})
      : applib::Model(handlerFactory, resourceMgr),
        mActiveRenderScale(videoOptions.renderScale),
        mActiveAntiAliasing(videoOptions.antiAliasing),
        mAmbientOcclusion(videoOptions.ambientOcclusion),
        mRenderTextureFilter(videoOptions.renderTextureFilter),
        mHorizontalMaterials(videoOptions.horizontalMaterials) {
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
};
