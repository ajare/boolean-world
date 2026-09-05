#define NOMINMAX
#include <Windows.h>
#undef NOMINMAX

#include <algorithm>
#include <cctype>
#include <format>
#include <limits>
#include <nfd/nfd.h>

#pragma warning(push)
#pragma warning(disable : 4307)
#include <spdlog/spdlog.h>
#pragma warning(pop)

#include <yaml-cpp/yaml.h>

#include <core/BinarySerializer.h>
#include <core/YamlSerializer.h>
#include <core/CirclePolygon.h>
#include <core/CircleSegmentPolygon.h>
#include <core/TorusPolygon.h>
#include <core/TorusSegmentPolygon.h>
#include <core/RectanglePolygon.h>
#include <core/RegularPolygon.h>
#include <core/SuperformulaPolygon.h>
#include <core/MeshPrimitive.h>
#include <core/World.h>

#include "imgui.h"

#include "AppHelpers.h"
#include "ApplicationCloseController.h"
#include "Defines.h"
#include "EditorException.h"
#include "Markdown.h"
#include "PrimitiveFieldPreview.h"

extern wp::Vector2 gViewOffset;
extern float gViewZoom;
extern wp::Vector2 gWorldViewSize;
extern std::map<std::string, std::string> gHelpFiles;
extern spdlog::logger* gLogger;

namespace {
constexpr nfdfilteritem_t kWorldFilters[] = {
    {"YAML world", "world.yaml"},
    {"Binary world", "world"},
};
editor::ApplicationCloseController applicationCloseController;
bool closeApproved{false};

bool hasExtension(std::string const& filepath, std::string const& extension) {
  if (filepath.size() < extension.size()) {
    return false;
  }

  auto const tail = filepath.substr(filepath.size() - extension.size());
  return std::equal(tail.begin(), tail.end(), extension.begin(), [](char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) ==
           std::tolower(static_cast<unsigned char>(b));
  });
}
}  // namespace

