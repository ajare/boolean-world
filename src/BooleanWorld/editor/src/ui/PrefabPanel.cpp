#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

string prefabTagsText(set<string> const& tags) {
  string text;
  for (auto const& tag : tags) {
    if (!text.empty()) text += ',';
    text += tag;
  }
  return text;
}

set<string> parsePrefabTags(string const& text) {
  set<string> tags;
  string tag;
  for (unsigned char character : text) {
    if (character == ',') {
      if (!tag.empty()) tags.insert(move(tag));
      tag.clear();
    } else if (character != ' ' && character != '\t' &&
               character != '\r' && character != '\n') {
      auto const isUpper = character >= 'A' && character <= 'Z';
      tag.push_back(isUpper ? static_cast<char>(character - 'A' + 'a')
                            : static_cast<char>(character));
    }
  }
  if (!tag.empty()) tags.insert(move(tag));
  return tags;
}

int filterPrefabTagCharacter(ImGuiInputTextCallbackData* data) {
  auto const character = data->EventChar;
  auto const isLetter =
      (character >= 'a' && character <= 'z') ||
      (character >= 'A' && character <= 'Z');
  auto const isDigit = character >= '0' && character <= '9';
  auto const isFormattingWhitespace =
      character == ' ' || character == '\t' || character == '\r' ||
      character == '\n';
  return isLetter || isDigit || character == '_' || character == '-' ||
                 character == ',' || isFormattingWhitespace
             ? 0
             : 1;
}

