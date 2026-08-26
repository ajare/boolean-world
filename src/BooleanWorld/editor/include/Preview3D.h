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

// Renders the preview into the editor's world viewport - the same central
// region the 2D level geometry is edited in, which the preview takes over
// while it is open - through the game's own WorldRenderer/mpp::Scene/
// RenderPipeline stack, so bloom, tonemapping and ambient occlusion match
// Launcher.exe. Must be called in place of the World window's own content,
// after renderWidgets() has published the viewport rect for this frame.
//
// The boolean Arrangement it draws is built once, synchronously, from the
// Primitive list passed to `openPreview3D`, so edits made while the preview
// is open are not live-reflected until it is reopened - which is why the
// editor keeps the document's structure frozen meanwhile.
void renderPreview3D();

// Draws the selected surface's authoring - the picked wall/floor/ceiling and
// its Sub-material editor - as one collapsible header, for the editor's left
// panel to host. Renders nothing at all unless the preview is open, and the
// header itself explains how to pick a surface until one is. Call it before
// renderPreview3D() in the frame: material edits made here are pushed into
// the scene by the render pass that follows.
void renderPreview3DSelectedSurface();

// Closes the preview and releases the GPU resources this open of it built,
// as pressing Escape inside the viewport does.
void closePreview3D();

// Releases the preview's render stack. Must run while the editor's GL
// context is still current, so the editor's shutdown calls this before
// destroying it. The EditorRenderSystem underneath outlives this and is
// destroyed separately - see destroyEditorRenderSystem().
void shutdownPreview3D();

// Feeds one SDL mouse-motion event's relative deltas to the preview. While
// the camera is being turned the pointer is in SDL's relative mode, which
// stops reporting absolute positions - so ImGui's io.MouseDelta reads zero
// and cannot drive the look direction. The event loop must call this
// instead. Motion outside a turn is accumulated and discarded.
void addPreview3DMouseMotion(float relativeX, float relativeY);

}  // namespace editor
