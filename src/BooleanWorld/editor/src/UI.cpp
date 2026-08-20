#define NOMINMAX

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include <nfd/nfd.h>

#include <core/LayerBuildStep.h>
#include <core/WorldData.h>
#include <core/RegularPolygon.h>
#include <core/CirclePolygon.h>
#include <core/CircleSegmentPolygon.h>
#include <core/TorusPolygon.h>
#include <core/TorusSegmentPolygon.h>
#include <core/RectanglePolygon.h>
#include <core/SuperformulaPolygon.h>
#include <core/MeshPrimitive.h>
#include <core/Defines.h>
#include <core/DynamicWorldDataGenerator.h>

#include <common/MaterialRegistry.h>

#define IMGUI_DEFINE_MATH_OPERATORS

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_curve.hpp"
#include "implot.h"
#include "IconsFontAwesome5.h"

#include "UI.h"
#include "UiHelpers.h"
#include "WidgetHelpers.h"
#include "AppHelpers.h"
#include "Defines.h"
#include "Document.h"
#include "Undo.h"
#include "Actions.h"
#include "Markdown.h"
#include "PrimitiveFieldPreview.h"
#include "PrimitiveFieldPlacement.h"
#include "ExitApplicationException.h"
#include "Render.h"
#include "HoverableType.h"

extern std::map<std::string, std::string> gHelpFiles;
extern std::map<std::string, bw::core::World*> gPrefabInstances;
extern wp::Vector2 gViewOffset;
extern editor::HoverableType gHoveredType;
extern std::vector<uint32_t> gHoveredIndices;

namespace editor {
using namespace std;

enum struct ActionType {
  None,
  Generic,
  Document
};

void resetAnimatorCaptures(editor::Document* doc) {
  auto world = doc->getWorld();

  uint32_t numPrimitives = world->getNumPrimitives();
  for (uint32_t i = 0; i < numPrimitives; ++i) {
    auto primitive = world->getPrimitive(i);
    primitive->resetAnimatorCaptures();
  }
}

void renderMenu(editor::Document* doc, editor::Settings& settings) {
  ActionType action{ActionType::None};
  bool checkDocumentModified = false;
  DocumentHelperFunction helperFunc;
  static string actionText;

  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New", "Ctrl+N")) {
        action = ActionType::Document;
        checkDocumentModified = true;
        helperFunc = newDocument;
        actionText = "New world";
      }

      if (ImGui::MenuItem("Open", "Ctrl+O")) {
        action = ActionType::Document;
        checkDocumentModified = true;
        helperFunc = openDocument;
        actionText = "Open world";
      }

      auto world = doc->getWorld();
      bool saveDisabled = !world || !doc->isModified();
      bool saveAsDisabled = !world;

      if (saveDisabled) {
        widgets::PushDisabled();
      }

      if (ImGui::MenuItem("Save", "Ctrl+S")) {
        action = ActionType::Document;
        checkDocumentModified = false;
        helperFunc = saveDocument;
        actionText = "Save world";
      }

      if (saveDisabled) {
        widgets::PopDisabled();
      }

      if (saveAsDisabled) {
        widgets::PushDisabled();
      }

      if (ImGui::MenuItem("Save as...")) {
        action = ActionType::Document;
        checkDocumentModified = false;
        helperFunc = saveDocumentAs;
        actionText = "Save world";
      }

      if (saveAsDisabled) {
        widgets::PopDisabled();
      }

      ImGui::Separator();
      if (ImGui::MenuItem("Exit")) {
        action = ActionType::Document;
        checkDocumentModified = true;
        helperFunc = exitApp;
        actionText = "Exit application";
      }

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
      auto world = doc->getWorld();

      bool canUndoAction = canUndo();
      bool canRedoAction = canRedo();
      bool hasPrimitiveSelection = !doc->getSelectedPrimitiveIndices().empty();
      bool hasTriggerLineSelection = doc->getSelectedTriggerLineIndex() != ~0u;
      bool resetDisabled = !world;

      if (!canUndoAction) {
        widgets::PushDisabled();
      }

      if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
        undo(doc);
      }

      if (!canUndoAction) {
        widgets::PopDisabled();
      }

      if (!canRedoAction) {
        widgets::PushDisabled();
      }

      if (ImGui::MenuItem("Redo", "Ctrl+Y")) {
        redo(doc);
      }

      if (!canRedoAction) {
        widgets::PopDisabled();
      }

      if (!hasPrimitiveSelection) {
        widgets::PushDisabled();
      }

      if (ImGui::MenuItem("Clone", "Ctrl+C")) {
        auto const& indices = doc->getSelectedPrimitiveIndices();
        uint32_t index = *indices.begin();

        transactUndoableAction(doc, format("Clone Primitive {}", index), bind(clonePrimitive, placeholders::_1, index));
      }

      if (!hasPrimitiveSelection) {
        widgets::PopDisabled();
      }

      if (!hasPrimitiveSelection && !hasTriggerLineSelection) {
        widgets::PushDisabled();
      }

      if (ImGui::MenuItem("Delete", "Del")) {
        auto const& primitiveIndices = doc->getSelectedPrimitiveIndices();

        if (!primitiveIndices.empty()) {
          transactUndoableAction(doc, format("Delete {} Primitive(s)", primitiveIndices.size()), bind(deletePrimitives, placeholders::_1, primitiveIndices));
          generateClipping(doc, settings, ED_CLIP_ON_PRIM_CREATE_DELETE);
        }

        auto triggerLineIndex = doc->getSelectedTriggerLineIndex();

        if (triggerLineIndex != ~0u) {
          transactUndoableAction(doc, "Delete TriggerLine", bind(deleteTriggerLine, placeholders::_1, triggerLineIndex));
        }
      }

      if (!hasPrimitiveSelection && !hasTriggerLineSelection) {
        widgets::PopDisabled();
      }

      if (!hasPrimitiveSelection) {
        widgets::PushDisabled();
      }

      if (ImGui::MenuItem("Bake to mesh", "Ctrl+B")) {
        auto const& indices = doc->getSelectedPrimitiveIndices();
        transactUndoableAction(doc, format("Bake {} Primitive(s)", indices.size()), bind(bakePrimitives, placeholders::_1, indices));
      }

      if (ImGui::MenuItem("Clip to grid")) {
        auto const& indices = doc->getSelectedPrimitiveIndices();
        transactUndoableAction(doc, format("Clip Primitive(s) to grid", indices.size()), bind(clipPrimitivesToGrid, placeholders::_1, indices, settings.gridSize));
      }

      if (!hasPrimitiveSelection) {
        widgets::PopDisabled();
      }

      if (!hasPrimitiveSelection && !hasTriggerLineSelection) {
        widgets::PushDisabled();
      }

      if (ImGui::MenuItem("Deselect all")) {
        transactUndoableAction(doc, "Clear Selections", clearSelections);
      }

      if (!hasPrimitiveSelection && !hasTriggerLineSelection) {
        widgets::PopDisabled();
      }

      if (world) {
        ImGui::Separator();
        if (ImGui::MenuItem("Generate Primitive Field\u2026")) {
          getPrimitiveFieldPreview().requestOpen();
        }

        if (ImGui::MenuItem("New Layer")) {
          auto layerName = format("Layer {}", world->getNumLayers());
          transactUndoableAction(doc, "New Layer", bind(addLayer, placeholders::_1, layerName));
        }
      }

      if (resetDisabled) {
        widgets::PushDisabled();
      }

      if (ImGui::MenuItem("Select & home on Ghost", "Shift+G")) {
        selectAndHomeGhost(doc);
      }

      if (ImGui::MenuItem("Reset captures", "R")) {
        resetAnimatorCaptures(doc);
      }

      if (ImGui::MenuItem("Use ghost primitive", "Ctrl+G", &settings.ghostActive)) {
        enableGhost(doc, settings.ghostActive);
      }

      if (resetDisabled) {
        widgets::PopDisabled();
      }

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
      if (ImGui::MenuItem("Reset view position", "H")) {
        goHome(nullptr);
      }

      ImGui::MenuItem("Minimap", "M", &settings.renderMiniMap);
      ImGui::MenuItem("Transform debug view", "F7", &settings.showDebugPanel);
      ImGui::MenuItem("Context help view", "F10", &settings.showContextSensitiveHelpPanel);

      ImGui::MenuItem("Grid", "G", &settings.showGrid);

      ImGui::MenuItem("Animated primitives", 0, &settings.renderAnimatedPrimitives);
      ImGui::MenuItem("Triangulation border", 0, &settings.renderWorldBorder);
      ImGui::MenuItem("Triangulation", 0, &settings.renderTriangulation);
      ImGui::MenuItem("Player view", "F3", &settings.renderPlayerView);
      ImGui::MenuItem("Primitive borders", "F4", &settings.renderPrimitiveBorders);
      ImGui::MenuItem("Primitive bounds", "F5", &settings.renderPrimitiveBounds);
      ImGui::MenuItem("Influence eyes", "F6", &settings.renderInfluenceEyes);
      ImGui::MenuItem("Trigger lines", "F7", &settings.renderTriggerLines);
      ImGui::MenuItem("Arrangement vertices", "F8", &settings.renderArrangementVertices);
      ImGui::MenuItem("Scale influence zones", 0, &settings.renderScaleInfluenceZones);
      ImGui::MenuItem("Angle influence zones", 0, &settings.renderAngleInfluenceZones);
      ImGui::MenuItem("Orbit angle influence zones", 0, &settings.renderOrbitAngleInfluenceZones);
      ImGui::MenuItem("Orbit distance influence zones", 0, &settings.renderOrbitDistanceInfluenceZones);
      ImGui::MenuItem("Time update distance", 0, &settings.renderTimeUpdateDistance);
      ImGui::MenuItem("Expert Mode", "F11", &settings.expertMode);

      if (ImGui::BeginMenu("Style")) {
        bool selected = settings.style == Settings::Style::Light;

        if (ImGui::MenuItem("Light", 0, &selected)) {
          if (selected) {
            settings.style = Settings::Style::Light;
            ImGui::StyleColorsLight();
          }
        }

        selected = settings.style == Settings::Style::Dark;

        if (ImGui::MenuItem("Dark", 0, &selected)) {
          if (selected) {
            settings.style = Settings::Style::Dark;
            ImGui::StyleColorsDark();
          }
        }

        selected = settings.style == Settings::Style::Classic;

        if (ImGui::MenuItem("Classic", 0, &selected)) {
          if (selected) {
            settings.style = Settings::Style::Classic;
            ImGui::StyleColorsClassic();
          }
        }

        ImGui::EndMenu();
      }

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("Show help", "F1")) {
        action = ActionType::Generic;
        checkDocumentModified = false;
        helperFunc = showHelp;
        actionText = "Help";
      }

      ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
  }

  // Handle action
  switch (action) {
    case ActionType::Generic:
      handleNonDocumentAction(actionText);
      checkNonDocumentOperation();
      break;

    case ActionType::Document:
      handleModifiedDocument(doc, true, checkDocumentModified, actionText, helperFunc);
      checkModifiedOperation(doc, actionText, helperFunc);
      break;

    case ActionType::None:
    default:
      break;
  }
}

void renderToolbar(Document* doc, editor::Settings& settings) {
  ImGuiViewportP* viewport = (ImGuiViewportP*)(void*)ImGui::GetMainViewport();

  auto windowFlags =
      ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_NoSavedSettings;

  bool docAction = false;
  bool checkDocumentModified = false;
  DocumentHelperFunction helperFunc;
  static string docText;

  if (ImGui::BeginViewportSideBar("Toolbar", viewport, ImGuiDir_Up, 35, windowFlags)) {
    auto world = doc->getWorld();

    bool saveDisabled = !world || !doc->isModified();
    bool saveAsDisabled = !world;
    bool canUndoAction = canUndo();
    bool canRedoAction = canRedo();
    bool hasPrimitiveSelection = !doc->getSelectedPrimitiveIndices().empty();
    bool hasTriggerLineSelection = doc->getSelectedTriggerLineIndex() != ~0u;

    //
    // File operations
    //
    if (ImGui::Button(ICON_FA_FILE)) {
      docAction = true;
      checkDocumentModified = true;
      helperFunc = newDocument;
      docText = "New world";
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_FA_FOLDER_OPEN)) {
      docAction = true;
      checkDocumentModified = true;
      helperFunc = openDocument;
      docText = "Open world";
    }

    ImGui::SameLine();

    if (saveDisabled) {
      widgets::PushDisabled();
    }

    if (ImGui::Button(ICON_FA_SAVE)) {
      docAction = true;
      checkDocumentModified = false;
      helperFunc = saveDocument;
      docText = "Save world";
    }

    if (saveDisabled) {
      widgets::PopDisabled();
    }

    //
    // Primitive operations
    //
    if (!hasPrimitiveSelection) {
      widgets::PushDisabled();
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_FA_CLONE)) {
      auto const& indices = doc->getSelectedPrimitiveIndices();
      uint32_t index = *indices.begin();

      transactUndoableAction(doc, format("Clone Primitive {}", index), bind(clonePrimitive, placeholders::_1, index));
    }

    if (!hasPrimitiveSelection) {
      widgets::PopDisabled();
    }

    ImGui::SameLine();

    if (!hasPrimitiveSelection && !hasTriggerLineSelection) {
      widgets::PushDisabled();
    }

    if (ImGui::Button(ICON_FA_ERASER)) {
      auto const& primitiveIndices = doc->getSelectedPrimitiveIndices();

      if (!primitiveIndices.empty()) {
        transactUndoableAction(doc, format("Delete {} Primitive(s)", primitiveIndices.size()), bind(deletePrimitives, placeholders::_1, primitiveIndices));
        generateClipping(doc, settings, ED_CLIP_ON_PRIM_CREATE_DELETE);
      }

      auto triggerLineIndex = doc->getSelectedTriggerLineIndex();

      if (triggerLineIndex != ~0u) {
        transactUndoableAction(doc, "Delete TriggerLine", bind(deleteTriggerLine, placeholders::_1, triggerLineIndex));
      }
    }

    if (!hasPrimitiveSelection && !hasTriggerLineSelection) {
      widgets::PopDisabled();
    }

    //
    // Undo/redo
    //
    ImGui::SameLine();

    if (!canUndoAction) {
      widgets::PushDisabled();
    }

    if (ImGui::Button(ICON_FA_UNDO)) {
      undo(doc);
    }

    if (!canUndoAction) {
      widgets::PopDisabled();
    }

    if (!canRedoAction) {
      widgets::PushDisabled();
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_FA_REDO)) {
      redo(doc);
    }

    if (!canRedoAction) {
      widgets::PopDisabled();
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_FA_HOME)) {
      goHome(nullptr);
    }

    ImGui::SameLine();

    if (widgets::ToggleButton("ToggleGhost", ICON_FA_GHOST, &settings.ghostActive)) {
      enableGhost(doc, settings.ghostActive);
    }

    ImGui::SameLine();

    widgets::ToggleButton("ToggleExpert", "Expert", &settings.expertMode);

    //
    // Visual
    //
    ImGui::SameLine();

    if (!world) {
      widgets::PushDisabled();
    }

    widgets::ToggleButton("ToggleAnimatedPrimitives", "Anims", &settings.renderAnimatedPrimitives);

    ImGui::SameLine();
    widgets::ToggleButton("TogglePrimitiveBorders", "Prims", &settings.renderPrimitiveBorders);

    ImGui::SameLine();
    widgets::ToggleButton("TogglePrimitiveBounds", "Bounds", &settings.renderPrimitiveBounds);

    ImGui::SameLine();
    widgets::ToggleButton("ToggleWorldBorder", "Border", &settings.renderWorldBorder);

    ImGui::SameLine();
    widgets::ToggleButton("ToggleScaleInfluenceZones", "[SZ]", &settings.renderScaleInfluenceZones);

    ImGui::SameLine();
    widgets::ToggleButton("ToggleAngleInfluenceZones", "[AZ]", &settings.renderAngleInfluenceZones);

    ImGui::SameLine();
    widgets::ToggleButton("ToggleOrbitAngleInfluenceZones", "[OAZ]", &settings.renderOrbitAngleInfluenceZones);

    ImGui::SameLine();
    widgets::ToggleButton("ToggleOrbitDistanceInfluenceZones", "[ODZ]", &settings.renderOrbitDistanceInfluenceZones);

    ImGui::SameLine();
    widgets::ToggleButton("ToggleTimeUpdateDistance", "[TUD]", &settings.renderTimeUpdateDistance);

    ImGui::SameLine();
    widgets::ToggleButton("TogglePrefabTiles", "[PT]", &settings.renderPrefabTiles);

    if (!world) {
      widgets::PopDisabled();
    }

    // Grid
    ImGui::SameLine();

    widgets::ToggleButton("ToggleGrid", "Grid", &settings.showGrid);

    ImGui::SameLine();

    ImGui::SetNextItemWidth(80);

    static int gridSize = (int)(log((float)settings.gridSize) / log(2.0f)) - 3;
    string gridSizeText = format("{}", (int)settings.gridSize);
    if (ImGui::SliderInt("Size##GridSize", &gridSize, 0, 3, gridSizeText.c_str())) {
      settings.gridSize = (float)(1 << (gridSize + 3));
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(64);

    // Active layer
    int activeLayer = (int)settings.activeLayer;
    if (ImGui::InputInt("Lyr", &activeLayer, 1, 1)) {
      if (activeLayer >= 0 && activeLayer < 256) {
        settings.activeLayer = (uint8_t)activeLayer;

        // Set WDG layer
        auto dataGenerator = world->getWorldDataGenerator();
        dataGenerator->setActiveLayer(settings.activeLayer);

        generateClipping(doc, settings, ED_CLIP_ON_ACTIVE_LAYER_CHANGE);
      }
    }

    // Handle action
    handleModifiedDocument(doc, docAction, checkDocumentModified, docText, helperFunc);
    checkModifiedOperation(doc, docText, helperFunc);

    ImGui::End();
  }
}

