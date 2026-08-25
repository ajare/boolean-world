#define NOMINMAX

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/vec3.hpp>
#pragma warning(pop)

#include <GL/glew.h>
#include <SDL3/SDL.h>

#include <common/GameDefines.h>

#include "Document.h"
#include "imgui.h"
#include "InputOptions.h"
#include "PlayerView.h"
#include "PreviewMaterialProgram.h"
#include "PreviewSurfacePick.h"
#include "PrimitivePreviewGeometry.h"
#include "Preview3D.h"
#include "ProcMaterialLibrary.h"
#include "ReactiveCamera.h"

extern SDL_Window* gWindow;

namespace editor {
namespace {

struct PreviewPrimitive {
  uint8_t priority{};
  PrimitivePreviewGeometry geometry;
};

// Whichever surface the centre of the view is pointing at, or which the user
// has clicked to select. Primitives are held by index because the session's
// list outlives any one frame.
struct PreviewSurfaceRef {
  bool valid{};
  size_t primitiveIndex{};
  PreviewSurface surface{PreviewSurface::None};
  size_t wallIndex{};
};

struct PreviewSession {
  bool open{};
  wp::Vector2 position;
  float angle{};
  float pitch{};
  float eyeZ{};
  float globalTime{};
  // What the crosshair is on this frame, and what the user has selected.
  // Selecting stops the crosshair search: the tint goes away, the pointer is
  // handed back, and the camera turns only while dragging.
  PreviewSurfaceRef lookedAt;
  PreviewSurfaceRef selection;
  // A left-drag that began inside the preview, so releasing the button over
  // another window still ends the turn.
  bool dragTurning{};
  // Mouse motion accumulated from SDL events since the last frame, in place
  // of ImGui's io.MouseDelta - see addPreview3DMouseMotion.
  float mouseMotionX{};
  float mouseMotionY{};
  bw::app::InputOptions inputOptions;
  std::unique_ptr<ReactiveCamera> camera;
  // Grounding and rendering deliberately share this exact in-scope Primitive
  // list. It remains valid while the input-blocking preview is open.
  std::vector<bw::core::Primitive const*> primitivesForGrounding;
  std::vector<PreviewPrimitive> primitives;
  ImVec2 viewportMin;
  ImVec2 viewportMax;
};

PreviewSession session;

// Compiled once for the process and reused across every open/close of the
// preview window - see PreviewMaterialProgram and GitHub issue #256.
PreviewMaterialProgram materialProgram;

// Per-vertex tint. world_pbr.frag multiplies a surface's own colour by this
// before lighting, so white renders the material exactly as authored.
struct Tint {
  float r{1.0f};
  float g{1.0f};
  float b{1.0f};
};

constexpr Tint untinted{};
// Leaves red and green alone and holds back only blue, so the surface reads
// as the same material seen under a warmer light rather than as a different
// colour painted over it. Gamma compresses this considerably and the
// specular terms are not tinted at all, so the value has to be well under 1
// to register on screen.
constexpr Tint lookedAtTint{1.0f, 1.0f, 0.35f};

// The game's 3D coordinates map its 2D world (X, Y) onto (X, Z).
PreviewGpuVertex toGpuVertex(PreviewVertex3 const& vertex, Tint const& tint) {
  PreviewGpuVertex result;
  result.r = tint.r;
  result.g = tint.g;
  result.b = tint.b;
  result.px = vertex.x;
  result.py = vertex.z;
  result.pz = vertex.y;
  result.nx = vertex.nx;
  result.ny = vertex.nz;
  result.nz = vertex.ny;
  result.u = vertex.u;
  result.v = vertex.v;
  return result;
}

void appendTriangles(
    std::vector<PreviewGpuVertex>& buffer,
    std::vector<PreviewTriangle> const& triangles,
    Tint const& tint) {
  buffer.reserve(buffer.size() + triangles.size() * 3);
  for (auto const& triangle : triangles) {
    for (auto const& vertex : triangle.vertices) {
      buffer.push_back(toGpuVertex(vertex, tint));
    }
  }
}

// A wall is one lofted Ring edge, so tintedIndex marks the single quad the
// viewer is looking at rather than every wall the Primitive owns.
void appendWallQuads(
    std::vector<PreviewGpuVertex>& buffer,
    std::vector<PreviewWallQuad> const& quads,
    size_t tintedIndex,
    Tint const& tint) {
  buffer.reserve(buffer.size() + quads.size() * 6);
  for (size_t index = 0; index < quads.size(); ++index) {
    auto const& quad = quads[index];
    auto const& quadTint = index == tintedIndex ? tint : untinted;
    buffer.push_back(toGpuVertex(quad.vertices[0], quadTint));
    buffer.push_back(toGpuVertex(quad.vertices[1], quadTint));
    buffer.push_back(toGpuVertex(quad.vertices[2], quadTint));
    buffer.push_back(toGpuVertex(quad.vertices[2], quadTint));
    buffer.push_back(toGpuVertex(quad.vertices[3], quadTint));
    buffer.push_back(toGpuVertex(quad.vertices[0], quadTint));
  }
}

// Whichever surface the centre of the view is pointing at, nearest across
// every previewed Primitive. Invalid when the view centre meets nothing.
PreviewSurfaceRef lookedAtSurface() {
  auto position = session.camera->getPosition();
  auto direction = session.camera->getDirection();
  // PrimitivePreviewGeometry keeps height in z, where the renderer's 3D
  // space keeps it in y.
  std::array<float, 3> origin{position.x, position.z, position.y};
  std::array<float, 3> ray{direction.x, direction.z, direction.y};

  // In draw order, which is what settles coincident surfaces.
  std::vector<PrimitivePreviewGeometry const*> geometries;
  geometries.reserve(session.primitives.size());
  for (auto const& primitive : session.primitives) {
    geometries.push_back(&primitive.geometry);
  }

  auto pick = pickPreviewSceneSurface(geometries, origin, ray);
  if (!pick.hit()) {
    return {};
  }
  return {
      true, pick.primitiveIndex, pick.surfaceHit.surface,
      pick.surfaceHit.wallIndex};
}

// A modeless window naming what the user selected. Closing it, by its own
// close button, means the same thing as right-clicking the preview.
void renderSelectedSurfaceWindow() {
  if (!session.selection.valid) {
    return;
  }
  auto& geometry =
      session.primitives[session.selection.primitiveIndex].geometry;
  auto* material = previewSurfaceMaterial(geometry, session.selection.surface);
  if (!material) {
    return;
  }

  ImGui::SetNextWindowPos(
      {session.viewportMin.x + 16.0f, session.viewportMin.y + 48.0f},
      ImGuiCond_Appearing);
  bool stayOpen = true;
  if (ImGui::Begin(
          "Selected surface", &stayOpen,
          ImGuiWindowFlags_AlwaysAutoResize |
              ImGuiWindowFlags_NoSavedSettings)) {
    auto surfaceName = previewSurfaceName(session.selection.surface);
    auto materialName = previewMaterialName(material->index);
    ImGui::Text(
        "%.*s material: %.*s", (int)surfaceName.size(), surfaceName.data(),
        (int)materialName.size(), materialName.data());
    ImGui::Separator();

    // Edited straight into the snapshot the renderer reads, so the preview
    // follows the slider as it moves - there is nothing to apply.
    auto parameters = previewMaterialParameters(material->index);
    if (parameters.empty()) {
      ImGui::TextUnformatted("This material has no adjustable parameters.");
    }
    for (auto const& parameter : parameters) {
      std::string label(parameter.name);
      ImGui::SliderFloat(
          label.c_str(), &material->definition.params[parameter.index],
          parameter.minimum, parameter.maximum);
    }

    if (!parameters.empty()) {
      ImGui::Separator();
      if (ImGui::Button("Reset to defaults")) {
        for (auto const& parameter : parameters) {
          material->definition.params[parameter.index] =
              parameter.defaultValue;
        }
      }
    }
  }
  ImGui::End();

  if (!stayOpen) {
    session.selection = {};
    session.dragTurning = false;
  }
}

// Single owner of the pointer grab. Enabling flushes pending mouse motion,
// so this only acts on an actual change of state - never once per frame.
void syncRelativeMouseMode(bool enabled) {
  if (!gWindow || SDL_GetWindowRelativeMouseMode(gWindow) == enabled) {
    return;
  }
  SDL_SetWindowRelativeMouseMode(gWindow, enabled);
}

// With nothing selected the pointer is grabbed and every scrap of motion
// turns the camera. Once something is selected the pointer belongs to the
// user again, so the camera only turns while they drag with the left button
// from inside the preview.
bool turningThisFrame(bool previewHovered) {
  if (!session.selection.valid) {
    return true;
  }
  if (previewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    session.dragTurning = true;
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    session.dragTurning = false;
  }
  return session.dragTurning;
}

void updateCameraFromInput(bool previewHovered) {
  auto const& io = ImGui::GetIO();

  // Motion accumulates from SDL whether or not it is wanted, so discard it
  // when not turning; carrying it over would fling the view on the next drag.
  if (!turningThisFrame(previewHovered)) {
    session.mouseMotionX = 0.0f;
    session.mouseMotionY = 0.0f;
  }

  float previousAngle = session.angle;
  float previousPitch = session.pitch;
  session.angle = bw::app::applyMouseYaw(
      previousAngle, session.mouseMotionX, session.inputOptions.mouseSensitivity);
  session.pitch = bw::app::applyMousePitch(
      previousPitch, session.mouseMotionY, session.inputOptions.mouseSensitivity);
  session.mouseMotionX = 0.0f;
  session.mouseMotionY = 0.0f;

  // ReactiveCamera consumes turn deltas, just as the game's player camera
  // does. Keyboard movement stays in the world plane; there is no vertical
  // input or physics in this preview.
  session.camera->yaw(previousAngle - session.angle);
  session.camera->pitch(session.pitch - previousPitch);

  wp::Vector2 movement = wp::Vector2::ZERO;
  // A material parameter being typed into takes the keyboard with it -
  // otherwise entering a value would fly the camera across the level.
  if (!io.WantTextInput) {
    if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
      movement.y += 1.0f;
    }
    if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
      movement.y -= 1.0f;
    }
    if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
      movement.x -= 1.0f;
    }
    if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
      movement.x += 1.0f;
    }
  }
  movement.normalise();
  session.position += bw::app::playerMovement(movement, session.angle) *
                      BW_PLAYER_SPEED * io.DeltaTime;

  if (auto floorZ = resolveGroundingFloorZ(
          session.primitivesForGrounding, session.position)) {
    session.eyeZ = *floorZ + BW_PLAYER_EYE_HEIGHT;
  }
  // Outside all in-scope Primitive coverage, retain the last grounded eye Z.
  session.camera->setPosition(
      {session.position.x, session.eyeZ, session.position.y});
}

