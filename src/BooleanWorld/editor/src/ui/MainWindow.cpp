#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

struct StartupAnimal {
  char const* glyph;
  char const* name;
};

StartupAnimal const& startupAnimal() {
  static constexpr array animals{
      StartupAnimal{ICON_FA_CAT, "cat"},
      StartupAnimal{ICON_FA_DOG, "dog"},
      StartupAnimal{ICON_FA_CROW, "crow"},
      StartupAnimal{ICON_FA_DOVE, "dove"},
      StartupAnimal{ICON_FA_DRAGON, "dragon"},
      StartupAnimal{ICON_FA_FISH, "fish"},
      StartupAnimal{ICON_FA_FROG, "frog"},
      StartupAnimal{ICON_FA_HIPPO, "hippo"},
      StartupAnimal{ICON_FA_HORSE, "horse"},
      StartupAnimal{ICON_FA_OTTER, "otter"},
      StartupAnimal{ICON_FA_SPIDER, "spider"}};
  static mt19937 randomEngine{random_device{}()};
  static uniform_int_distribution<size_t> distribution{0, animals.size() - 1};
  static size_t const selected = distribution(randomEngine);
  return animals[selected];
}

void drawStartupAnimal(
    ImDrawList* drawList,
    ImVec2 const& feet,
    float size,
    ImU32 colour,
    ImU32 outline) {
  auto* font = ImGui::GetFont();
  auto const& animal = startupAnimal();
  ImVec2 const dimensions =
      font->CalcTextSizeA(size, numeric_limits<float>::max(), 0.0f, animal.glyph);
  ImVec2 const position{
      feet.x - dimensions.x * 0.5f, feet.y - dimensions.y};

  // A dark four-way outline keeps every silhouette readable over both the 2D
  // world and the rendered preview, regardless of which animal this run got.
  constexpr float outlineOffset = 2.0f;
  drawList->AddText(
      font, size, {position.x - outlineOffset, position.y}, outline,
      animal.glyph);
  drawList->AddText(
      font, size, {position.x + outlineOffset, position.y}, outline,
      animal.glyph);
  drawList->AddText(
      font, size, {position.x, position.y - outlineOffset}, outline,
      animal.glyph);
  drawList->AddText(
      font, size, {position.x, position.y + outlineOffset}, outline,
      animal.glyph);
  drawList->AddText(font, size, position, colour, animal.glyph);
}