void renderPrefabsView(
    ViewContext& context, bw::core::DefinePrefabs* step) {
  auto* doc = context.doc;
  auto* layer = doc->getWorld()->getActiveLayer();
  bool listChanged = false;

  for (uint32_t i = 0; i < step->getNumPrefabs(); ++i) {
    auto* prefab = step->getPrefab(i);
    ImGui::PushID(prefab->getId());

    if (ImGui::RadioButton(
            "##Select", step->getSelectedPrefab() == prefab)) {
      selectPrefab(doc, layer, step, prefab);
    }

    ImGui::SameLine();
    // Only one Prefab's name can be under edit at a time, so a single static
    // buffer (keyed by which Prefab is active) is enough - it stays synced to
    // the model except while the user is actively typing into it.
    static char name[256]{};
    static bw::core::Prefab* editingPrefab = nullptr;
    if (editingPrefab != prefab) {
      snprintf(name, sizeof(name), "%s", prefab->getName().c_str());
    }
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("##Name", name, sizeof(name));
    if (ImGui::IsItemActivated()) {
      editingPrefab = prefab;
    }
    if (editingPrefab == prefab && ImGui::IsItemDeactivatedAfterEdit()) {
      transact(doc, CommandId::RenamePrefab, [&] { renamePrefab(doc, layer, step, prefab, string(name)); });
    }
    if (ImGui::IsItemDeactivated()) {
      editingPrefab = nullptr;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(92.0f);
    auto const currentSize = prefab->getTileSize();
    auto const currentSide = bw::core::prefabTileSide(currentSize);
    auto const currentSizeLabel = format("{}x{}", currentSide, currentSide);
    if (ImGui::BeginCombo("##TileSize", currentSizeLabel.c_str())) {
      for (auto size : bw::core::allPrefabTileSizes) {
        auto const side = bw::core::prefabTileSide(size);
        auto const label = format("{}x{}", side, side);
        auto const reason = prefabSizeChangeBlockedReason(
            layer, step, prefab, size);
        ImGui::BeginDisabled(!reason.empty());
        if (ImGui::Selectable(label.c_str(), size == currentSize) &&
            size != currentSize) {
          transact(doc, CommandId::SetPrefabTileSize, [&] { setPrefabTileSize(doc, layer, step, prefab, size); });
        }
        ImGui::EndDisabled();
        if (!reason.empty() &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
          ImGui::SetTooltip("%s", reason.c_str());
        }
      }
      ImGui::EndCombo();
    }

    ImGui::SameLine();
    auto blockedReason = prefabDeletionBlockedReason(layer, step, prefab);
    ImGui::BeginDisabled(!blockedReason.empty());
    if (ImGui::Button(ICON_FA_TRASH "##DeletePrefab")) {
      transact(doc, CommandId::DeletePrefab, [&] { deletePrefab(doc, layer, step, prefab); });
      listChanged = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
        !blockedReason.empty()) {
      ImGui::SetTooltip("%s", blockedReason.c_str());
    }

    ImGui::PopID();
    if (listChanged) {
      break;
    }
  }

  if (ImGui::Button("Create Prefab")) {
    transact(doc, CommandId::CreatePrefab, [&] { createPrefab(doc, layer, step); });
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Tiling type");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140.0f);
  auto const tilingType = step->getTilingType();
  auto const tilingTypeName = prefabTilingGuideName(tilingType);
  if (ImGui::BeginCombo("##PrefabTilingType", tilingTypeName.data())) {
    for (auto const& definition : prefabTilingGuideDefinitions()) {
      bool selected = tilingType == definition.type;
      if (ImGui::Selectable(definition.name.data(), selected) && !selected) {
        transact(doc, CommandId::SetPrefabTilingType, [&] { setPrefabTilingType(doc, layer, step, definition.type); });
      }
    }
    ImGui::EndCombo();
  }
}

void renderSelectedPrefabView(
    ViewContext& context, bw::core::DefinePrefabs* step,
    bw::core::Prefab* prefab) {
  auto* doc = context.doc;
  auto* layer = doc->getWorld()->getActiveLayer();
  static string text;
  static bw::core::Prefab* editingPrefab = nullptr;
  if (editingPrefab != prefab) {
    text = prefabTagsText(prefab->getTags());
  }

  ImGui::TextUnformatted("Tags");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1.0f);
  widgets::InputText(
      "##PrefabTags", &text, ImGuiInputTextFlags_CallbackCharFilter,
      filterPrefabTagCharacter);
  if (ImGui::IsItemActivated()) {
    editingPrefab = prefab;
  }
  if (editingPrefab == prefab && ImGui::IsItemDeactivatedAfterEdit()) {
    transact(doc, CommandId::SetPrefabTags, [&] { setPrefabTags(doc, layer, step, prefab, parsePrefabTags(text)); });
  }
  if (ImGui::IsItemDeactivated()) {
    editingPrefab = nullptr;
  }
}

void renderPrefabThumbnail(
    bw::core::Prefab* prefab,
    ImVec2 const& topLeft,
    float size,
    bool selected,
    editor::Settings const& settings) {
  auto* drawList = ImGui::GetWindowDrawList();
  auto const bottomRight = topLeft + ImVec2{size, size};
  auto const hovered = ImGui::IsItemHovered();
  drawList->AddRectFilled(
      topLeft, bottomRight,
      ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg),
      ImGui::GetStyle().FrameRounding);

  wp::Vector2 minExtent{numeric_limits<float>::max(), numeric_limits<float>::max()};
  wp::Vector2 maxExtent{numeric_limits<float>::lowest(), numeric_limits<float>::lowest()};
  bool hasGeometry = false;
  for (auto* primitive : prefab->getPrimitives()) {
    if (primitive->getNumVertices() > 0 && primitive->getVertices().empty()) {
      primitive->updateVertexPositions();
    }
    for (auto const& complexPolygon : primitive->getVertices()) {
      for (auto const& polygon : complexPolygon) {
        for (auto const& vertex : polygon) {
          minExtent.x = min(minExtent.x, vertex.p.x);
          minExtent.y = min(minExtent.y, vertex.p.y);
          maxExtent.x = max(maxExtent.x, vertex.p.x);
          maxExtent.y = max(maxExtent.y, vertex.p.y);
          hasGeometry = true;
        }
      }
    }
  }

  if (hasGeometry) {
    constexpr float padding = 8.0f;
    auto extent = maxExtent - minExtent;
    auto maxDimension = max(max(extent.x, extent.y), 0.001f);
    auto scale = (size - padding * 2.0f) / maxDimension;
    auto centre = (minExtent + maxExtent) * 0.5f;
    auto screenCentre = topLeft + ImVec2{size * 0.5f, size * 0.5f};
    auto toThumbnail = [&](wp::Vector2 const& point) {
      return ImVec2{screenCentre.x + (point.x - centre.x) * scale,
                    screenCentre.y - (point.y - centre.y) * scale};
    };

    drawList->PushClipRect(topLeft, bottomRight, true);
    for (auto const* primitive : prefab->getPrimitives()) {
      for (auto const& complexPolygon : primitive->getVertices()) {
        for (auto const& polygon : complexPolygon) {
          vector<ImVec2> points;
          points.reserve(polygon.size());
          for (auto const& vertex : polygon) {
            points.push_back(toThumbnail(vertex.p));
          }
          if (points.size() >= 2) {
            drawList->AddPolyline(
                points.data(), static_cast<int>(points.size()),
                settings.primitiveColour, ImDrawFlags_Closed, 1.5f);
          }
        }
      }
    }
    drawList->PopClipRect();
  } else {
    auto const* emptyText = "Empty";
    auto textSize = ImGui::CalcTextSize(emptyText);
    drawList->AddText(
        topLeft + ImVec2{(size - textSize.x) * 0.5f,
                         (size - textSize.y) * 0.5f},
        ImGui::GetColorU32(ImGuiCol_TextDisabled), emptyText);
  }

  drawList->AddRect(
      topLeft, bottomRight,
      selected ? static_cast<ImU32>(settings.selectedPrimitiveColour)
               : ImGui::GetColorU32(ImGuiCol_Border),
      ImGui::GetStyle().FrameRounding, 0, selected ? 3.0f : 1.0f);
}

