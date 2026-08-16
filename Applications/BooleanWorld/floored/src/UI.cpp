#include <cmath>
#include <exception>

#include <core/Arrangement.h>
#include <core/DynamicWorldDataGenerator.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "IconsFontAwesome5.h"

#include "UI.h"
#include "Render.h"
#include "WidgetHelpers.h"
#include "AppHelpers.h"
#include "UiHelpers.h"
#include "SelectionType.h"
#include "Document.h"

using namespace std;

pair<int, int> gHoveredObject{-1, -1};
pair<int, int> gSelectedObject{-1, -1};
floored::SelectionType gHoveredType{floored::SelectionType::None};
floored::SelectionType gSelectedType{floored::SelectionType::None};

namespace floored {
ImVec2 gMainMenuWindowSize;

void renderMenu(Document* doc, Settings& settings) {
  if (ImGui::BeginMainMenuBar()) {
    //
    // File
    //
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open world", "Ctrl+O")) {
        openWorld(doc);
      }

      ImGui::Separator();
      if (ImGui::MenuItem("Exit")) {
        exitApp(doc);
      }

      ImGui::EndMenu();
    }

    //
    // View
    //
    if (ImGui::BeginMenu("View")) {
      if (!doc->isActive()) {
        widgets::PushDisabled();
      }

      ImGui::MenuItem("Primitives", "F2", &settings.renderPrimitives);
      ImGui::MenuItem("Primitive bounds", "F3", &settings.renderPrimitiveBounds);
      ImGui::MenuItem("Primitive debug", "F4", &settings.renderPrimitiveDebug);
      ImGui::MenuItem("Graph vertices", "F6", &settings.renderGraphVertices);
      ImGui::MenuItem("Faces", "F7", &settings.renderFaces);

      if (!doc->isActive()) {
        widgets::PopDisabled();
      }

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

      if (ImGui::BeginMenu("Edges")) {
        bool selected = settings.edgeRenderMode == Settings::EdgeRenderMode::None;

        if (ImGui::MenuItem("None", 0, &selected)) {
          if (selected) {
            settings.edgeRenderMode = Settings::EdgeRenderMode::None;
          }
        }

        selected = settings.edgeRenderMode == Settings::EdgeRenderMode::Polygons;

        if (ImGui::MenuItem("Polygons", 0, &selected)) {
          if (selected) {
            settings.edgeRenderMode = Settings::EdgeRenderMode::Polygons;
          }
        }

        selected = settings.edgeRenderMode == Settings::EdgeRenderMode::Graph;

        if (ImGui::MenuItem("Graph", 0, &selected)) {
          if (selected) {
            settings.edgeRenderMode = Settings::EdgeRenderMode::Graph;
          }
        }

        ImGui::EndMenu();
      }

      ImGui::EndMenu();
    }

    gMainMenuWindowSize = ImGui::GetWindowSize();
    ImGui::EndMainMenuBar();
  }
}

void renderToolbar(Document* doc, Settings& settings) {
  ImGuiIO& io = ImGui::GetIO();

  auto windowFlags = 0 | ImGuiWindowFlags_NoDecoration;

  ImGui::SetNextWindowPos(ImVec2(0, gMainMenuWindowSize.y));
  ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 35));

  if (ImGui::Begin("Toolbar", nullptr, windowFlags)) {
    //
    // File operations
    //
    if (ImGui::Button(ICON_FA_GLOBE)) {
      openWorld(doc);
    }

    //
    // View / rendering
    //
    ImGui::SameLine();
    widgets::ToggleButton("TogglePrimitives", "Primitives", &settings.renderPrimitives);

    ImGui::SameLine();
    widgets::ToggleButton("ToggleFaces", "Faces", &settings.renderFaces);

    ImGui::End();
  }
}

void renderStatusbar(floored::Document* doc, floored::Settings& settings) {
  ImGuiViewportP* viewport = (ImGuiViewportP*)(void*)ImGui::GetMainViewport();

  auto windowFlags =
      ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_MenuBar;

  float height = ImGui::GetFrameHeight();

  if (ImGui::BeginViewportSideBar("##MainStatusBar", viewport, ImGuiDir_Down, height, windowFlags)) {
    if (ImGui::BeginMenuBar()) {
      if (doc->isActive()) {
        auto worldPos = floored::getMouseWorldPosition();
        ImGui::Text("World: %3.2f, %3.2f", worldPos.x, worldPos.y);
      }

      ImGui::EndMenuBar();
    }
    ImGui::End();
  }
}

void renderGraphEdgeDetails(floored::Document* doc, floored::Settings& settings, pair<int, int> const& selectedObject) {
  auto edgeIndex = selectedObject.first;

  auto const& arrangement = doc->getArrangement();
  auto const& edge = arrangement.edges[edgeIndex];

  ImGui::Text("Graph edge: %d", edgeIndex);
  ImGui::Separator();
  ImGui::Text("Face 0: %u", edge.face[0]);
  ImGui::Text("Face 1: %u", edge.face[1]);
}

void renderFaceDetails(floored::Document* doc, floored::Settings& settings, pair<int, int> const& selectedObject) {
  auto faceIndex = selectedObject.first;

  auto const& arrangement = doc->getArrangement();
  auto const& face = arrangement.faces[faceIndex];

  ImGui::Text("Face: %d", faceIndex);
  ImGui::Separator();

  if (face.primitiveIndex != ~0u) {
    ImGui::Text("Polygon: %u", face.primitiveIndex);
  } else {
    ImGui::TextUnformatted("Hole");
  }
}