void renderOpenGL(ImDrawList const*, ImDrawCmd const*) {
  auto const& io = ImGui::GetIO();
  auto scale = io.DisplayFramebufferScale;
  int x = static_cast<int>(session.viewportMin.x * scale.x);
  int width = std::max(
      1, static_cast<int>((session.viewportMax.x - session.viewportMin.x) * scale.x));
  int height = std::max(
      1, static_cast<int>((session.viewportMax.y - session.viewportMin.y) * scale.y));
  int framebufferHeight = static_cast<int>(io.DisplaySize.y * scale.y);
  int y = framebufferHeight -
          static_cast<int>(session.viewportMax.y * scale.y);

  // ImGui's renderer leaves its shader active; the following
  // ResetRenderState callback restores ImGui's shader and vertex state.
  glUseProgram(0);
  glEnable(GL_SCISSOR_TEST);
  glScissor(x, y, width, height);
  glViewport(x, y, width, height);
  glClearDepth(1.0);
  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  // Later, higher-priority Primitives deterministically replace coplanar
  // fragments emitted by earlier ones without changing raw geometry.
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);

  if (!materialProgram.ensureReady()) {
    glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    return;
  }

  session.globalTime += io.DeltaTime;

  auto cameraPosition = session.camera->getPosition();
  materialProgram.begin(
      session.camera->getViewTransform(),
      session.camera->getProjectionTransform(), cameraPosition,
      cameraPosition, session.globalTime);

  // Decided during the UI pass, so the surface drawn as highlighted is the
  // same one a click in that frame selected.
  auto const& lookedAt = session.lookedAt;

  std::vector<PreviewGpuVertex> buffer;
  for (size_t index = 0; index < session.primitives.size(); ++index) {
    auto const& geometry = session.primitives[index].geometry;
    auto tintOf = [&](PreviewSurface surface) {
      return lookedAt.valid && lookedAt.primitiveIndex == index &&
                     lookedAt.surface == surface
                 ? lookedAtTint
                 : untinted;
    };

    buffer.clear();
    appendTriangles(
        buffer, geometry.floorTriangles, tintOf(PreviewSurface::Floor));
    materialProgram.setMaterial(
        geometry.floorMaterial.index, geometry.floorMaterial.definition.params);
    materialProgram.draw(buffer);

    buffer.clear();
    appendTriangles(
        buffer, geometry.ceilingTriangles, tintOf(PreviewSurface::Ceiling));
    materialProgram.setMaterial(
        geometry.ceilingMaterial.index,
        geometry.ceilingMaterial.definition.params);
    materialProgram.draw(buffer);

    buffer.clear();
    auto tintedWall = lookedAt.valid && lookedAt.primitiveIndex == index &&
                              lookedAt.surface == PreviewSurface::Wall
                          ? lookedAt.wallIndex
                          : std::numeric_limits<size_t>::max();
    appendWallQuads(buffer, geometry.wallQuads, tintedWall, lookedAtTint);
    materialProgram.setMaterial(
        geometry.wallMaterial.index, geometry.wallMaterial.definition.params);
    materialProgram.draw(buffer);
  }

  materialProgram.end();

  glDisable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glEnable(GL_BLEND);
}

}  // namespace

