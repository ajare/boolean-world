#define NOMINMAX

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
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
#include "PreviewHighlight.h"
#include "PreviewMaterialProgram.h"
#include "PreviewSurfacePick.h"
#include "PrimitivePreviewGeometry.h"
#include "Preview3D.h"
#include "ReactiveCamera.h"

extern SDL_Window* gWindow;

namespace editor {
namespace {

struct PreviewPrimitive {
  uint8_t priority{};
  PrimitivePreviewGeometry geometry;
};

struct PreviewSession {
  bool open{};
  wp::Vector2 position;
  float angle{};
  float pitch{};
  float eyeZ{};
  float globalTime{};
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

// The game's 3D coordinates map its 2D world (X, Y) onto (X, Z).
PreviewGpuVertex toGpuVertex(PreviewVertex3 const& vertex) {
  PreviewGpuVertex result;
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
    std::vector<PreviewTriangle> const& triangles) {
  buffer.reserve(buffer.size() + triangles.size() * 3);
  for (auto const& triangle : triangles) {
    for (auto const& vertex : triangle.vertices) {
      buffer.push_back(toGpuVertex(vertex));
    }
  }
}

void appendWallQuads(
    std::vector<PreviewGpuVertex>& buffer,
    std::vector<PreviewWallQuad> const& quads) {
  buffer.reserve(buffer.size() + quads.size() * 6);
  for (auto const& quad : quads) {
    buffer.push_back(toGpuVertex(quad.vertices[0]));
    buffer.push_back(toGpuVertex(quad.vertices[1]));
    buffer.push_back(toGpuVertex(quad.vertices[2]));
    buffer.push_back(toGpuVertex(quad.vertices[2]));
    buffer.push_back(toGpuVertex(quad.vertices[3]));
    buffer.push_back(toGpuVertex(quad.vertices[0]));
  }
}

// The triangles outlining whichever surface the centre of the view is
// pointing at, nearest first across every previewed Primitive. Empty when
// the view centre meets nothing.
std::vector<PreviewTriangle> lookedAtSurfaceTriangles() {
  auto position = session.camera->getPosition();
  auto direction = session.camera->getDirection();
  // PrimitivePreviewGeometry keeps height in z, where the renderer's 3D
  // space keeps it in y.
  std::array<float, 3> origin{position.x, position.z, position.y};
  std::array<float, 3> ray{direction.x, direction.z, direction.y};

  PreviewSurfaceHit nearest;
  PrimitivePreviewGeometry const* nearestGeometry = nullptr;
  for (auto const& primitive : session.primitives) {
    auto hit = pickPreviewSurface(primitive.geometry, origin, ray);
    if (!hit.hit() || (nearest.hit() && hit.distance >= nearest.distance)) {
      continue;
    }
    nearest = hit;
    nearestGeometry = &primitive.geometry;
  }

  if (!nearest.hit()) {
    return {};
  }

  switch (nearest.surface) {
    case PreviewSurface::Floor:
      return nearestGeometry->floorTriangles;
    case PreviewSurface::Ceiling:
      return nearestGeometry->ceilingTriangles;
    case PreviewSurface::Wall: {
      // A wall is one lofted Ring edge, so only that quad lights up rather
      // than every wall the Primitive owns.
      auto const& quad = nearestGeometry->wallQuads[nearest.wallIndex];
      return {
          PreviewTriangle{
              {quad.vertices[0], quad.vertices[1], quad.vertices[2]}},
          PreviewTriangle{
              {quad.vertices[2], quad.vertices[3], quad.vertices[0]}}};
    }
    case PreviewSurface::None:
      break;
  }
  return {};
}

// Single owner of the pointer grab. Enabling flushes pending mouse motion,
// so this only acts on an actual change of state - never once per frame.
void syncRelativeMouseMode(bool enabled) {
  if (!gWindow || SDL_GetWindowRelativeMouseMode(gWindow) == enabled) {
    return;
  }
  SDL_SetWindowRelativeMouseMode(gWindow, enabled);
}

void updateCameraFromInput() {
  auto const& io = ImGui::GetIO();

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

  std::vector<PreviewGpuVertex> buffer;
  for (auto const& primitive : session.primitives) {
    auto const& geometry = primitive.geometry;

    buffer.clear();
    appendTriangles(buffer, geometry.floorTriangles);
    materialProgram.setMaterial(
        geometry.floorMaterial.index, geometry.floorMaterial.definition.params);
    materialProgram.draw(buffer);

    buffer.clear();
    appendTriangles(buffer, geometry.ceilingTriangles);
    materialProgram.setMaterial(
        geometry.ceilingMaterial.index,
        geometry.ceilingMaterial.definition.params);
    materialProgram.draw(buffer);

    buffer.clear();
    appendWallQuads(buffer, geometry.wallQuads);
    materialProgram.setMaterial(
        geometry.wallMaterial.index, geometry.wallMaterial.definition.params);
    materialProgram.draw(buffer);
  }

  materialProgram.end();

  drawPreviewHighlight(
      lookedAtSurfaceTriangles(), session.camera->getViewTransform(),
      session.camera->getProjectionTransform());

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
          {primitive->getPriority(), extrudePrimitiveForPreview(*primitive)});
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
  ImGui::SetNextWindowFocus();
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
    ImGui::SetWindowFocus();
    session.viewportMin = ImGui::GetWindowPos();
    session.viewportMax = {
        session.viewportMin.x + ImGui::GetWindowSize().x,
        session.viewportMin.y + ImGui::GetWindowSize().y};

    session.camera->setAspectRatio(
        std::max(ImGui::GetWindowSize().x / ImGui::GetWindowSize().y, 0.01f));
    bool closing = ImGui::Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteGlobal);
    if (!closing) {
      // Relative mode already hides the pointer; this stops the ImGui SDL3
      // backend from calling SDL_ShowCursor() behind its back every frame.
      // ImGui resets the cursor to the arrow each NewFrame, so it reappears
      // on its own once the preview closes.
      ImGui::SetMouseCursor(ImGuiMouseCursor_None);
      updateCameraFromInput();
    }

    auto* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        session.viewportMin, session.viewportMax, IM_COL32(0, 0, 0, 255));
    drawList->AddCallback(renderOpenGL, nullptr);
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

    ImGui::SetCursorPos({12.0f, 12.0f});
    ImGui::TextUnformatted(
        "WASD/Arrows to move, mouse to look, ESC to exit preview");

    if (closing) {
      // Keep this frame's snapshotted geometry alive until ImGui executes the
      // queued OpenGL callback later in the frame.
      session.open = false;
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);

  // Relative mode keeps reporting motion past the window edge, so looking
  // around is never bounded by the screen. Requiring `visible` too means a
  // window ImGui declined to draw releases the pointer instead of holding
  // it hostage with no way to reach the Escape shortcut.
  syncRelativeMouseMode(session.open && visible);
}

}  // namespace editor
