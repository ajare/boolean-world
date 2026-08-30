#define NOMINMAX

#include <algorithm>
#include <array>
#include <cstddef>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4201)
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#pragma warning(pop)

#include <GL/glew.h>
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include <common/GameDefines.h>
#include <core/ArrangementWorldData.h>
#include <core/ArrangementWorldDataGenerator.h>
#include <core/Defines.h>
#include <core/Emboss.h>
#include <core/Layer.h>
#include <core/World.h>

#include "Actions.h"
#include "Defines.h"
#include "Document.h"
#include "EditorRenderSystem.h"
#include "EmbossingCatalogLibrary.h"
#include "imgui.h"
#include "InputOptions.h"
#include "PlayerView.h"
#include "PreviewRenderScene.h"
#include "PreviewSurfacePick.h"
#include "Preview3D.h"
#include "ProcMaterialLibrary.h"
#include "ReactiveCamera.h"
#include "SubMaterialThumbnailRenderer.h"
#include "Undo.h"
#include "WidgetHelpers.h"

extern SDL_Window* gWindow;
extern spdlog::logger* gLogger;

// The screen-space rect of the editor's world viewport - the central region
// the 2D level geometry is edited in, which the preview takes over while it
// is open. Published by renderWidgets() each frame before it calls in here.
extern wp::Vector2 gWorldViewScreenOrigin;
extern wp::Vector2 gWorldViewSize;

