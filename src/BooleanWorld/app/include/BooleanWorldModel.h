#pragma once

#include <applib/Model.h>

#include "Platform.h"
#include "VideoOptions.h"

struct BooleanWorldModel : public applib::Model {
private:
  bw::app::RenderScale mActiveRenderScale;

public:
  BooleanWorldModel(applib::EntityHandlerFactoryFunction handlerFactory,
                    wp::application::resourcesystem::ResourceManager* resourceMgr,
                    bw::app::VideoOptions const& videoOptions = {})
      : applib::Model(handlerFactory, resourceMgr),
        mActiveRenderScale(videoOptions.renderScale) {
  }

  bw::app::RenderScale getActiveRenderScale() const {
    return mActiveRenderScale;
  }

  void setActiveRenderScale(bw::app::RenderScale renderScale) {
    mActiveRenderScale = renderScale;
  }
};
