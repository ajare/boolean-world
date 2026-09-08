#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

struct SubMaterialAuthoringState {
  char name[256]{};
  uint32_t materialIndex{0};
  vector<float> params;
  array<float, 3> colour{};
  bw::core::ChipGenerationParameters chip;
  string editingId;
  string deletionReport;
};

struct SubMaterialPickerState {
  // The Sub-material id selected inside the open picker modal, distinct from
  // the Primitive's committed id until OK (or a double-click) applies it.
  string pendingId;
};

// Owns the thumbnail renderer backing the Sub-material picker modal. It is
// process-lifetime like EditorRenderSystem, but must be released while the GL
// context is still current - see shutdownMaterialPickerThumbnails().
std::unique_ptr<SubMaterialThumbnailRenderer> gMaterialPickerThumbnails;

struct WallMaskBlendPreviewTexture {
  uint32_t texture{};
  uint32_t standardTexture{};
  uint32_t secondaryTexture{};
  string maskReference;
  int maskChannel{};
  vector<float> secondaryParams;
  array<float, 3> secondaryColour{};
};

WallMaskBlendPreviewTexture gWallMaskBlendPreviewTexture;

void shutdownMaterialPickerThumbnails() {
  gMaterialPickerThumbnails.reset();
  if (gWallMaskBlendPreviewTexture.texture) {
    glDeleteTextures(1, &gWallMaskBlendPreviewTexture.texture);
  }
  gWallMaskBlendPreviewTexture = {};
}

void setTechniqueDefaults(
    SubMaterialAuthoringState& state,
    bw::core::TechniqueSchema const& schema) {
  state.materialIndex = schema.materialIndex;
  state.params.clear();
  for (auto const& parameter : schema.parameters) {
    state.params.push_back(parameter.defaultValue);
  }
  state.colour = {0.5f, 0.5f, 0.5f};
  // A Technique says nothing about Chip generation.
  state.chip = {};
}

void renderSubMaterialFields(
    SubMaterialAuthoringState& state,
    bw::core::ProcMaterialData const& data, bool chooseTechnique) {
  if (chooseTechnique) {
    auto schema = find_if(data.techniqueSchemas.begin(), data.techniqueSchemas.end(),
                          [&](auto const& value) {
                            return value.materialIndex == state.materialIndex;
                          });
    string preview = "Technique " + to_string(state.materialIndex);
    if (state.materialIndex < bw::common::TechniqueNames.size()) {
      preview = string(bw::common::TechniqueNames[state.materialIndex]);
    }
    if (ImGui::BeginCombo("Technique", preview.c_str())) {
      for (auto const& candidate : data.techniqueSchemas) {
        string name = "Technique " + to_string(candidate.materialIndex);
        if (candidate.materialIndex < bw::common::TechniqueNames.size()) {
          name = string(bw::common::TechniqueNames[candidate.materialIndex]);
        }
        if (ImGui::Selectable(name.c_str(), candidate.materialIndex == state.materialIndex)) {
          setTechniqueDefaults(state, candidate);
        }
      }
      ImGui::EndCombo();
    }
    if (schema == data.techniqueSchemas.end()) return;
  }

  ImGui::InputText("Name", state.name, sizeof(state.name));
  auto const* schema = data.findTechniqueSchema(state.materialIndex);
  if (schema && !schema->parameters.empty()) {
    ImGui::SeparatorText("Technique parameters");
    for (size_t i = 0; i < schema->parameters.size() && i < state.params.size(); ++i) {
      auto const& parameter = schema->parameters[i];
      ImGui::SliderFloat(parameter.name.c_str(), &state.params[i],
                         parameter.minimum, parameter.maximum);
    }
  }
  ImGui::ColorEdit3("Base colour", state.colour.data());
  ImGui::SeparatorText("Chipping");
  widgets::ChipFields(state.chip);
}

