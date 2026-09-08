#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

using CreatePrimitiveFunction = function<unique_ptr<bw::core::Primitive>()>;

CreatePrimitiveFunction createPrimitiveFunction(bw::core::PrimitiveSpec spec) {
  return [spec = move(spec)] {
    return bw::core::PrimitiveFactory::create(spec);
  };
}

constexpr int minGridSizeExponent = 1;  // 2 world units
constexpr int maxGridSizeExponent = 8;  // 256 world units

filesystem::path editorResourceRoot() {
#ifdef BW_EDITOR_RESOURCE_ROOT
  // NFD's Windows backend rejects initial directories containing unresolved
  // parent components (the CMake path reaches app/resources via "../").
  return filesystem::path(BW_EDITOR_RESOURCE_ROOT).lexically_normal();
#else
  return filesystem::current_path();
#endif
}

void cycleGridSize(Settings& settings) {
  auto exponent = std::clamp(
      static_cast<int>(std::lround(std::log2(settings.gridSize))),
      minGridSizeExponent, maxGridSizeExponent);
  exponent = exponent == maxGridSizeExponent
                 ? minGridSizeExponent
                 : exponent + 1;
  settings.gridSize = static_cast<float>(1 << exponent);
}

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

void renderMenu(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
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
        // Application close has its own persistent Save/Don't Save/Cancel
        // state because a native Save As dialog may itself be cancelled.
        exitApp(doc);
      }

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
      auto world = doc->getWorld();

      bool canUndoAction = canUndo();
      bool canRedoAction = canRedo();
      bool hasPrimitiveSelection = !doc->getSelectedPrimitiveIndices().empty();
      bool hasTriggerLineSelection = doc->getSelectedTriggerLineIndex() != ~0u;
      bool hasAnySelection = doc->hasSelection();
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
        beginClonePlacement(doc, doc->getSelectedPrimitiveIndices());
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
          transact(doc, format("Delete {} Primitive(s)", primitiveIndices.size()), [&] { deletePrimitives(doc, primitiveIndices); });
        }

        auto triggerLineIndex = doc->getSelectedTriggerLineIndex();

        if (triggerLineIndex != ~0u) {
          transact(doc, "Delete TriggerLine", [&] { deleteTriggerLine(doc, triggerLineIndex); });
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
        transact(doc, format("Bake {} Primitive(s)", indices.size()), [&] { bakePrimitives(doc, indices); });
      }

      if (ImGui::MenuItem("Clip to grid")) {
        auto const& indices = doc->getSelectedPrimitiveIndices();
        transact(doc, format("Clip Primitive(s) to grid", indices.size()), [&] { clipPrimitivesToGrid(doc, indices, settings.gridSize); });
      }

      if (!hasPrimitiveSelection) {
        widgets::PopDisabled();
      }

      if (!world) {
        widgets::PushDisabled();
      }

      if (ImGui::MenuItem("Select all", "Ctrl+A")) {
        if (settings.mode == Settings::Mode::Mesh) {
          if (doc->getActiveMesh()) {
            transact(doc, "Select All Mesh Sub-objects", [&] { selectAllMeshSubObjects(doc, settings.meshSubMode); });
          }
        } else {
          auto indices = doc->getSelectablePrimitiveIndices(settings);
          transact(doc, "Select All", [&] { selectPrimitives(doc, set<uint32_t>(indices.begin(), indices.end())); });
        }
      }

      if (!world) {
        widgets::PopDisabled();
      }

      if (!hasAnySelection) {
        widgets::PushDisabled();
      }

      if (ImGui::MenuItem("Deselect all", "Ctrl+D")) {
        transact(doc, "Clear Selections", [&] { clearSelections(doc); });
      }

      if (!hasAnySelection) {
        widgets::PopDisabled();
      }

      if (world) {
        ImGui::Separator();
        if (ImGui::MenuItem("Generate Primitive Field\u2026")) {
          getPrimitiveFieldPreview().requestOpen();
        }

        if (ImGui::MenuItem("New Layer")) {
          auto layerName = format("Layer {}", world->getNumLayers());
          transact(doc, "New Layer", [&] { addLayer(doc, layerName); });
        }

        if (ImGui::MenuItem("Regenerate world data")) {
          regenerateWorldData(doc);
        }
      }

      if (resetDisabled) {
        widgets::PushDisabled();
      }

      if (ImGui::MenuItem("Select & home on Ghost")) {
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
        goHome(doc);
      }

      ImGui::MenuItem("Minimap", nullptr, &settings.renderMiniMap);
      ImGui::MenuItem("Transform debug view", "F7", &settings.showDebugPanel);
      ImGui::MenuItem("Context help view", "F10", &settings.showContextSensitiveHelpPanel);

      ImGui::MenuItem("Grid", "G", &settings.showGrid);
      if (ImGui::MenuItem("Cycle grid size", "Shift+G")) {
        cycleGridSize(settings);
      }

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

