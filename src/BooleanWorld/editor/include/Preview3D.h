#pragma once

#include <vector>

#include <core/Primitive.h>
#include <willpower/common/Vector2.h>

namespace editor {

[[nodiscard]] bool preview3DIsOpen();

void openPreview3D(
    std::vector<bw::core::Primitive const*> primitives,
    wp::Vector2 const& playerPosition,
    float playerAngle,
    float floorZ);

// Renders the input-blocking preview window. The window owns no authored
// state: all geometry and camera values are snapshotted when it opens.
void renderPreview3D();

// Feeds one SDL mouse-motion event's relative deltas to the preview. While
// the preview is open the pointer is in SDL's relative mode, which stops
// reporting absolute positions - so ImGui's io.MouseDelta reads zero and
// cannot drive the look direction. The event loop must call this instead.
void addPreview3DMouseMotion(float relativeX, float relativeY);

}  // namespace editor