bool renderSubMaterialPicker(
    char const* label, string* subMaterialId, editor::Document* doc,
    bw::core::Primitive* primitive, editor::PrimitiveMaterialSurface surface) {
  auto const& catalogs = procMaterialLibrary().catalogs();
  if (catalogs.empty()) {
    ImGui::TextDisabled("%s material: no ProcMaterial resources", label);
    return false;
  }

  static map<string, int> selectedCatalogs;
  auto& selectedCatalog = selectedCatalogs[label];
  if (auto const* owner = procMaterialLibrary().findCatalogForSubMaterial(*subMaterialId)) {
    selectedCatalog = (int)distance(catalogs.data(), owner);
  }
  selectedCatalog = clamp(selectedCatalog, 0, (int)catalogs.size() - 1);

  string catalogItems;
  for (auto const& catalog : catalogs) {
    catalogItems += catalog.resourceName;
    catalogItems += '\0';
  }
  ImGui::SetNextItemWidth(256);
  ImGui::Combo(format("{} ProcMaterial", label).c_str(), &selectedCatalog,
               catalogItems.c_str(), 6);

  auto const& catalog = catalogs[selectedCatalog];
  int selectedSubMaterial{-1};
  for (size_t i = 0; i < catalog.data.subMaterials.size(); ++i) {
    auto const& subMaterial = catalog.data.subMaterials[i];
    if (subMaterial.id == *subMaterialId) selectedSubMaterial = (int)i;
  }

  static map<string, SubMaterialPickerState> pickerStates;
  auto& pickerState = pickerStates[label];
  auto pickPopup = format("Select {} Sub-material", label);

  string currentName = "(unassigned)";
  if (selectedSubMaterial >= 0) {
    currentName = catalog.data.subMaterials[selectedSubMaterial].displayName;
  }
  if (ImGui::Button(
          format("{} Sub-material: {}##{}-select", label, currentName, label)
              .c_str())) {
    pickerState.pendingId = *subMaterialId;
    ImGui::OpenPopup(pickPopup.c_str());
  }

  constexpr float thumbnailSize =
      static_cast<float>(SubMaterialThumbnailRenderer::size);
  constexpr float tileWidth = thumbnailSize + 12.0f;
  constexpr int pickerColumns = 6;

  if (ImGui::BeginPopupModal(pickPopup.c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Choose a Sub-material from %s.", catalog.resourceName.c_str());

    if (catalog.data.subMaterials.empty()) {
      ImGui::TextDisabled("This ProcMaterial has no Sub-materials.");
    } else {
      if (!gMaterialPickerThumbnails) {
        if (auto* renderSystem = editorRenderSystem()) {
          gMaterialPickerThumbnails =
              make_unique<SubMaterialThumbnailRenderer>(*renderSystem);
        }
      }

      bool applyPicker = false;
      // The grid is laid out in a fixed-width child so the columns do
      // not chase the modal's AutoResize width, and so a large catalog
      // scrolls instead of growing past the bottom of the screen.
      ImGui::BeginChild(
          "thumbnails",
          ImVec2{pickerColumns * tileWidth + ImGui::GetStyle().ScrollbarSize,
                 420.0f});

      for (size_t i = 0; i < catalog.data.subMaterials.size(); ++i) {
        auto const& material = catalog.data.subMaterials[i];
        ImGui::PushID(material.id.c_str());
        ImGui::BeginGroup();
        auto texture = gMaterialPickerThumbnails
                           ? gMaterialPickerThumbnails->texture(material.id)
                           : 0u;
        bool const selected = material.id == pickerState.pendingId;
        if (selected) {
          ImGui::PushStyleColor(
              ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
          ImGui::PushStyleColor(
              ImGuiCol_ButtonHovered,
              ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
        }
        bool clicked =
            texture
                ? ImGui::ImageButton(
                      "thumbnail", static_cast<ImTextureID>(texture),
                      {thumbnailSize, thumbnailSize}, {0.0f, 1.0f},
                      {1.0f, 0.0f})
                : ImGui::Button("Unavailable", {thumbnailSize, thumbnailSize});
        if (selected) ImGui::PopStyleColor(2);
        auto textWidth = ImGui::CalcTextSize(material.displayName.c_str()).x;
        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() +
            max(0.0f, (thumbnailSize - textWidth) * 0.5f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + thumbnailSize);
        ImGui::TextWrapped("%s", material.displayName.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::PopID();

        if (clicked) {
          pickerState.pendingId = material.id;
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            applyPicker = true;
          }
        }
        if ((static_cast<int>(i) + 1) % pickerColumns != 0) {
          ImGui::SameLine();
        }
      }
      ImGui::EndChild();

      bool canApply = !pickerState.pendingId.empty() &&
                      any_of(catalog.data.subMaterials.begin(),
                             catalog.data.subMaterials.end(),
                             [&](auto const& material) {
                               return material.id == pickerState.pendingId;
                             });
      if (!canApply) ImGui::BeginDisabled();
      if (ImGui::Button("OK")) applyPicker = true;
      if (!canApply) ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();

      if (applyPicker && canApply) {
        auto const id = pickerState.pendingId;
        if (id != *subMaterialId) {
          *subMaterialId = id;
          transact(doc, CommandId::SetPrimitiveSubMaterial, [&] {
            setPrimitiveSubMaterial(doc, primitive, surface, id);
          });
        }
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

  static map<string, SubMaterialAuthoringState> authoringStates;
  auto& state = authoringStates[label];
  auto newPopup = format("Create Sub-material##{}", label);
  auto editPopup = format("Edit Sub-material##{}", label);

  if (ImGui::Button(format("New##{}", label).c_str()) &&
      !catalog.data.techniqueSchemas.empty()) {
    state = {};
    snprintf(state.name, sizeof(state.name), "New Sub-material");
    setTechniqueDefaults(state, catalog.data.techniqueSchemas.front());
    ImGui::OpenPopup(newPopup.c_str());
  }
  ImGui::SameLine();
  bool hasSelection = selectedSubMaterial >= 0;
  if (!hasSelection) ImGui::BeginDisabled();
  if (ImGui::Button(format("Edit##{}", label).c_str())) {
    auto const& selected = catalog.data.subMaterials[selectedSubMaterial];
    state = {};
    snprintf(state.name, sizeof(state.name), "%s", selected.displayName.c_str());
    state.materialIndex = selected.materialIndex;
    state.params = selected.paramValues;
    state.colour = selected.baseColour;
    state.chip = selected.chip;
    state.editingId = selected.id;
    ImGui::OpenPopup(editPopup.c_str());
  }
  ImGui::SameLine();
  if (ImGui::Button(format("Delete##{}", label).c_str())) {
    auto const id = catalog.data.subMaterials[selectedSubMaterial].id;
    state.deletionReport = subMaterialDeletionBlockedReason(doc, id);
    if (state.deletionReport.empty()) {
      transactUndoableActionAtomically(
          doc, CommandId::DeleteSubMaterial, [id](Document* doc) {
            return deleteSubMaterial(doc, &procMaterialLibrary(), id);
          });
      if (*subMaterialId == id) subMaterialId->clear();
    }
  }
  if (!hasSelection) ImGui::EndDisabled();
  if (!state.deletionReport.empty()) {
    ImGui::TextWrapped("%s", state.deletionReport.c_str());
  }

  if (ImGui::BeginPopupModal(newPopup.c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    renderSubMaterialFields(state, catalog.data, true);
    if (ImGui::Button("Create")) {
      auto name = string(state.name);
      auto params = state.params;
      auto colour = state.colour;
      auto chip = state.chip;
      auto resourceName = catalog.resourceName;
      auto materialIndex = state.materialIndex;
      string createdId;
      transactUndoableActionAtomically(
          doc, CommandId::CreateSubMaterial, [&](Document* doc) {
            if (!createSubMaterial(doc, &procMaterialLibrary(), resourceName,
                                   name, materialIndex, params, colour, chip,
                                   &createdId)) {
              return false;
            }
            return setPrimitiveSubMaterial(doc, primitive, surface, createdId);
          });
      *subMaterialId = createdId;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal(editPopup.c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    renderSubMaterialFields(state, catalog.data, false);
    if (ImGui::Button("Save")) {
      auto id = state.editingId;
      auto name = string(state.name);
      auto params = state.params;
      auto colour = state.colour;
      auto chip = state.chip;
      transactUndoableActionAtomically(
          doc, CommandId::EditSubMaterial, [&](Document* doc) {
            renameSubMaterial(doc, &procMaterialLibrary(), id, name);
            return editSubMaterial(
                doc, &procMaterialLibrary(), id, params, colour, chip);
          });
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  return false;
}

void renderSubMaterialValue(char const* label, string const& subMaterialId) {
  auto const* catalog = procMaterialLibrary().findCatalogForSubMaterial(subMaterialId);
  if (!catalog) {
    ImGui::Text("%s material: %s", label,
                subMaterialId.empty() ? "(unassigned)" : subMaterialId.c_str());
    return;
  }
  auto const& subMaterials = catalog->data.subMaterials;
  auto found = find_if(subMaterials.begin(), subMaterials.end(),
                       [&subMaterialId](auto const& value) {
                         return value.id == subMaterialId;
                       });
  ImGui::Text("%s material: %s / %s", label, catalog->resourceName.c_str(),
              found->displayName.c_str());
}

// Thumbnail textures for loaded ImageResources, keyed by qualified name.
// Built lazily from the decoded pixels; deleted at shutdown while the GL
// context is still current (see shutdownImageResourceThumbnails).
map<string, uint32_t> gImageResourceThumbnails;

uint32_t imageResourceThumbnail(
    wp::application::resourcesystem::ImageResource const& image) {
  auto key = image.getQualifiedName();
  if (auto found = gImageResourceThumbnails.find(key);
      found != gImageResourceThumbnails.end()) {
    return found->second;
  }

  auto const* data = image.getData();
  auto width = image.getWidth();
  auto height = image.getHeight();
  auto channels = image.getNumChannels();
  if (!data || width <= 0 || height <= 0 || (channels != 3 && channels != 4)) {
    gImageResourceThumbnails[key] = 0u;
    return 0u;
  }

  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(
      GL_TEXTURE_2D, 0, channels == 4 ? GL_RGBA8 : GL_RGB8, width, height, 0,
      channels == 4 ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, data);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glBindTexture(GL_TEXTURE_2D, 0);
  gImageResourceThumbnails[key] = texture;
  return texture;
}

void shutdownImageResourceThumbnails() {
  for (auto const& [key, texture] : gImageResourceThumbnails) {
    (void)key;
    if (texture) glDeleteTextures(1, &texture);
  }
  gImageResourceThumbnails.clear();
}

// The name authored World content stores so Resource::splitName with the
// "World" namespace resolves it back to this exact Resource: unqualified for
// World-namespace resources, "/name" for the default namespace, qualified
// otherwise.
string worldResourceReference(
    wp::application::resourcesystem::Resource const& resource) {
  if (resource.getNamespace() == "World") return resource.getName();
  if (resource.getNamespace().empty()) return "/" + resource.getName();
  return resource.getQualifiedName();
}

// Resolves a stored override reference (as produced by worldImageReference) to
// the ImageResource it names, or nullptr when it does not name one.
wp::application::resourcesystem::ImageResource const* imageResourceForReference(
    string const& reference) {
  auto* renderSystem = editorRenderSystem();
  auto* manager = renderSystem ? renderSystem->resourceManager() : nullptr;
  if (!manager || reference.empty()) return nullptr;

  string namesp;
  string name;
  wp::application::resourcesystem::Resource::splitName(
      reference, "World", &namesp, &name);
  try {
    auto resource = manager->getResource(name, namesp);
    return dynamic_pointer_cast<
               wp::application::resourcesystem::ImageResource>(resource)
        .get();
  } catch (...) {
    return nullptr;
  }
}

// The number of channel options a wall-mask image offers. ImageResource only
// decodes RGB or RGBA, so an unresolved or unexpected image falls back to the
// pre-existing four-option list; Apply still validates the chosen channel.
int wallMaskChannelCount(char const* reference) {
  if (auto const* image = imageResourceForReference(reference)) {
    if (image->getNumChannels() == 3 || image->getNumChannels() == 4) {
      return image->getNumChannels();
    }
  }
  return 4;
}

uint32_t wallMaskBlendedPreviewTexture(
    uint32_t standardTexture, uint32_t secondaryTexture,
    string const& maskReference, int maskChannel,
    vector<float> const& secondaryParams,
    array<float, 3> const& secondaryColour) {
  auto const* mask = imageResourceForReference(maskReference);
  if (!standardTexture || !secondaryTexture || !mask || !mask->getData() ||
      mask->getWidth() <= 0 || mask->getHeight() <= 0 ||
      mask->getNumChannels() < 3 || maskChannel < 0 ||
      maskChannel >= mask->getNumChannels()) {
    return 0;
  }

  auto& preview = gWallMaskBlendPreviewTexture;
  if (preview.texture && preview.standardTexture == standardTexture &&
      preview.secondaryTexture == secondaryTexture &&
      preview.maskReference == maskReference &&
      preview.maskChannel == maskChannel &&
      preview.secondaryParams == secondaryParams &&
      preview.secondaryColour == secondaryColour) {
    return preview.texture;
  }

  constexpr auto size = SubMaterialThumbnailRenderer::size;
  vector<uint8_t> standardPixels(size * size * 4u);
  vector<uint8_t> secondaryPixels(size * size * 4u);
  vector<uint8_t> blendedPixels(size * size * 4u);
  GLint previousTexture{};
  GLint previousPackAlignment{};
  GLint previousUnpackAlignment{};
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
  glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glBindTexture(GL_TEXTURE_2D, standardTexture);
  glGetTexImage(
      GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, standardPixels.data());
  glBindTexture(GL_TEXTURE_2D, secondaryTexture);
  glGetTexImage(
      GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, secondaryPixels.data());

  auto const* maskPixels = mask->getData();
  auto const maskWidth = mask->getWidth();
  auto const maskHeight = mask->getHeight();
  auto const maskChannels = mask->getNumChannels();
  for (uint32_t y = 0; y < size; ++y) {
    auto maskY = static_cast<int64_t>(y) * maskHeight / size;
    for (uint32_t x = 0; x < size; ++x) {
      auto maskX = static_cast<int64_t>(x) * maskWidth / size;
      auto maskIndex =
          (maskY * maskWidth + maskX) * maskChannels + maskChannel;
      auto weight = static_cast<float>(maskPixels[maskIndex]) / 255.0f;
      auto pixel = (y * size + x) * 4u;
      for (uint32_t component = 0; component < 4; ++component) {
        blendedPixels[pixel + component] = static_cast<uint8_t>(std::round(
            standardPixels[pixel + component] * (1.0f - weight) +
            secondaryPixels[pixel + component] * weight));
      }
    }
  }

  if (!preview.texture) glGenTextures(1, &preview.texture);
  glBindTexture(GL_TEXTURE_2D, preview.texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(
      GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA,
      GL_UNSIGNED_BYTE, blendedPixels.data());
  glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
  glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
  glBindTexture(GL_TEXTURE_2D, previousTexture);

  preview.standardTexture = standardTexture;
  preview.secondaryTexture = secondaryTexture;
  preview.maskReference = maskReference;
  preview.maskChannel = maskChannel;
  preview.secondaryParams = secondaryParams;
  preview.secondaryColour = secondaryColour;
  return preview.texture;
}

struct ImageResourcePickerState {
  string pendingReference;
};

bool renderImageResourcePicker(
    char const* label, char* reference, size_t referenceCapacity) {
  auto* renderSystem = editorRenderSystem();
  auto* manager = renderSystem ? renderSystem->resourceManager() : nullptr;
  if (!manager) {
    ImGui::TextDisabled("%s: no ImageResources available.", label);
    return false;
  }

  auto resources = manager->getResourcesByType("Image");
  vector<wp::application::resourcesystem::ImageResource const*> images;
  images.reserve(resources.size());
  for (auto const& resource : resources) {
    if (auto image = dynamic_pointer_cast<
            wp::application::resourcesystem::ImageResource>(resource)) {
      images.push_back(image.get());
    }
  }
  sort(images.begin(), images.end(), [](auto* left, auto* right) {
    return left->getQualifiedName() < right->getQualifiedName();
  });

  static map<string, ImageResourcePickerState> pickerStates;
  auto& pickerState = pickerStates[label];
  auto popup = format("Select {} ImageResource", label);

  string current(reference);
  // Canonicalise the stored spelling so the dialog highlights the same
  // resource it will write back on OK.
  auto canonicalReference = [&](string const& candidate) {
    for (auto const* image : images) {
      if (worldResourceReference(*image) == candidate ||
          image->getQualifiedName() == candidate) {
        return worldResourceReference(*image);
      }
    }
    return candidate;
  };

  auto currentCanonical = canonicalReference(current);
  string currentDisplay = currentCanonical.empty() ? "(none)" : currentCanonical;
  for (auto const* image : images) {
    if (worldResourceReference(*image) == currentCanonical) {
      currentDisplay = image->getName();
      break;
    }
  }

  if (ImGui::Button(
          format("{} Image: {}##{}-select", label, currentDisplay, label)
              .c_str())) {
    pickerState.pendingReference = currentCanonical;
    ImGui::OpenPopup(popup.c_str());
  }

  constexpr float thumbnailSize = 96.0f;
  constexpr float tileWidth = thumbnailSize + 12.0f;
  constexpr int pickerColumns = 6;

  if (ImGui::BeginPopupModal(popup.c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Choose an ImageResource.");

    if (images.empty()) {
      ImGui::TextDisabled("No ImageResources are available.");
    } else {
      bool applyPicker = false;
      ImGui::BeginChild(
          "thumbnails",
          ImVec2{pickerColumns * tileWidth + ImGui::GetStyle().ScrollbarSize,
                 360.0f});

      for (size_t i = 0; i < images.size(); ++i) {
        auto const* image = images[i];
        auto key = image->getQualifiedName();
        auto imageReference = worldResourceReference(*image);
        ImGui::PushID(key.c_str());
        ImGui::BeginGroup();
        auto texture = imageResourceThumbnail(*image);
        bool const selected = imageReference == pickerState.pendingReference;
        if (selected) {
          ImGui::PushStyleColor(
              ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
          ImGui::PushStyleColor(
              ImGuiCol_ButtonHovered,
              ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
        }
        bool clicked =
            texture
                ? ImGui::ImageButton(
                      "thumbnail", static_cast<ImTextureID>(texture),
                      {thumbnailSize, thumbnailSize}, {0.0f, 1.0f},
                      {1.0f, 0.0f})
                : ImGui::Button("Unavailable", {thumbnailSize, thumbnailSize});
        if (selected) ImGui::PopStyleColor(2);
        auto textWidth = ImGui::CalcTextSize(image->getName().c_str()).x;
        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() +
            max(0.0f, (thumbnailSize - textWidth) * 0.5f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + thumbnailSize);
        ImGui::TextWrapped("%s", image->getName().c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::PopID();

        if (clicked) {
          pickerState.pendingReference = imageReference;
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            applyPicker = true;
          }
        }
        if ((static_cast<int>(i) + 1) % pickerColumns != 0) {
          ImGui::SameLine();
        }
      }
      ImGui::EndChild();

      bool canApply = !pickerState.pendingReference.empty();
      if (!canApply) ImGui::BeginDisabled();
      if (ImGui::Button("OK")) applyPicker = true;
      if (!canApply) ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();

      if (applyPicker && canApply) {
        std::snprintf(reference, referenceCapacity, "%s",
                      pickerState.pendingReference.c_str());
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }
  return false;
}

struct EmbossPresetPanelState {
  string editingId;
  char name[256]{};
  bw::core::EmbossData emboss;
};

void loadEmbossPresetPanel(
    EmbossPresetPanelState& state, string const& presetId) {
  state = {};
  auto const* preset = embossingCatalogLibrary().findPreset(presetId);
  if (!preset) return;
  state.editingId = presetId;
  snprintf(state.name, sizeof(state.name), "%s", preset->displayName.c_str());
  state.emboss = preset->emboss;
}

void reloadAuthoredEmbossingCatalog() {
  auto* renderSystem = editorRenderSystem();
  if (!renderSystem) return;
  renderSystem->reloadEmbossingCatalog(
      embossingCatalogLibrary().resourceName());
}

void renderEmbossPresetPanel(
    char const* label, string* presetId, Document* doc,
    bw::core::Primitive* primitive, PrimitiveMaterialSurface surface) {
  auto header = format("{} Embossing", label);
  if (!ImGui::CollapsingHeader(header.c_str())) return;

  static map<string, EmbossPresetPanelState> states;
  auto& state = states[label];
  if (state.editingId != *presetId) loadEmbossPresetPanel(state, *presetId);

  auto const& presets = embossingCatalogLibrary().data().presets;
  int selected = 0;
  string items = "None";
  items += '\0';
  for (size_t i = 0; i < presets.size(); ++i) {
    if (presets[i].id == *presetId) selected = static_cast<int>(i + 1);
    items += presets[i].displayName;
    items += '\0';
  }
  ImGui::SetNextItemWidth(256);
  if (ImGui::Combo(
          format("Preset##{}", label).c_str(), &selected, items.c_str(), 8)) {
    auto id = selected == 0 ? string{} : presets[selected - 1].id;
    *presetId = id;
    transact(doc, CommandId::SetPrimitiveEmbossPreset, [&] {
      setPrimitiveEmbossPreset(
          doc, primitive, surface, id);
    });
    loadEmbossPresetPanel(state, id);
  }

  if (state.editingId.empty()) {
    ImGui::TextDisabled("No Emboss preset assigned.");
    return;
  }

  ImGui::InputText(
      format("Name##{} Emboss", label).c_str(), state.name,
      sizeof(state.name));
  widgets::EmbossFields(state.emboss);

  if (ImGui::Button(format("Save existing##{} Emboss", label).c_str())) {
    auto id = state.editingId;
    auto name = string(state.name);
    auto emboss = state.emboss;
    if (transactUndoableActionAtomically(
            doc, CommandId::EditEmbossPreset, [&](Document* doc) {
              renameEmbossPreset(
                  doc, &embossingCatalogLibrary(), id, name);
              return editEmbossPreset(
                  doc, &embossingCatalogLibrary(), id, emboss);
            })) {
      reloadAuthoredEmbossingCatalog();
      loadEmbossPresetPanel(state, id);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button(
          format("Save as new Emboss preset##{}", label).c_str())) {
    auto name = string(state.name);
    auto emboss = state.emboss;
    string createdId;
    if (transactUndoableActionAtomically(
            doc, CommandId::CreateEmbossPreset, [&](Document* doc) {
              if (!createEmbossPreset(
                      doc, &embossingCatalogLibrary(), name, emboss,
                      &createdId)) {
                return false;
              }
              return setPrimitiveEmbossPreset(
                  doc, primitive, surface, createdId);
            })) {
      *presetId = createdId;
      reloadAuthoredEmbossingCatalog();
      loadEmbossPresetPanel(state, createdId);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button(format("Revert##{} Emboss", label).c_str())) {
    loadEmbossPresetPanel(state, state.editingId);
  }
}

void renderEmbossPresetValue(char const* label, string const& presetId) {
  auto const* preset = embossingCatalogLibrary().findPreset(presetId);
  ImGui::Text(
      "%s Embossing: %s", label,
      preset ? preset->displayName.c_str()
             : (presetId.empty() ? "None" : presetId.c_str()));
}

bool renderPrimitivePropertySet(
    bw::core::PrimitivePropertySet* properties, bool editable,
    editor::Document* doc, editor::Settings&,
    bw::core::Primitive* primitive) {
  bool updateProperties{false};

  ImGui::SetNextItemWidth(128);

  if (editable) {
    updateProperties |= ImGui::InputFloat("Floor base elevation", &properties->floorZ.baseElevation, 1, 8, "%2.1f", ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SetNextItemWidth(128);
    updateProperties |= ImGui::InputFloat2(
        "Floor gradient (X, Y)", &properties->floorZ.gradient.x, "%g",
        ImGuiInputTextFlags_EnterReturnsTrue);
  } else {
    ImGui::Text("Floor base elevation: %2.1f", properties->floorZ.baseElevation);
    ImGui::Text(
        "Floor gradient: (%g, %g)", properties->floorZ.gradient.x,
        properties->floorZ.gradient.y);
  }

  ImGui::SetNextItemWidth(128);

  if (editable) {
    updateProperties |= ImGui::InputFloat("Ceiling base elevation", &properties->ceilingZ.baseElevation, 1, 8, "%2.1f", ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SetNextItemWidth(128);
    updateProperties |= ImGui::InputFloat2(
        "Ceiling gradient (X, Y)", &properties->ceilingZ.gradient.x, "%g",
        ImGuiInputTextFlags_EnterReturnsTrue);
  } else {
    ImGui::Text("Ceiling base elevation: %2.1f", properties->ceilingZ.baseElevation);
    ImGui::Text(
        "Ceiling gradient: (%g, %g)", properties->ceilingZ.gradient.x,
        properties->ceilingZ.gradient.y);
  }

  // Liquid level and type are inert on any operation but Union, so they are
  // only offered where they mean something. The face inspector passes no
  // Primitive and so cannot tell, and shows nothing.
  if (primitive && primitive->getOperation() == bw::core::Primitive::Operation::Union) {
    ImGui::SetNextItemWidth(128);

    if (editable) {
      if (ImGui::InputFloat("Liquid Level", &properties->liquidLevel, 1, 8, "%2.1f", ImGuiInputTextFlags_EnterReturnsTrue)) {
        properties->liquidLevel = std::max(0.0f, properties->liquidLevel);
        updateProperties = true;
      }
    } else {
      ImGui::Text("Liquid Level: %2.1f", properties->liquidLevel);
    }

    ImGui::SetNextItemWidth(128);

    if (editable) {
      if (ImGui::BeginCombo(
              "Liquid Type", bw::core::LiquidTypeName(properties->liquidType))) {
        for (int32_t i = 0; i < bw::core::LiquidTypeCount; ++i) {
          auto liquidType = static_cast<bw::core::LiquidType>(i);
          auto selected = liquidType == properties->liquidType;
          if (ImGui::Selectable(bw::core::LiquidTypeName(liquidType), selected)) {
            properties->liquidType = liquidType;
            updateProperties = true;
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
    } else {
      ImGui::Text("Liquid Type: %s", bw::core::LiquidTypeName(properties->liquidType));
    }
  }

  if (editable) {
    renderSubMaterialPicker(
        "Floor", &properties->floorMaterialId, doc, primitive,
        PrimitiveMaterialSurface::Floor);
    renderSubMaterialPicker(
        "Ceiling", &properties->ceilingMaterialId, doc, primitive,
        PrimitiveMaterialSurface::Ceiling);
    renderSubMaterialPicker(
        "Wall", &properties->wallMaterialId, doc, primitive,
        PrimitiveMaterialSurface::Wall);
    renderEmbossPresetPanel(
        "Floor", &properties->floorEmbossPresetId, doc, primitive,
        PrimitiveMaterialSurface::Floor);
    renderEmbossPresetPanel(
        "Ceiling", &properties->ceilingEmbossPresetId, doc, primitive,
        PrimitiveMaterialSurface::Ceiling);
    renderEmbossPresetPanel(
        "Wall", &properties->wallEmbossPresetId, doc, primitive,
        PrimitiveMaterialSurface::Wall);
  } else {
    renderSubMaterialValue("Floor", properties->floorMaterialId);
    renderSubMaterialValue("Ceiling", properties->ceilingMaterialId);
    renderSubMaterialValue("Wall", properties->wallMaterialId);
    renderEmbossPresetValue("Floor", properties->floorEmbossPresetId);
    renderEmbossPresetValue("Ceiling", properties->ceilingEmbossPresetId);
    renderEmbossPresetValue("Wall", properties->wallEmbossPresetId);
  }

  return updateProperties;
}

}  // namespace editor
