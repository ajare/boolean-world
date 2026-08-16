#pragma once

#include <mpp/Scene.h>

#include <core/World.h>
#include <core/WorldData.h>

#include "Platform.h"

struct StatePlayTransitionData {
  bw::core::World* world;
  bw::core::WorldDataPtr worldData;
  mpp::ScenePtr scene;
};