void renderToolbar(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
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

    // The open preview draws an Arrangement built once from direct pointers
    // into the open World's Primitives, so the document's structure must not
    // change underneath it. Everything here is frozen while it is open.
    bool const previewing = preview3DIsOpen();
    ImGui::BeginDisabled(previewing);

    bool saveDisabled = !world || !doc->isModified();
    bool saveAsDisabled = !world;
    bool canUndoAction = canUndo();
    bool canRedoAction = canRedo();
    bool hasPrimitiveSelection = !doc->getSelectedPrimitiveIndices().empty();
    bool hasTriggerLineSelection = doc->getSelectedTriggerLineIndex() != ~0u;

    // Authoring mode is an editor preference, so it deliberately bypasses
    // undoable actions and Document modification.
    ImGui::SetNextItemWidth(105);
    int mode = static_cast<int>(settings.mode);
    if (ImGui::Combo("Mode##Editor", &mode, "Primitive\0Mesh\0\0", 2)) {
      setEditorMode(doc, settings, static_cast<Settings::Mode>(mode));
    }

    if (settings.mode == Settings::Mode::Mesh) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(100);
      int subMode = static_cast<int>(settings.meshSubMode);
      if (ImGui::Combo("Select##Mesh", &subMode,
                       "Vertex\0Edge\0Polygon\0\0", 3)) {
        setMeshSubMode(
            doc, settings, static_cast<Settings::MeshSubMode>(subMode));
      }
    }

    // Keep the fixed-height viewport toolbar on one row after the mode
    // controls. Without this, the file buttons begin a clipped second row.
    ImGui::SameLine();

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
      beginClonePlacement(doc, doc->getSelectedPrimitiveIndices());
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
        transact(doc, format("Delete {} Primitive(s)", primitiveIndices.size()), [&] { deletePrimitives(doc, primitiveIndices); });
      }

      auto triggerLineIndex = doc->getSelectedTriggerLineIndex();

      if (triggerLineIndex != ~0u) {
        transact(doc, "Delete TriggerLine", [&] { deleteTriggerLine(doc, triggerLineIndex); });
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
      goHome(doc);
    }

    ImGui::SameLine();

    if (!world) {
      widgets::PushDisabled();
    }

    if (ImGui::Button(ICON_FA_SYNC)) {
      regenerateWorldData(doc);
    }

    if (!world) {
      widgets::PopDisabled();
    }

    ImGui::SameLine();

    if (!world) {
      widgets::PushDisabled();
    }

    if (widgets::ToggleButton("ToggleGhost", ICON_FA_GHOST, &settings.ghostActive)) {
      enableGhost(doc, settings.ghostActive);
    }

    if (!world) {
      widgets::PopDisabled();
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

    if (!world) {
      widgets::PopDisabled();
    }

    // Grid
    ImGui::SameLine();

    widgets::ToggleButton("ToggleGrid", "Grid", &settings.showGrid);

    ImGui::SameLine();

    ImGui::SetNextItemWidth(80);

    int gridSizeExponent = std::clamp(
        static_cast<int>(std::lround(std::log2(settings.gridSize))),
        minGridSizeExponent, maxGridSizeExponent);
    string gridSizeText = format("{}", 1 << gridSizeExponent);
    if (ImGui::SliderInt(
            "Size##GridSize", &gridSizeExponent,
            minGridSizeExponent, maxGridSizeExponent,
            gridSizeText.c_str())) {
      settings.gridSize = static_cast<float>(1 << gridSizeExponent);
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(64);

    // Active layer
    if (!world) {
      widgets::PushDisabled();
    }

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

    if (!world) {
      widgets::PopDisabled();
    }

    ImGui::EndDisabled();

    // Handle action
    handleModifiedDocument(doc, docAction, checkDocumentModified, docText, helperFunc);
    checkModifiedOperation(doc, docText, helperFunc);

    ImGui::End();
  }
}

void renderStatusbar(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto const* worldData = context.worldData;
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
        ImGui::TextColored(c, "%s", numPrimsText.c_str());

        ImGui::SameLine();

        auto const& arrangement = worldData->getArrangement();
        string processData = format(
            "| {} vertices & {} face(s)",
            arrangement.vertices.size(),
            arrangement.faces.size());

        ImGui::TextColored(c, "%s", processData.c_str());

        // Mouse
        auto mousePos = getMouseWorldPosition();
        string mouseData = format("| {:.1f}, {:.1f}", mousePos.x, mousePos.y);

        ImGui::SameLine();
        ImGui::TextColored(c, "%s", mouseData.c_str());

        // Zoom
        string zoomData = format("| {:.0f}%", gViewZoom * 100.0f);

        ImGui::SameLine();
        ImGui::TextColored(c, "%s", zoomData.c_str());

        if (settings.mode == editor::Settings::Mode::Mesh &&
            doc->meshDrawToolArmed()) {
          auto snappedPosition = editor::Document::snapMeshDrawPosition(
              mousePos, settings.showGrid, settings.gridSize);
          auto drawState = doc->getMeshDrawPositionState(
              snappedPosition, settings);
          char const* action =
              drawState == editor::Document::MeshDrawPositionState::CloseRing
                  ? "click to close"
              : drawState == editor::Document::MeshDrawPositionState::ConnectVertex
                  ? "click to connect vertex"
              : drawState == editor::Document::MeshDrawPositionState::Invalid
                  ? "invalid position"
                  : "click to place vertex";
          auto const& vertices = doc->getMeshDrawVertices();
          auto drawStatus = vertices.empty()
                                ? format("| Mesh draw armed - {}", action)
                                : format("| Drawing Ring: {} vertices - {}",
                                         vertices.size(), action);
          ImGui::SameLine();
          ImGui::TextColored(
              drawState == editor::Document::MeshDrawPositionState::Invalid
                  ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f)
                  : ImVec4(0.7f, 0.85f, 1.0f, 1.0f),
              "%s", drawStatus.c_str());
        }

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


}  // namespace editor
