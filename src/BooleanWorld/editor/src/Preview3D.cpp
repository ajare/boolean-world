#define NOMINMAX

#include <algorithm>
#include <array>
#include <cstddef>
#include <cctype>
#include <cstdio>
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
#include <core/Layer.h>

#include "Actions.h"
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
#include "Undo.h"

extern SDL_Window* gWindow;

namespace editor {
namespace {

struct PreviewPrimitive {
  uint8_t priority{};
  PrimitivePreviewGeometry geometry;
  // The authored source remains owned by the open Document. Procedural step
  // output is previewable but deliberately not editable (ADR-0015).
  bw::core::Primitive* source{};
  bool editable{};
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

struct PreviewMaterialEditorState {
  bool initialized{};
  bool hasDraft{};
  int catalogIndex{};
  std::string editingId;
  char name[256]{};
  uint32_t materialIndex{};
  std::vector<float> params;
  std::array<float, 3> colour{};
};

struct PreviewSession {
  bool open{};
  Document* document{};
  bw::core::World const* world{};
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
  PreviewMaterialEditorState materialEditor;
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

Tint materialTint(PreviewMaterial const& material, Tint const& tint = untinted) {
  if (!material.resolved) return tint;
  return {
      material.definition.baseColour[0] * tint.r,
      material.definition.baseColour[1] * tint.g,
      material.definition.baseColour[2] * tint.b};
}

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
    Tint const& baseTint,
    Tint const& highlightedTint) {
  buffer.reserve(buffer.size() + quads.size() * 6);
  for (size_t index = 0; index < quads.size(); ++index) {
    auto const& quad = quads[index];
    auto const& quadTint =
        index == tintedIndex ? highlightedTint : baseTint;
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

void refreshPreviewMaterials() {
  for (auto& primitive : session.primitives) {
    if (primitive.source) {
      primitive.geometry = extrudePrimitiveForPreview(
          *primitive.source, &procMaterialLibrary());
    }
  }
}

PrimitiveMaterialSurface materialSurface(PreviewSurface surface) {
  switch (surface) {
    case PreviewSurface::Floor:
      return PrimitiveMaterialSurface::Floor;
    case PreviewSurface::Ceiling:
      return PrimitiveMaterialSurface::Ceiling;
    case PreviewSurface::Wall:
    case PreviewSurface::None:
      return PrimitiveMaterialSurface::Wall;
  }
  return PrimitiveMaterialSurface::Wall;
}

std::string surfaceSubMaterialId(
    bw::core::Primitive const& primitive, PreviewSurface surface) {
  auto const& properties = primitive.getProperties();
  switch (surface) {
    case PreviewSurface::Floor:
      return properties.floorMaterialId;
    case PreviewSurface::Ceiling:
      return properties.ceilingMaterialId;
    case PreviewSurface::Wall:
      return properties.wallMaterialId;
    case PreviewSurface::None:
      return {};
  }
  return {};
}

void loadMaterialDraft(std::string const& id) {
  auto& state = session.materialEditor;
  state.initialized = true;
  state.hasDraft = false;
  state.editingId.clear();

  auto const& catalogs = procMaterialLibrary().catalogs();
  auto const* owner = procMaterialLibrary().findCatalogForSubMaterial(id);
  if (!owner) {
    state.catalogIndex = catalogs.empty()
                             ? 0
                             : std::clamp(state.catalogIndex, 0,
                                          static_cast<int>(catalogs.size()) - 1);
    return;
  }

  state.catalogIndex = static_cast<int>(std::distance(catalogs.data(), owner));
  auto const* material = procMaterialLibrary().findSubMaterial(id);
  if (!material) return;

  state.hasDraft = true;
  state.editingId = id;
  std::snprintf(state.name, sizeof(state.name), "%s", material->displayName.c_str());
  state.materialIndex = material->materialIndex;
  state.params = material->paramValues;
  state.colour = material->baseColour;
}

PreviewMaterial draftPreviewMaterial() {
  PreviewMaterial result;
  auto const& state = session.materialEditor;
  result.index = state.materialIndex;
  for (size_t i = 0;
       i < state.params.size() && i < result.definition.params.size(); ++i) {
    result.definition.params[i] = state.params[i];
  }
  result.definition.baseColour = state.colour;
  result.resolved = true;
  return result;
}

// Preview an unsaved shared Sub-material edit everywhere that stable id is
// used. Saving as new changes editingId, so the draft then follows only the
// selected surface's newly assigned id.
void applyMaterialDraft() {
  auto const& state = session.materialEditor;
  if (!state.hasDraft || state.editingId.empty()) return;
  auto draft = draftPreviewMaterial();
  for (auto& primitive : session.primitives) {
    if (!primitive.source) continue;
    auto const& properties = primitive.source->getProperties();
    if (properties.floorMaterialId == state.editingId) {
      primitive.geometry.floorMaterial = draft;
    }
    if (properties.ceilingMaterialId == state.editingId) {
      primitive.geometry.ceilingMaterial = draft;
    }
    if (properties.wallMaterialId == state.editingId) {
      primitive.geometry.wallMaterial = draft;
    }
  }
}

void renderPreviewMaterialEditor(PreviewPrimitive& previewPrimitive) {
  auto const& catalogs = procMaterialLibrary().catalogs();
  if (catalogs.empty()) {
    ImGui::TextDisabled("No ProcMaterial resources are available.");
    return;
  }

  auto& state = session.materialEditor;
  if (!state.initialized) {
    loadMaterialDraft(surfaceSubMaterialId(
        *previewPrimitive.source, session.selection.surface));
  }
  state.catalogIndex = std::clamp(
      state.catalogIndex, 0, static_cast<int>(catalogs.size()) - 1);

  std::string catalogItems;
  for (auto const& catalog : catalogs) {
    catalogItems += catalog.resourceName;
    catalogItems += '\0';
  }
  ImGui::SetNextItemWidth(280.0f);
  if (ImGui::Combo(
          "ProcMaterial", &state.catalogIndex, catalogItems.c_str(), 6)) {
    refreshPreviewMaterials();
    state.hasDraft = false;
    state.editingId.clear();
  }

  auto const& catalog = catalogs[state.catalogIndex];
  int selectedSubMaterial = -1;
  std::string subMaterialItems;
  for (size_t i = 0; i < catalog.data.subMaterials.size(); ++i) {
    auto const& material = catalog.data.subMaterials[i];
    if (material.id == state.editingId) selectedSubMaterial = static_cast<int>(i);
    subMaterialItems += material.displayName;
    subMaterialItems += '\0';
  }
  ImGui::SetNextItemWidth(280.0f);
  if (!catalog.data.subMaterials.empty() &&
      ImGui::Combo(
          "Sub-material", &selectedSubMaterial,
          subMaterialItems.c_str(), 8)) {
    auto const id = catalog.data.subMaterials[selectedSubMaterial].id;
    transactUndoableAction(
        session.document, "Set preview surface Sub-material",
        [&](Document* actionDoc) {
          return setPrimitiveSubMaterial(
              actionDoc, previewPrimitive.source,
              materialSurface(session.selection.surface), id);
        });
    refreshPreviewMaterials();
    loadMaterialDraft(id);
  }

  if (!state.hasDraft) {
    ImGui::TextDisabled("Select a Sub-material to edit its parameters.");
    return;
  }

  if (ImGui::CollapsingHeader(
          "Material parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::InputText("Name", state.name, sizeof(state.name));
    if (auto const* schema =
            catalog.data.findTechniqueSchema(state.materialIndex)) {
      for (size_t i = 0;
           i < schema->parameters.size() && i < state.params.size(); ++i) {
        auto const& parameter = schema->parameters[i];
        ImGui::SliderFloat(
            parameter.name.c_str(), &state.params[i],
            parameter.minimum, parameter.maximum);
      }
    }
    ImGui::ColorEdit3("Base colour", state.colour.data());

    if (ImGui::Button("Save existing")) {
      auto id = state.editingId;
      auto name = std::string(state.name);
      auto params = state.params;
      auto colour = state.colour;
      if (transactUndoableActionAtomically(
              session.document, "Save Sub-material",
              [&](Document* actionDoc) {
                renameSubMaterial(
                    actionDoc, &procMaterialLibrary(), id, name);
                return editSubMaterial(
                    actionDoc, &procMaterialLibrary(), id, params, colour);
              })) {
        refreshPreviewMaterials();
        loadMaterialDraft(id);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save as new Sub-material")) {
      auto name = std::string(state.name);
      auto params = state.params;
      auto colour = state.colour;
      auto materialIndex = state.materialIndex;
      auto resourceName = catalog.resourceName;
      std::string createdId;
      if (transactUndoableActionAtomically(
              session.document, "Save new Sub-material",
              [&](Document* actionDoc) {
                if (!createSubMaterial(
                        actionDoc, &procMaterialLibrary(), resourceName, name,
                        materialIndex, params, colour, &createdId)) {
                  return false;
                }
                return setPrimitiveSubMaterial(
                    actionDoc, previewPrimitive.source,
                    materialSurface(session.selection.surface), createdId);
              })) {
        refreshPreviewMaterials();
        loadMaterialDraft(createdId);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
      auto id = state.editingId;
      refreshPreviewMaterials();
      loadMaterialDraft(id);
    }
  }

  applyMaterialDraft();
}

// A modeless authoring window for the clicked wall/floor/ceiling. Closing it,
// by its own close button, means the same thing as right-clicking the preview.
void renderSelectedSurfaceWindow() {
  if (!session.selection.valid ||
      session.selection.primitiveIndex >= session.primitives.size()) {
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
    std::string label(surfaceName);
    if (!label.empty()) {
      label.front() = static_cast<char>(std::toupper(label.front()));
    }
    ImGui::Text("Selected %s", label.c_str());

    auto& previewPrimitive =
        session.primitives[session.selection.primitiveIndex];
    if (!previewPrimitive.editable || !previewPrimitive.source ||
        !session.document) {
      ImGui::TextDisabled(
          "This surface is generated by a step that does not permit direct editing.");
    } else {
      renderPreviewMaterialEditor(previewPrimitive);
    }
  }
  ImGui::End();

  if (!stayOpen) {
    refreshPreviewMaterials();
    session.selection = {};
    session.materialEditor = {};
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
    auto tintOf = [&](PreviewSurface surface, PreviewMaterial const& material) {
      auto highlight = lookedAt.valid && lookedAt.primitiveIndex == index &&
                               lookedAt.surface == surface
                           ? lookedAtTint
                           : untinted;
      return materialTint(material, highlight);
    };

    buffer.clear();
    appendTriangles(
        buffer, geometry.floorTriangles,
        tintOf(PreviewSurface::Floor, geometry.floorMaterial));
    materialProgram.setMaterial(
        geometry.floorMaterial.index, geometry.floorMaterial.definition.params);
    materialProgram.draw(buffer);

    buffer.clear();
    appendTriangles(
        buffer, geometry.ceilingTriangles,
        tintOf(PreviewSurface::Ceiling, geometry.ceilingMaterial));
    materialProgram.setMaterial(
        geometry.ceilingMaterial.index,
        geometry.ceilingMaterial.definition.params);
    materialProgram.draw(buffer);

    buffer.clear();
    auto tintedWall = lookedAt.valid && lookedAt.primitiveIndex == index &&
                              lookedAt.surface == PreviewSurface::Wall
                          ? lookedAt.wallIndex
                          : std::numeric_limits<size_t>::max();
    appendWallQuads(
        buffer, geometry.wallQuads, tintedWall,
        materialTint(geometry.wallMaterial),
        materialTint(geometry.wallMaterial, lookedAtTint));
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
    Document* document,
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
  session.document = document;
  session.world = document && document->isActive()
                      ? document->getWorld().get()
                      : nullptr;
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
      bool editable = false;
      if (document && document->isActive()) {
        for (auto const* layer : document->getWorld()->getLayers()) {
          auto ownerIndex = layer->getOwningStepIndex(primitive);
          if (ownerIndex != ~0u) {
            editable =
                layer->getStep(ownerIndex)->permitsDirectPrimitiveEditing();
            break;
          }
        }
      }
      session.primitivesForGrounding.push_back(primitive);
      session.primitives.push_back(
          {primitive->getPriority(),
           extrudePrimitiveForPreview(*primitive, &procMaterialLibrary()),
           const_cast<bw::core::Primitive*>(primitive), editable});
    }
  }
}

void renderPreview3D() {
  if (!session.open) {
    return;
  }
  // Undo, New, and Open replace Document::mWorld wholesale. They can still
  // be reached through an underlying menu while the selected-surface window
  // owns the pointer, so never retain authored Primitive pointers across that
  // replacement.
  if (!session.document || !session.document->isActive() ||
      session.document->getWorld().get() != session.world) {
    session.open = false;
    syncRelativeMouseMode(false);
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
          session.materialEditor = {};
          session.lookedAt = {};
        }
      } else if (
          previewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        refreshPreviewMaterials();
        session.selection = {};
        session.materialEditor = {};
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
