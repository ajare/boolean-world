#pragma once

#include "core/ArrangementWorldData.h"

namespace bw::core {
// Compatibility name for the published world-geometry snapshot. The only
// implementation is arrangement-backed.
using WorldData = ArrangementWorldData;
using WorldDataPtr = ArrangementWorldDataPtr;
}  // namespace bw::core
