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

#include <common/GameDefines.h>

#include "Document.h"
#include "imgui.h"
#include "InputOptions.h"
#include "PlayerView.h"
#include "PreviewMaterialProgram.h"
#include "PrimitivePreviewGeometry.h"
#include "Preview3D.h"
#include "ReactiveCamera.h"

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

void updateCameraFromInput() {
  auto const& io = ImGui::GetIO();

  float previousAngle = session.angle;
  float previousPitch = session.pitch;
  session.angle = bw::app::applyMouseYaw(
      previousAngle, io.MouseDelta.x, session.inputOptions.mouseSensitivity);
  session.pitch = bw::app::applyMousePitch(
      previousPitch, io.MouseDelta.y, session.inputOptions.mouseSensitivity);

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

  glDisable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glEnable(GL_BLEND);
}

}  // namespace

bool preview3DIsOpen() {
  return session.open;
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
      // The mouse drives the look direction rather than pointing at anything,
      // so hide the cursor for the duration. ImGui resets the cursor to the
      // arrow each NewFrame, so it reappears on its own once the preview
      // closes - including on this frame's closing branch below.
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
}

}  // namespace editor