void renderSelectedObjectView(floored::Document* doc, floored::Settings& settings) {
  if (gSelectedObject.first >= 0) {
    if (ImGui::Begin("Selection")) {
      switch (gSelectedType) {
        case floored::SelectionType::PolygonEdge:
          break;

        case floored::SelectionType::GraphEdge:
          renderGraphEdgeDetails(doc, settings, gSelectedObject);
          break;

        case floored::SelectionType::Face:
          renderFaceDetails(doc, settings, gSelectedObject);
          break;
      }
    }

    ImGui::End();
  }
}

void handleShortcuts(Document* doc, Settings& settings) {
  if (ImGui::Shortcut(ImGuiKey_O | ImGuiMod_Ctrl, 0, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      openWorld(doc);
    }
  }

  if (ImGui::Shortcut(ImGuiKey_F2, 0, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      settings.renderPrimitives = !settings.renderPrimitives;
    }
  }

  if (ImGui::Shortcut(ImGuiKey_F3, 0, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      settings.renderPrimitiveBounds = !settings.renderPrimitiveBounds;
    }
  }

  if (ImGui::Shortcut(ImGuiKey_F4, 0, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      settings.renderPrimitiveDebug = !settings.renderPrimitiveDebug;
    }
  }

  if (ImGui::Shortcut(ImGuiKey_F6, 0, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      settings.renderGraphVertices = !settings.renderGraphVertices;
    }
  }

  if (ImGui::Shortcut(ImGuiKey_F7, 0, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      settings.renderFaces = !settings.renderFaces;
    }
  }
}

pair<int, int> getHoveredPolygonEdge(floored::Document* doc, wp::Vector2 const& pos) {
  auto const& contours = doc->getContours();

  for (int i = 0; i < (int)contours.size(); ++i) {
    auto const& contour = contours[i];

    auto numVertices = (int)contour.size();
    for (int j = 0; j < numVertices; ++j) {
      int k = (j + 1) % numVertices;

      wp::Vector2 v0{
          bw::core::arr::ToWorldCoordinate(contour[j].x),
          bw::core::arr::ToWorldCoordinate(contour[j].y)};
      wp::Vector2 v1{
          bw::core::arr::ToWorldCoordinate(contour[k].x),
          bw::core::arr::ToWorldCoordinate(contour[k].y)};

      if (pos.distanceToLine(v0, v1) < 2) {
        return {i, j};
      }
    }
  }

  return {-1, -1};
}

pair<int, int> getHoveredGraphEdge(floored::Document* doc, wp::Vector2 const& pos) {
  auto const& arrangement = doc->getArrangement();

  for (int i = 0; i < (int)arrangement.edges.size(); ++i) {
    auto const& edge = arrangement.edges[i];

    wp::Vector2 v0{
        bw::core::arr::ToWorldCoordinate(arrangement.vertices[edge.v[0]].x),
        bw::core::arr::ToWorldCoordinate(arrangement.vertices[edge.v[0]].y)};
    wp::Vector2 v1{
        bw::core::arr::ToWorldCoordinate(arrangement.vertices[edge.v[1]].x),
        bw::core::arr::ToWorldCoordinate(arrangement.vertices[edge.v[1]].y)};

    if (pos.distanceToLine(v0, v1) < 2) {
      return {i, -1};
    }
  }

  return {-1, -1};
}

int getHoveredFace(floored::Document* doc, wp::Vector2 const& pos) {
  auto v = bw::core::arr::FixedPointVertex{
      bw::core::arr::ToFixedPointCoordinate(pos.x),
      bw::core::arr::ToFixedPointCoordinate(pos.y)};
  auto const& arrangement = doc->getArrangement();

  for (int i = 0; i < (int)arrangement.faces.size(); ++i) {
    auto const& face = arrangement.faces[i];

    if (bw::core::arr::PointInFace(v, face, arrangement)) {
      return i;
    }
  }

  return -1;
}

void handleMouseInteraction(floored::Document* doc, floored::Settings& settings) {
  auto worldPos = getMouseWorldPosition();

  gHoveredObject = {-1, -1};
  gHoveredType = floored::SelectionType::None;
  pair<int, int> hoveredObject{-1, -1};

  // Check hovered items
  switch (settings.edgeRenderMode) {
    case Settings::EdgeRenderMode::Polygons:
      hoveredObject = getHoveredPolygonEdge(doc, worldPos);

      if (hoveredObject.first >= 0) {
        gHoveredObject = hoveredObject;
        gHoveredType = floored::SelectionType::PolygonEdge;
      }
      break;

    case Settings::EdgeRenderMode::Graph:
      hoveredObject = getHoveredGraphEdge(doc, worldPos);

      if (hoveredObject.first >= 0) {
        gHoveredObject = hoveredObject;
        gHoveredType = floored::SelectionType::GraphEdge;
      }
      break;

    default:
      break;
  }

  // If no edges, check faces
  if (gHoveredType == floored::SelectionType::None) {
    int hoveredFace = getHoveredFace(doc, worldPos);

    if (hoveredFace >= 0) {
      gHoveredObject = {hoveredFace, -1};
      gHoveredType = floored::SelectionType::Face;
    }
  }

  if (ImGui::IsMouseDown(0)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      if (gHoveredType != floored::SelectionType::None) {
        gSelectedType = gHoveredType;
        gSelectedObject = gHoveredObject;
      }
    }
  }
  if (ImGui::IsMouseDown(1)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      gSelectedType = {floored::SelectionType::None};
      gSelectedObject = {-1, -1};
    }
  }
}

void renderUI(Document* doc, Settings& settings) {
  handleShortcuts(doc, settings);
  handleMouseInteraction(doc, settings);
  renderMenu(doc, settings);
  renderToolbar(doc, settings);
  renderStatusbar(doc, settings);

  // Create world data here
  if (doc->isActive()) {
    renderSelectedObjectView(doc, settings);

    // Render background
    renderWorld(doc, settings);
  }
}

}  // namespace floored