bool preview3DIsOpen() {
  return session.open;
}

void addPreview3DMouseMotion(float relativeX, float relativeY) {
  if (!session.open) {
    return;
  }
  session.mouseMotionX += relativeX;
  session.mouseMotionY += relativeY;
}

void openPreview3D(
    std::vector<bw::core::Primitive const*> primitives,
    wp::Vector2 const& playerPosition,
    float playerAngle,
    float floorZ) {
  std::stable_sort(
      primitives.begin(), primitives.end(),
      [](auto const* left, auto const* right) {
        return left->getPriority() < right->getPriority();
      });

  session = {};
  session.open = true;
  session.position = playerPosition;
  session.angle = playerAngle;
  session.eyeZ = floorZ + BW_PLAYER_EYE_HEIGHT;
  session.camera = std::make_unique<ReactiveCamera>(
      glm::vec3{playerPosition.x, session.eyeZ, playerPosition.y},
      bw::app::cameraYaw(playerAngle), 0.0f, BW_PLAYER_FOV, 1.0f);
  session.camera->setClipDistances(0.1f, 1000000.0f);
  session.primitives.reserve(primitives.size());
  session.primitivesForGrounding.reserve(primitives.size());
  for (auto const* primitive : primitives) {
    if (primitive) {
      session.primitivesForGrounding.push_back(primitive);
      session.primitives.push_back(
          {primitive->getPriority(), extrudePrimitiveForPreview(
                                         *primitive, &procMaterialLibrary())});
    }
  }
}