namespace editor {
namespace {

struct PreviewPrimitive {
  // The authored source remains owned by the open Document. Procedural step
  // output is previewable but deliberately not editable (ADR-0015).
  bw::core::Primitive* source{};
  bool editable{};
};

// Whichever resolved Arrangement surface the centre of the view is pointing
// at, or which the user has clicked to select. primitiveIndex is the index in
// getTriangles() for horizontal surfaces; wallIndex is the index in getWalls().
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
  bw::core::ChipGenerationParameters chip;
};

struct PreviewEmbossEditorState {
  bool initialized{};
  bool hasDraft{};
  std::string editingId;
  char name[256]{};
  bw::core::EmbossData emboss;
};

struct PreviewSession {
  bool open{};
  Document* document{};
  bw::core::World const* world{};
  wp::Vector2 position;
  float angle{};
  float pitch{};
  float eyeZ{};
  // What the pointer is over this frame, and what the user has selected.
  // The looked-at surface's border is drawn over the finished image as a
  // yellow wireframe, so hovering shows what a click would select.
  PreviewSurfaceRef lookedAt;
  PreviewSurfaceRef selection;
  PreviewMaterialEditorState materialEditor;
  PreviewEmbossEditorState embossEditor;
  // A right-drag that began inside the preview, so releasing the button over
  // another window still ends the turn.
  bool dragTurning{};
  // Mouse motion accumulated from SDL events since the last frame, in place
  // of ImGui's io.MouseDelta - see addPreview3DMouseMotion.
  float mouseMotionX{};
  float mouseMotionY{};
  bw::app::InputOptions inputOptions;
  // Shared rather than unique because mpp::RenderSystem::renderScene takes
  // the camera by mpp::CameraPtr, exactly as StatePlayBooleanWorld does.
  std::shared_ptr<ReactiveCamera> camera;
  // Grounding and Arrangement generation deliberately share this exact
  // in-scope Primitive list. It stays valid for as long as the preview is
  // open: the editor keeps the document's structure frozen meanwhile.
  std::vector<bw::core::Primitive const*> primitivesForGrounding;
  // In the order they are handed to the Arrangement generator, which is how
  // a picked surface names its owner - see sourcePrimitive.
  std::vector<PreviewPrimitive> primitives;
  // Built once, synchronously, from that same scoped list when the preview
  // opens - see openPreview3D. This is what the WorldRenderer draws.
  bw::core::ArrangementWorldDataPtr worldData;
  // Null when the render stack could not be stood up; the preview then shows
  // an explanation rather than a black rectangle.
  std::unique_ptr<PreviewRenderScene> renderScene;
  std::unique_ptr<SubMaterialThumbnailRenderer> materialThumbnails;
  ImVec2 viewportMin;
  ImVec2 viewportMax;
};

PreviewSession session;

// Whichever surface the pointer is over, nearest across every previewed
// Primitive. Invalid when the pointer is outside the viewport or meets
// nothing. Embedded in the world viewport the preview leaves the pointer
// free, so aiming is done with the cursor rather than the centre of the view.
PreviewSurfaceRef surfaceUnderCursor() {
  if (!session.worldData) {
    return {};
  }

  ImVec2 size{
      session.viewportMax.x - session.viewportMin.x,
      session.viewportMax.y - session.viewportMin.y};
  if (size.x <= 0.0f || size.y <= 0.0f) {
    return {};
  }
  auto mouse = ImGui::GetIO().MousePos;
  float ndcX = ((mouse.x - session.viewportMin.x) / size.x) * 2.0f - 1.0f;
  float ndcY = 1.0f - ((mouse.y - session.viewportMin.y) / size.y) * 2.0f;
  if (std::abs(ndcX) > 1.0f || std::abs(ndcY) > 1.0f) {
    return {};
  }

  // The same perspective the camera's projection builds: a direction through
  // the pointer's pixel on the near plane, in the camera's own basis.
  auto position = session.camera->getPosition();
  auto forward = session.camera->getDirection();
  auto up = session.camera->getUp();
  auto right = glm::normalize(glm::cross(forward, up));
  auto tanHalfFov = std::tan(glm::radians(session.camera->getFov() * 0.5f));
  auto direction = glm::normalize(
      forward +
      right * (ndcX * tanHalfFov * session.camera->getAspectRatio()) +
      up * (ndcY * tanHalfFov));

  // Arrangement geometry keeps height in z. Renderer space keeps height in y
  // and maps authored world +Y to renderer -Z.
  std::array<float, 3> origin{position.x, -position.z, position.y};
  std::array<float, 3> ray{direction.x, -direction.z, direction.y};
  auto pick = pickPreviewSceneSurface(*session.worldData, origin, ray);
  if (!pick.hit()) {
    return {};
  }
  return {
      true, pick.primitiveIndex, pick.surfaceHit.surface,
      pick.surfaceHit.wallIndex};
}

void rebuildPreviewWorldData() {
  std::vector<bw::core::Primitive*> primitives;
  primitives.reserve(session.primitivesForGrounding.size());
  for (auto const* primitive : session.primitivesForGrounding) {
    primitives.push_back(const_cast<bw::core::Primitive*>(primitive));
  }
  bw::core::ArrangementWorldDataGenerator generator;
  generator.setChipParametersResolver([](std::string const& subMaterialId) {
    auto const* material = procMaterialLibrary().findSubMaterial(subMaterialId);
    return material ? material->chip
                    : bw::core::ChipGenerationParameters{};
  });
  generator.generate(primitives);
  session.worldData = std::make_shared<bw::core::ArrangementWorldData>(
      generator.getWorldData(), session.world->getExtents(),
      float(BW_WORLD_SIZE / BW_PRIMITIVE_GRID_DIM_MAX),
      session.world->getStepThreshold(), nullptr,
      session.world->getWedgeGenerationParameters());
}

// Defined below, with the render-scene lifecycle it belongs to.
void rebuildPreviewForSurfaceEdit();

void reconcileSavedProcMaterial(std::string const& resourceName) {
  auto* renderSystem = editorRenderSystem();
  if (!session.renderScene || !renderSystem) {
    return;
  }
  // The render ResourceManager caches a separate ProcMaterial view from the
  // authoring library. Reload its YAML and cheaply rebuild only the
  // resolver's hash map; the scene, pipeline, and mesh buckets stay live.
  renderSystem->reloadProcMaterial(resourceName);
  session.renderScene->reloadSubMaterialResolver(
      renderSystem->resourceManager());
  // Chip parameters affect snapshot geometry rather than shader uniforms.
  // Resolve the newly saved values through the same authoring library hook.
  rebuildPreviewWorldData();
  session.renderScene->worldGeometryChanged();
}

void reconcileSavedEmbossingCatalog() {
  auto* renderSystem = editorRenderSystem();
  if (!session.renderScene || !renderSystem) return;
  renderSystem->reloadEmbossingCatalog(
      embossingCatalogLibrary().resourceName());
  session.renderScene->reloadSubMaterialResolver(
      renderSystem->resourceManager());
}

void applyMaterialDraft() {
  auto const& draft = session.materialEditor;
  if (draft.hasDraft && session.renderScene) {
    session.renderScene->updateMaterialDraft(
        draft.editingId, draft.materialIndex, draft.params, draft.colour);
  }
}

void applyEmbossDraft() {
  auto const& draft = session.embossEditor;
  if (draft.hasDraft && session.renderScene) {
    session.renderScene->updateEmbossPresetDraft(
        draft.editingId, draft.emboss);
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

// The selection in the form the shared resolver takes: it is the same record
// pickPreviewSceneSurface produced, minus the distance nothing downstream
// wants.
PreviewScenePick asPick(PreviewSurfaceRef const& surface) {
  PreviewScenePick pick;
  if (!surface.valid) {
    return pick;
  }
  pick.primitiveIndex = surface.primitiveIndex;
  pick.surfaceHit.surface = surface.surface;
  pick.surfaceHit.wallIndex = surface.wallIndex;
  return pick;
}

// The Primitive whose properties the picked surface draws with - the one an
// edit to that surface's material has to reach.
PreviewPrimitive* sourcePrimitive(PreviewSurfaceRef const& surface) {
  if (!session.worldData) {
    return nullptr;
  }
  auto owner = resolvePreviewSurfaceOwner(*session.worldData, asPick(surface));
  // session.primitives holds the same Primitives, in the same order, as the
  // list openPreview3D hands the Arrangement generator, so the owner's place
  // in that list is its place here.
  return owner.valid() && owner.primitiveListIndex < session.primitives.size()
             ? &session.primitives[owner.primitiveListIndex]
             : nullptr;
}

std::string surfaceSubMaterialId(PreviewSurfaceRef const& surface) {
  return session.worldData
             ? previewSurfaceSubMaterialId(*session.worldData, asPick(surface))
             : std::string{};
}

std::string surfaceEmbossPresetId(PreviewSurfaceRef const& surface) {
  return session.worldData
             ? previewSurfaceEmbossPresetId(*session.worldData, asPick(surface))
             : std::string{};
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
  state.chip = material->chip;
}

void loadEmbossDraft(std::string const& id) {
  auto& state = session.embossEditor;
  state.initialized = true;
  state.hasDraft = false;
  state.editingId.clear();
  auto const* preset = embossingCatalogLibrary().findPreset(id);
  if (!preset) return;
  state.hasDraft = true;
  state.editingId = id;
  std::snprintf(
      state.name, sizeof(state.name), "%s", preset->displayName.c_str());
  state.emboss = preset->emboss;
}

void renderEmbossEditor(PreviewPrimitive& previewPrimitive) {
  if (!ImGui::CollapsingHeader(
          "Embossing", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  auto& state = session.embossEditor;
  if (!state.initialized) {
    loadEmbossDraft(surfaceEmbossPresetId(session.selection));
  }

  auto const& presets = embossingCatalogLibrary().data().presets;
  int selected = 0;
  std::string items = "None";
  items += '\0';
  for (size_t i = 0; i < presets.size(); ++i) {
    if (presets[i].id == state.editingId) selected = static_cast<int>(i + 1);
    items += presets[i].displayName;
    items += '\0';
  }
  ImGui::SetNextItemWidth(280.0f);
  if (ImGui::Combo("Emboss preset", &selected, items.c_str(), 8)) {
    auto id = selected == 0 ? std::string{} : presets[selected - 1].id;
    transactUndoableAction(
        session.document, "Set preview surface Emboss preset",
        [&](Document* actionDoc) {
          return setPrimitiveEmbossPreset(
              actionDoc, previewPrimitive.source,
              materialSurface(session.selection.surface), id);
        });
    rebuildPreviewForSurfaceEdit();
    loadEmbossDraft(id);
  }

  if (!state.hasDraft) {
    ImGui::TextDisabled("Select an Emboss preset to edit it.");
    return;
  }

  ImGui::InputText("Preset name", state.name, sizeof(state.name));
  ImGui::SetNextItemWidth(280.0f);
  widgets::EmbossFields(state.emboss);

  if (ImGui::Button("Save existing##Emboss")) {
    auto id = state.editingId;
    auto name = std::string(state.name);
    auto emboss = state.emboss;
    if (transactUndoableActionAtomically(
            session.document, "Save Emboss preset",
            [&](Document* actionDoc) {
              renameEmbossPreset(
                  actionDoc, &embossingCatalogLibrary(), id, name);
              return editEmbossPreset(
                  actionDoc, &embossingCatalogLibrary(), id, emboss);
            })) {
      reconcileSavedEmbossingCatalog();
      loadEmbossDraft(id);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Save as new Emboss preset")) {
    auto name = std::string(state.name);
    auto emboss = state.emboss;
    std::string createdId;
    if (transactUndoableActionAtomically(
            session.document, "Save new Emboss preset",
            [&](Document* actionDoc) {
              if (!createEmbossPreset(
                      actionDoc, &embossingCatalogLibrary(), name, emboss,
                      &createdId)) {
                return false;
              }
              return setPrimitiveEmbossPreset(
                  actionDoc, previewPrimitive.source,
                  materialSurface(session.selection.surface), createdId);
            })) {
      reconcileSavedEmbossingCatalog();
      rebuildPreviewForSurfaceEdit();
      loadEmbossDraft(createdId);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Revert##Emboss")) {
    loadEmbossDraft(state.editingId);
  }
}

// How far a Chip bites into and reaches along this Sub-material - see
// CONTEXT.md's "Chip" entry. Saving rebuilds detail geometry; an unsaved drag
// remains editor state because these values cannot be changed by uniforms.
void renderChipEditor(PreviewMaterialEditorState& state) {
  if (!ImGui::CollapsingHeader("Chipping", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  ImGui::SetNextItemWidth(280.0f);
  widgets::ChipFields(state.chip);
}

void renderPreviewMaterialEditor(PreviewPrimitive& previewPrimitive) {
  auto const& catalogs = procMaterialLibrary().catalogs();
  if (catalogs.empty()) {
    ImGui::TextDisabled("No ProcMaterial resources are available.");
    return;
  }

  auto& state = session.materialEditor;
  if (!state.initialized) {
    loadMaterialDraft(surfaceSubMaterialId(session.selection));
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
    state.hasDraft = false;
    state.editingId.clear();
  }

  auto const& catalog = catalogs[state.catalogIndex];
  if (!session.materialThumbnails) {
    if (auto* renderSystem = editorRenderSystem()) {
      session.materialThumbnails =
          std::make_unique<SubMaterialThumbnailRenderer>(*renderSystem);
    }
  }

  constexpr float thumbnailSize =
      static_cast<float>(SubMaterialThumbnailRenderer::size);
  constexpr float tileWidth = thumbnailSize + 12.0f;
  auto columns = std::max(
      1, static_cast<int>(ImGui::GetContentRegionAvail().x / tileWidth));
  for (size_t i = 0; i < catalog.data.subMaterials.size(); ++i) {
    auto const& material = catalog.data.subMaterials[i];
    ImGui::PushID(material.id.c_str());
    ImGui::BeginGroup();
    auto texture = session.materialThumbnails
                       ? session.materialThumbnails->texture(material.id)
                       : 0u;
    bool const selected = material.id == state.editingId;
    if (selected) {
      ImGui::PushStyleColor(
          ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
      ImGui::PushStyleColor(
          ImGuiCol_ButtonHovered,
          ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
    }
    bool clicked = texture
                       ? ImGui::ImageButton(
                             "thumbnail", static_cast<ImTextureID>(texture),
                             {thumbnailSize, thumbnailSize}, {0.0f, 1.0f},
                             {1.0f, 0.0f})
                       : ImGui::Button(
                             "Unavailable", {thumbnailSize, thumbnailSize});
    if (selected) ImGui::PopStyleColor(2);
    auto textWidth = ImGui::CalcTextSize(material.displayName.c_str()).x;
    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX() + std::max(0.0f, (thumbnailSize - textWidth) * 0.5f));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + thumbnailSize);
    ImGui::TextWrapped("%s", material.displayName.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::PopID();

    if (clicked && !selected) {
      auto const id = material.id;
      transactUndoableAction(
          session.document, "Set preview surface Sub-material",
          [&](Document* actionDoc) {
            return setPrimitiveSubMaterial(
                actionDoc, previewPrimitive.source,
                materialSurface(session.selection.surface), id);
          });
      rebuildPreviewForSurfaceEdit();
      loadMaterialDraft(id);
    }
    if ((static_cast<int>(i) + 1) % columns != 0) ImGui::SameLine();
  }

  if (!state.hasDraft) {
    ImGui::TextDisabled("Select a Sub-material to edit its parameters.");
    renderEmbossEditor(previewPrimitive);
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
  }

  renderEmbossEditor(previewPrimitive);
  renderChipEditor(state);

  if (ImGui::CollapsingHeader("Save", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::Button("Save existing")) {
      auto id = state.editingId;
      auto name = std::string(state.name);
      auto params = state.params;
      auto colour = state.colour;
      auto chip = state.chip;
      if (transactUndoableActionAtomically(
              session.document, "Save Sub-material",
              [&](Document* actionDoc) {
                renameSubMaterial(
                    actionDoc, &procMaterialLibrary(), id, name);
                return editSubMaterial(
                    actionDoc, &procMaterialLibrary(), id, params, colour,
                    chip);
              })) {
        reconcileSavedProcMaterial(catalog.resourceName);
        loadMaterialDraft(id);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save as new Sub-material")) {
      auto name = std::string(state.name);
      auto params = state.params;
      auto colour = state.colour;
      auto chip = state.chip;
      auto materialIndex = state.materialIndex;
      auto resourceName = catalog.resourceName;
      std::string createdId;
      if (transactUndoableActionAtomically(
              session.document, "Save new Sub-material",
              [&](Document* actionDoc) {
                if (!createSubMaterial(
                        actionDoc, &procMaterialLibrary(), resourceName, name,
                        materialIndex, params, colour, chip, &createdId)) {
                  return false;
                }
                return setPrimitiveSubMaterial(
                    actionDoc, previewPrimitive.source,
                    materialSurface(session.selection.surface), createdId);
              })) {
        reconcileSavedProcMaterial(resourceName);
        rebuildPreviewForSurfaceEdit();
        loadMaterialDraft(createdId);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
      loadMaterialDraft(state.editingId);
    }
  }
}

void clearSelectedSurface() {
  session.selection = {};
  session.materialEditor = {};
  session.embossEditor = {};
  session.dragTurning = false;
}

// Single owner of the pointer grab. Enabling flushes pending mouse motion,
// so this only acts on an actual change of state - never once per frame.
void syncRelativeMouseMode(bool enabled) {
  if (!gWindow || SDL_GetWindowRelativeMouseMode(gWindow) == enabled) {
    return;
  }
  SDL_SetWindowRelativeMouseMode(gWindow, enabled);
}

// Right-drag turns the camera, which leaves the left button free to pick
// surfaces and, more importantly, leaves the pointer usable everywhere else
// in the editor - the preview shares the window with the panels around it
// now, so it cannot hold the pointer for as long as it is open. A drag that
// began inside the preview keeps turning until the button is released,
// wherever the pointer ends up.
bool turningThisFrame(bool previewHovered) {
  if (previewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    session.dragTurning = true;
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
    session.dragTurning = false;
  }
  return session.dragTurning;
}

void updateCameraFromInput(bool previewHovered, bool acceptKeyboard) {
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
  session.camera->yaw(session.angle - previousAngle);
  session.camera->pitch(session.pitch - previousPitch);

  wp::Vector2 movement = wp::Vector2::ZERO;
  // Movement keys belong to the preview only while it has the pointer or the
  // focus; otherwise WASD is whatever the rest of the editor makes of it. A
  // material parameter being typed into takes the keyboard with it too -
  // entering a value must not fly the camera across the level.
  // Shift claims the arrows for the selected surface, so holding it must not
  // also fly the camera. WASD is unaffected either way.
  bool const arrowsMoveTheCamera = !io.KeyShift;
  if (acceptKeyboard && !io.WantTextInput) {
    if (ImGui::IsKeyDown(ImGuiKey_W) ||
        (arrowsMoveTheCamera && ImGui::IsKeyDown(ImGuiKey_UpArrow))) {
      movement.y += 1.0f;
    }
    if (ImGui::IsKeyDown(ImGuiKey_S) ||
        (arrowsMoveTheCamera && ImGui::IsKeyDown(ImGuiKey_DownArrow))) {
      movement.y -= 1.0f;
    }
    if (ImGui::IsKeyDown(ImGuiKey_A) ||
        (arrowsMoveTheCamera && ImGui::IsKeyDown(ImGuiKey_LeftArrow))) {
      movement.x -= 1.0f;
    }
    if (ImGui::IsKeyDown(ImGuiKey_D) ||
        (arrowsMoveTheCamera && ImGui::IsKeyDown(ImGuiKey_RightArrow))) {
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
      {session.position.x, session.eyeZ, -session.position.y});
}

// How far Shift+Up/Down moves the selected floor or ceiling, and how far the
// same with Ctrl held moves it. Eight units is the step the 2D panel's
// Floor Z field takes on a fast click, so the two agree; one unit is for
// placing a surface exactly.
constexpr float coarseSurfaceZStep = 8.0f;
constexpr float fineSurfaceZStep = 1.0f;

// Moves the selected floor or ceiling, on the same Primitive an edit to that
// surface's material would reach - the one that owns the polygon it belongs
// to. Walls are left alone: a wall has no height of its own, only the gap
// between the two polygons it stands between, which moves when their floors
// and ceilings do.
void updateSelectedSurfaceFromInput(bool acceptKeyboard) {
  auto const& io = ImGui::GetIO();
  auto const surface = session.selection.surface;
  if (!acceptKeyboard || io.WantTextInput || !io.KeyShift ||
      !session.selection.valid ||
      (surface != PreviewSurface::Floor && surface != PreviewSurface::Ceiling)) {
    return;
  }

  // Repeating, so the surface keeps moving while the key is held.
  auto const step = io.KeyCtrl ? fineSurfaceZStep : coarseSurfaceZStep;
  float delta = 0.0f;
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
    delta += step;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
    delta -= step;
  }
  if (delta == 0.0f) {
    return;
  }

  auto* previewPrimitive = sourcePrimitive(session.selection);
  if (!previewPrimitive || !previewPrimitive->editable ||
      !previewPrimitive->source || !session.document) {
    return;
  }

  auto* primitive = previewPrimitive->source;
  auto const current = primitive->getProperties();
  auto const moved =
      movedSurfaceZ(current, materialSurface(surface), delta);
  // A nudge into the opposing surface is clamped to nothing at all, and an
  // action that changes nothing has no business on the undo stack.
  if (moved.floorZ == current.floorZ && moved.ceilingZ == current.ceilingZ) {
    return;
  }

  transactUndoableAction(
      session.document, "Move preview surface",
      [primitive, &moved](Document* actionDoc) {
        return setPrimitiveProperties(actionDoc, primitive, moved);
      });
  rebuildPreviewForSurfaceEdit();
}

// Arrangement geometry keeps height in z and its ground plane in x/y; the
// renderer's 3D space keeps height in y.
glm::vec3 outlinePoint(
    bw::core::arr::FixedPointVertex const& vertex, float z) {
  return {
      bw::core::arr::ToWorldCoordinate(vertex.x), z,
      bw::core::arr::ToWorldCoordinate(vertex.y)};
}

// Red for the surface the user has selected, yellow for the one the pointer
// is over: two colours nothing the world's own materials render comes close
// to, so a border always reads as one.
glm::vec4 const selectedOutlineColour{1.0f, 0.0f, 0.0f, 1.0f};
glm::vec4 const hoveredOutlineColour{1.0f, 1.0f, 0.0f, 1.0f};

// Whether two picks name the same resolved surface. A hover over the already
// selected surface must not paint over its red border with a yellow one.
bool sameSurface(PreviewSurfaceRef const& left, PreviewSurfaceRef const& right) {
  if (!left.valid || !right.valid || left.surface != right.surface) {
    return false;
  }
  return left.surface == PreviewSurface::Wall
             ? left.wallIndex == right.wallIndex
             : left.primitiveIndex == right.primitiveIndex;
}

// The border of one resolved surface, as endpoint pairs for a line list. A
// wall is the quad the pick itself tested; a floor or ceiling is the
// Arrangement face's own boundary - the outer ring plus every hole - which is
// the clipped polygon the tessellation fills, not the one triangle of it the
// ray happened to meet.
std::vector<glm::vec3> surfaceOutline(PreviewSurfaceRef const& surface) {
  std::vector<glm::vec3> segments;
  if (!surface.valid || !session.worldData) {
    return segments;
  }
  auto const& arrangement = session.worldData->getArrangement();

  auto appendLoop = [&](std::vector<uint32_t> const& loop, float z) {
    for (size_t index = 0; index < loop.size(); ++index) {
      auto const& from = arrangement.vertices[loop[index]];
      auto const& to = arrangement.vertices[loop[(index + 1) % loop.size()]];
      segments.push_back(outlinePoint(from, z));
      segments.push_back(outlinePoint(to, z));
    }
  };

  if (surface.surface == PreviewSurface::Wall) {
    auto const& walls = session.worldData->getWalls();
    if (surface.wallIndex >= walls.size()) {
      return segments;
    }
    auto const& wall = walls[surface.wallIndex];
    auto const& edge = arrangement.edges[wall.edge];
    std::array<glm::vec3, 4> quad{
        outlinePoint(arrangement.vertices[edge.v[0]], wall.minZ),
        outlinePoint(arrangement.vertices[edge.v[1]], wall.minZ),
        outlinePoint(arrangement.vertices[edge.v[1]], wall.maxZ),
        outlinePoint(arrangement.vertices[edge.v[0]], wall.maxZ)};
    for (size_t index = 0; index < quad.size(); ++index) {
      segments.push_back(quad[index]);
      segments.push_back(quad[(index + 1) % quad.size()]);
    }
    return segments;
  }

  auto const& triangles = session.worldData->getTriangles();
  if (surface.primitiveIndex >= triangles.size()) {
    return segments;
  }
  auto const& face = arrangement.faces[triangles[surface.primitiveIndex].face];
  auto const& properties = arrangement.palette[face.paletteIndex];
  auto z = surface.surface == PreviewSurface::Ceiling ? properties.ceilingZ
                                                      : properties.floorZ;
  appendLoop(face.outerBoundaryVertices, z);
  for (auto const& hole : face.innerBoundaryVertices) {
    appendLoop(hole, z);
  }
  return segments;
}

// A size in framebuffer pixels, which is what the render graph's images are
// sized against - the ImGui coordinates the window is laid out in are not the
// same thing on a scaled display.
struct PreviewPixelSize {
  size_t width;
  size_t height;
};

// The editor's world viewport - the central region where the 2D level
// geometry is otherwise edited - which the preview takes over while it is
// open. Both the open and render paths size the pipeline from this, so they
// cannot disagree.
ImVec2 previewViewportPos() {
  return {gWorldViewScreenOrigin.x, gWorldViewScreenOrigin.y};
}

ImVec2 previewViewportSize() {
  return {std::max(gWorldViewSize.x, 1.0f), std::max(gWorldViewSize.y, 1.0f)};
}

PreviewPixelSize previewPixelSize(ImVec2 const& windowSize) {
  auto scale = ImGui::GetIO().DisplayFramebufferScale;
  return {
      static_cast<size_t>(std::max(1.0f, windowSize.x * scale.x)),
      static_cast<size_t>(std::max(1.0f, windowSize.y * scale.y))};
}

// Stands the whole render stack up for one open of the preview. Failure is
// reported and left non-fatal: the editor keeps running, and the preview
// window says why it is empty.
void createPreviewScene(ImVec2 const& windowSize) {
  if (!session.world || !session.worldData) {
    return;
  }

  // Built once while the editor started up, so nothing here pays for it.
  auto* renderSystem = editorRenderSystem();
  if (!renderSystem) {
    if (gLogger) {
      gLogger->error(
          "3D preview cannot be rendered: the editor render system was not created.");
    }
    return;
  }

  try {
    auto size = previewPixelSize(windowSize);
    session.renderScene = std::make_unique<PreviewRenderScene>(
        *renderSystem, session.document->getWorld().get(), size.width,
        size.height);
  } catch (std::exception const& exception) {
    session.renderScene.reset();
    if (gLogger) {
      gLogger->error(
          std::string("3D preview render stack could not be created: ") +
          exception.what());
    }
  }
}

// Draws the world into the pipeline's offscreen images and hands ImGui the
// resolved texture. Nothing here touches the backbuffer, so the preview is no
// longer scissored into the window ImGui is itself drawing into.
void renderPreviewScene(ImVec2 const& windowSize) {
  auto* preview = session.renderScene.get();
  if (!preview || !session.worldData) {
    ImGui::SetCursorPos({12.0f, 36.0f});
    ImGui::TextUnformatted(
        "The 3D preview could not be rendered - see the editor log.");
    return;
  }

  auto const& io = ImGui::GetIO();
  auto size = previewPixelSize(windowSize);
  preview->resize(size.width, size.height);

  // This is a uniform-only push on every preview frame, matching the game
  // renderer's update cadence and keeping slider drags free of re-tessellation.
  applyMaterialDraft();
  applyEmbossDraft();
  // The selection keeps its border for as long as it is selected; the hover
  // border is drawn after it, and so over it, when they are different
  // surfaces.
  std::vector<PreviewOutline> outlines;
  if (session.selection.valid) {
    outlines.push_back(
        {surfaceOutline(session.selection), selectedOutlineColour});
  }
  if (session.lookedAt.valid &&
      !sameSurface(session.lookedAt, session.selection)) {
    outlines.push_back({surfaceOutline(session.lookedAt), hoveredOutlineColour});
  }

  auto textureId = preview->render(
      session.document->getWorld().get(), *session.worldData, session.camera,
      session.camera->getPosition(), io.DeltaTime, outlines);
  if (textureId == 0) {
    return;
  }

  // The texture's origin is bottom-left, ImGui's is top-left, so V is flipped.
  ImGui::SetCursorPos({0.0f, 0.0f});
  ImGui::Image(
      static_cast<ImTextureID>(textureId), windowSize, {0.0f, 1.0f},
      {1.0f, 0.0f});
}

// Editing the selected surface - assigning it a Sub-material, or moving its
// floor or ceiling - changes more than a uniform, so nothing the preview
// built beforehand can be reused:
//
//   - which mesh bucket a triangle or wall lands in is decided by the
//     Sub-material id in the Arrangement palette, and this session's
//     Arrangement is a snapshot taken when the preview opened; and
//   - the buckets themselves are baked in WorldBatch::createModelStream from
//     the material ids every Primitive carried when the render scene was
//     built, so a Sub-material that no Primitive was using then has no bucket
//     at all - and getMeshIndexForMaterialHash answers a missing bucket with
//     zero, which is a real, and wrong, mesh.
//
// A moved floor or ceiling is plainer still: the Arrangement's own geometry,
// the walls it derives from the step between two polygons, and the triangles
// the renderer draws all come out of that snapshot.
//
// So rebuild both, exactly as opening the preview does. This runs on an
// explicit pick or nudge, never on a slider drag: parameter and colour edits
// still go through the uniform-only draft path.
void rebuildPreviewForSurfaceEdit() {
  if (!session.world || !session.document || !session.document->isActive()) {
    return;
  }
  rebuildPreviewWorldData();
  session.renderScene.reset();
  createPreviewScene(previewViewportSize());
}

}  // namespace

bool preview3DIsOpen() {
  return session.open;
}

void shutdownPreview3D() {
  session.open = false;
  session.renderScene.reset();
  session.materialThumbnails.reset();
}

void closePreview3D() {
  if (!session.open) {
    return;
  }
  session.open = false;
  session.renderScene.reset();
  session.materialThumbnails.reset();
  syncRelativeMouseMode(false);
}

void renderPreview3DSelectedSurface() {
  if (!session.open) {
    return;
  }

  if (!ImGui::CollapsingHeader(
          "Selected surface", ImGuiTreeNodeFlags_DefaultOpen)) {
    return;
  }

  if (!session.selection.valid) {
    ImGui::TextDisabled("Click a surface in the preview to select it.");
    return;
  }

  auto surfaceName = previewSurfaceName(session.selection.surface);
  std::string label(surfaceName);
  if (!label.empty()) {
    label.front() = static_cast<char>(std::toupper(label.front()));
  }
  ImGui::Text("Selected %s", label.c_str());
  ImGui::SameLine();
  // What the window's close button used to do, and what clicking nothing in
  // the preview still does.
  if (ImGui::SmallButton("Deselect")) {
    clearSelectedSurface();
    return;
  }

  auto* previewPrimitive = sourcePrimitive(session.selection);
  if (!previewPrimitive || !previewPrimitive->editable ||
      !previewPrimitive->source || !session.document) {
    ImGui::TextDisabled(
        "This surface is generated by a step that does not permit direct editing.");
    return;
  }
  renderPreviewMaterialEditor(*previewPrimitive);
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
  session.camera = std::make_shared<ReactiveCamera>(
      glm::vec3{playerPosition.x, session.eyeZ, -playerPosition.y},
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
          {const_cast<bw::core::Primitive*>(primitive), editable});
    }
  }

  if (session.world) {
    rebuildPreviewWorldData();
  }

  // Per preview-open, unlike the process-lifetime EditorRenderSystem it is
  // built on: the Scene, pipeline and WorldRenderer all belong to this one
  // session and go away with it.
  createPreviewScene(previewViewportSize());
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
    session.renderScene.reset();
    session.materialThumbnails.reset();
    syncRelativeMouseMode(false);
    return;
  }

  auto windowSize = previewViewportSize();
  ImGui::SetNextWindowPos(previewViewportPos());
  ImGui::SetNextWindowSize(windowSize);

  // Chromeless and pinned to the world viewport's rect, exactly as the World
  // window it stands in for is - but taking input, because this one is
  // steered with the mouse and keyboard.
  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoSavedSettings;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  bool visible = ImGui::Begin("##3D preview", nullptr, flags);
  if (visible) {
    session.viewportMin = ImGui::GetWindowPos();
    session.viewportMax = {
        session.viewportMin.x + ImGui::GetWindowSize().x,
        session.viewportMin.y + ImGui::GetWindowSize().y};
    // False when the selected-surface window sits over the pointer, which is
    // what keeps dragging that window from turning the camera underneath it.
    bool previewHovered = ImGui::IsWindowHovered();
    bool previewFocused = ImGui::IsWindowFocused();

    session.camera->setAspectRatio(
        std::max(ImGui::GetWindowSize().x / ImGui::GetWindowSize().y, 0.01f));
    // Escape belongs to the preview only while the pointer or the focus is
    // in it: the rest of the editor is live around it now, so this must not
    // be routed globally.
    bool closing = (previewHovered || previewFocused) &&
                   ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (!closing) {
      if (session.dragTurning) {
        // Relative mode already hides the pointer; this stops the ImGui SDL3
        // backend from calling SDL_ShowCursor() behind its back every frame.
        // ImGui resets the cursor to the arrow each NewFrame, so it comes
        // back on its own once the drag ends.
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
      }
      auto const acceptKeyboard = previewHovered || previewFocused;
      updateCameraFromInput(previewHovered, acceptKeyboard);
      // After the camera, so a nudge that rebuilds the Arrangement leaves the
      // eye height regrounded against the surface it just moved.
      updateSelectedSurfaceFromInput(acceptKeyboard);

      // Hovering outlines whichever surface a click would select - including
      // while one is already selected, so the next pick is just as visible
      // as the first.
      session.lookedAt =
          previewHovered && !session.dragTurning ? surfaceUnderCursor()
                                                 : PreviewSurfaceRef{};

      // Clicking empty space is how a selection is dropped; the
      // selected-surface window's close button does the same thing.
      if (previewHovered && !session.dragTurning &&
          ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        session.selection = session.lookedAt;
        session.materialEditor = {};
        session.embossEditor = {};
      }
    }

    auto* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        session.viewportMin, session.viewportMax, IM_COL32(0, 0, 0, 255));

    // Skipped on the closing frame: the pipeline's images are released below,
    // before ImGui gets to render anything referring to them.
    if (!closing) {
      renderPreviewScene(ImGui::GetWindowSize());
    }

    ImGui::SetCursorPos({12.0f, 12.0f});
    ImGui::TextUnformatted(
        "WASD/Arrows to move, right-drag to look, click a surface to select, "
        "shift+up/down to raise a floor or ceiling (+ctrl for one unit), "
        "ESC to exit preview");

    if (closing) {
      session.open = false;
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);

  // Every GPU resource this open of the preview built goes away with it. The
  // process-lifetime EditorRenderSystem underneath is deliberately kept.
  if (!session.open) {
    session.renderScene.reset();
    session.materialThumbnails.reset();
  }

  // Relative mode keeps reporting motion past the window edge, so looking
  // around is never bounded by the viewport's edges. It lasts exactly as long
  // as the drag that turns the camera: everything else in the editor needs
  // the pointer back the instant the button comes up. Requiring `visible`
  // too means a window ImGui declined to draw releases the pointer instead
  // of holding it hostage.
  syncRelativeMouseMode(session.open && visible && session.dragTurning);
}

}  // namespace editor