void renderPreviewDropControl(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto world = doc->getWorld();
  if (!world || !world->getWorldDataGenerator()) {
    return;
  }

  constexpr float controlWidth = 54.0f;
  constexpr float controlHeight = 66.0f;
  constexpr float margin = 14.0f;
  ImVec2 const controlPos{
      gWorldViewScreenOrigin.x + gWorldViewSize.x - controlWidth - margin,
      gWorldViewScreenOrigin.y + gWorldViewSize.y - controlHeight - margin};

  ImGui::SetNextWindowPos(controlPos);
  ImGui::SetNextWindowSize({controlWidth, controlHeight});
  ImGui::SetNextWindowBgAlpha(0.0f);
  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav;

  static bool dragging = false;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
  bool const controlVisible =
      ImGui::Begin("##PreviewAnimalControl", nullptr, flags);
  ImGui::PopStyleVar();
  if (controlVisible) {
    ImGui::SetCursorScreenPos(controlPos);
    bool const clicked = ImGui::InvisibleButton(
        "##PreviewAnimal", {controlWidth, controlHeight},
        ImGuiButtonFlags_MouseButtonLeft);
    bool const hovered = ImGui::IsItemHovered();
    bool const active = ImGui::IsItemActive();
    bool const previewing = preview3DIsOpen();
    if (!previewing && active &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
      dragging = true;
    }
    if (hovered || active) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (hovered && !dragging) {
      ImGui::SetTooltip(
          previewing
              ? "Exit the 3D preview."
              : format(
                    "Drag the {} onto the world to enter the 3D preview.",
                    startupAnimal().name)
                    .c_str());
    }

    auto* drawList = ImGui::GetWindowDrawList();
    ImVec2 const controlMax{
        controlPos.x + controlWidth, controlPos.y + controlHeight};
    drawList->AddRectFilled(
        controlPos, controlMax, IM_COL32(42, 48, 56, 225), 5.0f);
    drawList->AddRect(
        controlPos, controlMax,
        hovered || active ? IM_COL32(255, 214, 48, 255)
                          : IM_COL32(125, 133, 145, 255),
        5.0f, 0, 1.5f);

    if (previewing) {
      dragging = false;
      ImVec2 const centre{
          controlPos.x + controlWidth * 0.5f,
          controlPos.y + controlHeight * 0.5f};
      constexpr float crossRadius = 13.0f;
      drawList->AddLine(
          {centre.x - crossRadius, centre.y - crossRadius},
          {centre.x + crossRadius, centre.y + crossRadius},
          IM_COL32(235, 238, 242, 255), 4.0f);
      drawList->AddLine(
          {centre.x + crossRadius, centre.y - crossRadius},
          {centre.x - crossRadius, centre.y + crossRadius},
          IM_COL32(235, 238, 242, 255), 4.0f);
      if (clicked) {
        closePreview3D();
      }
    } else {
      drawStartupAnimal(
          drawList,
          {controlPos.x + controlWidth * 0.5f, controlMax.y - 10.0f},
          40.0f, IM_COL32(255, 193, 7, 255), IM_COL32(92, 65, 0, 255));
    }

    if (!previewing && dragging) {
      ImVec2 const mouse = ImGui::GetMousePos();
      bool const insideViewport =
          mouse.x >= gWorldViewScreenOrigin.x &&
          mouse.y >= gWorldViewScreenOrigin.y &&
          mouse.x < gWorldViewScreenOrigin.x + gWorldViewSize.x &&
          mouse.y < gWorldViewScreenOrigin.y + gWorldViewSize.y;
      bool const overControl =
          mouse.x >= controlPos.x && mouse.y >= controlPos.y &&
          mouse.x < controlMax.x && mouse.y < controlMax.y;
      wp::Vector2 const worldPosition = screenToWorldPosition(mouse);
      auto const primitives = inScopePrimitives(
          *world, world->getWorldDataGenerator()->getLayerSelection(), settings);
      auto const floorZ = insideViewport && !overControl
                              ? resolveGroundingFloorZ(primitives, worldPosition)
                              : optional<float>{};

      auto* foreground = ImGui::GetForegroundDrawList();
      ImU32 const validityColour = floorZ ? IM_COL32(70, 205, 105, 255)
                                          : IM_COL32(225, 75, 75, 255);
      foreground->AddCircle(mouse, 7.0f, validityColour, 20, 2.0f);
      drawStartupAnimal(
          foreground, mouse, 46.0f, validityColour,
          IM_COL32(45, 45, 45, 255));

      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        dragging = false;
        if (floorZ) {
          // The dropped animal always starts square to the canonical world
          // plane: angle zero is intentional rather than inherited from the
          // Player proxy.
          requestOpenPreview3D(
              doc, primitives, worldPosition, 0.0f, *floorZ);
        }
      }
    } else if (!previewing && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      dragging = false;
    }
  }
  ImGui::End();
}

void dockMainWindowAtBottom(ImGuiID dockspaceId) {
  static bool initialDockingApplied = false;
  if (initialDockingApplied) {
    return;
  }

  // Preserve a layout the user has already saved, including an intentionally
  // floating Main window. Only a brand-new window receives the default split.
  if (ImGui::FindWindowSettingsByID(ImHashStr("Main"))) {
    initialDockingApplied = true;
    return;
  }

  auto* centralNode = ImGui::DockBuilderGetCentralNode(dockspaceId);
  if (!centralNode) {
    return;
  }

  ImGuiID bottomNode = 0;
  ImGui::DockBuilderSplitNode(
      centralNode->ID, ImGuiDir_Down, 0.25f, &bottomNode, nullptr);
  ImGui::DockBuilderDockWindow("Main", bottomNode);
  ImGui::DockBuilderFinish(dockspaceId);
  initialDockingApplied = true;
}

