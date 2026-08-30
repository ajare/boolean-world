#pragma once

#include <array>
#include <vector>

#include <core/ArrangementWorldData.h>

#include "PreviewSurfacePick.h"

namespace editor {

// Builds the selected/hovered surface border in renderer coordinates. Core's
// Arrangement uses (x, y, height); WorldRenderer uses (x, height, -y), so the
// reflected third coordinate must match the shaded geometry exactly.
[[nodiscard]] std::vector<std::array<float, 3>> previewSurfaceOutline(
    bw::core::ArrangementWorldData const& worldData,
    PreviewScenePick const& surface);

}  // namespace editor