void renderStatusbar(editor::Document* doc, editor::Settings& settings, bw::core::WorldData const* worldData) {
  ImGuiViewportP* viewport = (ImGuiViewportP*)(void*)ImGui::GetMainViewport();

  auto windowFlags =
      ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_MenuBar;

  float height = ImGui::GetFrameHeight();

  if (ImGui::BeginViewportSideBar("##MainStatusBar", viewport, ImGuiDir_Down, height, windowFlags)) {
    if (ImGui::BeginMenuBar()) {
      if (doc->isActive()) {
        auto world = doc->getWorld();

        // Number of primitives
        auto numPrimitives = world->getNumPrimitives();
        string numPrimsText = format("{} total primitive(s)", numPrimitives);

        ImVec4 c = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImGui::TextColored(c, numPrimsText.c_str());

        ImGui::SameLine();

        auto const& arrangement = worldData->getArrangement();
        string processData = format(
            "| {} vertices & {} face(s)",
            arrangement.vertices.size(),
            arrangement.faces.size());

        ImGui::TextColored(c, processData.c_str());

        // Mouse
        auto mousePos = getMouseWorldPosition();
        string mouseData = format("| {:.1f}, {:.1f}", mousePos.x, mousePos.y);

        ImGui::SameLine();
        ImGui::TextColored(c, mouseData.c_str());

        // Hovered objects
        switch (gHoveredType) {
          case editor::HoverableType::Primitive:
            ImGui::SameLine();
            ImGui::TextColored(c, "Hovered: %d Prims", gHoveredIndices.size());
            break;

          case editor::HoverableType::TriggerLine:
            ImGui::SameLine();
            ImGui::TextColored(c, "Hovered TriggerLine: %d", gHoveredIndices[0]);
            break;

          case editor::HoverableType::WorldVertex:
            ImGui::SameLine();
            ImGui::TextColored(c, "Hovered Vertex: %d", gHoveredIndices[0]);
            break;

          case editor::HoverableType::None:
            ImGui::SameLine();
            ImGui::TextColored(c, "Hovered <nothing>");
            break;

          default:
            break;
        }
      }

      ImGui::EndMenuBar();
    }
    ImGui::End();
  }
}

void renderWorldView(editor::Document* doc, editor::Settings& settings) {
  auto world = doc->getWorld();

  // Name
  string worldName = world->getName();

  ImGui::SetNextItemWidth(192);
  char buffer[256];
  strcpy_s(buffer, 256, worldName.c_str());
  if (ImGui::InputText("Name##World", buffer, 256, ImGuiInputTextFlags_EnterReturnsTrue)) {
    transactUndoableAction(doc, "Set World name",
                           bind(setWorldName, placeholders::_1, string(buffer)));
  }

  // Description
  string worldDesc = world->getDescription();

  char buffer2[2048];
  strcpy_s(buffer2, 2048, worldDesc.c_str());
  if (ImGui::InputTextMultiline("Description##World", buffer2, 2048, ImVec2(512, 96))) {
    // Don't make this transactional as every character change will create an undo state
    // transactUndoableAction(doc, "Set World description",
    //	bind(setWorldDescription, placeholders::_1, string(buffer2)));
    setWorldDescription(doc, string(buffer2));
    doc->setModified(true);
  }

  // Player start angle
  float playerStartAngle = wp::MathsUtils::radians(world->getPlayerStartAngle());

  widgets::HelpMarker("Set the starting angle of the player.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderAngle("PlayerStartAngle##World", &playerStartAngle, 0, 360)) {
    transactUndoableAction(doc, "Set Player start angle",
                           bind(setPlayerStartAngle, placeholders::_1, wp::MathsUtils::degrees(playerStartAngle)));
  }

  // Player start position
  wp::Vector2 const& playerStartPos = world->getPlayerStartPosition();

  widgets::HelpMarker("Set the starting position of the player.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  float pPosition[2] = {playerStartPos.x, playerStartPos.y};

  if (ImGui::InputFloat2("PlayerStartPos##World", pPosition)) {
    wp::Vector2 position{pPosition[0], pPosition[1]};

    transactUndoableAction(doc, "Set Primitive Position",
                           bind(setPlayerStartPosition, placeholders::_1, position));
  }

  // Layers
  ImGui::Separator();
  widgets::HelpMarker("Selecting a Layer here also makes it the active Layer, so Create/Edit Primitive writes into it.");
  ImGui::SameLine();
  ImGui::Text("Layers");

  auto* activeLayer = world->getActiveLayer();
  for (auto* layer : world->getLayers()) {
    ImGui::PushID((int)layer->getId());

    if (ImGui::Selectable(layer->getName().c_str(), layer == activeLayer)) {
      world->setActiveLayer(layer);
    }

    ImGui::PopID();
  }
}

tuple<string, CreatePrimitiveFunction, bool> renderCreateRegularPolygon(editor::Document* doc, bw::core::Primitive::Operation op, bw::core::Primitive::FillRule fillRule, uint8_t priority, wp::Vector2 const& position, float scale, float angle) {
  static int numSides = 3;
  bool modified{false};

  widgets::HelpMarker("Set the number of sides - minimum 3, maximum 8.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputInt("Sides##CreateRegularPolygon", &numSides, 1, 1)) {
    numSides = clamp(numSides, ED_MIN_REGULAR_POLYGON_SIDES, ED_MAX_REGULAR_POLYGON_SIDES);
    modified = true;
  }

  return {
      format("Create Regular {}-Gon Primitive", numSides),
      bind(createRegularPolygonPrimitive, op, fillRule, (uint32_t)numSides, priority, position, scale, angle),
      modified};
}

tuple<string, CreatePrimitiveFunction, bool> renderCreateCirclePolygon(editor::Document* doc, bw::core::Primitive::Operation op, bw::core::Primitive::FillRule fillRule, uint8_t priority, wp::Vector2 const& position, float scale, float angle) {
  static float resolution = 0.5f;
  bool modified{false};

  ImGui::SetNextItemWidth(128);

  widgets::HelpMarker("This value determines the number of sides in the circle polygon.");
  ImGui::SameLine();
  if (ImGui::InputFloat("Res##CreateCircle", &resolution, 0.01f, 0.1f)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);
    modified = true;
  }

  return {
      "Create Circle Primitive",
      bind(createCirclePrimitive, op, fillRule, resolution, priority, position, scale, angle),
      modified};
}

tuple<string, CreatePrimitiveFunction, bool> renderCreateCircleSegmentPolygon(editor::Document* doc, bw::core::Primitive::Operation op, bw::core::Primitive::FillRule fillRule, uint8_t priority, wp::Vector2 const& position, float scale, float angle) {
  static float arcLength = 90.0f;
  static float resolution = 0.5f;
  bool modified{false};

  ImGui::SetNextItemWidth(128);

  widgets::HelpMarker("Arc length in degrees.");
  ImGui::SameLine();
  if (ImGui::InputFloat("ArcLength##CreateCircleSegment", &arcLength, 1.0f, 5.0f)) {
    arcLength = clamp(arcLength, ED_MIN_ARC_LENGTH, ED_MAX_ARC_LENGTH);
    modified = true;
  }

  ImGui::SetNextItemWidth(128);

  widgets::HelpMarker("This value determines the number of sides in the circle segment polygon.");
  ImGui::SameLine();
  if (ImGui::InputFloat("Res##CreateCircleSegment", &resolution, 0.01f, 0.1f)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);
    modified = true;
  }

  return {
      "Create Circle Segment Primitive",
      bind(createCircleSegmentPrimitive, op, fillRule, arcLength, resolution, priority, position, scale, angle),
      modified};
}

tuple<string, CreatePrimitiveFunction, bool> renderCreateTorusPolygon(editor::Document* doc, bw::core::Primitive::Operation op, bw::core::Primitive::FillRule fillRule, uint8_t priority, wp::Vector2 const& position, float scale, float angle) {
  static float thickness = 0.5f, resolution = 0.5f;
  bool modified{false};

  ImGui::SetNextItemWidth(128);

  widgets::HelpMarker("This is the thickness of the torus, in [0.01, 0.99].");
  ImGui::SameLine();
  if (ImGui::SliderFloat("Thickness##CreateTorus", &thickness, 0.01f, 0.99f)) {
    modified = true;
  }

  widgets::HelpMarker("This value determines the number of sides in the torus polygon.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat("Res##CreateTorus", &resolution, 0.01f, 0.1f)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);
    modified = true;
  }

  return {
      "Create Torus Primitive",
      bind(createTorusPrimitive, op, fillRule, thickness, resolution, priority, position, scale, angle),
      modified};
}

tuple<string, CreatePrimitiveFunction, bool> renderCreateTorusSegmentPolygon(editor::Document* doc, bw::core::Primitive::Operation op, bw::core::Primitive::FillRule fillRule, uint8_t priority, wp::Vector2 const& position, float scale, float angle) {
  static float thickness = 0.5f;
  static float arcLength = 90.0f;
  static float resolution = 0.5f;
  bool modified{false};

  widgets::HelpMarker("This is the thickness of the torus segment, in [0.01, 0.99].");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderFloat("Thickness##CreateTorusSegment", &thickness, 0.01f, 0.99f)) {
    modified = true;
  }

  widgets::HelpMarker("Arc length in degrees.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat("ArcLength##CreateTorusSegment", &arcLength, 1.0f, 5.0f)) {
    arcLength = clamp(arcLength, ED_MIN_ARC_LENGTH, ED_MAX_ARC_LENGTH);
    modified = true;
  }

  widgets::HelpMarker("This value determines the number of sides in the torus segment polygon.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat("Res##CreateTorusSegment", &resolution, 0.01f, 0.1f)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);
    modified = true;
  }

  return {
      "Create Torus Segment Primitive",
      bind(createTorusSegmentPrimitive, op, fillRule, thickness, arcLength, resolution, priority, position, scale, angle),
      modified};
}

tuple<string, CreatePrimitiveFunction, bool> renderCreateRectanglePolygon(editor::Document* doc, bw::core::Primitive::Operation op, bw::core::Primitive::FillRule fillRule, uint8_t priority, wp::Vector2 const& position, float scale, float angle) {
  static float xyRatio = 2.0f;
  bool modified{false};

  widgets::HelpMarker("This value sets the ratio of the rectangle width to its height.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat("Ratio##CreateRectangle", &xyRatio, 0.01f, 0.1f)) {
    xyRatio = clamp(xyRatio, ED_MIN_RECTANGLE_XYRATIO, ED_MAX_RECTANGLE_XYRATIO);
    modified = true;
  }

  return {
      "Create Rectangle Primitive",
      bind(createRectanglePrimitive, op, fillRule, xyRatio, priority, position, scale, angle),
      modified};
}

