#pragma once

#include <vector>

#include <core/Primitive.h>
#include <willpower/common/Vector2.h>

namespace editor {

class Document;

[[nodiscard]] bool preview3DIsOpen();

void openPreview3D(
    Document* document,
    std::vector<bw::core::Primitive const*> primitives,
    wp::Vector2 const& playerPosition,
    float playerAngle,
    float floorZ);

// Renders the input-blocking preview window. Geometry is snapshotted when it
// opens; material assignments and shared Sub-material data selected through
// the surface editor are reflected immediately. This also applies to the
// boolean Arrangement `openPreview3D` builds: it is built once, synchronously,
// from the Primitive list passed to `openPreview3D`, so shape edits made
// while the preview is open are not live-reflected until it is reopened.
void renderPreview3D();

// Feeds one SDL mouse-motion event's relative deltas to the preview. While
// the preview is open the pointer is in SDL's relative mode, which stops
// reporting absolute positions - so ImGui's io.MouseDelta reads zero and
// cannot drive the look direction. The event loop must call this instead.
void addPreview3DMouseMotion(float relativeX, float relativeY);

}  // namespace editor
