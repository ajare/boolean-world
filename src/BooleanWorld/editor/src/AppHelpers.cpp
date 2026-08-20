#define NOMINMAX
#include <Windows.h>
#undef NOMINMAX

#include <algorithm>
#include <format>
#include <filesystem>
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
#include "Defines.h"
#include "Markdown.h"
#include "ExitApplicationException.h"
#include "PrimitiveFieldPreview.h"

extern wp::Vector2 gViewOffset;
extern float gViewZoom;
extern wp::Vector2 gWorldViewSize;
extern std::map<std::string, std::string> gHelpFiles;
extern spdlog::logger* gLogger;

namespace editor {
using namespace std;

void newDocument(editor::Document* doc) {
  getPrimitiveFieldPreview().close();
  doc->newDoc();
}

void openDocument(editor::Document* doc) {
  vector<pair<string, string>> extensions = {
      make_pair("YAML", "yaml"),
      make_pair("Binary world", "world")};

  auto numExtensions = extensions.size();

  auto filters = new nfdfilteritem_t[numExtensions];
  for (uint32_t i = 0; i < numExtensions; ++i) {
    filters[i] = {extensions[i].first.c_str(), extensions[i].second.c_str()};
  }

  nfdchar_t* outPath;
  auto res = NFD_OpenDialog(&outPath, filters, (nfdfiltersize_t)numExtensions, nullptr);

  if (res == NFD_OKAY) {
    string filepath(outPath);
    getPrimitiveFieldPreview().close();
    bool ok = doc->openDoc(filepath);

    NFD_FreePath(outPath);

    if (!ok) {
      ImGui::OpenPopup("Open file failed");
    }
  }

  delete[] filters;
}

void saveDocumentAs(editor::Document* doc) {
  vector<pair<string, string>> extensions = {
      make_pair("YAML", "yaml"),
      make_pair("Binary world", "world")};

  auto numExtensions = extensions.size();

  auto filters = new nfdfilteritem_t[numExtensions];
  for (uint32_t i = 0; i < numExtensions; ++i) {
    filters[i] = {extensions[i].first.c_str(), extensions[i].second.c_str()};
  }

  nfdchar_t* outPath;
  auto res = NFD_SaveDialog(&outPath, filters, (nfdfiltersize_t)numExtensions, nullptr, nullptr);

  if (res == NFD_OKAY) {
    string filepath(outPath);
    doc->saveDocAs(filepath);

    NFD_FreePath(outPath);
  }

  delete[] filters;
}

void saveDocument(editor::Document* doc) {
  if (!doc->hasFilepath()) {
    saveDocumentAs(doc);
  } else {
    doc->saveDoc();
  }
}

void exitApp(editor::Document* doc) {
  getPrimitiveFieldPreview().close();
  throw ExitApplicationException(0, "Exit");
}

void showHelp(editor::Document* doc) {
}

void checkModifiedOperation(editor::Document* doc, string const& title, DocumentHelperFunction func) {
  // Centre dialogue
  ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

  if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    string saveText = "Do you want to save changes?";
    ImGui::Text(saveText.c_str());
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
  auto path = filesystem::path(filepath);
  auto ext = path.extension().string();
  transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  bw::core::World* world{nullptr};

  if (ext == ".yaml" || ext == ".world") {
    shared_ptr<bw::core::Serializer> ser = ext == ".yaml"
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

void goHome(bw::core::Primitive const* primitive) {
  if (primitive) {
    gViewOffset = primitive->getPosition();
  } else {
    gViewOffset.set(0.0f, 0.0f);
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
  goHome(doc->getGhost());
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

bw::core::Primitive* createMeshPrimitive(
    bw::core::Primitive::Operation op,
    bw::core::Primitive::FillRule fillRule,
    uint8_t priority,
    wp::Vector2 const& position,
    float scale,
    float angle) {
  auto p = new bw::core::MeshPrimitive(op, fillRule, {});
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