tuple<string, CreatePrimitiveFunction, bool> renderCreateSuperformulaPolygon(editor::Document* doc, bw::core::Primitive::Operation op, bw::core::Primitive::FillRule fillRule, uint8_t priority, wp::Vector2 const& position, float scale, float angle) {
  static float resolution = 0.5f;
  static float values[6] = {1.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  bool modified{false};

  widgets::HelpMarker("This value determines the number of sides in the superformula polygon.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat("Res##CreateSuperformula", &resolution, 0.01f, 0.1f)) {
    resolution = clamp(resolution, ED_MIN_SUPERFORMULA_RESOLUTION, 1.0f);
    modified = true;
  }

  widgets::HelpMarker("Preset for initial values.");
  ImGui::SameLine();
  static int selectedPreset{0};
  if (ImGui::Combo("Preset", &selectedPreset, "Soft triangle\0Soft square\0Starfish\0Soft X\0Eye\0Cusp\0\0", 6)) {
    modified = true;
    switch (selectedPreset) {
      case 0:
        values[0] = 1.0f;
        values[1] = 1.0f;
        values[2] = 3.0f;
        values[3] = 4.5f;
        values[4] = 10.0f;
        values[5] = 10.0f;
        break;

      case 1:
        values[0] = 1.0f;
        values[1] = 1.0f;
        values[2] = 4.0f;
        values[3] = 12.0f;
        values[4] = 15.0f;
        values[5] = 15.0f;
        break;

      case 2:
        values[0] = 1.0f;
        values[1] = 1.0f;
        values[2] = 5.0f;
        values[3] = 2.0f;
        values[4] = 7.0f;
        values[5] = 7.0f;
        break;

      case 3:
        values[0] = 1.0f;
        values[1] = 1.0f;
        values[2] = 4.0f;
        values[3] = 1.0f;
        values[4] = 7.0f;
        values[5] = 8.0f;
        break;

      case 4:
        values[0] = 1.0f;
        values[1] = 1.0f;
        values[2] = 2.0f;
        values[3] = 0.5f;
        values[4] = 0.5f;
        values[5] = 0.5f;
        break;

      case 5:
        values[0] = 1.0f;
        values[1] = 1.0f;
        values[2] = 2.0f;
        values[3] = 1.0f;
        values[4] = 1.0f;
        values[5] = 1.0f;
        break;
      default:
        throw EditorException("Unknown preset");
    }
  }

  widgets::HelpMarker("Superformula parameter.  This is usually set to 1.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderFloat("a##CreateSuperformula", &values[0], ED_MIN_SUPERFORMULA_A, ED_MAX_SUPERFORMULA_A)) {
    modified = true;
  }

  widgets::HelpMarker("Superformula parameter.  This is usually set to 1.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderFloat("b##CreateSuperformula", &values[1], ED_MIN_SUPERFORMULA_B, ED_MAX_SUPERFORMULA_B)) {
    modified = true;
  }

  widgets::HelpMarker("Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderFloat("m##CreateSuperformula", &values[2], ED_MIN_SUPERFORMULA_M, ED_MAX_SUPERFORMULA_M)) {
    modified = true;
  }

  widgets::HelpMarker("Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.");
  ImGui::SameLine();

  ImGui::SetNextItemWidth(128);
  if (ImGui::SliderFloat("n1##CreateSuperformula", &values[3], ED_MIN_SUPERFORMULA_N1, ED_MAX_SUPERFORMULA_N1)) {
    modified = true;
  }

  widgets::HelpMarker("Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.");
  ImGui::SameLine();

  ImGui::SetNextItemWidth(128);
  if (ImGui::SliderFloat("n2##CreateSuperformula", &values[4], ED_MIN_SUPERFORMULA_N2, ED_MAX_SUPERFORMULA_N2)) {
    modified = true;
  }

  widgets::HelpMarker("Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.");
  ImGui::SameLine();

  ImGui::SetNextItemWidth(128);
  if (ImGui::SliderFloat("n3##CreateSuperformula", &values[5], ED_MIN_SUPERFORMULA_N3, ED_MAX_SUPERFORMULA_N3)) {
    modified = true;
  }

  return {
      "Create Superformula Primitive",
      bind(createSuperformulaPrimitive, op, fillRule, values, resolution, priority, position, scale, angle),
      modified};
}

bw::core::Primitive::Operation setOperationWidget(Document* doc, bw::core::Primitive* primitive, int mode) {
  int selectedOperation;
  bw::core::Primitive::Operation editOperation = primitive ? primitive->getOperation()
                                                           : bw::core::Primitive::Operation::Union;

  string name;
  switch (mode) {
    case 0:
      name = "Operation##CreatePrimitive";
      break;

    case 1:
      name = "Operation##EditPrimitive";
      break;

    case 2:
      name = "Operation##OrderPrimitive";
      break;

    default:
      throw EditorException("Unknown widget mode");
  }
  switch (editOperation) {
    case bw::core::Primitive::Operation::Union:
      selectedOperation = 0;
      break;

    case bw::core::Primitive::Operation::Difference:
      selectedOperation = 1;
      break;

    case bw::core::Primitive::Operation::Intersection:
      selectedOperation = 2;
      break;

    case bw::core::Primitive::Operation::XOR:
      selectedOperation = 3;
      break;

    default:
      throw EditorException("Unknown operation");
  }

  widgets::HelpMarker("CSG operation to perform on primitives.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::Combo(name.c_str(), &selectedOperation, "Union\0Difference\0Intersection\0XOR\0\0", 6)) {
    switch (selectedOperation) {
      case 0:
        editOperation = bw::core::Primitive::Operation::Union;
        break;

      case 1:
        editOperation = bw::core::Primitive::Operation::Difference;
        break;

      case 2:
        editOperation = bw::core::Primitive::Operation::Intersection;
        break;

      case 3:
        editOperation = bw::core::Primitive::Operation::XOR;
        break;

      default:
        throw EditorException("Unknown operation");
    }

    if (primitive) {
      transactUndoableAction(doc, "Set operation", [primitive, editOperation](editor::Document* doc) {
        return setPrimitiveOperation(doc, primitive, editOperation);
      });
    }
  }

  return editOperation;
}

bw::core::Primitive::FillRule setFillRuleWidget(Document* doc, bw::core::Primitive* primitive, int mode) {
  int selectedFillRule;
  bw::core::Primitive::FillRule editFillRule = primitive ? primitive->getFillRule()
                                                         : bw::core::Primitive::FillRule::NonZero;

  string name;
  switch (mode) {
    case 0:
      name = "FillRule##CreatePrimitive";
      break;

    case 1:
      name = "FillRule##EditPrimitive";
      break;

    case 2:
      name = "FillRule##OrderPrimitive";
      break;

    default:
      throw EditorException("Unknown widget mode");
  }

  switch (editFillRule) {
    case bw::core::Primitive::FillRule::NonZero:
      selectedFillRule = 0;
      break;

    case bw::core::Primitive::FillRule::EvenOdd:
      selectedFillRule = 1;
      break;

    default:
      throw EditorException("Unknown fill rule");
  }

  widgets::HelpMarker("This determines whether intersecting sections are filled using non-zero winding or even-odd parity.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::Combo(name.c_str(), &selectedFillRule, "NonZero\0EvenOdd\0\0", 6)) {
    switch (selectedFillRule) {
      case 0:
        editFillRule = bw::core::Primitive::FillRule::NonZero;
        break;

      case 1:
        editFillRule = bw::core::Primitive::FillRule::EvenOdd;
        break;

      default:
        throw EditorException("Unknown fill rule");
    }

    if (primitive) {
      transactUndoableAction(doc, "Set fill rule", [primitive, editFillRule](editor::Document* doc) {
        return setPrimitiveFillRule(doc, primitive, editFillRule);
      });
    }
  }

  return editFillRule;
}

void renderCreateNewPrimitive(editor::Document* doc, editor::Settings& settings) {
  static int selectedPrimitiveType = 0;
  bool modified{false};

  auto ghost = doc->getWorld()->getPrimitive(0);

  bw::core::Primitive::Operation createOperation = ghost->getOperation();
  bw::core::Primitive::FillRule createFillRule = ghost->getFillRule();

  // Primitive tyoe
  vector<string> primitiveTypes = {
      "Regular Polygon",
      "Circle",
      "Circle Segment",
      "Torus",
      "Torus Segment",
      "Rectangle",
      "Superformula",
  };

  string primitiveTypesStr;

  for (auto const& primitiveType : primitiveTypes) {
    primitiveTypesStr += primitiveType;
    primitiveTypesStr += '\0';
  }

  widgets::HelpMarker("Choose the primitive type to create.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::Combo("Type##CreatePrimitive", &selectedPrimitiveType, primitiveTypesStr.c_str(), 6)) {
    modified = true;
  }

  // Operation
  createOperation = setOperationWidget(doc, ghost, 0);

  // Fill rule
  createFillRule = setFillRuleWidget(doc, ghost, 0);

  // Layer
  static uint32_t createPrimitiveLayerId = doc->getWorld()->getActiveLayer()->getId();

  widgets::HelpMarker("The Layer to place the Primitive on.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);
  createPrimitiveLayerId = widgets::LayerPicker("Layer##CreatePrimitive", doc->getWorld().get(), createPrimitiveLayerId);

  // Priority
  int primitivePriority = (int)ghost->getPriority();

  widgets::HelpMarker("Priority determines the order in which primitives are folded.  Lower value means earlier in the order.  Allowed values are 0 to 255.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderInt("Priority##CreatePrimitive", &primitivePriority, BW_PRIORITY_MIN_VALUE, BW_PRIORITY_MAX_VALUE)) {
    modified = true;
  }

  // Position
  auto const& pos = ghost->getPosition();
  float primitivePosition[2] = {pos.x, pos.y};

  widgets::HelpMarker("Initial position in the world.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat2("Position##CreatePrimitive", primitivePosition)) {
    modified = true;
  }

  wp::Vector2 primitivePos{primitivePosition[0], primitivePosition[1]};

  // Scale
  float primitiveScale = ghost->getSize().x;
  widgets::HelpMarker("Initial size.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderFloat("Size##CreatePrimitive", &primitiveScale, ED_MIN_PRIMITIVE_SIZE, ED_MAX_PRIMITIVE_SIZE)) {
    modified = true;
  }

  // Angle
  float primitiveAngle = ghost->getAnimationInterpolator(bw::core::VertexTransformer::Key::Angle).getValue(0.0f);
  widgets::HelpMarker("Initial angle.  This will create two keyframes in the angle interpolator at times 0 and 1, set to this value.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderFloat("Angle##CreatePrimitive", &primitiveAngle, 0.0f, 360.0f)) {
    modified = true;
  }

  tuple<string, CreatePrimitiveFunction, bool> funcDetails;
  switch (selectedPrimitiveType) {
    case 0:
      funcDetails = renderCreateRegularPolygon(doc, createOperation, createFillRule, (uint8_t)primitivePriority, primitivePos, primitiveScale, primitiveAngle);
      break;

    case 1:
      funcDetails = renderCreateCirclePolygon(doc, createOperation, createFillRule, (uint8_t)primitivePriority, primitivePos, primitiveScale, primitiveAngle);
      break;

    case 2:
      funcDetails = renderCreateCircleSegmentPolygon(doc, createOperation, createFillRule, (uint8_t)primitivePriority, primitivePos, primitiveScale, primitiveAngle);
      break;

    case 3:
      funcDetails = renderCreateTorusPolygon(doc, createOperation, createFillRule, (uint8_t)primitivePriority, primitivePos, primitiveScale, primitiveAngle);
      break;

    case 4:
      funcDetails = renderCreateTorusSegmentPolygon(doc, createOperation, createFillRule, (uint8_t)primitivePriority, primitivePos, primitiveScale, primitiveAngle);
      break;

    case 5:
      funcDetails = renderCreateRectanglePolygon(doc, createOperation, createFillRule, (uint8_t)primitivePriority, primitivePos, primitiveScale, primitiveAngle);
      break;

    case 6:
      funcDetails = renderCreateSuperformulaPolygon(doc, createOperation, createFillRule, (uint8_t)primitivePriority, primitivePos, primitiveScale, primitiveAngle);
      break;

    default:
      throw EditorException("Unknown primitive type");
  }

  auto funcText = get<0>(funcDetails);
  auto newPrimitiveFunc = get<1>(funcDetails);
  auto primOptionsModified = get<2>(funcDetails);

  if (modified || primOptionsModified) {
    auto newGhost = newPrimitiveFunc();
    doc->updateGhost(doc->getWorld(), newGhost);
  }

  widgets::HelpMarker("Create primitive.");
  ImGui::SameLine();
  if (ImGui::Button("Create##CreatePrimitive")) {
    auto const targetLayerId = createPrimitiveLayerId;
    transactUndoableAction(doc, funcText, [targetLayerId](editor::Document* doc) {
      auto world = doc->getWorld();

      if (!createPrimitiveFromGhost(doc)) {
        return false;
      }

      if (targetLayerId != world->getActiveLayer()->getId()) {
        if (auto* destinationLayer = world->getLayer(targetLayerId)) {
          world->movePrimitiveToLayer(world->getPrimitive(world->getNumPrimitives() - 1), destinationLayer);
        }
      }

      return true;
    });
    generateClipping(doc, settings, ED_CLIP_ON_PRIM_CREATE_DELETE);
  }
}

void renderCreatePrimitiveView(editor::Document* doc, editor::Settings& settings) {
  auto windowFlags = 0;

  bool docIsActive = doc->isActive();

  if (!docIsActive) {
    widgets::PushDisabled();
  }

  renderCreateNewPrimitive(doc, settings);

  if (!docIsActive) {
    widgets::PopDisabled();
  }
}

void renderEditRegularPolygon(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
}

void renderEditCirclePolygon(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  ImGui::SetNextItemWidth(128);

  auto circle = static_cast<bw::core::CirclePolygon*>(primitive);
  float resolution = circle->getResolution();

  widgets::HelpMarker("This value determines the number of sides in the circle polygon.");
  ImGui::SameLine();
  if (ImGui::InputFloat("Res##EditPrimitive", &resolution, 0.01f, 0.1f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);

    transactUndoableAction(doc, format("Set Circle Resolution to {}", resolution), [circle, resolution](editor::Document* doc) {
      auto staticBefore = circle->isStatic();

      circle->setResolution(resolution);
      return true;
    });
  }
}

void renderEditCircleSegmentPolygon(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  ImGui::SetNextItemWidth(128);

  auto circleSeg = static_cast<bw::core::CircleSegmentPolygon*>(primitive);
  float arcLength = circleSeg->getArcLength();
  float resolution = circleSeg->getResolution();

  widgets::HelpMarker("Arc length in degrees.");
  ImGui::SameLine();
  if (ImGui::InputFloat("ArcLength##EditPrimitive", &arcLength, 1.0f, 5.0f)) {
    arcLength = clamp(arcLength, ED_MIN_ARC_LENGTH, ED_MAX_ARC_LENGTH);

    transactUndoableAction(doc, format("Set Circle Segment Arc Length to {}", arcLength), [circleSeg, arcLength, resolution](editor::Document* doc) {
      auto staticBefore = circleSeg->isStatic();

      circleSeg->setArcLength(arcLength);
      return true;
    });
  }

  widgets::HelpMarker("This value determines the number of sides in the circle polygon.");
  ImGui::SameLine();
  if (ImGui::InputFloat("Res##EditPrimitive", &resolution, 0.01f, 0.1f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);

    transactUndoableAction(doc, format("Set Circle Segment Resolution to {}", resolution), [circleSeg, resolution](editor::Document* doc) {
      circleSeg->setResolution(resolution);
      return true;
    });
  }
}

void renderEditTorusPolygon(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  ImGui::SetNextItemWidth(128);

  auto torus = static_cast<bw::core::TorusPolygon*>(primitive);
  float thickness = torus->getThickness();
  float resolution = torus->getResolution();

  widgets::HelpMarker("This is the thickness of the torus, in [0.01, 0.99].");
  ImGui::SameLine();
  if (ImGui::SliderFloat("Thickness##CreateTorus", &thickness, 0.01f, 0.99f)) {
    torus->setThickness(thickness);
  }

  widgets::HelpMarker("This value determines the number of sides in the torus polygon.");
  ImGui::SameLine();
  if (ImGui::InputFloat("Res##EditPrimitive", &resolution, 0.01f, 0.1f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);

    transactUndoableAction(doc, format("Set Torus Resolution to {}", resolution), [torus, resolution](editor::Document* doc) {
      torus->setResolution(resolution);
      return true;
    });
  }
}

void renderEditTorusSegmentPolygon(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  ImGui::SetNextItemWidth(128);

  auto torusSeg = static_cast<bw::core::TorusSegmentPolygon*>(primitive);
  float thickness = torusSeg->getThickness();
  float arcLength = torusSeg->getArcLength();
  float resolution = torusSeg->getResolution();

  widgets::HelpMarker("This is the thickness of the torus segment, in [0.01, 0.99].");
  ImGui::SameLine();
  if (ImGui::SliderFloat("Thickness##CreateTorus", &thickness, 0.01f, 0.99f)) {
    torusSeg->setThickness(thickness);
  }

  widgets::HelpMarker("Arc length in degrees.");
  ImGui::SameLine();
  if (ImGui::InputFloat("ArcLength##EditPrimitive", &arcLength, 1.0f, 5.0f)) {
    arcLength = clamp(arcLength, ED_MIN_ARC_LENGTH, ED_MAX_ARC_LENGTH);

    transactUndoableAction(doc, format("Set Torus Segment Arc Length to {}", arcLength), [torusSeg, arcLength, resolution](editor::Document* doc) {
      auto staticBefore = torusSeg->isStatic();

      torusSeg->setArcLength(arcLength);
      return true;
    });
  }

  widgets::HelpMarker("This value determines the number of sides in the torus segment polygon.");
  ImGui::SameLine();
  if (ImGui::InputFloat("Res##EditPrimitive", &resolution, 0.01f, 0.1f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);

    transactUndoableAction(doc, format("Set Torus Resolution to {}", resolution), [torusSeg, resolution](editor::Document* doc) {
      torusSeg->setResolution(resolution);
      return true;
    });
  }
}

void renderEditRectanglePolygon(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  ImGui::SetNextItemWidth(128);

  auto rectangle = static_cast<bw::core::RectanglePolygon*>(primitive);

  float xyRatio = rectangle->getXyRatio();

  widgets::HelpMarker("This value sets the ratio of the rectangle width to its height.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat("Ratio##EditRectangle", &xyRatio, 0.01f, 0.1f)) {
    auto staticBefore = rectangle->isStatic();

    xyRatio = clamp(xyRatio, ED_MIN_RECTANGLE_XYRATIO, ED_MAX_RECTANGLE_XYRATIO);

    rectangle->setXyRatio(xyRatio);
  }

  if (ImGui::IsItemActivated()) {
    beginUndoableAction(doc, "", bind(editor::recordCurrentState, placeholders::_1, true), 0.0f);
  } else if (ImGui::IsItemDeactivatedAfterEdit()) {
    commitUndoableAction(doc, format("Set Rectangle X/Y ratio to {}", rectangle->getXyRatio()));
  } else if (ImGui::IsItemDeactivated()) {
    abandonUndoableAction(doc);
  }
}

void completeSuperformulaControlValueEdit(editor::Document* doc, bw::core::SuperformulaPolygon* superformula, uint32_t index, string const& name) {
  if (ImGui::IsItemActivated()) {
    beginUndoableAction(doc, "", bind(editor::recordCurrentState, placeholders::_1, true), 0.0f);
  } else if (ImGui::IsItemDeactivatedAfterEdit()) {
    commitUndoableAction(doc, format("Set Superformula {} to {}", name, superformula->getValue(index)));
  } else if (ImGui::IsItemDeactivated()) {
    abandonUndoableAction(doc);
  }
}

void renderEditSuperformulaPolygon(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  ImGui::SetNextItemWidth(128);

  auto sf = static_cast<bw::core::SuperformulaPolygon*>(primitive);
  float resolution = sf->getResolution();

  widgets::HelpMarker("This value determines the number of sides in the superformula polygon.");
  ImGui::SameLine();
  if (ImGui::InputFloat("Res##EditPrimitive", &resolution, 0.01f, 0.1f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
    resolution = clamp(resolution, ED_MIN_SUPERFORMULA_RESOLUTION, 1.0f);

    transactUndoableAction(doc, format("Set Superformula Resolution to {}", resolution), [sf, resolution](editor::Document* doc) {
      sf->setResolution(resolution);
      return true;
    });
  }

  float values[6];
  for (uint32_t i = 0; i < 6; ++i) {
    values[i] = sf->getValue(i);
  }

  struct SuperformulaControl {
    char const* label;
    char const* name;
    char const* help;
    float minimum;
    float maximum;
  };
  SuperformulaControl const controls[] = {
      {"a##EditPrimitive", "a", "Superformula parameter.  This is usually set to 1.", ED_MIN_SUPERFORMULA_A, ED_MAX_SUPERFORMULA_A},
      {"b##EditPrimitive", "b", "Superformula parameter.  This is usually set to 1.", ED_MIN_SUPERFORMULA_B, ED_MAX_SUPERFORMULA_B},
      {"m##EditPrimitive", "m", "Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.", ED_MIN_SUPERFORMULA_M, ED_MAX_SUPERFORMULA_M},
      {"n1##EditPrimitive", "n1", "Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.", ED_MIN_SUPERFORMULA_N1, ED_MAX_SUPERFORMULA_N1},
      {"n2##EditPrimitive", "n2", "Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.", ED_MIN_SUPERFORMULA_N2, ED_MAX_SUPERFORMULA_N2},
      {"n3##EditPrimitive", "n3", "Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.", ED_MIN_SUPERFORMULA_N3, ED_MAX_SUPERFORMULA_N3},
  };

  for (uint32_t i = 0; i < 6; ++i) {
    auto const& control = controls[i];
    widgets::HelpMarker(control.help);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(128);
    if (ImGui::SliderFloat(control.label, &values[i], control.minimum, control.maximum)) {
      sf->setValue(i, values[i]);
    }
    completeSuperformulaControlValueEdit(doc, sf, i, control.name);
  }
}

void renderEditMeshPrimitive(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
}

void renderTransformFlow(editor::Document* doc, bw::core::Primitive* primitive, vector<bw::core::tTransform> const& flow, bw::core::VertexTransformer::Key key, string const& keyName) {
  ImGui::Text("%s Transform Flow", keyName.c_str());

  char const* operandTypes[] = {"Input", "Constant", "Sine", "Inv Cosine", "Triangle", "Saw", "Square", "TriggerLine (both)", "TriggerLine (red)", "TriggerLine (blue)", "Previous"};
  char const* inputTypes[] = {"Eye dist", "Eye angle", "Global angle", "Player move", "Player turn", "Player move/turn", "User 1", "User 2", "User 3", "User 4"};

  for (uint32_t i = 0; i < (uint32_t)flow.size(); ++i) {
    ImGui::PushID(i);

    auto& transform = flow[i];

    int operand0 = (int)transform.operands[0];
    int operand1 = (int)transform.operands[1];
    float constant0 = transform.constants[0];
    float constant1 = transform.constants[1];
    float fnMultiplier0 = transform.fnMultipliers[0];
    float fnMultiplier1 = transform.fnMultipliers[1];
    int index0 = (int)transform.indices[0];
    int index1 = (int)transform.indices[1];
    int input0 = (int)transform.inputs[0];
    int input1 = (int)transform.inputs[1];
    int operation = (int)transform.operation;

    // Don't allow "previous" on first entry in flow
    auto numOperandTypes = IM_ARRAYSIZE(operandTypes);

    if (i == 0) {
      numOperandTypes--;
    }

    // Operand 1 type
    widgets::HelpMarker("Type of value to use for the left side of the transform equation.  Either a pre-defined input source, a constant value, or the result of the previous calculation in the transform.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(96);
    if (ImGui::Combo("##Op1TransformFlow", &operand0, operandTypes, numOperandTypes)) {
      transactUndoableAction(doc, "Set Transform Operand 1", [primitive, key, i, operand0](editor::Document* doc) {
        return setTransformOperand(doc, primitive, key, i, 0, (bw::core::tTransform::OperandType)operand0);
      });
    }

    ImGui::SameLine();

    // Operand 1 value
    widgets::HelpMarker("Value to use for the left side of the transform equation.  If 'Input' was selected as type then the selected value will take the incoming output from the relevant Input interpolator.  If 'Constant' was chosen, then enter a value in [0, 1].");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(112);
    switch ((bw::core::tTransform::OperandType)operand0) {
      case bw::core::tTransform::OperandType::Input:
        if (ImGui::Combo("##In1TransformFlow", &input0, inputTypes, IM_ARRAYSIZE(inputTypes))) {
          transactUndoableAction(doc, "Set Transform Input 1", [primitive, key, i, input0](editor::Document* doc) {
            return setTransformInput(doc, primitive, key, i, 0, (bw::core::InputType)input0);
          });
        }
        break;

      case bw::core::tTransform::OperandType::Constant:
        if (ImGui::InputFloat("##Cn1TransformFlow", &constant0, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
          transactUndoableAction(doc, "Set Transform Constant 1", [primitive, key, i, constant0](editor::Document* doc) {
            // float c = clamp(constant0, ED_MIN_TRANSFORM_CONSTANT, ED_MAX_TRANSFORM_CONSTANT);
            float c = constant0;
            return setTransformConstant(doc, primitive, key, i, 0, c);
          });
        }
        break;

      case bw::core::tTransform::OperandType::Sine:
      case bw::core::tTransform::OperandType::InvCosine:
      case bw::core::tTransform::OperandType::Triangle:
      case bw::core::tTransform::OperandType::Saw:
      case bw::core::tTransform::OperandType::Square:
        if (ImGui::InputFloat("##Fn1TransformFlow", &fnMultiplier0, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
          if (fnMultiplier0 > 0.0f) {
            transactUndoableAction(doc, "Set Transform Function 1", [primitive, key, i, fnMultiplier0](editor::Document* doc) {
              float c = fnMultiplier0;
              return setTransformFnMultiplier(doc, primitive, key, i, 0, c);
            });
          }
        }
        break;

      case bw::core::tTransform::OperandType::TriggerLine:
      case bw::core::tTransform::OperandType::TriggerLineRed:
      case bw::core::tTransform::OperandType::TriggerLineBlue:
        if (ImGui::InputInt("##Tr1TransformFlow", &index0, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue)) {
          transactUndoableAction(doc, "Set Transform Index 1", [primitive, key, i, index0](editor::Document* doc) {
            uint32_t i0 = max(0, index0);
            return setTransformTriggerLine(doc, primitive, key, i, 0, i0);
          });
        }
        break;

      case bw::core::tTransform::OperandType::TransformOutput:
        ImGui::Text("<result>");
        break;
    }

    ImGui::SameLine();

    // Operation
    char const* opTypes[] = {"+", "*", "|-|", "Min", "Max", "Avg", "<", ">", "<=", ">=", "%/"};

    widgets::HelpMarker("Operator to use for the transform equation.  |-| means the absolute difference of the two values.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(64);
    if (ImGui::Combo("##OpTransformFlow", &operation, opTypes, IM_ARRAYSIZE(opTypes))) {
      transactUndoableAction(doc, "Set Transform Operation", [primitive, key, i, operation](editor::Document* doc) {
        return setTransformOperation(doc, primitive, key, i, (bw::core::tTransform::Operation)operation);
      });
    }

    ImGui::SameLine();

    // Operand 2 type
    widgets::HelpMarker("Type of value to use for the right side of the transform equation.  Either a pre-defined input source, a constant value, or the result of the previous calculation in the transform.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(96);
    if (ImGui::Combo("##Op2TransformFlow", &operand1, operandTypes, numOperandTypes)) {
      transactUndoableAction(doc, "Set Transform Operand 1", [primitive, key, i, operand1](editor::Document* doc) {
        return setTransformOperand(doc, primitive, key, i, 1, (bw::core::tTransform::OperandType)operand1);
      });
    }

    ImGui::SameLine();

    // Operand 2 value
    widgets::HelpMarker("Value to use for the right side of the transform equation.  If 'Input' was selected as type then the selected value will take the incoming output from the relevant Input interpolator.  If 'Constant' was chosen, then enter a value in [0, 1].");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(112);
    switch ((bw::core::tTransform::OperandType)operand1) {
      case bw::core::tTransform::OperandType::Input:
        if (ImGui::Combo("##In2TransformFlow", &input1, inputTypes, IM_ARRAYSIZE(inputTypes))) {
          transactUndoableAction(doc, "Set Transform Input 2", [primitive, key, i, input1](editor::Document* doc) {
            return setTransformInput(doc, primitive, key, i, 1, (bw::core::InputType)input1);
          });
        }
        break;

      case bw::core::tTransform::OperandType::Constant:
        if (ImGui::InputFloat("##Cn2TransformFlow", &constant1, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
          transactUndoableAction(doc, "Set Transform Constant 2", [primitive, key, i, constant1](editor::Document* doc) {
            // float c = clamp(constant1, ED_MIN_TRANSFORM_CONSTANT, ED_MAX_TRANSFORM_CONSTANT);
            float c = constant1;

            return setTransformConstant(doc, primitive, key, i, 1, c);
          });
        }
        break;

      case bw::core::tTransform::OperandType::Sine:
      case bw::core::tTransform::OperandType::InvCosine:
      case bw::core::tTransform::OperandType::Triangle:
      case bw::core::tTransform::OperandType::Saw:
      case bw::core::tTransform::OperandType::Square:
        if (ImGui::InputFloat("##Fn2TransformFlow", &fnMultiplier1, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
          if (fnMultiplier1 > 0.0f) {
            transactUndoableAction(doc, "Set Transform Function 2", [primitive, key, i, fnMultiplier1](editor::Document* doc) {
              float c = fnMultiplier1;
              return setTransformFnMultiplier(doc, primitive, key, i, 1, c);
            });
          }
        }
        break;

      case bw::core::tTransform::OperandType::TriggerLine:
      case bw::core::tTransform::OperandType::TriggerLineRed:
      case bw::core::tTransform::OperandType::TriggerLineBlue:
        if (ImGui::InputInt("##Tr2TransformFlow", &index1, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue)) {
          transactUndoableAction(doc, "Set Transform Index 2", [primitive, key, i, index1](editor::Document* doc) {
            uint32_t i1 = max(0, index1);
            return setTransformTriggerLine(doc, primitive, key, i, 1, i1);
          });
        }
        break;

      case bw::core::tTransform::OperandType::TransformOutput:
        ImGui::Text("<result>");
        break;
    }

    // Move up/down, delete
    ImGui::SameLine();

    int counter = 0;
    ImGui::PushButtonRepeat(false);

    if (i > 0) {
      ImGui::SameLine();
      if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
        counter--;
      }
    }

    if (i < (uint32_t)(flow.size() - 1)) {
      ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
      if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
        counter++;
      }
    }

    ImGui::PopButtonRepeat();

    if (counter < 0) {
      transactUndoableAction(doc, format("Swap {} Transforms", keyName),
                             bind(swapTransforms, placeholders::_1, primitive, key, i, i - 1));

    } else if (counter > 0) {
      transactUndoableAction(doc, format("Swap {} Transforms", keyName),
                             bind(swapTransforms, placeholders::_1, primitive, key, i, i + 1));
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_FA_ERASER)) {
      transactUndoableAction(doc, format("Remove {} Transform", keyName),
                             bind(removeTransform, placeholders::_1, primitive, key, i));
    }

    ImGui::PopID();
  }

  if (ImGui::Button(ICON_FA_PLUS)) {
    transactUndoableAction(doc, format("Add {} Transform", keyName),
                           bind(addTransform, placeholders::_1, primitive, key));
  }
}

void renderTransformFlow(editor::Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key) {
  string keyName;

  switch (key) {
    case bw::core::VertexTransformer::Key::Scale:
      keyName = "Scale";
      break;

    case bw::core::VertexTransformer::Key::Angle:
      keyName = "Angle";
      break;

    case bw::core::VertexTransformer::Key::OrbitAngle:
      keyName = "Orbit Angle";
      break;

    case bw::core::VertexTransformer::Key::OrbitDistance:
      keyName = "Orbit Distance";
      break;
  }

  ImGui::PushID(keyName.c_str());

  switch (key) {
    case bw::core::VertexTransformer::Key::Scale:
      renderTransformFlow(doc, primitive, primitive->getScaleTransforms(), key, keyName);
      break;

    case bw::core::VertexTransformer::Key::Angle:
      renderTransformFlow(doc, primitive, primitive->getAngleTransforms(), key, keyName);
      break;

    case bw::core::VertexTransformer::Key::OrbitAngle:
      renderTransformFlow(doc, primitive, primitive->getOrbitAngleTransforms(), key, keyName);
      break;

    case bw::core::VertexTransformer::Key::OrbitDistance:
      renderTransformFlow(doc, primitive, primitive->getOrbitDistanceTransforms(), key, keyName);
      break;
  }

  ImGui::PopID();
}

vector<string> gEasingStrings = {
    "Linear",
    "EaseInSine",
    "EaseInCubic",
    "EaseInQuintic",
    "EaseOutSine",
    "EaseOutCubic",
    "EaseOutQuintic",
    "EaseInOutSine",
    "EaseInOutCubic",
    "EaseInOutQuintic",
    "EaseInBack",
    "EaseOutBack",
    "EaseInOutBack",
    "EaseInExpo",
    "EaseOutExpo",
    "EaseInOutExpo",
    "EaseInElastic",
    "EaseOutElastic",
    "EaseInOutElastic",
    "EaseInBounce",
    "EaseOutBounce",
    "EaseInOutBounce"};

typedef function<void()> AdditionalWidgetsFunction;

void renderInterpolator(editor::Document* doc, bw::core::Primitive* primitive, bw::core::Interpolator<float> const& lerper, bw::core::VertexTransformer::Key key, int curveIndex, string const& name, string const& lerperType, float proxyValue, float curValue, AdditionalWidgetsFunction addWidgetFunc = {}) {
  string easingsStr, lockTypesStr;

  for (auto const& easing : gEasingStrings) {
    easingsStr += easing;
    easingsStr += '\0';
  }

  vector<bw::core::Interpolator<float>::Point> points = lerper.getPoints();
  vector<vector<bw::core::Interpolator<float>::Point>> renderValues = lerper.render(100.0f);

  wp::Vector2 scaleMin, scaleMax;
  lerper.getScale(&scaleMin, &scaleMax);

  ImGui::PushID(name.c_str());

  ImGui::Text(name.c_str());

  if (addWidgetFunc) {
    addWidgetFunc();
  }

  auto numPoints = (uint32_t)points.size();

  array<ImVec2, bw::core::Interpolator<float>::MaxPoints> imPoints;
  for (uint32_t i = 0; i < numPoints; ++i) {
    imPoints[i] = {points[i].first, points[i].second};
  }

  int nPoints = (int)numPoints, editPoint;
  int curveWidgetWidth = 0;
  bool clicked, released;

  if (ImGui::MultiCurve(name.c_str(),
                        imPoints.data(),
                        &nPoints,
                        bw::core::Interpolator<float>::MaxPoints,
                        &editPoint,
                        false,
                        scaleMin.x,
                        scaleMin.y,
                        scaleMax.x,
                        scaleMax.y,
                        renderValues,
                        proxyValue,
                        curValue,
                        128,
                        &curveWidgetWidth,
                        &clicked,
                        &released)) {
    // We've either created, deleted or moved a point, depending on the new size.
    // So we may need to update the segments
    if (nPoints > (int)numPoints) {
      auto const& p = imPoints[editPoint];
      transactUndoableAction(doc, format("Add {} Point at {:.2f}", name, p.x), bind(addKeyToInterpolator, placeholders::_1, lerperType, primitive, key, p.x, p.y));
    } else if (nPoints < (int)numPoints) {
      transactUndoableAction(doc, format("Remove {} Point {}", name, editPoint), bind(removeKeyFromInterpolator, placeholders::_1, lerperType, primitive, key, editPoint));
    } else {
      wp::Vector2 editValue = {imPoints[editPoint].x, imPoints[editPoint].y};

      if (clicked) {
        beginUndoableAction(doc, "Move point", bind(editor::recordCurrentState, placeholders::_1, true), editValue);
      }

      // Moved
      updateAnimationKeyInInterpolator(doc, lerperType, primitive, key, editPoint, editValue.x, editValue.y);
    }
  }

  if (released) {
    if (editPoint >= 0) {
      wp::Vector2 editValue = {imPoints[editPoint].x, imPoints[editPoint].y};
      if (editor::transactionValueHasChanged(editValue)) {
        commitUndoableAction(doc);
      }
    } else {
      abandonUndoableAction(doc);
    }
  }

  // Segments
  int pointToRemove{-1};
  {
    points = lerper.getPoints();
    vector<bw::core::Interpolator<float>::Segment> const& segments = lerper.getSegments();

    auto numPoints = (uint32_t)points.size();
    auto numSegments = (uint32_t)segments.size();

    for (uint32_t i = 0; i < numSegments; ++i) {
      ImGui::PushID(i);

      if (ImGui::Button(ICON_FA_ERASER)) {
        pointToRemove = (int)i;
      }

      // Ignore segments where diff(x) is 0
      bool disable = i < numSegments && points[i].first == points[i + 1].first;

      if (disable) {
        widgets::PushDisabled();
      }

      auto& segment = segments[i];

      ImGui::SameLine();

      widgets::HelpMarker("Set the easing function to use when interpolating between the points.");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(128);
      int curEasing = (int)segment.easing;
      if (ImGui::Combo("###Interpolator", &curEasing, easingsStr.c_str(), 6)) {
        transactUndoableAction(doc, "Set Interpolator Segment Easing", [lerperType, primitive, key, i, curEasing](editor::Document* doc) {
          return setInterpolatorEasing(doc, lerperType, primitive, key, i, (bw::core::Easing)curEasing);
        });
      }

      ImGui::SameLine();

      widgets::HelpMarker("Manual specification of point values.  Note that discontinuous segments are disabled and need to be moved to be editable.");
      ImGui::SameLine();
      ImGui::SetNextItemWidth((float)curveWidgetWidth - 256);
      float inputValues[4] = {points[i + 0].first, points[i + 0].second, points[i + 1].first, points[i + 1].second};
      if (ImGui::InputFloat4("##Interpolator", inputValues, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue)) {
        // Clamp to neighbours to ensure we don't end up with non-ascending values
        if (i == 0) {
          // x value can't be changed
          inputValues[0] = scaleMin.x;
        } else {
          inputValues[0] = max(points[i - 1].first, inputValues[0]);
        }

        inputValues[0] = min(points[i + 1].first, inputValues[0]);
        inputValues[2] = max(inputValues[0], inputValues[2]);

        if (i == (numSegments - 1)) {
          // x value can't be changed
          inputValues[2] = scaleMax.x;
        } else {
          inputValues[2] = min(points[i + 2].first, inputValues[2]);
        }

        // Clamp to defined extents
        inputValues[0] = clamp(inputValues[0], scaleMin.x, scaleMax.x);
        inputValues[1] = clamp(inputValues[1], scaleMin.y, scaleMax.y);
        inputValues[2] = clamp(inputValues[2], scaleMin.x, scaleMax.x);
        inputValues[3] = clamp(inputValues[3], scaleMin.y, scaleMax.y);

        transactUndoableAction(doc, "Update Points", [lerperType, primitive, key, i, inputValues](Document* doc) {
          updateAnimationKeyInInterpolator(doc, lerperType, primitive, key, i, inputValues[0], inputValues[1]);
          updateAnimationKeyInInterpolator(doc, lerperType, primitive, key, i + 1, inputValues[2], inputValues[3]);
          return true;
        });
      }

      if (disable) {
        widgets::PopDisabled();
      }

      ImGui::PopID();
    }
  }

  ImGui::PopID();

  if (pointToRemove != -1) {
    removeKeyFromInterpolator(doc, lerperType, primitive, key, (uint32_t)pointToRemove);
  }
}

void renderInterpolator(editor::Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, bw::core::Interpolator<float> const& interpolator, string const& lerperType, float proxyValue, float curValue) {
  string keyName;

  switch (key) {
    case bw::core::VertexTransformer::Key::Scale:
      keyName = "Scale";
      break;

    case bw::core::VertexTransformer::Key::Angle:
      keyName = "Angle";
      break;

    case bw::core::VertexTransformer::Key::OrbitAngle:
      keyName = "Orbit Angle";
      break;

    case bw::core::VertexTransformer::Key::OrbitDistance:
      keyName = "Orbit Distance";
      break;

    default:
      throw EditorException("Unknown transform key");
  }

  renderInterpolator(doc, primitive, interpolator, key, (int)key + 3, format("{} {}", keyName, lerperType), lerperType, proxyValue, curValue);
}

void renderValueCapture(editor::Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key) {
  vector<string> captureModes = {
      "Distance/Sticky",
      "Distance/Delta Up",
      "Distance/Delta Down",
      "Distance/Latched Up",
      "Distance/Latched Down",
      "Angle/Sticky",
      "Angle/Delta Up",
      "Angle/Delta Down",
      "Angle/Latched Up",
      "Angle/Latched Down"};

  string captureModesStr;

  for (auto const& captureMode : captureModes) {
    captureModesStr += captureMode;
    captureModesStr += '\0';
  }

  widgets::HelpMarker("Capture mode for value.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  int selectedMode = (int)primitive->getCaptureMode(key);
  if (ImGui::Combo("Capture mode", &selectedMode, captureModesStr.c_str(), 6)) {
    primitive->setCaptureMode(key, (bw::core::ValueCaptureMode)selectedMode);
  }
}

void renderAnimatedPropertyEvents(editor::Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key) {
  ImGui::Text("Events");

  widgets::HelpMarker("Add new event.");
  ImGui::SameLine();

  if (ImGui::Button(ICON_FA_PLUS)) {
    primitive->addAnimatedPropertyEvent(key,
                                        0,
                                        bw::core::AnimatedPropertyEventTriggerType::UpDown,
                                        0.5f);
  }

  vector<string> triggerTypes = {
      "Up",
      "Down",
      "Up or down"};

  vector<string> eventTypes = {
      "Debug",
      "Gen clip"};

  string triggerTypesStr, eventTypesStr;

  for (auto const& triggerType : triggerTypes) {
    triggerTypesStr += triggerType;
    triggerTypesStr += '\0';
  }

  for (auto const& eventType : eventTypes) {
    eventTypesStr += eventType;
    eventTypesStr += '\0';
  }

  int indexToDelete{-1};
  auto const& events = primitive->getAnimatedPropertyEvents(key);

  for (uint32_t i = 0; i < (uint32_t)events.size(); ++i) {
    auto const& event = events[i];

    widgets::HelpMarker("Delete event.");
    ImGui::SameLine();

    if (ImGui::Button(ICON_FA_ERASER)) {
      indexToDelete = (int)i;
    }

    ImGui::SameLine();

    widgets::HelpMarker("Trigger type.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(128);
    int triggerType = (int)event.triggerType;

    if (ImGui::Combo("Trigger", &triggerType, triggerTypesStr.c_str(), 6)) {
      transactUndoableAction(doc, "Set Primitive Event Trigger", bind(setPrimitiveAnimatedPropertyEvent, placeholders::_1, primitive, key, i, event.eventType, (bw::core::AnimatedPropertyEventTriggerType)triggerType, event.value));
    }

    ImGui::SameLine();

    widgets::HelpMarker("Event type.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(128);
    int eventType = (int)log2(event.eventType);

    if (ImGui::Combo("Action", &eventType, eventTypesStr.c_str(), 6)) {
      transactUndoableAction(doc, "Set Primitive Event Action", bind(setPrimitiveAnimatedPropertyEvent, placeholders::_1, primitive, key, i, 1 << event.eventType, event.triggerType, event.value));
    }

    ImGui::SameLine();

    widgets::HelpMarker("Trigger value.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(64);
    float value = event.value;
    if (ImGui::InputFloat("Value", &value, 0.0f, 0.0f, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue)) {
      transactUndoableAction(doc, "Set Primitive Event Value", bind(setPrimitiveAnimatedPropertyEvent, placeholders::_1, primitive, key, i, 1 << event.eventType, event.triggerType, value));
    }
  }

  if (indexToDelete >= 0) {
    transactUndoableAction(doc, "Delete Primitive Event", bind(deletePrimitiveAnimatedPropertyEvent, placeholders::_1, primitive, key, (uint32_t)indexToDelete));
  }
}

void renderAnimatedProperty(editor::Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, editor::Settings& settings, double globalTime) {
  ImGui::PushID((int)key);

  widgets::HelpMarker("Reset animator.");
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    primitive->resetAnimator(key);
  }

  ImGui::Separator();

  renderTransformFlow(doc, primitive, key);
  ImGui::Separator();

  float proxyValue = primitive->transformT(key, globalTime);
  float curValue = primitive->getCurCapturedValue(key);
  bw::core::Primitive const* constPrim = primitive;

  renderInterpolator(doc, primitive, key, constPrim->getAnimationInterpolator(key), "Animation", proxyValue, curValue);
  ImGui::Separator();

  float inflPreview = (BW_INTERPOLATOR_MAX_DISTANCE - primitive->getInputs().entityInfluenceDistance) / BW_INTERPOLATOR_MAX_DISTANCE;
  renderInterpolator(doc, primitive, key, constPrim->getInfluenceInterpolator(key), "Influence", inflPreview, -1.0f);
  ImGui::Separator();

  renderValueCapture(doc, primitive, key);
  ImGui::Separator();

  renderAnimatedPropertyEvents(doc, primitive, key);

  ImGui::PopID();
}

void renderEditPrimitiveGeometry(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings, double globalTime) {
  if (primitive->isStatic()) {
    ImGui::Text("Primitive is static");
  } else {
    ImGui::Text("Primitive is animated");
  }

  // Copy and rotate
  static float copyAngle{0.0f};

  ImGui::SetNextItemWidth(128);
  ImGui::SliderAngle("Copy Angle", &copyAngle, 0, 360);
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_CLONE)) {
    auto index = primitive->getId();
    transactUndoableAction(doc, format("Clone&Rotate Primitive {}", index), bind(cloneRotatedPrimitive, placeholders::_1, index, wp::MathsUtils::degrees(copyAngle)));
  }

  ImGui::Separator();

  setOperationWidget(doc, primitive, 1);
  setFillRuleWidget(doc, primitive, 1);

  // Layer
  auto world = doc->getWorld();
  auto currentLayerId = world->getActiveLayer()->getId();

  widgets::HelpMarker("Move the Primitive to a different Layer.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);
  auto const pickedLayerId = widgets::LayerPicker("Layer##EditPrimitive", world.get(), currentLayerId);

  if (pickedLayerId != currentLayerId) {
    if (auto* destinationLayer = world->getLayer(pickedLayerId)) {
      auto index = primitive->getId();
      transactUndoableAction(doc, "Move Primitive to Layer", [index, destinationLayer](editor::Document* doc) {
        auto world = doc->getWorld();
        world->movePrimitiveToLayer(world->getPrimitive(index), destinationLayer);
        doc->clearSelections();
        return true;
      });
    }
  }

  // Priority
  int primitivePriority = (int)primitive->getPriority();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderInt("Priority##EditPrimitive", &primitivePriority, BW_PRIORITY_MIN_VALUE, BW_PRIORITY_MAX_VALUE)) {
    transactUndoableAction(doc, "Set Primitive Priority",
                           bind(setPrimitivePriority, placeholders::_1, primitive, (uint8_t)primitivePriority));
  }

  if (ImGui::IsItemActivated()) {
    beginUndoableAction(doc, "", bind(editor::recordCurrentState, placeholders::_1, true), 0.0f);
  } else if (ImGui::IsItemDeactivatedAfterEdit()) {
    commitUndoableAction(doc, format("Set Primitive Priority to {}", (int)primitive->getPriority()));
  } else if (ImGui::IsItemDeactivated()) {
    abandonUndoableAction(doc);
  }

  /*
   * Don't allow user to change orientation, it's really there to support prefab rotation
   *
  float orientation = wp::MathsUtils::radians(primitive->getOrientation());

  widgets::HelpMarker("Set the orientation of the primitive."); ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderAngle("Orientation##EditPrimitive", &orientation, 0, 360))
  {
          transactUndoableAction(doc, "Set Primitive Orientation",
                  bind(setPrimitiveOrientation, placeholders::_1, primitive, wp::MathsUtils::degrees(orientation)));
  }
  */

  float primitiveSize = primitive->getSize().x;

  widgets::HelpMarker("Set the base size of the primitive.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderFloat("Size##EditPrimitive", &primitiveSize, ED_MIN_PRIMITIVE_SIZE, ED_MAX_PRIMITIVE_SIZE)) {
    transactUndoableAction(doc, "Set Primitive Size",
                           bind(setPrimitiveSize, placeholders::_1, primitive, primitiveSize));
  }

  wp::Vector2 const& primitivePosition = primitive->getPosition();

  widgets::HelpMarker("Set the origin (global centre position) of the primitive.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  float pPosition[2] = {primitivePosition.x, primitivePosition.y};

  if (ImGui::InputFloat2("Position##EditPrimitive", pPosition)) {
    wp::Vector2 position{pPosition[0], pPosition[1]};

    transactUndoableAction(doc, "Set Primitive Position",
                           bind(setPrimitivePosition, placeholders::_1, primitive, position));
  }

  wp::Vector2 const& primitiveTransformOrigin = primitive->getTransformOffset();

  widgets::HelpMarker("Set the transform offset (ie scale/rotation centre) of the primitive.  This value is in [-1, 1] as it is relative to the primitive boundaries: the cenre cannot be outside the primitive: for that, use orbit distance.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  float pTransformOrigin[2] = {primitiveTransformOrigin.x, primitiveTransformOrigin.y};

  if (ImGui::InputFloat2("Transform offset##EditPrimitive", pTransformOrigin)) {
    // wp::Vector2 transformOrigin{
    //	clamp(pTransformOrigin[0], -1.0f, 1.0f),
    //	clamp(pTransformOrigin[1], -1.0f, 1.0f)
    // };
    wp::Vector2 transformOrigin = {pTransformOrigin[0], pTransformOrigin[1]};

    transactUndoableAction(doc, "Set Primitive Transform Offset",
                           bind(setPrimitiveTransformOffset, placeholders::_1, primitive, transformOrigin));
  }

  wp::Vector2 primitiveInfluenceOriginOffset = primitive->getInfluenceEyeOriginOffset();

  widgets::HelpMarker("Set the influence eye offset from the origin.  This determines the position from where input interpolators base their values.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  float pInfluenceOriginOffset[2] = {primitiveInfluenceOriginOffset.x, primitiveInfluenceOriginOffset.y};

  if (ImGui::InputFloat2("Influence Origin Offset##EditPrimitive", pInfluenceOriginOffset)) {
    wp::Vector2 influenceOriginOffset{pInfluenceOriginOffset[0], pInfluenceOriginOffset[1]};

    transactUndoableAction(doc, "Set Primitive Influence Origin Offset",
                           bind(setPrimitiveInfluenceOriginOffset, placeholders::_1, primitive, influenceOriginOffset));
  }

  if (primitive->getType() == "Regular") {
    renderEditRegularPolygon(doc, primitive, settings);
  } else if (primitive->getType() == "Circle") {
    renderEditCirclePolygon(doc, primitive, settings);
  } else if (primitive->getType() == "CircleSegment") {
    renderEditCircleSegmentPolygon(doc, primitive, settings);
  } else if (primitive->getType() == "Torus") {
    renderEditTorusPolygon(doc, primitive, settings);
  } else if (primitive->getType() == "TorusSegment") {
    renderEditTorusSegmentPolygon(doc, primitive, settings);
  } else if (primitive->getType() == "Rectangle") {
    renderEditRectanglePolygon(doc, primitive, settings);
  } else if (primitive->getType() == "Superformula") {
    renderEditSuperformulaPolygon(doc, primitive, settings);
  } else if (primitive->getType() == "Mesh") {
    renderEditMeshPrimitive(doc, primitive, settings);
  } else {
    throw EditorException("Unknown primitive type: " + primitive->getType());
  }

  widgets::HelpMarker("If selected, this will orient a primitive so that it rotates to face the central point around which it is rotating, if an orbit distance of greater than zero is set.");
  ImGui::SameLine();
  bool orientOrbitAngle = primitive->getFollowOrbitAngle();
  if (ImGui::Checkbox("Follow orbit angle", &orientOrbitAngle)) {
    string action = orientOrbitAngle ? "Set Primitive angle to Orbit" : "Unset Primitive Angle to Orbit";

    transactUndoableAction(doc, action,
                           bind(setPrimitiveFollowOrbitAngle, placeholders::_1, primitive, orientOrbitAngle));

    primitive->setFollowOrbitAngle(orientOrbitAngle);
  }

  widgets::HelpMarker("Normally, angle from player to a primitive is taken with 0 degrees being [0, 1].  This value adds an offset (in degrees to that angle).");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  auto eyeAngleOffset = primitive->getInfluenceEyeAngleOffset();

  if (ImGui::SliderFloat("Eye Angle Offset##EditPrimitive", &eyeAngleOffset, 0.0f, 360.0f)) {
    primitive->setInfluenceEyeAngleOffset(eyeAngleOffset);
  }

  if (ImGui::IsItemActivated()) {
    beginUndoableAction(doc, "", bind(editor::recordCurrentState, placeholders::_1, true), 0.0f);
  } else if (ImGui::IsItemDeactivatedAfterEdit()) {
    commitUndoableAction(doc, format("Set Eye Angle Offset to {}", primitive->getInfluenceEyeAngleOffset()));
  } else if (ImGui::IsItemDeactivated()) {
    abandonUndoableAction(doc);
  }

  char const* animatedPropertyNames[] = {
      "Scale",
      "Angle",
      "Orbit Angle",
      "Orbit Distance"};

  for (int i = 0; i < (int)bw::core::VertexTransformer::Key::COUNT; ++i) {
    if (ImGui::CollapsingHeader(animatedPropertyNames[i])) {
      renderAnimatedProperty(doc, primitive, (bw::core::VertexTransformer::Key)i, settings, globalTime);
    }
  }
}

void renderEditPrimitiveSettings(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  int flags = (int)primitive->getFlags();

  auto f0 = ImGui::CheckboxFlags("Don't update Primitive Time when Player is static", &flags, BW_PRIMITIVE_NO_TIME_UPDATE_PLAYER_STATIC);
  auto f1 = ImGui::CheckboxFlags("Don't update Primitive Time when visible to Player", &flags, BW_PRIMITIVE_NO_TIME_UPDATE_IF_VISIBLE);
  auto f2 = ImGui::CheckboxFlags("Calculate exact bounds based on vertex position", &flags, BW_PRIMITIVE_EXACT_BOUNDS_FLAG);

  if (f0 || f1 || f2) {
    transactUndoableAction(doc, "Update Primitive flags", [primitive, flags](Document* doc) {
      primitive->setFlags((uint32_t)flags);
      return true;
    });
  }

  auto timeUpdateDist = primitive->getTimeUpdateDistance();

  widgets::HelpMarker("Distance within which Primitive Time updates.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat("Time update distance", &timeUpdateDist, 0.0f, 0.0f, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue)) {
    if (timeUpdateDist >= 0.0f) {
      transactUndoableAction(doc, "Update Primitive Time Update distance", [primitive, timeUpdateDist](Document* doc) {
        primitive->setTimeUpdateDistance(timeUpdateDist);
        return true;
      });
    }
  }
}

bool renderEditMaterialParameters(string const& name, uint32_t materialIndex, bw::core::MaterialDefinitionData* materialDefinition) {
  bool update = false;

  ImGui::PushID(name.c_str());

  auto const& material = bw::common::MaterialNames[materialIndex];
  auto numParams = (uint32_t)get<1>(material);

  // Colour
  float colour[3] = {
      materialDefinition->baseColour[0],
      materialDefinition->baseColour[1],
      materialDefinition->baseColour[2]};

  ImGui::SetNextItemWidth(256);

  if (ImGui::ColorEdit3("Base colour", colour)) {
    materialDefinition->baseColour[0] = colour[0];
    materialDefinition->baseColour[1] = colour[1];
    materialDefinition->baseColour[2] = colour[2];

    update = true;
  }

  if (ImGui::Button("Load defaults")) {
    setPrimitiveDefaultMaterial(materialIndex, materialDefinition);
    update = true;
  }

  // Params
  for (uint32_t i = 0; i < numParams; ++i) {
    auto paramName = get<0>(bw::common::MaterialParams[materialIndex][i]);
    auto paramMin = get<1>(bw::common::MaterialParams[materialIndex][i]);
    auto paramMax = get<2>(bw::common::MaterialParams[materialIndex][i]);
    float* paramCur = &materialDefinition->params[i];

    ImGui::SetNextItemWidth(256);

    if (ImGui::SliderFloat(format("{}##renderMaterialParams", paramName).c_str(), paramCur, paramMin, paramMax)) {
      update = true;
    }
  }

  ImGui::PopID();

  return update;
}

bool renderPrimitivePropertySet(bw::core::PrimitivePropertySet* properties, bool editable, editor::Document* doc, editor::Settings& settings) {
  bool updateProperties{false};

  ImGui::SetNextItemWidth(128);

  if (editable) {
    updateProperties |= ImGui::InputFloat("Floor Z", &properties->floorZ, 1, 8, "%2.1f", ImGuiInputTextFlags_EnterReturnsTrue);
  } else {
    ImGui::Text("Floor Z: %2.1f", properties->floorZ);
  }

  ImGui::SetNextItemWidth(128);

  if (editable) {
    updateProperties |= ImGui::InputFloat("Ceiling Z", &properties->ceilingZ, 1, 8, "%2.1f", ImGuiInputTextFlags_EnterReturnsTrue);
  } else {
    ImGui::Text("Ceiling Z: %2.1f", properties->ceilingZ);
  }

  // Materials
  string materialsStr;

  for (auto const& material : bw::common::MaterialNames) {
    materialsStr += get<0>(material);
    materialsStr += '\0';
  }

  // Floor material
  auto floorMaterialIndex = (int)properties->floorMaterialIndex;

  ImGui::SetNextItemWidth(256);

  if (editable) {
    if (ImGui::Combo("Floor material", &floorMaterialIndex, materialsStr.c_str(), 6)) {
      properties->floorMaterialIndex = (uint32_t)floorMaterialIndex;
      updateProperties = true;
    }
  } else {
    ImGui::Text("Floor material: %s", get<0>(bw::common::MaterialNames[floorMaterialIndex]).data());
  }

  if (editable) {
    updateProperties |= renderEditMaterialParameters("Floor", properties->floorMaterialIndex, &properties->floorMaterialDef.data);
  }

  // Ceiling material
  auto ceilingMaterialIndex = (int)properties->ceilingMaterialIndex;

  ImGui::SetNextItemWidth(256);

  if (editable) {
    if (ImGui::Combo("Ceiling material", &ceilingMaterialIndex, materialsStr.c_str(), 6)) {
      properties->ceilingMaterialIndex = (uint32_t)ceilingMaterialIndex;
      updateProperties = true;
    }
  } else {
    ImGui::Text("Ceiling material: %s", get<0>(bw::common::MaterialNames[ceilingMaterialIndex]).data());
  }

  if (editable) {
    updateProperties |= renderEditMaterialParameters("Ceiling", properties->ceilingMaterialIndex, &properties->ceilingMaterialDef.data);
  }

  // Wall material
  auto wallMaterialIndex = (int)properties->wallMaterialIndex;

  ImGui::SetNextItemWidth(256);

  if (editable) {
    if (ImGui::Combo("Wall material", &wallMaterialIndex, materialsStr.c_str(), 6)) {
      properties->wallMaterialIndex = (uint32_t)wallMaterialIndex;
      updateProperties = true;
    }
  } else {
    ImGui::Text("Wall material: %s", get<0>(bw::common::MaterialNames[wallMaterialIndex]).data());
  }

  if (editable) {
    updateProperties |= renderEditMaterialParameters("Walls", properties->wallMaterialIndex, &properties->wallMaterialDef.data);
  }

  return updateProperties;
}

void renderEditPrimitiveProperties(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  // Properties
  auto properties = primitive->getProperties();
  auto updateProperties = renderPrimitivePropertySet(&properties, true, doc, settings);

  // Update
  if (updateProperties) {
    transactUndoableAction(doc, "Update Primitive properties", [primitive, properties](editor::Document* doc) {
      primitive->setProperties(properties);
      return true;
    });
  }
}

void renderEditPrimitiveSettings(editor::Document* doc, editor::Settings& settings) {
  auto world = doc->getWorld();
  auto const& selectedIndices = doc->getSelectedPrimitiveIndices();

  switch (selectedIndices.size()) {
    case 0:
      break;

    case 1:
      renderEditPrimitiveSettings(doc, world->getPrimitive(*selectedIndices.begin()), settings);
      break;

    default:
      ImGui::Text("Multiple primitives selected.");
      break;
  }
}

void renderEditPrimitiveGeometry(editor::Document* doc, editor::Settings& settings, double globalTime) {
  auto world = doc->getWorld();
  auto const& selectedIndices = doc->getSelectedPrimitiveIndices();

  switch (selectedIndices.size()) {
    case 0:
      break;

    case 1:
      renderEditPrimitiveGeometry(doc, world->getPrimitive(*selectedIndices.begin()), settings, globalTime);
      break;

    default:
      ImGui::Text("Multiple primitives selected.");
      break;
  }
}

void renderEditPrimitiveProperties(editor::Document* doc, editor::Settings& settings) {
  auto world = doc->getWorld();
  auto const& selectedIndices = doc->getSelectedPrimitiveIndices();

  switch (selectedIndices.size()) {
    case 0:
      break;

    case 1:
      renderEditPrimitiveProperties(doc, world->getPrimitive(*selectedIndices.begin()), settings);
      break;

    default:
      ImGui::Text("Multiple primitives selected.");
      break;
  }
}

// Whether the Edit Primitive view has anything at all to show. Document's
// hasSelection() is not the question: it is also true for a TriggerLine or a
// world vertex. Nor is a selected ghost, which is authoring furniture with
// nothing to edit - and being index 0 it sorts first, so it is what the view
// would otherwise reach for.
bool hasEditablePrimitiveSelection(editor::Document* doc) {
  auto const& selectedIndices = doc->getSelectedPrimitiveIndices();

  return !selectedIndices.empty() &&
         *selectedIndices.begin() != uint32_t(ED_GHOST_INDEX);
}

void renderEditPrimitiveView(editor::Document* doc, editor::Settings& settings, double globalTime) {
  if (!hasEditablePrimitiveSelection(doc)) {
    return;
  }

  ImGui::SeparatorText("Settings");
  renderEditPrimitiveSettings(doc, settings);

  ImGui::SeparatorText("Geometry");
  renderEditPrimitiveGeometry(doc, settings, globalTime);

  ImGui::SeparatorText("Properties");
  renderEditPrimitiveProperties(doc, settings);
}

void renderPrefabView(editor::Document* doc, editor::Settings& settings) {
  auto world = doc->getWorld();

  // World settings
  vector<string> prefabTilingTypes = {
      "None",
      "Squares"};

  string prefabTilingTypesStr;

  for (auto const& prefabTilingType : prefabTilingTypes) {
    prefabTilingTypesStr += prefabTilingType;
    prefabTilingTypesStr += '\0';
  }

  int selectedTiling = (int)world->getPrefabAreaTilingType();

  widgets::HelpMarker("Tiling type to use for building prefab.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::Combo("Prefab tiling", &selectedTiling, prefabTilingTypesStr.c_str(), 6)) {
    world->setPrefabAreaTilingType((bw::core::PrefabAreaTilingType)selectedTiling);
  }

  // Prefab placement settings
  static int prefabTileX = 0, prefabTileY = 0;
  static int prefabRotation = 0;

  ImGui::SetNextItemWidth(96);
  if (ImGui::InputInt("Tile X", &prefabTileX, 1, 5, ImGuiInputTextFlags_EnterReturnsTrue)) {
    if (prefabTileX < -3) {
      prefabTileX = -3;
    }
    if (prefabTileX > 3) {
      prefabTileX = 3;
    }
  }

  ImGui::SameLine();

  ImGui::SetNextItemWidth(96);
  if (ImGui::InputInt("Tile Y", &prefabTileY, 1, 5, ImGuiInputTextFlags_EnterReturnsTrue)) {
    if (prefabTileY < -3) {
      prefabTileY = -3;
    }
    if (prefabTileY > 3) {
      prefabTileY = 3;
    }
  }

  ImGui::SameLine();

  ImGui::SetNextItemWidth(96);
  ImGui::Combo("Rotation", &prefabRotation, "North\0East\0South\0West\0\0", 4);

  // Layer
  static uint32_t prefabLayerId = world->getActiveLayer()->getId();

  widgets::HelpMarker("The Layer to place the prefab instance's Primitives and WorldTriggerLines on.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);
  prefabLayerId = widgets::LayerPicker("Layer##Prefab", world.get(), prefabLayerId);

  ImGui::Separator();

  // Loaded prefabs
  for (auto item : gPrefabInstances) {
    ImGui::PushID(item.first.c_str());

    auto const& [name, prefab] = item;

    ImGui::Text(name.c_str());
    ImGui::SameLine();

    bool enablePrefab = prefab->getPrefabAreaTilingType() == world->getPrefabAreaTilingType();

    if (!enablePrefab) {
      widgets::PushDisabled();
    }

    if (ImGui::Button("Create")) {
      float r = prefabRotation * 90.0f;
      auto* destinationLayer = world->getLayer(prefabLayerId);
      doc->addPrefabInstance(prefab, prefabTileX, prefabTileY, r, destinationLayer ? destinationLayer : world->getActiveLayer());
      generateClipping(doc, settings, ED_CLIP_ON_PREFAB_CREATE_DELETE);
    }

    if (!enablePrefab) {
      widgets::PopDisabled();
    }

    ImGui::PopID();
  }
}

void renderPrimitiveOrderView(editor::Document* doc, editor::Settings& settings) {
  auto const& selection = doc->getSelectedPrimitiveIndices();
  auto primitives = doc->getWorld()->getPrimitivesByPriority();
  auto numPrimitives = (uint32_t)primitives.size();

  for (uint32_t i = 0; i < numPrimitives; ++i) {
    ImGui::PushID(i);

    auto primitive = primitives[i];
    bool isGhost = primitive->getFlags() & BW_PRIMITIVE_GHOST_FLAG;
    bool selected = doc->indexInSelection(primitive->getId());

    if (!isGhost || settings.ghostActive) {
      if (ImGui::Button(ICON_FA_HAND_POINTER)) {
        auto selectId = primitive->getId();
        auto f = bind(editor::selectPrimitive, placeholders::_1, selectId);
        editor::transactUndoableAction(doc, format("Select Primitive {}", selectId), f);
      }
    }

    ImGui::SameLine();

    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1));
    }

    int primitivePriority = (int)primitive->getPriority();
    ImGui::Text("%s :: Priority %d", primitive->getName().c_str(), primitivePriority);

    if (!isGhost) {
      int counter = 0;
      ImGui::PushButtonRepeat(false);

      if (i > 0) {
        if (primitives[i - 1]->getPriority() != primitives[i]->getPriority()) {
          ImGui::SameLine();
          if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
            counter--;
          }
        }
      }

      if (i < (numPrimitives - 1)) {
        if (primitives[i]->getPriority() != primitives[i + 1]->getPriority()) {
          ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
          if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
            counter++;
          }
        }
      }

      ImGui::PopButtonRepeat();

      if (counter < 0) {
        auto prevPrim = primitives[i - 1];
        transactUndoableAction(doc, "Swap Primitive Priorities", [primitive, prevPrim](editor::Document* doc) {
          auto prevPriority = prevPrim->getPriority();
          prevPrim->setPriority(primitive->getPriority());
          primitive->setPriority(prevPriority);
          return true;
        });
      } else if (counter > 0) {
        auto nextPrim = primitives[i + 1];
        transactUndoableAction(doc, "Swap Primitive Priorities", [primitive, nextPrim](editor::Document* doc) {
          auto nextPriority = nextPrim->getPriority();
          nextPrim->setPriority(primitive->getPriority());
          primitive->setPriority(nextPriority);
          return true;
        });
      }

      if (i != 0) {
        setOperationWidget(doc, primitive, 2);
        setFillRuleWidget(doc, primitive, 2);
      }
    }

    if (selected) {
      ImGui::PopStyleColor();
    }

    ImGui::Separator();

    ImGui::PopID();
  }
}

void renderLayerStepsView(editor::Document* doc, editor::Settings& settings) {
  auto world = doc->getWorld();
  auto* layer = world->getActiveLayer();
  auto numSteps = layer->getNumSteps();

  widgets::HelpMarker("Disabling a step and rebuilding removes its Primitives from this Layer; re-enabling restores them. The first step can be disabled but never removed, retyped, or reordered. The active step (radio button) is where Create/Edit Primitive writes.");

  widgets::HelpMarker("When off, the world view only shows Primitives from the active step and earlier ones; Primitives from later steps are hidden, and contribute no geometry. Either way, Primitives outside the active step render faded.");
  ImGui::SameLine();
  if (ImGui::Checkbox("Show all steps' Primitives##Layer", &settings.showAllStepPrimitives)) {
    // The filter reads the setting live, so what changed here is only which
    // Primitives it now admits: regenerate unconditionally, since a view the
    // user just asked for is not something a config flag should gate.
    world->getWorldDataGenerator()->refreshPrimitiveFilter();
  }

  auto activeStepIndex = layer->getActiveStepIndex();

  for (uint32_t i = 0; i < numSteps; ++i) {
    ImGui::PushID(i);

    // A step list mutation below invalidates numSteps and every later
    // step's index for the rest of this frame, so this loop stops as soon
    // as one happens; the next frame renders the fresh list.
    bool listChanged = false;

    auto* step = layer->getStep(i);
    bool enabled = step->isEnabled();

    // Not undoable: like World's active Layer, a Layer's active step is
    // ephemeral editor-authoring focus, never serialized.
    if (ImGui::RadioButton("##StepActive", i == activeStepIndex)) {
      layer->setActiveStep(i);

      // The step filter is anchored on the active step, so moving it changes
      // which Primitives reach the fold.
      if (!settings.showAllStepPrimitives) {
        world->getWorldDataGenerator()->refreshPrimitiveFilter();
      }
    }
    widgets::HelpMarker("Make this the step Create/Edit Primitive writes into.");
    ImGui::SameLine();

    ImGui::Text("%u :: %s", i, step->getType().c_str());
    ImGui::SameLine();

    if (widgets::ToggleButton("##StepEnabled", "Enabled", &enabled)) {
      transactUndoableAction(doc, format("Toggle Layer Step {}", i), bind(setLayerBuildStepEnabled, placeholders::_1, layer, i, enabled));
    }

    if (i != 0) {
      ImGui::PushButtonRepeat(false);

      ImGui::SameLine();
      // Moving step 1 up would move it into the reserved index 0, so that
      // arrow is omitted rather than offered and rejected.
      if (i > 1) {
        if (ImGui::ArrowButton("##StepUp", ImGuiDir_Up)) {
          transactUndoableAction(doc, format("Move Layer Step {}", i), bind(moveLayerBuildStep, placeholders::_1, layer, i, i - 1));
          listChanged = true;
        }
        ImGui::SameLine();
      }

      if (!listChanged && i < numSteps - 1) {
        if (ImGui::ArrowButton("##StepDown", ImGuiDir_Down)) {
          transactUndoableAction(doc, format("Move Layer Step {}", i), bind(moveLayerBuildStep, placeholders::_1, layer, i, i + 1));
          listChanged = true;
        }
        ImGui::SameLine();
      }

      ImGui::PopButtonRepeat();

      if (!listChanged && ImGui::Button("Remove")) {
        transactUndoableAction(doc, format("Remove Layer Step {}", i), bind(removeLayerBuildStep, placeholders::_1, layer, i));
        listChanged = true;
      }
    }

    ImGui::Separator();

    ImGui::PopID();

    if (listChanged) {
      break;
    }
  }

  if (ImGui::Button("Add PrimitiveField Step")) {
    transactUndoableAction(doc, "Add Layer Step", bind(addLayerBuildStep, placeholders::_1, layer));
  }
}

void renderCreateTriggerLineView(editor::Document* doc, editor::Settings& settings) {
  auto world = doc->getWorld();

  // Layer
  static uint32_t createTriggerLineLayerId = world->getActiveLayer()->getId();

  widgets::HelpMarker("The Layer to place the WorldTriggerLine on.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);
  createTriggerLineLayerId = widgets::LayerPicker("Layer##CreateTriggerLine", world.get(), createTriggerLineLayerId);

  if (ImGui::Button("Create at ghost##CreateTriggerLine")) {
    auto const targetLayerId = createTriggerLineLayerId;
    transactUndoableAction(doc, "Create Trigger Line##CreateTriggerLine", [world, targetLayerId](editor::Document* doc) {
      auto ghost = world->getPrimitive(0);
      auto triggerLine = new bw::core::WorldTriggerLine(ghost->getPosition() - wp::Vector2(100, 0), ghost->getPosition() + wp::Vector2(100, 0));

      world->addTriggerLine(triggerLine);

      if (targetLayerId != world->getActiveLayer()->getId()) {
        if (auto* destinationLayer = world->getLayer(targetLayerId)) {
          world->moveTriggerLineToLayer(triggerLine, destinationLayer);
        }
      }

      return true;
    });
  }
}

void renderEditTriggerLineView(editor::Document* doc, editor::Settings& settings, uint32_t triggerLineIndex) {
  auto world = doc->getWorld();
  auto triggerLine = world->getTriggerLine(triggerLineIndex);

  // Layer
  auto currentLayerId = world->getActiveLayer()->getId();

  widgets::HelpMarker("Move the WorldTriggerLine to a different Layer.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);
  auto const pickedLayerId = widgets::LayerPicker("Layer##EditTriggerLine", world.get(), currentLayerId);

  if (pickedLayerId != currentLayerId) {
    if (auto* destinationLayer = world->getLayer(pickedLayerId)) {
      transactUndoableAction(doc, "Move Trigger Line to Layer", [triggerLine, destinationLayer](editor::Document* doc) {
        doc->getWorld()->moveTriggerLineToLayer(triggerLine, destinationLayer);
        doc->clearSelections();
        return true;
      });
    }
  }

  ImGui::Text("ID: %d", triggerLine->getId());

  ImGui::SeparatorText("Trigger counts");
  ImGui::TextColored(settings.triggerLineRed, "RED: %d", triggerLine->getTriggerCount(bw::core::WorldTriggerLineSide::Red));
  ImGui::TextColored(settings.triggerLineBlue, "BLUE: %d", triggerLine->getTriggerCount(bw::core::WorldTriggerLineSide::Blue));

  // Side
  int selectedSide = (int)triggerLine->getSide();

  widgets::HelpMarker("Which side(s) trigger on crossing.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::Combo("Side##EditTriggerLine", &selectedSide, "Red\0Blue\0Both\0\0", 6)) {
    triggerLine->setSide((bw::core::WorldTriggerLineSide)selectedSide);
  }
}

void renderArrangementFaceView(
    editor::Document* doc,
    editor::Settings& settings,
    bw::core::ArrangementWorldData const* worldData) {
  if (!worldData) {
    return;
  }
  auto faceIndex = worldData->getContainingFaceIndex(getMouseWorldPosition());
  if (faceIndex == ~0u) {
    ImGui::Text("Exterior");
    return;
  }
  auto const& arrangement = worldData->getArrangement();
  auto const& face = arrangement.faces[faceIndex];
  ImGui::Text("Face: %u", faceIndex);
  ImGui::Text("Primitive: %u", face.primitiveIndex);
  auto properties = arrangement.palette[face.paletteIndex];
  ImGui::Text(
      "Floor / ceiling: %.2f / %.2f",
      properties.floorZ,
      properties.ceilingZ);
  renderPrimitivePropertySet(&properties, false, doc, settings);
}

void renderConfigView(editor::Document* doc, editor::Settings& settings) {
  auto world = doc->getWorld();
  auto wdg = static_cast<bw::core::DynamicWorldDataGenerator*>(world->getWorldDataGenerator());

  if (ImGui::TreeNode("Options")) {
    int* configFlags = &settings.configFlags;

    ImGui::SeparatorText("Auto clipping");

    ImGui::CheckboxFlags("On Primitive transform end", configFlags, ED_CLIP_ON_PRIM_TRANSFORM_END);
    ImGui::CheckboxFlags("On Primitive create/delete", configFlags, ED_CLIP_ON_PRIM_CREATE_DELETE);
    ImGui::CheckboxFlags("On Primitive setting change", configFlags, ED_CLIP_ON_PRIM_SETTING_CHANGE);
    ImGui::CheckboxFlags("On active layer change", configFlags, ED_CLIP_ON_ACTIVE_LAYER_CHANGE);
    ImGui::CheckboxFlags("On prefab create/delete", configFlags, ED_CLIP_ON_PREFAB_CREATE_DELETE);
    ImGui::CheckboxFlags("On undo/redo", configFlags, ED_CLIP_ON_UNDO_REDO);

    ImGui::SeparatorText("Primitive vertices");

    int updateVertices = 0;

    if (wdg->getAlwaysUpdateVertices()) {
      updateVertices = 1;
    } else if (world->getAlwaysUpdateVertices()) {
      updateVertices = 2;
    }

    ImGui::SetNextItemWidth(256);

    if (ImGui::Combo("Position update", &updateVertices, "When not visible on clip\0Always on clip\0Every frame\0\0", 6)) {
      switch (updateVertices) {
        case 0:
          wdg->setAlwaysUpdateVertices(false);
          world->setAlwaysUpdateVertices(false);
          break;

        case 1:
          wdg->setAlwaysUpdateVertices(true);
          world->setAlwaysUpdateVertices(false);
          break;

        case 2:
          wdg->setAlwaysUpdateVertices(false);
          world->setAlwaysUpdateVertices(true);
          break;
      }
    }

    ImGui::TreePop();
    // ImGui::Spacing();
  }

  float intervalSchedule = wdg->getScheduledGenerationInterval();

  ImGui::SetNextItemWidth(128);
  if (ImGui::InputFloat("Generation interval", &intervalSchedule)) {
    if (intervalSchedule >= 1.0f) {
      wdg->setScheduledGenerationInterval(intervalSchedule);
    }
  }

  ImGui::SameLine();

  bool scheduledGenRunning = wdg->isScheduledGenerationRunning();

  if (widgets::ToggleButton("ToggleScheduledGeneration", ICON_FA_ATOM, &scheduledGenRunning)) {
    if (scheduledGenRunning) {
      wdg->startGenerationSchedule(intervalSchedule);
    } else {
      wdg->stopGenerationSchedule();
    }
  }
}

void renderConfigPanel(editor::Document* doc, editor::Settings& settings) {
  if (ImGui::Begin("Configuration")) {
    renderConfigView(doc, settings);
  }

  ImGui::End();
}

void renderHistoryView(editor::Document* doc, editor::Settings& settings) {
  auto history = getActionHistory();

  if (history.empty()) {
    return;
  }

  // Get number of undos and redos ahead of time
  uint32_t undoCount = 0;
  for (; undoCount < (uint32_t)history.size(); ++undoCount) {
    if (!history[undoCount].isUndo) {
      break;
    }
  }

  uint32_t redoCount = (uint32_t)history.size() - undoCount;

  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.7f, 0.7f, 1));

  for (uint32_t i = 0; i < (uint32_t)history.size(); ++i) {
    auto const& item = history[i];
    bool isUndo = i < undoCount;

    ImGui::PushID(i);

    if (i == undoCount) {
      ImGui::Separator();

      ImGui::PopStyleColor();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 1.0f, 0.7f, 1));
    }

    if (ImGui::Button(isUndo ? ICON_FA_UNDO : ICON_FA_REDO)) {
      // Work out how many to go back or forward
      if (isUndo) {
        undo(doc, undoCount - i);
      } else {
        redo(doc, (i + 1) - undoCount);
      }
    }

    ImGui::SameLine();
    ImGui::Text(item.id.c_str());

    ImGui::PopID();
  }

  ImGui::PopStyleColor();
}

void renderHistoryPanel(editor::Document* doc, editor::Settings& settings) {
  if (ImGui::Begin("History")) {
    renderHistoryView(doc, settings);
  }

  ImGui::End();
}

void renderCombinedPanel(
    editor::Document* doc,
    editor::Settings& settings,
    bw::core::WorldData const* worldData,
    double globalTime) {
  if (!doc->isActive()) {
    return;
  }

  if (ImGui::Begin("Editing")) {
    auto windowFlags = 0;

    if (ImGui::CollapsingHeader("World", nullptr, windowFlags)) {
      renderWorldView(doc, settings);
    }

    if (ImGui::CollapsingHeader("Prefabs", nullptr, windowFlags)) {
      renderPrefabView(doc, settings);
    }

    if (ImGui::CollapsingHeader("Layer", nullptr, windowFlags)) {
      renderLayerStepsView(doc, settings);
    }

    // Below Layer: creating a Primitive writes into the active Layer's active
    // step, so the choice of where comes before the making of what, and
    // editing one comes after both.
    if (ImGui::CollapsingHeader("Create Primitive", nullptr, windowFlags)) {
      renderCreatePrimitiveView(doc, settings);
    }

    // The header follows the view: no header where the view would have
    // nothing under it.
    if (hasEditablePrimitiveSelection(doc)) {
      if (ImGui::CollapsingHeader("Edit Primitive", nullptr, windowFlags)) {
        renderEditPrimitiveView(doc, settings, globalTime);
      }
    }

    if (ImGui::CollapsingHeader("Clip Order", nullptr, windowFlags)) {
      renderPrimitiveOrderView(doc, settings);
    }

    if (ImGui::CollapsingHeader("Create Trigger Line", nullptr, windowFlags)) {
      renderCreateTriggerLineView(doc, settings);
    }

    auto selectedTriggerLineIndex = doc->getSelectedTriggerLineIndex();

    if (selectedTriggerLineIndex != ~0u) {
      if (ImGui::CollapsingHeader("Edit Trigger Line", nullptr, windowFlags)) {
        renderEditTriggerLineView(doc, settings, selectedTriggerLineIndex);
      }
    }

    if (ImGui::CollapsingHeader("Region under cursor", nullptr, windowFlags)) {
      renderArrangementFaceView(doc, settings, worldData);
    }

    if (!getActionHistory().empty()) {
      if (ImGui::CollapsingHeader("History", nullptr, windowFlags)) {
        renderHistoryView(doc, settings);
      }
    }

    if (ImGui::CollapsingHeader("Configuration", nullptr, windowFlags)) {
      renderConfigView(doc, settings);
    }
  }

  ImGui::End();
}

void handleShortcuts(editor::Document* doc, editor::Settings& settings) {
  if (ImGui::Shortcut(ImGuiKey_N | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      handleModifiedDocument(doc, true, true, "New world", newDocument);
    }
  }

  checkModifiedOperation(doc, "New world", newDocument);

  if (ImGui::Shortcut(ImGuiKey_O | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      handleModifiedDocument(doc, true, true, "Open world", openDocument);
    }
  }

  checkModifiedOperation(doc, "Open world", openDocument);

  if (ImGui::Shortcut(ImGuiKey_S | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      handleModifiedDocument(doc, true, false, "Save world", saveDocument);
    }
  }

  checkModifiedOperation(doc, "Save world", saveDocument);

  if (ImGui::Shortcut(ImGuiKey_Y | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      if (canRedo()) {
        redo(doc);
      }
    }
  }

  if (ImGui::Shortcut(ImGuiKey_Z | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      if (canUndo()) {
        undo(doc);
      }
    }
  }

  if (ImGui::Shortcut(ImGuiKey_C | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      if (doc->hasSelection() && !doc->getSelectedPrimitiveIndices().empty()) {
        auto const& indices = doc->getSelectedPrimitiveIndices();
        uint32_t index = *indices.begin();

        transactUndoableAction(doc, format("Clone Primitive {}", index), bind(clonePrimitive, placeholders::_1, index));
      }
    }
  }

  if (ImGui::Shortcut(ImGuiKey_Delete, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      if (doc->hasSelection()) {
        auto const& primitiveIndices = doc->getSelectedPrimitiveIndices();

        if (!primitiveIndices.empty()) {
          transactUndoableAction(doc, format("Delete {} Primitive(s)", primitiveIndices.size()), bind(deletePrimitives, placeholders::_1, primitiveIndices));
          generateClipping(doc, settings, ED_CLIP_ON_PRIM_CREATE_DELETE);
        }

        auto triggerLineIndex = doc->getSelectedTriggerLineIndex();

        if (triggerLineIndex != ~0u) {
          transactUndoableAction(doc, "Delete TriggerLine", bind(deleteTriggerLine, placeholders::_1, triggerLineIndex));
        }
      }
    }
  }

  if (ImGui::Shortcut(ImGuiKey_LeftBracket | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      if (doc->hasSelection() && !doc->getSelectedPrimitiveIndices().empty()) {
        auto const& indices = doc->getSelectedPrimitiveIndices();
        uint32_t index = *indices.begin();

        auto prim = doc->getWorld()->getPrimitive(index);
        transactUndoableAction(doc, format("Decrease Primitive priority", index), bind(decreasePrimitivePriority, placeholders::_1, prim));
      }
    }
  }

  if (ImGui::Shortcut(ImGuiKey_RightBracket | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      if (doc->hasSelection() && !doc->getSelectedPrimitiveIndices().empty()) {
        auto const& indices = doc->getSelectedPrimitiveIndices();
        uint32_t index = *indices.begin();

        auto prim = doc->getWorld()->getPrimitive(index);
        transactUndoableAction(doc, format("Increase Primitive priority", index), bind(increasePrimitivePriority, placeholders::_1, prim));
      }
    }
  }

  if (ImGui::Shortcut(ImGuiKey_B | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      auto const& indices = doc->getSelectedPrimitiveIndices();
      transactUndoableAction(doc, format("Bake {} Primitive(s)", indices.size()), bind(bakePrimitives, placeholders::_1, indices));
    }
  }

  if (ImGui::Shortcut(ImGuiKey_G, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      settings.showGrid = !settings.showGrid;
    }
  }

  if (ImGui::Shortcut(ImGuiKey_H, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      goHome(nullptr);
    }
  }

  if (ImGui::Shortcut(ImGuiKey_M, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      settings.renderMiniMap = !settings.renderMiniMap;
    }
  }

  if (ImGui::Shortcut(ImGuiKey_G | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused() && doc->isActive()) {
      settings.ghostActive = !settings.ghostActive;
      enableGhost(doc, settings.ghostActive);
    }
  }

  if (ImGui::Shortcut(ImGuiKey_G | ImGuiMod_Shift, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused() && doc->isActive()) {
      selectAndHomeGhost(doc);
    }
  }

  if (ImGui::Shortcut(ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      auto const& selected = doc->getSelectedPrimitiveIndices();

      if (selected.size() == 1) {
        goHome(doc->getWorld()->getPrimitive(*selected.begin()));
      }
    }
  }

  if (ImGui::Shortcut(ImGuiKey_P, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      doc->getWorld()->generateClipping(true);
    }
  }

  if (ImGui::Shortcut(ImGuiKey_F1, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      ImGui::OpenPopup("Help");
    }
  }

  if (ImGui::Shortcut(ImGuiKey_F3, ImGuiInputFlags_RouteGlobal)) {
    settings.renderPlayerView = !settings.renderPlayerView;
  }

  if (ImGui::Shortcut(ImGuiKey_F4, ImGuiInputFlags_RouteGlobal)) {
    settings.renderPrimitiveBorders = !settings.renderPrimitiveBorders;
  }

  if (ImGui::Shortcut(ImGuiKey_F5, ImGuiInputFlags_RouteGlobal)) {
    settings.renderPrimitiveBounds = !settings.renderPrimitiveBounds;
  }

  if (ImGui::Shortcut(ImGuiKey_F6, ImGuiInputFlags_RouteGlobal)) {
    settings.renderInfluenceEyes = !settings.renderInfluenceEyes;
  }

  if (ImGui::Shortcut(ImGuiKey_F7, ImGuiInputFlags_RouteGlobal)) {
    settings.renderTriggerLines = !settings.renderTriggerLines;
  }

  if (ImGui::Shortcut(ImGuiKey_F8, ImGuiInputFlags_RouteGlobal)) {
    settings.renderArrangementVertices = !settings.renderArrangementVertices;
  }

  if (ImGui::Shortcut(ImGuiKey_F10, ImGuiInputFlags_RouteGlobal)) {
    settings.showContextSensitiveHelpPanel = !settings.showContextSensitiveHelpPanel;
  }

  if (ImGui::Shortcut(ImGuiKey_F11, ImGuiInputFlags_RouteGlobal)) {
    settings.expertMode = !settings.expertMode;
  }

  if (ImGui::Shortcut(ImGuiKey_R, ImGuiInputFlags_RouteGlobal)) {
    if (doc->getWorld()) {
      resetAnimatorCaptures(doc);
    }
  }

  if (ImGui::Shortcut(ImGuiKey_C, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      transactUndoableAction(doc, format("Create {} Primitive", doc->getGhost()->getType()), createPrimitiveFromGhost);
      generateClipping(doc, settings, ED_CLIP_ON_PRIM_CREATE_DELETE);
    }
  }
}

void handleMouseInteraction(editor::Document* doc, editor::Settings& settings) {
  // Right mouse button
  if (ImGui::IsMouseDown(1)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      auto worldPos = getMouseWorldPosition();
      doc->setPlayerProxyPosition(worldPos);

      auto angle = doc->getPlayerProxyAngle();
      if (ImGui::IsKeyDown(ImGuiKey_Q)) {
        angle -= 1.0f;
        if (angle < 0.0f) {
          angle += 360.0f;
        }
        doc->setPlayerProxyAngle(angle);
      }
      if (ImGui::IsKeyDown(ImGuiKey_W)) {
        angle += 1.0f;
        if (angle >= 360.0f) {
          angle -= 360.0f;
        }
        doc->setPlayerProxyAngle(angle);
      }
    }
  }
}

void checkModalPopups(editor::Document* doc, editor::Settings& settings) {
  ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
  auto& primitiveFieldPreview = getPrimitiveFieldPreview();
  constexpr char PrimitiveFieldPopupTitle[] = "Generate Primitive Field\u2026";

  if (primitiveFieldPreview.openRequested) {
    ImGui::OpenPopup(PrimitiveFieldPopupTitle);
    primitiveFieldPreview.openRequested = false;
  }

  ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  bool keepPrimitiveFieldOpen = primitiveFieldPreview.open;
  if (ImGui::BeginPopupModal(
          PrimitiveFieldPopupTitle, &keepPrimitiveFieldOpen,
          ImGuiWindowFlags_AlwaysAutoResize)) {
    auto world = doc->getWorld();
    if (!world) {
      ImGui::TextUnformatted("The document world is no longer available.");
      if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
        primitiveFieldPreview.close();
      }
    } else {
      wp::Vector2 worldMinimum;
      wp::Vector2 worldMaximum;
      world->getExtents().getExtents(worldMinimum, worldMaximum);
      primitiveFieldPreview.poll(
          {worldMinimum, worldMaximum}, world->getNumPrimitives());

      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::DragFloat(
              "Minimum site spacing", &primitiveFieldPreview.minimumSpacing,
              1.0f, 0.0f, 0.0f, "%.3f")) {
        primitiveFieldPreview.invalidateLayout();
      }

      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::InputInt(
              "Requested batch maximum",
              &primitiveFieldPreview.maximumSites)) {
        primitiveFieldPreview.invalidateLayout();
      }

      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::InputInt("Lloyd iterations",
                          &primitiveFieldPreview.lloydIterations)) {
        primitiveFieldPreview.invalidateLayout();
      }

      ImGui::TextUnformatted("Primitive types");
      auto typeCheckbox = [&](char const* label, bool& enabled) {
        if (ImGui::Checkbox(label, &enabled)) {
          primitiveFieldPreview.refreshPrimitives();
        }
      };
      typeCheckbox("Rectangle", primitiveFieldPreview.enabledTypes.rectangle);
      typeCheckbox("Triangle", primitiveFieldPreview.enabledTypes.triangle);
      typeCheckbox("Pentagon", primitiveFieldPreview.enabledTypes.pentagon);
      typeCheckbox("Hexagon", primitiveFieldPreview.enabledTypes.hexagon);
      typeCheckbox("Circle", primitiveFieldPreview.enabledTypes.circle);

      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::DragFloat(
              "Cell occupancy (%)", &primitiveFieldPreview.occupancyPercent,
              0.25f, 0.0f, 100.0f, "%.2f")) {
        primitiveFieldPreview.refreshPrimitives();
      }
      ImGui::TextDisabled("The cell at 0, 0 is always occupied.");

      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::DragFloat(
              "Hole chance (%)", &primitiveFieldPreview.holeChancePercent,
              0.25f, 0.0f, 100.0f, "%.2f")) {
        primitiveFieldPreview.refreshPrimitives();
      }
      ImGui::TextDisabled(
          "Occupied non-origin cells may receive a half-size Difference polygon.");

      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::DragFloat(
              "Overlap (%)", &primitiveFieldPreview.overlapPercent, 0.25f,
              0.0f, 100.0f, "%.2f")) {
        primitiveFieldPreview.refreshPrimitives();
      }

      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::InputInt("Seed", &primitiveFieldPreview.seed)) {
        primitiveFieldPreview.invalidateLayout();
      }
      ImGui::SameLine();
      if (ImGui::Button("Randomize")) {
        static std::random_device randomDevice;
        auto randomizedSeed = static_cast<int32_t>(randomDevice());
        if (randomizedSeed == primitiveFieldPreview.seed) {
          randomizedSeed = randomizedSeed == std::numeric_limits<int32_t>::max()
                               ? std::numeric_limits<int32_t>::min()
                               : randomizedSeed + 1;
        }
        primitiveFieldPreview.seed = randomizedSeed;
        primitiveFieldPreview.invalidateLayout();
      }

      auto controls = primitiveFieldPreview.evaluateControls(
          {worldMinimum, worldMaximum}, world->getNumPrimitives());
      auto generatedCount = primitiveFieldPreview.layout
                                ? primitiveFieldPreview.layout->sites.size()
                                : size_t{0};
      ImGui::Separator();
      if (controls.valid()) {
        ImGui::Text("Approximate uncapped sites: %llu",
                    static_cast<unsigned long long>(
                        controls.approximateUncappedSites));
      } else {
        ImGui::TextUnformatted("Approximate uncapped sites: unavailable");
      }
      ImGui::Text("Requested batch maximum: %d",
                  primitiveFieldPreview.maximumSites);
      ImGui::Text("Remaining world capacity: %u (authored primitives and ghost included)",
                  controls.remainingWorldCapacity);
      ImGui::Text("Effective placement cap: %u",
                  controls.effectivePlacementCap);
      ImGui::Text("Generated cells: %zu", generatedCount);
      auto holeCount = static_cast<size_t>(std::count_if(
          primitiveFieldPreview.primitives.begin(),
          primitiveFieldPreview.primitives.end(),
          [](PrimitiveFieldPrimitivePreview const& primitive) {
            return primitive.isHole;
          }));
      ImGui::Text("Occupied cells: %zu",
                  primitiveFieldPreview.primitives.size() - holeCount);
      ImGui::Text("Hole primitives: %zu", holeCount);
      ImGui::Text("Primitives to place: %zu",
                  primitiveFieldPreview.primitives.size());

      auto expensiveRequest = controls.approximateUncappedSites > 2000 ||
                              controls.effectivePlacementCap > 2000 ||
                              generatedCount > 2000;
      if (expensiveRequest) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.7f, 0.15f, 1.0f),
            "Performance warning: counts above 2,000 may take substantial time and memory.");
      }
      if (primitiveFieldPreview.layout &&
          primitiveFieldPreview.layout->samplingStoppedAtMaximum) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.7f, 0.15f, 1.0f),
            "Capped: center-outward sampling stopped at the effective placement cap.");
      }
      if (primitiveFieldPreview.primitives.size() >
          controls.remainingWorldCapacity) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
            "The occupied cells and holes require more primitive slots than remain.");
      }

      auto generationActive = primitiveFieldPreview.isGenerating();
      if (!controls.valid()) ImGui::BeginDisabled();
      if (ImGui::Button("Generate Layout")) {
        primitiveFieldPreview.generate(
            {worldMinimum, worldMaximum}, world->getNumPrimitives());
        generationActive = primitiveFieldPreview.isGenerating();
      }
      if (!controls.valid()) ImGui::EndDisabled();

      if (generationActive) {
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
          primitiveFieldPreview.cancelGeneration();
          generationActive = false;
        }
      }

      ImGui::SameLine();
      auto canPlace = controls.valid() &&
                      primitiveFieldPreview.hasCompletePreview() &&
                      !primitiveFieldPreview.primitives.empty() &&
                      primitiveFieldPreview.primitives.size() <=
                          controls.remainingWorldCapacity &&
                      !generationActive;
      if (!canPlace) ImGui::BeginDisabled();
      if (ImGui::Button("Place Primitives") && primitiveFieldPreview.layout) {
        primitiveFieldPreview.beginPlacement();
        auto result = placePrimitiveField(
            doc, *primitiveFieldPreview.layout,
            primitiveFieldPreview.primitives, settings);
        primitiveFieldPreview.finishPlacement(result.placed, result.error);
        if (result.placed) {
          ImGui::CloseCurrentPopup();
          primitiveFieldPreview.close();
        }
      }
      if (!canPlace) ImGui::EndDisabled();

      if (generationActive) {
        auto phaseLabel = "Sampling sites";
        switch (primitiveFieldPreview.generationPhase()) {
          case bw::core::PrimitiveFieldLayoutPhase::Sampling:
            phaseLabel = "Sampling sites";
            break;
          case bw::core::PrimitiveFieldLayoutPhase::LloydRelaxation:
            phaseLabel = "Lloyd relaxation";
            break;
          case bw::core::PrimitiveFieldLayoutPhase::VoronoiConstruction:
            phaseLabel = "Constructing bounded Voronoi cells";
            break;
          case bw::core::PrimitiveFieldLayoutPhase::Validation:
            phaseLabel = "Validating layout";
            break;
          case bw::core::PrimitiveFieldLayoutPhase::Complete:
            phaseLabel = "Layout complete";
            break;
        }
        ImGui::ProgressBar(
            primitiveFieldPreview.generationProgress(), ImVec2(360.0f, 0.0f),
            phaseLabel);
      }

      char const* workflowStatus = "Idle — configure controls, then generate a layout.";
      switch (primitiveFieldPreview.state) {
        case PrimitiveFieldWorkflowState::Idle:
          break;
        case PrimitiveFieldWorkflowState::Generating:
          workflowStatus = "Generating layout…";
          break;
        case PrimitiveFieldWorkflowState::CurrentPreview:
          workflowStatus = primitiveFieldPreview.primitives.empty()
                               ? "Preview is current, but no cells were selected for placement."
                               : "Preview is current and ready to place.";
          break;
        case PrimitiveFieldWorkflowState::StalePreview:
          workflowStatus = "Preview is stale; generate the layout again.";
          break;
        case PrimitiveFieldWorkflowState::Cancelled:
          workflowStatus = "Generation was cancelled; document unchanged.";
          break;
        case PrimitiveFieldWorkflowState::Failed:
          workflowStatus = "Request failed; previous preview and document preserved.";
          break;
        case PrimitiveFieldWorkflowState::Placing:
          workflowStatus = "Placing primitives…";
          break;
        case PrimitiveFieldWorkflowState::NoCapacity:
          workflowStatus = "No capacity remains; delete primitives to continue.";
          break;
      }
      ImGui::TextWrapped("Status: %s", workflowStatus);

      if (!controls.valid() && primitiveFieldPreview.error.empty()) {
        ImGui::PushStyleColor(
            ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s", controls.error.c_str());
        ImGui::PopStyleColor();
      }
      if (!primitiveFieldPreview.error.empty()) {
        ImGui::PushStyleColor(
            ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s", primitiveFieldPreview.error.c_str());
        ImGui::PopStyleColor();
      }

      auto const* closeLabel = primitiveFieldPreview.layout ? "Close" : "Cancel";
      if (ImGui::Button(closeLabel, ImVec2(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
        primitiveFieldPreview.close();
      }
    }

    ImGui::EndPopup();
  }
  if (!keepPrimitiveFieldOpen && primitiveFieldPreview.open) {
    primitiveFieldPreview.close();
  }

  //
  // Open file failed
  //
  ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  if (ImGui::BeginPopupModal("Open file failed", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Load failed!  See error log.");

    if (ImGui::Button("OK", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  //
  // Help / instructions
  //
  ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(ED_WINDOW_WIDTH - 200, ED_WINDOW_HEIGHT - 200));

  if (ImGui::BeginPopupModal("Help", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
    renderHelp();
    ImGui::EndPopup();
  }
}

void renderDebug(
    editor::Document* doc,
    editor::Settings& settings,
    bw::core::WorldData const* worldData,
    double globalTime) {
  auto windowFlags = 0;

  if (ImGui::Begin("Debug")) {
    if (ImGui::CollapsingHeader("Transform", nullptr, windowFlags)) {
      auto const& proxyPos = doc->getPlayerProxyPosition();
      auto proxyAngle = doc->getPlayerProxyAngle();

      ImGui::Text("Proxy position: %3.2f, %3.2f", proxyPos.x, proxyPos.y);
      ImGui::Text("Proxy angle: %3.2f", proxyAngle);

      if (doc->isActive()) {
        auto const& selection = doc->getSelectedPrimitiveIndices();

        if (selection.size() == 1) {
          auto world = doc->getWorld();

          auto primitive = world->getPrimitive(*selection.begin());
          auto const& influencePos = primitive->getInfluenceEyeOriginPosition();

          ImGui::Text("Eye position: %3.2f, %3.2f", influencePos.x, influencePos.y);

          auto const& inputs = primitive->getInputs();

          ImGui::Text("Influence dist: %3.2f", inputs.entityInfluenceDistance);
          ImGui::Text("Influence angle: %3.2f", inputs.entityInfluenceAngle);
          ImGui::Text("Entity angle: %3.2f", inputs.entityGlobalAngle);

          auto scaleOut = primitive->transformT(bw::core::VertexTransformer::Key::Scale, globalTime);
          auto angleOut = primitive->transformT(bw::core::VertexTransformer::Key::Angle, globalTime);
          auto orbitAngleOut = primitive->transformT(bw::core::VertexTransformer::Key::OrbitAngle, globalTime);
          auto orbitDistOut = primitive->transformT(bw::core::VertexTransformer::Key::OrbitDistance, globalTime);

          ImGui::Text("Trans scale out: %3.2f", scaleOut);
          ImGui::Text("Trans angle out: %3.2f", angleOut);
          ImGui::Text("Trans orbit angle out: %3.2f", orbitAngleOut);
          ImGui::Text("Trans orbit dist out: %3.2f", orbitDistOut);

          auto scaleAnim = primitive->getAnimationInterpolator(bw::core::VertexTransformer::Key::Scale).getValue(scaleOut);
          auto angleAnim = primitive->getAnimationInterpolator(bw::core::VertexTransformer::Key::Angle).getValue(angleOut);
          auto orbitAngleAnim = primitive->getAnimationInterpolator(bw::core::VertexTransformer::Key::OrbitAngle).getValue(orbitAngleOut);
          auto orbitDistAnim = primitive->getAnimationInterpolator(bw::core::VertexTransformer::Key::OrbitDistance).getValue(orbitDistOut);

          ImGui::Text("Anim scale: %3.2f", scaleAnim);
          ImGui::Text("Anim angle: %3.2f", angleAnim);
          ImGui::Text("Anim orbit angle: %3.2f", orbitAngleAnim);
          ImGui::Text("Anim orbit dist: %3.2f", orbitDistAnim);

          float infl = BW_INTERPOLATOR_MAX_DISTANCE - primitive->getInputs().entityInfluenceDistance;

          auto scaleInfl = scaleAnim * primitive->getInfluenceInterpolator(bw::core::VertexTransformer::Key::Scale).getValue(infl);
          auto angleInfl = angleAnim * primitive->getInfluenceInterpolator(bw::core::VertexTransformer::Key::Angle).getValue(infl);
          auto orbitAngleInfl = orbitAngleAnim * primitive->getInfluenceInterpolator(bw::core::VertexTransformer::Key::OrbitAngle).getValue(infl);
          auto orbitDistInfl = orbitDistAnim * primitive->getInfluenceInterpolator(bw::core::VertexTransformer::Key::OrbitDistance).getValue(infl);

          ImGui::Text("Influenced scale: %3.2f", scaleInfl);
          ImGui::Text("Influenced angle: %3.2f", angleInfl);
          ImGui::Text("Influenced orbit angle: %3.2f", orbitAngleInfl);
          ImGui::Text("Influenced orbit dist: %3.2f", orbitDistInfl);
        }
      }
    }

    if (doc->isActive() && worldData) {
      if (ImGui::CollapsingHeader("Arrangement face", nullptr, windowFlags)) {
        ImGui::Text(
            "Total vertices: %u",
            uint32_t(worldData->getArrangement().vertices.size()));
        renderArrangementFaceView(doc, settings, worldData);
      }
    }
  }

  ImGui::End();
}

void renderContextSensitiveHelp(editor::Document* doc, editor::Settings& settings) {
  auto windowFlags = 0;

  if (ImGui::Begin("Context Help")) {
    if (doc->isActive()) {
      auto const& io = ImGui::GetIO();

      auto hoveredPrimitiveIndex = editor::getHoveredPrimitiveIndex(doc, settings);
      auto const& selectedPrimitiveIndices = doc->getSelectedPrimitiveIndices();

      //
      // Left mouse button
      //
      string transformAction;

      int modifierMask = (io.KeyCtrl ? 1 : 0) + (io.KeyShift ? 2 : 0) + (io.KeyAlt ? 4 : 0);
      switch (modifierMask) {
        case 0:
          if (hoveredPrimitiveIndex != ~0u) {
            transformAction = "select primitive";
          }
          break;
        case 1:
          if (hoveredPrimitiveIndex != ~0u) {
            if (selectedPrimitiveIndices.find(hoveredPrimitiveIndex) != selectedPrimitiveIndices.end()) {
              transformAction = "deselect primitive";
            } else {
              transformAction = "select primitive";
            }
          }
          break;
        case 2:
          if ((hoveredPrimitiveIndex != ~0u) || !selectedPrimitiveIndices.empty()) {
            transformAction = "scale primitive(s)";
          }
          break;
        case 3:
          break;
        case 4:
          if ((hoveredPrimitiveIndex != ~0u) || !selectedPrimitiveIndices.empty()) {
            transformAction = "rotate primitive(s)";
          }
          break;
        case 5:
          break;
        case 6:
          if ((hoveredPrimitiveIndex != ~0u) || !selectedPrimitiveIndices.empty()) {
            transformAction = "scale and rotate primitive(s)";
          }
          break;
        case 7:
          break;
      }

      if (transformAction != "") {
        ImGui::Text("LMB: %s", transformAction.c_str());
      }
    }
  }

  ImGui::End();
}

void renderWidgets(
    editor::Document* doc,
    editor::Settings& settings,
    bw::core::WorldData const* worldData,
    double globalTime) {
  handleShortcuts(doc, settings);
  handleMouseInteraction(doc, settings);

  renderMenu(doc, settings);
  renderToolbar(doc, settings);
  ImGui::DockSpaceOverViewport(
      0,
      ImGui::GetMainViewport(),
      ImGuiDockNodeFlags_PassthruCentralNode);

  if (doc->isActive()) {
    if (!settings.expertMode) {
      renderCombinedPanel(doc, settings, worldData, globalTime);

      if (settings.showContextSensitiveHelpPanel) {
        renderContextSensitiveHelp(doc, settings);
      }
    }
  }

  // Create world data here
  bw::core::WorldDataPtr generatedWorldData;

  if (doc->isActive()) {
    // If world data has not been created, then do so here, for the case where we load a map
    if (!worldData) {
      generatedWorldData = doc->getWorld()->getWorldData();

      worldData = generatedWorldData.get();
    }

    // Views which use world data need to be done after it's been created
    if (settings.showDebugPanel) {
      renderDebug(doc, settings, worldData, globalTime);
    }

    renderStatusbar(doc, settings, worldData);

    // Render background
    renderWorld(doc, settings, worldData, globalTime);

    if (settings.renderMiniMap) {
      renderMiniMap(doc, settings, worldData, globalTime);
    }
  }

  checkModalPopups(doc, settings);
}

}  // namespace editor