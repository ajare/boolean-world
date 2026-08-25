#define NOMINMAX

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <GL/glew.h>

#include <common/GameDefines.h>

#include "imgui.h"
#include "PrimitivePreviewGeometry.h"
#include "Preview3D.h"

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
  float eyeZ{};
  std::vector<PreviewPrimitive> primitives;
  ImVec2 viewportMin;
  ImVec2 viewportMax;
};

PreviewSession session;

std::array<float, 3> materialColour(PreviewMaterial const& material) {
  return material.definition.baseColour;
}

void setProjection(float aspect) {
  constexpr float nearPlane = 0.1f;
  constexpr float farPlane = 1000000.0f;
  constexpr float verticalFovDegrees = 75.0f;
  float top = nearPlane * std::tan(verticalFovDegrees * 3.1415926535f / 360.0f);
  float right = top * std::max(aspect, 0.01f);
  glFrustum(-right, right, -top, top, nearPlane, farPlane);
}

void submitVertex(PreviewVertex3 const& vertex, float sine, float cosine) {
  float dx = vertex.x - session.position.x;
  float dy = vertex.y - session.position.y;
  // Angle zero looks along world +Y. OpenGL's camera looks down -Z.
  float cameraX = dx * cosine - dy * sine;
  float depth = dx * sine + dy * cosine;
  glVertex3f(cameraX, vertex.z - session.eyeZ, -depth);
}

void renderTriangle(
    PreviewTriangle const& triangle,
    PreviewMaterial const& material,
    float brightness,
    float sine,
    float cosine) {
  auto colour = materialColour(material);
  glColor3f(
      colour[0] * brightness,
      colour[1] * brightness,
      colour[2] * brightness);
  for (auto const& vertex : triangle.vertices) {
    submitVertex(vertex, sine, cosine);
  }
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

  // ImGui's renderer leaves its shader active. The compatibility-profile
  // immediate-mode pass below intentionally uses the fixed-function matrices;
  // the following ResetRenderState callback restores ImGui's shader and VAO.
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

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  setProjection(static_cast<float>(width) / static_cast<float>(height));
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  float radians = session.angle * 3.1415926535f / 180.0f;
  float sine = std::sin(radians);
  float cosine = std::cos(radians);

  for (auto const& primitive : session.primitives) {
    auto const& geometry = primitive.geometry;
    glBegin(GL_TRIANGLES);
    for (auto const& triangle : geometry.floorTriangles) {
      renderTriangle(
          triangle, geometry.floorMaterial, 0.85f, sine, cosine);
    }
    for (auto const& triangle : geometry.ceilingTriangles) {
      renderTriangle(
          triangle, geometry.ceilingMaterial, 0.65f, sine, cosine);
    }
    auto wallColour = materialColour(geometry.wallMaterial);
    glColor3f(wallColour[0], wallColour[1], wallColour[2]);
    for (auto const& quad : geometry.wallQuads) {
      submitVertex(quad.vertices[0], sine, cosine);
      submitVertex(quad.vertices[1], sine, cosine);
      submitVertex(quad.vertices[2], sine, cosine);
      submitVertex(quad.vertices[2], sine, cosine);
      submitVertex(quad.vertices[3], sine, cosine);
      submitVertex(quad.vertices[0], sine, cosine);
    }
    glEnd();
  }

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
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
  session.primitives.reserve(primitives.size());
  for (auto const* primitive : primitives) {
    if (primitive) {
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
  ImGui::SetNextWindowPos(
      {viewport->Pos.x + margin, viewport->Pos.y + margin});
  ImGui::SetNextWindowSize(
      {viewport->Size.x - margin * 2.0f, viewport->Size.y - margin * 2.0f});
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

    auto* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        session.viewportMin, session.viewportMax, IM_COL32(0, 0, 0, 255));
    drawList->AddCallback(renderOpenGL, nullptr);
    drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

    ImGui::SetCursorPos({12.0f, 12.0f});
    ImGui::TextUnformatted("Press ESC to exit preview");

    if (ImGui::Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteGlobal)) {
      // Keep this frame's snapshotted geometry alive until ImGui executes the
      // queued OpenGL callback later in the frame.
      session.open = false;
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
}

}  // namespace editor