void renderPrefabFieldView(
    ViewContext& context, bw::core::PrefabField* field) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto* layer = doc->getWorld()->getActiveLayer();
  auto* definitions = field->getDefinePrefabs(*layer);
  if (!definitions) {
    ImGui::TextUnformatted("DefinePrefabs step");
    for (uint32_t i = 0; i < layer->getNumSteps(); ++i) {
      auto* candidate = dynamic_cast<bw::core::DefinePrefabs*>(layer->getStep(i));
      if (candidate && ImGui::Selectable(format("{} :: DefinePrefabs", i).c_str())) {
        transact(doc, CommandId::BindPrefabField, [&] { bindPrefabField(doc, layer, field, candidate); });
      }
    }
    return;
  }

  constexpr float thumbnailSize = 88.0f;
  auto const& style = ImGui::GetStyle();
  auto availableWidth = ImGui::GetContentRegionAvail().x;
  auto columnWidth = thumbnailSize + style.CellPadding.x * 2.0f;
  auto columns = max(1, static_cast<int>(
                            (availableWidth + style.ItemSpacing.x) /
                            (columnWidth + style.ItemSpacing.x)));
  auto selectedPrefab = field->getSelectedPrefab(*layer);

  if (ImGui::BeginTable(
          "##PrefabThumbnailGrid", columns,
          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
    for (int column = 0; column < columns; ++column) {
      ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, columnWidth);
    }

    for (auto* prefab : definitions->getPrefabs()) {
      ImGui::TableNextColumn();
      ImGui::PushID(prefab->getId());

      auto topLeft = ImGui::GetCursorScreenPos();
      if (ImGui::InvisibleButton(
              "##Thumbnail", {thumbnailSize, thumbnailSize})) {
        selectPrefabForField(doc, layer, field, prefab);
      }
      renderPrefabThumbnail(
          prefab, topLeft, thumbnailSize, selectedPrefab == prefab, settings);

      auto nameSize = ImGui::CalcTextSize(prefab->getName().c_str(), nullptr, false, thumbnailSize);
      ImGui::SetCursorPosX(
          ImGui::GetCursorPosX() + max(0.0f, (thumbnailSize - nameSize.x) * 0.5f));
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + thumbnailSize);
      ImGui::TextWrapped("%s", prefab->getName().c_str());
      ImGui::PopTextWrapPos();

      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  if (field->getSelectedPrefab(*layer) && ImGui::Button("Clear palette selection")) {
    selectPrefabForField(doc, layer, field, nullptr);
  }

  if (field->hasSelectedTile()) {
    auto const tile = field->getSelectedTile();
    auto const* instance = field->getInstance(tile);
    if (instance && tile.size != bw::core::PrefabTileSize::Size256) {
      bool add = instance->mode == bw::core::TileMode::Add;
      if (ImGui::Checkbox("Add", &add)) {
        transact(doc, CommandId::SetPrefabInstanceMode, [&] { setPrefabInstanceMode(doc, layer, field, tile, add ? bw::core::TileMode::Add : bw::core::TileMode::Replace); });
      }
    }
  }
}


}  // namespace editor
