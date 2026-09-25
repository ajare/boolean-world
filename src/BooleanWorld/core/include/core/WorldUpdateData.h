#pragma once

#include <willpower/common/Vector2.h>

#include "core/LayerSelection.h"
#include "core/Platform.h"

namespace bw {
namespace core {

struct WorldUpdateData {
  wp::Vector2 entityPosition;
  float entityAngle;
  float entityRadius;
  float entityFov;
  float entityViewDist;
  bool entityMoved;
  bool entityTurned;
  LayerSelection layerSelection;
  // Gameplay with swept Zone changes delivers accepted physical segments
  // separately. Other hosts retain the historical update-position triggers.
  bool entityTriggersEnabled{true};
};

}  // namespace core
}  // namespace bw