void renderPreview3D() {
  if (!session.open) {
    return;
  }

  auto* viewport = ImGui::GetMainViewport();
  constexpr float margin = 8.0f;
  ImVec2 windowSize{
      (viewport->Size.x - margin * 2.0f) * 0.5f,
      (viewport->Size.y - margin * 2.0f) * 0.5f};
  ImGui::SetNextWindowPos(
      {viewport->Pos.x + (viewport->Size.x - windowSize.x) * 0.5f,
       viewport->Pos.y + (viewport->Size.y - windowSize.y) * 0.5f});
  ImGui::SetNextWindowSize(windowSize);
  // Taking focus every frame would stop the selected-surface window from
  // being clicked or dragged, so only insist on it while the preview owns
  // the pointer outright.
  if (!session.selection.valid) {
    ImGui::SetNextWindowFocus();
  }
  ImGui::SetNextFrameWantCaptureMouse(true);
  ImGui::SetNextFrameWantCaptureKeyboard(true);

  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  bool visible = ImGui::Begin("##3D preview", nullptr, flags);
  if (visible) {
    if (!session.selection.valid) {
      ImGui::SetWindowFocus();
    }
    session.viewportMin = ImGui::GetWindowPos();
    session.viewportMax = {
        session.viewportMin.x + ImGui::GetWindowSize().x,
        session.viewportMin.y + ImGui::GetWindowSize().y};
    // False when the selected-surface window sits over the pointer, which is
    // what keeps dragging that window from turning the camera underneath it.
    bool previewHovered = ImGui::IsWindowHovered();

    session.camera->setAspectRatio(
        std::max(ImGui::GetWindowSize().x / ImGui::GetWindowSize().y, 0.01f));
    bool closing = ImGui::Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteGlobal);
    if (!closing) {
      if (!session.selection.valid) {
        // Relative mode already hides the pointer; this stops the ImGui SDL3
        // backend from calling SDL_ShowCursor() behind its back every frame.
        // ImGui resets the cursor to the arrow each NewFrame, so it comes
        // back on its own once something is selected or the preview closes.
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
      }
      updateCameraFromInput(previewHovered);

      // Only hunt for a surface while none is selected: selecting is what
      // takes the tint away.
      session.lookedAt =
          session.selection.valid ? PreviewSurfaceRef{} : lookedAtSurface();

      if (!session.selection.valid) {
        // The pointer is grabbed and aiming is done with the whole window,
        // so this deliberately does not ask where the cursor is.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            session.lookedAt.valid) {
          session.selection = session.lookedAt;
          session.lookedAt = {};
        }
      } else if (
          previewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        session.selection = {};
        session.dragTurning = false;
      }
    }

    auto* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        session.viewportMin, session.viewportMax, IM_COL32(0, 0, 0, 255));
    drawList->AddCallback(renderOpenGL, nullptr);
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

    ImGui::SetCursorPos({12.0f, 12.0f});
    ImGui::TextUnformatted(
        session.selection.valid
            ? "WASD/Arrows to move, left-drag to look, right-click to "
              "deselect, ESC to exit preview"
            : "WASD/Arrows to move, mouse to look, click a surface to select, "
              "ESC to exit preview");

    if (closing) {
      // Keep this frame's snapshotted geometry alive until ImGui executes the
      // queued OpenGL callback later in the frame.
      session.open = false;
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);

  renderSelectedSurfaceWindow();

  // Relative mode keeps reporting motion past the window edge, so looking
  // around is never bounded by the screen. Requiring `visible` too means a
  // window ImGui declined to draw releases the pointer instead of holding
  // it hostage with no way to reach the Escape shortcut. A selection hands
  // the pointer back so the user can reach the window naming it.
  syncRelativeMouseMode(
      session.open && visible && !session.selection.valid);
}

}  // namespace editor
