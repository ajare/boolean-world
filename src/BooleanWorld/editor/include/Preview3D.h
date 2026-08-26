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

// Renders the input-blocking preview window through the game's own
// WorldRenderer/mpp::Scene/RenderPipeline stack, so bloom, tonemapping and
// ambient occlusion match Launcher.exe. The boolean Arrangement it draws is
// built once, synchronously, from the Primitive list passed to
// `openPreview3D`, so edits made while the preview is open are not
// live-reflected until it is reopened.
void renderPreview3D();

// Releases the preview's render stack, including the process-lifetime
// EditorRenderSystem. Must run while the editor's GL context is still
// current, so the editor's shutdown calls this before destroying it.
void shutdownPreview3D();

// Feeds one SDL mouse-motion event's relative deltas to the preview. While
// the preview is open the pointer is in SDL's relative mode, which stops
// reporting absolute positions - so ImGui's io.MouseDelta reads zero and
// cannot drive the look direction. The event loop must call this instead.
void addPreview3DMouseMotion(float relativeX, float relativeY);

}  // namespace editor