namespace editor {
using namespace std;

void newDocument(editor::Document* doc) {
  getPrimitiveFieldPreview().close();
  doc->newDoc();
}

void openDocument(editor::Document* doc) {
  nfdchar_t* outPath;
  auto res = NFD_OpenDialog(&outPath, kWorldFilters, (nfdfiltersize_t)std::size(kWorldFilters), nullptr);

  if (res == NFD_OKAY) {
    string filepath(outPath);
    NFD_FreePath(outPath);

    getPrimitiveFieldPreview().close();
    bool ok = doc->openDoc(filepath);

    if (!ok) {
      ImGui::OpenPopup("Open file failed");
    }
  }
}

void saveDocumentAs(editor::Document* doc) {
  nfdchar_t* outPath;
  auto res = NFD_SaveDialog(&outPath, kWorldFilters, (nfdfiltersize_t)std::size(kWorldFilters), nullptr, nullptr);

  if (res == NFD_OKAY) {
    string filepath(outPath);
    NFD_FreePath(outPath);

    doc->saveDocAs(filepath);
  }
}

void saveDocument(editor::Document* doc) {
  if (!doc->hasFilepath()) {
    saveDocumentAs(doc);
  } else {
    doc->saveDoc();
  }
}

void exitApp(editor::Document* doc) {
  auto result = applicationCloseController.requestClose(
      doc->isActive(), doc->isActive() && doc->isModified());
  if (result == ApplicationCloseResult::Close) {
    getPrimitiveFieldPreview().close();
    closeApproved = true;
  }
}

void renderApplicationCloseDialog(editor::Document* doc) {
  if (!applicationCloseController.confirmationPending()) {
    return;
  }

  // Open at the root ID stack. File > Exit requests close from inside a menu,
  // while a native window close requests it outside any ImGui window; opening
  // here gives both paths the same persistent modal identity.
  if (!ImGui::IsPopupOpen("Save changes before closing?")) {
    ImGui::OpenPopup("Save changes before closing?");
  }
  auto centre = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal(
          "Save changes before closing?", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }

  ImGui::TextUnformatted("The current World has unsaved changes.");
  ImGui::TextUnformatted("Do you want to save them before closing?");
  ImGui::Separator();

  if (ImGui::Button("Save", ImVec2(120, 0))) {
    saveDocument(doc);
    if (applicationCloseController.saveCompleted(!doc->isModified()) ==
        ApplicationCloseResult::Close) {
      getPrimitiveFieldPreview().close();
      closeApproved = true;
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SetItemDefaultFocus();
  ImGui::SameLine();
  if (ImGui::Button("Don't Save", ImVec2(120, 0))) {
    if (applicationCloseController.discard() == ApplicationCloseResult::Close) {
      getPrimitiveFieldPreview().close();
      closeApproved = true;
    }
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0))) {
    applicationCloseController.cancel();
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

bool applicationCloseApproved() {
  return closeApproved;
}

void showHelp(editor::Document* doc) {
}

void checkModifiedOperation(editor::Document* doc, string const& title, DocumentHelperFunction func) {
  // Centre dialogue
  ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    string saveText = "Do you want to save changes?";
    ImGui::TextUnformatted(saveText.c_str());
    ImGui::Separator();

    if (ImGui::Button("Save", ImVec2(120, 0))) {
      saveDocument(doc);
      func(doc);
      ImGui::CloseCurrentPopup();
    }

    ImGui::SetItemDefaultFocus();

    ImGui::SameLine();
    if (ImGui::Button("Don't Save", ImVec2(120, 0))) {
      func(doc);
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}

void checkNonDocumentOperation() {
  ImVec2 centre = ImGui::GetMainViewport()->GetCenter();

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

void handleModifiedDocument(editor::Document* doc, bool docAction, bool checkDocumentModified, string const& docText, DocumentHelperFunction helperFunc) {
  if (docAction) {
    if (checkDocumentModified && doc->isModified()) {
      char const* docTextStr = docText.c_str();
      ImGui::OpenPopup(docTextStr);
    } else {
      helperFunc(doc);
    }
  }
}

void handleNonDocumentAction(string const& action) {
  ImGui::OpenPopup(action.c_str());
}

void renderHelp() {
  if (ImGui::BeginTabBar("##HelpTabBar", ImGuiTabBarFlags_None)) {
    for (auto const& item : gHelpFiles) {
      auto const& [name, content] = item;

      if (ImGui::BeginTabItem(name.c_str())) {
        renderMarkdown(content);
        ImGui::EndTabItem();
      }
    }

    ImGui::EndTabBar();
  }

  if (ImGui::Button("Close", ImVec2(120, 0))) {
    ImGui::CloseCurrentPopup();
  }
}

bw::core::World* loadWorld(string const& filepath) {
  auto const yaml = hasExtension(filepath, ".world.yaml");
  auto const binary = hasExtension(filepath, ".world") && !yaml;

  bw::core::World* world{nullptr};

  if (yaml || binary) {
    shared_ptr<bw::core::Serializer> ser = yaml
                                               ? shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::fromFile(filepath))
                                               : shared_ptr<bw::core::Serializer>(bw::core::BinarySerializer::fromFile(filepath));

    try {
      ser->deserialize();
    } catch (exception& e) {
      gLogger->error(e.what());
      throw EditorException(format("Could not open {} (errors loading)", filepath));
    }

    world = new bw::core::World(ED_DEFAULT_WORLD_SIZE, ED_DEFAULT_WORLD_ACCEL_GRID_SIZE);
    auto workData = bw::core::SerializationWorkData{};

    if (world->deserialize(ser, workData)) {
      auto const& warnings = world->getDeserializationWarnings();

      if (!warnings.empty()) {
        for (auto const& warning : warnings) {
          gLogger->warn(warning);
        }
      }

      return world;
    } else {
      auto const& errors = world->getDeserializationErrors();

      if (!errors.empty()) {
        for (auto const& error : errors) {
          gLogger->error(error);
        }
      }

      throw EditorException(format("Could not open {} (errors loading)", filepath));
    }
  } else {
    throw EditorException(format("Could not open {} (filetype not supported)", filepath));
  }
}

void goHome(editor::Document* doc) {
  auto world = doc->getWorld();
  auto const& selected = doc->getSelectedPrimitiveIndices();

  if (!world || selected.empty()) {
    gViewOffset.set(0.0f, 0.0f);
    gViewZoom = 1.0f;
    return;
  }

  wp::Vector2 minExtent{numeric_limits<float>::max(), numeric_limits<float>::max()};
  wp::Vector2 maxExtent{numeric_limits<float>::lowest(), numeric_limits<float>::lowest()};

  for (auto index : selected) {
    wp::Vector2 primMin, primMax;
    world->getPrimitive(index)->getBounds().getExtents(primMin, primMax);

    minExtent.x = min(minExtent.x, primMin.x);
    minExtent.y = min(minExtent.y, primMin.y);
    maxExtent.x = max(maxExtent.x, primMax.x);
    maxExtent.y = max(maxExtent.y, primMax.y);
  }

  gViewOffset = (minExtent + maxExtent) * 0.5f;

  // Leave a border around the framed selection rather than filling the
  // window edge to edge.
  constexpr float ED_HOME_VIEW_PADDING_SCALE = 0.9f;

  // goHome only ever zooms out to fit the selection; if it's already fully
  // visible at the current zoom, leave zoom alone and just recentre on it.
  auto selectionSize = maxExtent - minExtent;
  if (selectionSize.x > 0.0f && selectionSize.y > 0.0f) {
    auto fitZoom = clamp(
        min(gWorldViewSize.x / selectionSize.x, gWorldViewSize.y / selectionSize.y) * ED_HOME_VIEW_PADDING_SCALE,
        ED_MIN_VIEW_ZOOM, ED_MAX_VIEW_ZOOM);
    gViewZoom = min(gViewZoom, fitZoom);
  }
}

void frameAllWorld(editor::Document* doc) {
  auto world = doc->getWorld();
  if (!world) {
    return;
  }

  wp::Vector2 minExtent, maxExtent;
  world->getExtents().getExtents(minExtent, maxExtent);

  gViewOffset = world->getExtents().getCentre();

  auto worldSize = maxExtent - minExtent;
  if (worldSize.x > 0.0f && worldSize.y > 0.0f) {
    gViewZoom = clamp(
        min(gWorldViewSize.x / worldSize.x, gWorldViewSize.y / worldSize.y),
        ED_MIN_VIEW_ZOOM, ED_MAX_VIEW_ZOOM);
  }
}

void enableGhost(editor::Document* doc, bool enable) {
  if (!doc->isActive()) {
    return;
  }

  auto ghost = doc->getGhost();

  if (enable) {
    ghost->setFlags(ghost->getFlags() | BW_PRIMITIVE_INTERACTS_FLAG);
  } else {
    doc->removeSelectedPrimitiveIndex(ED_GHOST_INDEX);
    ghost->setFlags(ghost->getFlags() & ~BW_PRIMITIVE_INTERACTS_FLAG);
  }
}

void selectAndHomeGhost(editor::Document* doc) {
  doc->setSelectedPrimitiveIndices({ED_GHOST_INDEX});
  goHome(doc);
}

bw::core::Primitive* createRegularPolygonPrimitive(
    bw::core::Primitive::Operation op,
    bw::core::Primitive::FillRule fillRule,
    uint32_t numSides,
    uint8_t priority,
    wp::Vector2 const& position,
    float scale,
    float angle) {
  auto p = new bw::core::RegularPolygon(op, fillRule, numSides);
  _setPrimitiveParameters(p, priority, position, wp::Vector2::ZERO, scale, angle);
  return p;
}

bw::core::Primitive* createCirclePrimitive(
    bw::core::Primitive::Operation op,
    bw::core::Primitive::FillRule fillRule,
    float resolution,
    uint8_t priority,
    wp::Vector2 const& position,
    float scale,
    float angle) {
  auto p = new bw::core::CirclePolygon(op, fillRule, resolution);
  _setPrimitiveParameters(p, priority, position, wp::Vector2::ZERO, scale, angle);
  return p;
}

bw::core::Primitive* createCircleSegmentPrimitive(
    bw::core::Primitive::Operation op,
    bw::core::Primitive::FillRule fillRule,
    float arcLength,
    float resolution,
    uint8_t priority,
    wp::Vector2 const& position,
    float scale,
    float angle) {
  auto p = new bw::core::CircleSegmentPolygon(op, fillRule, arcLength, resolution);
  _setPrimitiveParameters(p, priority, position, wp::Vector2::ZERO, scale, angle);
  return p;
}

bw::core::Primitive* createTorusPrimitive(
    bw::core::Primitive::Operation op,
    bw::core::Primitive::FillRule fillRule,
    float thickness,
    float resolution,
    uint8_t priority,
    wp::Vector2 const& position,
    float scale,
    float angle) {
  auto p = new bw::core::TorusPolygon(op, fillRule, thickness, resolution);
  _setPrimitiveParameters(p, priority, position, wp::Vector2::ZERO, scale, angle);
  return p;
}

bw::core::Primitive* createTorusSegmentPrimitive(
    bw::core::Primitive::Operation op,
    bw::core::Primitive::FillRule fillRule,
    float thickness,
    float arcLength,
    float resolution,
    uint8_t priority,
    wp::Vector2 const& position,
    float scale,
    float angle) {
  auto p = new bw::core::TorusSegmentPolygon(op, fillRule, thickness, arcLength, resolution);
  _setPrimitiveParameters(p, priority, position, wp::Vector2::ZERO, scale, angle);
  return p;
}

bw::core::Primitive* createRectanglePrimitive(
    bw::core::Primitive::Operation op,
    bw::core::Primitive::FillRule fillRule,
    float xyRatio,
    uint8_t priority,
    wp::Vector2 const& position,
    float scale,
    float angle) {
  auto p = new bw::core::RectanglePolygon(op, fillRule, xyRatio);
  _setPrimitiveParameters(p, priority, position, wp::Vector2::ZERO, scale, angle);
  return p;
}

bw::core::Primitive* createSuperformulaPrimitive(
    bw::core::Primitive::Operation op,
    bw::core::Primitive::FillRule fillRule,
    float values[6],
    float resolution,
    uint8_t priority,
    wp::Vector2 const& position,
    float scale,
    float angle) {
  auto p = new bw::core::SuperformulaPolygon(op, fillRule, resolution, values);
  _setPrimitiveParameters(p, priority, position, wp::Vector2::ZERO, scale, angle);
  return p;
}

}  // namespace editor