void renderScriptLog() {
  auto* renderSystem = editorRenderSystem();
  auto const* logs = renderSystem ? &renderSystem->scriptLogs() : nullptr;
  if (!logs || logs->empty()) {
    ImGui::TextDisabled("No Layer build scripts have run.");
    return;
  }

  if (ImGui::BeginTabBar("##ScriptLogs")) {
    for (size_t index = 0; index < logs->size(); ++index) {
      auto const& log = (*logs)[index];
      auto label = format("{}###ScriptLog{}", log.name, index);
      if (ImGui::BeginTabItem(label.c_str())) {
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::Button("Clear")) {
          renderSystem->clearScriptLog(log.name);
        }
        ImGui::Separator();

        if (ImGui::BeginChild(
                "##Contents", ImVec2{}, ImGuiChildFlags_Borders,
                ImGuiWindowFlags_HorizontalScrollbar)) {
          for (auto const& line : log.lines) {
            if (line.error) {
              ImGui::PushStyleColor(
                  ImGuiCol_Text, ImVec4{1.0f, 0.25f, 0.25f, 1.0f});
            }
            ImGui::TextUnformatted(line.message.c_str());
            if (line.error) {
              ImGui::PopStyleColor();
            }
          }
        }
        ImGui::EndChild();
        ImGui::PopID();
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
}

void renderMainWindow(ImGuiID dockspaceId) {
  dockMainWindowAtBottom(dockspaceId);

  if (ImGui::Begin("Main")) {
    if (ImGui::BeginTabBar("##MainTabs")) {
      if (ImGui::BeginTabItem("Script Log")) {
        renderScriptLog();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::End();
}


void renderEditor(ViewContext& context) {
  // Preview construction is deliberately one frame behind the drop that
  // requests it. The preceding frame can then reach presentation with the
  // status-bar message visible before this synchronous work begins.
  processPendingPreview3DOpen();

  auto* doc = context.doc;
  auto& settings = context.settings;
  auto const* worldData = context.worldData;
  auto globalTime = context.globalTime;

  // The preview takes over the world viewport rather than opening a window
  // of its own, so the editor's chrome renders around it exactly as it does
  // around the 2D world view. What it cannot survive is the document being
  // restructured underneath the Arrangement and Primitive pointers it built
  // when it opened, so the panels around it are visible but frozen; the
  // preview's own surface and Sub-material authoring stays live.
  bool previewing = preview3DIsOpen();
  if (previewing && !doc->isActive()) {
    closePreview3D();
    previewing = false;
  }

  if (!previewing) {
    handleShortcuts(context);
    handleMouseInteraction(context);
  }

  ImGui::BeginDisabled(previewing);
  renderMenu(context);
  ImGui::EndDisabled();
  renderToolbar(context);

  auto dockspaceId = ImGui::DockSpaceOverViewport(
      0,
      ImGui::GetMainViewport(),
      ImGuiDockNodeFlags_PassthruCentralNode);

  if (doc->isActive()) {
    if (previewing) {
      // The preview's selected-surface authoring lives in this panel, so it
      // is shown even in expert mode - which otherwise hides it - and stays
      // live rather than being disabled with the rest of the editor.
      renderCombinedPanel(context);
    } else if (!settings.expertMode) {
      renderCombinedPanel(context);

      if (settings.showContextSensitiveHelpPanel) {
        renderContextSensitiveHelp(context);
      }
    }

    renderMainWindow(dockspaceId);
  }

  ImGui::BeginDisabled(previewing);

  // Create world data here
  bw::core::WorldDataPtr generatedWorldData;

  if (doc->isActive()) {
    // If world data has not been created, then do so here, for the case where we load a map
    if (!worldData) {
      generatedWorldData = doc->getWorld()->getWorldData();

      worldData = generatedWorldData.get();
      context.worldData = worldData;
    }

    // Views which use world data need to be done after it's been created
    if (settings.showDebugPanel) {
      renderDebug(context);
    }

    renderStatusbar(context);

    // The World window fills the dockspace's central node - chromeless, so it
    // reads the same as the old background draw list did, but as real window
    // content instead of something drawn behind every other panel.
    ImVec2 worldPos, worldSize;
    if (auto* centralNode = ImGui::DockBuilderGetCentralNode(dockspaceId)) {
      worldPos = centralNode->Pos;
      worldSize = centralNode->Size;
    } else {
      auto* mainViewport = ImGui::GetMainViewport();
      worldPos = mainViewport->WorkPos;
      worldSize = mainViewport->WorkSize;
    }

    gWorldViewScreenOrigin = {worldPos.x, worldPos.y};
    gWorldViewSize = {worldSize.x, worldSize.y};

    if (previewing) {
      // Stands in for the World window below, filling the same rect from the
      // origin/size just published - the 3D view of the level replaces the 2D
      // one in place, rather than covering the editor with a window.
      ImGui::EndDisabled();
      renderPreview3D();
      renderPreviewDropControl(context);
      ImGui::BeginDisabled(previewing);
    } else {
      ImGui::SetNextWindowPos(worldPos);
      ImGui::SetNextWindowSize(worldSize);

      // NoInputs keeps this window transparent to ImGui's own mouse handling
      // (it won't set io.WantCaptureMouse), so the raw-mouse world interaction
      // code below continues to see the canvas exactly as it did when this was
      // drawn to the background draw list rather than a window.
      constexpr ImGuiWindowFlags worldWindowFlags =
          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
          ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking |
          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;

      if (ImGui::Begin("World", nullptr, worldWindowFlags)) {
        renderWorld(doc, settings, worldData, globalTime);

        if (settings.renderMiniMap) {
          renderMiniMap(doc, settings, worldData, globalTime);
        }
      }
      ImGui::End();

      // This is a real ImGui control layered over the otherwise input-
      // transparent World window, so it can own a drag without turning the
      // whole canvas into an ImGui input target.
      renderPreviewDropControl(context);
    }
  }

  ImGui::EndDisabled();

  checkModalPopups(context);
}
}  // namespace editor
