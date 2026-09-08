#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

void renderMeshDrawToolView(editor::Document* doc, editor::Settings& settings) {
  auto* ghost = doc->getGhost();

  setOperationWidget(doc, ghost, 3);

  int priority = (int)ghost->getPriority();
  widgets::HelpMarker(
      "Priority the drawn MeshPrimitive receives within this LayerBuildStep. Lower values fold earlier inside the step.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderInt("Priority##MeshDraw", &priority, BW_PRIORITY_MIN_VALUE, BW_PRIORITY_MAX_VALUE)) {
    ghost->setPriority((uint8_t)priority);
  }

  if (ImGui::IsItemActivated()) {
    beginTransaction(doc, "", 0.0f);
  } else if (ImGui::IsItemDeactivatedAfterEdit()) {
    commitUndoableAction(doc, format("Set Drawn Mesh Priority to {}", (int)ghost->getPriority()));
  } else if (ImGui::IsItemDeactivated()) {
    abandonUndoableAction(doc);
  }

  auto reason = doc->meshDrawToolUnavailableReason(settings);
  bool armed = doc->meshDrawToolArmed();

  widgets::HelpMarker(reason.empty()
                          ? "Arm the draw tool, then click in the world view to place vertices."
                          : reason.c_str());
  ImGui::SameLine();
  ImGui::BeginDisabled(!reason.empty() || armed);

  if (ImGui::Button("Draw mesh##MeshDraw")) {
    doc->armMeshDrawTool(settings);
  }

  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextUnformatted("Ctrl+Shift+C");

  if (!reason.empty()) {
    ImGui::TextWrapped("%s", reason.c_str());
  } else if (armed) {
    ImGui::Text("Drawing: %zu vertex(es) placed.", doc->getMeshDrawVertices().size());
    if (!doc->getMeshDrawVertices().empty()) {
      ImGui::TextUnformatted(doc->meshDrawCreatesHole()
                                 ? "Context: hole in the highlighted filled region."
                             : doc->meshDrawCreatesIsland()
                                 ? "Context: filled island in the highlighted hole."
                             : doc->meshDrawTouchesRingBoundary()
                                 ? "Context: touching an existing Ring; cut or sibling resolved on close."
                                 : "Context: new MeshPrimitive (unconfined).");
    }
    if (!doc->getMeshDrawRejection().empty()) {
      ImGui::TextColored(
          ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "%s",
          doc->getMeshDrawRejection().c_str());
    }
    ImGui::TextWrapped(
        "Click to place a vertex, and click the first vertex to close the shape "
        "(three vertices minimum).  Backspace steps back a vertex; Esc discards "
        "the shape, and a second Esc disarms the tool.");
  }
}

bool activeMeshBelongsToPrefab(editor::Document* doc) {
  auto* activeLayer = doc->getWorld()->getActiveLayer();
  auto* definitions = dynamic_cast<bw::core::DefinePrefabs*>(
      activeLayer->getActiveStep());
  auto const primitiveIndex = doc->getActiveMeshPrimitiveIndex();
  return definitions && primitiveIndex < activeLayer->getNumPrimitives() &&
         definitions->ownsPrimitive(activeLayer->getPrimitive(primitiveIndex));
}

void renderPrefabTopologyMetadata(
    editor::Document* doc, uint32_t topologyIndex, bool edge) {
  using MetadataEntry = pair<string, string>;
  static wp::geometry::Mesh const* draftMesh = nullptr;
  static uint32_t draftIndex = ~0u;
  static bool draftIsEdge = false;
  static vector<MetadataEntry> draft;

  auto* mesh = doc->getActiveMesh();
  if (draftMesh != mesh || draftIndex != topologyIndex ||
      draftIsEdge != edge) {
    draftMesh = mesh;
    draftIndex = topologyIndex;
    draftIsEdge = edge;
    auto const metadata = edge
                              ? doc->getActiveMeshEdgeMetadata(topologyIndex)
                              : doc->getActiveMeshVertexMetadata(topologyIndex);
    draft.assign(metadata.begin(), metadata.end());
  }

  auto toMetadata = [&]() -> optional<map<string, string>> {
    map<string, string> metadata;
    for (auto const& [key, value] : draft) {
      if (key.empty() || !metadata.emplace(key, value).second) return nullopt;
    }
    return metadata;
  };
  auto commit = [&] {
    auto metadata = toMetadata();
    if (!metadata) return;
    if (edge) {
      transact(doc, "Set Prefab Edge Metadata", [&] { setMeshEdgeMetadata(doc, topologyIndex, *metadata); });
    } else {
      transact(doc, "Set Prefab Vertex Metadata", [&] { setMeshVertexMetadata(doc, topologyIndex, *metadata); });
    }
  };

  ImGui::PushID(edge ? "PrefabEdgeMetadata" : "PrefabVertexMetadata");
  ImGui::Separator();
  ImGui::TextUnformatted(
      edge ? "Prefab edge metadata" : "Prefab vertex metadata");
  ImGui::TextUnformatted("Key");
  ImGui::SameLine(154.0f);
  ImGui::TextUnformatted("Value");
  bool commitAfterRow = false;
  optional<size_t> deleteRow;
  for (size_t index = 0; index < draft.size(); ++index) {
    ImGui::PushID(static_cast<int>(index));
    ImGui::SetNextItemWidth(130.0f);
    widgets::InputText("##MetadataKey", &draft[index].first);
    commitAfterRow |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    widgets::InputText("##MetadataValue", &draft[index].second);
    commitAfterRow |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TRASH "##DeleteMetadata")) {
      deleteRow = index;
    }
    ImGui::PopID();
  }

  if (!toMetadata()) {
    ImGui::TextDisabled("Metadata keys must be non-empty and unique.");
  }
  if (deleteRow) {
    draft.erase(draft.begin() + *deleteRow);
    commit();
  } else if (commitAfterRow) {
    commit();
  }
  if (ImGui::Button("Add metadata")) {
    draft.emplace_back();
  }
  ImGui::PopID();
}

void renderMeshView(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  renderMeshDrawToolView(doc, settings);
  ImGui::Separator();

  auto index = doc->getActiveMeshPrimitiveIndex();
  if (doc->getActiveMesh() && !doc->meshIneligibilityReason(index).empty()) {
    doc->clearActiveMesh();
    settings.activeMeshPrimitiveIndex = ~0u;
    index = ~0u;
  }
  if (!doc->getActiveMesh()) {
    ImGui::TextUnformatted("No active MeshPrimitive");
    auto const& explanation = doc->getMeshHoverExplanation();
    if (!explanation.empty()) {
      ImGui::TextWrapped("%s", explanation.c_str());
    } else {
      ImGui::TextWrapped("Click an eligible MeshPrimitive in the selected LayerBuildStep.");
    }
    return;
  }

  ImGui::Text("Active MeshPrimitive: %u", index);
  ImGui::Text("Vertices selected: %zu", doc->getSelectedMeshVertexIndices().size());
  ImGui::Text("Edges selected: %zu", doc->getSelectedMeshEdgeIndices().size());
  ImGui::Text("Rings selected: %zu", doc->getSelectedMeshRingIndices().size());
  ImGui::TextUnformatted("Showing t=0 rest pose while active.");

  auto sliceReason = doc->meshSliceToolUnavailableReason(settings);
  auto sliceArmed = doc->meshSliceToolArmed();
  ImGui::BeginDisabled(!sliceReason.empty() || sliceArmed);
  if (ImGui::Button("Slice##MeshSlice")) {
    doc->armMeshSliceTool(settings);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextUnformatted("Ctrl+Shift+S");
  if (sliceArmed) {
    ImGui::TextWrapped(
        doc->getMeshSliceFirstVertexIndex() == ~0u
            ? "Slice: select a Vertex on a Shell or Island."
            : "Slice: select a non-adjacent Vertex on the same Ring. Esc cancels.");
  } else if (!sliceReason.empty() &&
             settings.meshSubMode == Settings::MeshSubMode::Vertex) {
    ImGui::TextWrapped("%s", sliceReason.c_str());
  }

  auto const& explanation = doc->getMeshHoverExplanation();
  if (!explanation.empty() && explanation != "Nothing under the cursor.") {
    ImGui::TextWrapped("Under cursor: %s", explanation.c_str());
  }

  auto const& selectedVertices = doc->getSelectedMeshVertexIndices();
  if (selectedVertices.size() == 1) {
    auto vertexIndex = *selectedVertices.begin();
    auto const& position = doc->getActiveMesh()->getVertex(vertexIndex).getPosition();

    ImGui::Text("Selected vertex: %u", vertexIndex);
    float x = position.x;
    float y = position.y;
    ImGui::SetNextItemWidth(112);
    if (ImGui::InputFloat("X##MeshVertexPosition", &x)) {
      transact(doc, "Set Mesh Vertex X Position", [&] { setMeshVertexPosition(doc, vertexIndex, wp::Vector2{x, position.y}); });
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(112);
    if (ImGui::InputFloat("Y##MeshVertexPosition", &y)) {
      transact(doc, "Set Mesh Vertex Y Position", [&] { setMeshVertexPosition(doc, vertexIndex, wp::Vector2{position.x, y}); });
    }

    ImGui::SameLine();
    auto indices = set<uint32_t>{vertexIndex};
    auto canDelete =
        doc->previewMeshSubObjectDeletionCount(
            Settings::MeshSubMode::Vertex, indices) > 0;
    ImGui::BeginDisabled(!canDelete);
    if (ImGui::Button(ICON_FA_TRASH "##DeleteSelectedMeshVertex")) {
      transact(doc, "Delete Mesh Vertex", [&] { deleteMeshSubObjects(doc, Settings::MeshSubMode::Vertex, indices); });
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip(canDelete
                            ? "Delete selected vertex"
                            : "This Ring cannot contain fewer than three vertices");
    }

    if (activeMeshBelongsToPrefab(doc)) {
      renderPrefabTopologyMetadata(doc, vertexIndex, false);
    }
  }

  auto const& selectedEdges = doc->getSelectedMeshEdgeIndices();
  if (settings.meshSubMode != Settings::MeshSubMode::Edge) {
    ImGui::TextDisabled("Wall normal map: unavailable in this Mesh sub-mode.");
  } else if (selectedEdges.empty()) {
    ImGui::TextDisabled("Wall normal map: select exactly one External edge.");
  } else if (selectedEdges.size() != 1) {
    ImGui::TextDisabled("Wall normal map: multiple edges are selected; select exactly one External edge.");
  } else if (!doc->isActiveMeshEdgeNormalMapEditable(*selectedEdges.begin())) {
    ImGui::TextDisabled("Wall normal map: the selected edge is Internal; only External edges are editable.");
  }
  if (settings.meshSubMode != Settings::MeshSubMode::Edge) {
    ImGui::TextDisabled("Wall mask: unavailable in this Mesh sub-mode.");
  } else if (selectedEdges.empty()) {
    ImGui::TextDisabled("Wall mask: select exactly one External edge.");
  } else if (selectedEdges.size() != 1) {
    ImGui::TextDisabled("Wall mask: multiple edges are selected; select exactly one External edge.");
  } else if (!doc->isActiveMeshEdgeWallMaskEditable(*selectedEdges.begin())) {
    ImGui::TextDisabled("Wall mask: the selected edge is Internal; only External edges are editable.");
  }
  if (selectedEdges.size() == 1) {
    auto edgeIndex = *selectedEdges.begin();
    auto indices = set<uint32_t>{edgeIndex};
    ImGui::Text("Selected edge: %u", edgeIndex);
    if (activeMeshBelongsToPrefab(doc)) {
      renderPrefabTopologyMetadata(doc, edgeIndex, true);
    }
    if (doc->isActiveMeshEdgeCollisionEditable(edgeIndex)) {
      auto collisionOverride =
          doc->getActiveMeshEdgeCollisionOverride(edgeIndex);
      int collisionOption = !collisionOverride.has_value()
                                ? 0
                                : (*collisionOverride ? 1 : 2);
      if (ImGui::Combo(
              "Collision##SelectedMeshEdge", &collisionOption,
              "Not set\0Collides\0Doesn't collide\0")) {
        optional<bool> value = collisionOption == 0
                                   ? nullopt
                                   : optional<bool>{collisionOption == 1};
        transact(doc, "Set Mesh Edge Collision Override", [&] { setMeshEdgeCollisionOverride(doc, edgeIndex, value); });
      }
    }
    if (doc->isActiveMeshEdgeVisibilityEditable(edgeIndex)) {
      auto visible = doc->getActiveMeshEdgeVisible(edgeIndex);
      if (ImGui::Checkbox("Visible##SelectedMeshEdge", &visible)) {
        transact(doc, "Set Mesh Edge Visible", [&] { setMeshEdgeVisible(doc, edgeIndex, visible); });
      }
    }

    static uint32_t normalMapDraftEdge = ~0u;
    static int normalMapState = 0;
    static char normalMapResource[512]{};
    static float normalMapRepeat = 1.0f;
    static float normalMapStrength = 1.0f;
    static string normalMapError;
    if (normalMapDraftEdge != edgeIndex) {
      normalMapDraftEdge = edgeIndex;
      normalMapError.clear();
      auto value = doc->getActiveMeshEdgeNormalMapOverride(edgeIndex);
      normalMapState = static_cast<int>(value.state());
      normalMapResource[0] = '\0';
      if (auto image = value.imageData()) {
        std::snprintf(normalMapResource, sizeof(normalMapResource), "%s",
                      image->resourceName.c_str());
        normalMapRepeat = image->repeat;
        normalMapStrength = image->strength;
      }
    }
    auto normalMapEditable =
        settings.meshSubMode == Settings::MeshSubMode::Edge &&
        doc->isActiveMeshEdgeNormalMapEditable(edgeIndex);
    ImGui::BeginDisabled(!normalMapEditable);
    ImGui::Combo("Normal map##SelectedMeshEdge", &normalMapState,
                 "Not set\0Disabled\0Image\0");
    if (normalMapState == static_cast<int>(
                              bw::core::WallNormalMapOverride::State::Image)) {
      renderImageResourcePicker("Wall normal map", normalMapResource,
                                sizeof(normalMapResource));
      ImGui::InputFloat("Repeat##WallNormalMap", &normalMapRepeat);
      ImGui::InputFloat("Strength##WallNormalMap", &normalMapStrength);
    }
    if (ImGui::Button("Apply wall normal map##SelectedMeshEdge")) {
      try {
        auto value = normalMapState == 0
                         ? bw::core::WallNormalMapOverride::unset()
                     : normalMapState == 1
                         ? bw::core::WallNormalMapOverride::disabled()
                         : bw::core::WallNormalMapOverride::image(
                               normalMapResource, normalMapRepeat,
                               normalMapStrength);
        if (auto image = value.imageData()) {
          auto* renderSystem = editorRenderSystem();
          if (!renderSystem) {
            throw runtime_error("The editor resource system is unavailable.");
          }
          auto* manager = renderSystem->resourceManager();
          string namesp;
          string name;
          wp::application::resourcesystem::Resource::splitName(
              image->resourceName, "World", &namesp, &name);
          auto resource = manager->getResource(name, namesp);
          if (!dynamic_pointer_cast<
                  wp::application::resourcesystem::ImageResource>(resource)) {
            throw runtime_error("The named resource is not an ImageResource.");
          }
          manager->createResource(resource);
          manager->loadResource(resource);
        }
        if (!transactUndoableActionAtomically(doc, "Set Mesh Edge Wall Normal Map", [&](Document* doc) { return setMeshEdgeNormalMapOverride(doc, edgeIndex, value); })) {
          normalMapError = "The selected edge cannot accept a wall normal map.";
        } else {
          string dependencyError;
          if (!editorRenderSystem()->loadWorldDependencies(
                  doc->getWorld()->getDependentResourceNames(), "World",
                  &dependencyError)) {
            throw runtime_error(dependencyError);
          }
          normalMapError.clear();
        }
      } catch (exception const& error) {
        normalMapError = error.what();
      }
    }
    ImGui::EndDisabled();
    if (!normalMapError.empty()) {
      ImGui::TextWrapped("%s", normalMapError.c_str());
    }

    static uint32_t wallMaskDraftEdge = ~0u;
    static int wallMaskState = 0;
    static char wallMaskResource[512]{};
    static int wallMaskChannel = 0;
    static bw::core::WallMaskOverride::BlendParameters wallMaskBlend{};
    static bw::core::WallMaskOverride::BlendColour wallMaskBlendColour{};
    static string wallMaskError;
    static uint32_t wallMaskPreviewEdge = ~0u;
    static vector<float> wallMaskPreviewParams;
    static bw::core::WallMaskOverride::BlendColour wallMaskPreviewColour{};
    static bool wallMaskPreviewBlendEnabled{};

    // The blend sliders share the primary wall Sub-material's Technique
    // schema: same parameter names, count, and [minimum, maximum] bounds.
    bw::core::SubMaterial const* primaryWall = nullptr;
    bw::core::TechniqueSchema const* wallSchema = nullptr;
    auto meshPrimitiveIndex = doc->getActiveMeshPrimitiveIndex();
    if (meshPrimitiveIndex != ~0u) {
      if (auto* primitive = doc->getWorld()->getPrimitive(meshPrimitiveIndex)) {
        primaryWall = procMaterialLibrary().findSubMaterial(
            primitive->getProperties().wallMaterialId);
        if (primaryWall) {
          if (auto const* catalog = procMaterialLibrary().findCatalogForSubMaterial(
                  primaryWall->id)) {
            wallSchema = catalog->data.findTechniqueSchema(
                primaryWall->materialIndex);
          }
        }
      }
    }

    if (wallMaskDraftEdge != edgeIndex) {
      wallMaskDraftEdge = edgeIndex;
      wallMaskError.clear();
      auto value = doc->getActiveMeshEdgeWallMaskOverride(edgeIndex);
      wallMaskState = static_cast<int>(value.state());
      wallMaskResource[0] = '\0';
      wallMaskChannel = 0;
      wallMaskBlend.fill(0.0f);
      wallMaskBlendColour = {1.0f, 1.0f, 1.0f};
      if (auto image = value.imageData()) {
        std::snprintf(wallMaskResource, sizeof(wallMaskResource), "%s",
                      image->resourceName.c_str());
        wallMaskChannel = image->channel;
        wallMaskBlend = image->blendParameters;
        wallMaskBlendColour = image->blendColour;
      } else if (primaryWall) {
        // First enable defaults to a copy of the primary's current
        // parameters and base colour, so the mask starts as a visible no-op.
        for (size_t i = 0;
             i < primaryWall->paramValues.size() && i < wallMaskBlend.size();
             ++i) {
          wallMaskBlend[i] = primaryWall->paramValues[i];
        }
        wallMaskBlendColour = primaryWall->baseColour;
      }
    }
    auto wallMaskEditable =
        settings.meshSubMode == Settings::MeshSubMode::Edge &&
        doc->isActiveMeshEdgeWallMaskEditable(edgeIndex);
    ImGui::BeginDisabled(!wallMaskEditable);
    ImGui::Combo("Wall mask##SelectedMeshEdge", &wallMaskState,
                 "Not set\0Disabled\0Image\0");
    if (wallMaskState ==
        static_cast<int>(bw::core::WallMaskOverride::State::Image)) {
      renderImageResourcePicker("Wall mask", wallMaskResource,
                                sizeof(wallMaskResource));
      auto channelCount = wallMaskChannelCount(wallMaskResource);
      wallMaskChannel = clamp(wallMaskChannel, 0, channelCount - 1);
      string channelItems;
      auto addChannel = [&](char const* name) {
        channelItems += name;
        channelItems += '\0';
      };
      addChannel("Red");
      addChannel("Green");
      addChannel("Blue");
      if (channelCount >= 4) addChannel("Alpha");
      ImGui::Combo("Channel##WallMask", &wallMaskChannel,
                   channelItems.c_str());
      if (!wallSchema) {
        ImGui::TextDisabled(
            "Wall mask blend: the wall Sub-material has no Technique schema.");
      }
    }
    if (ImGui::Button("Apply wall mask##SelectedMeshEdge")) {
      try {
        auto value = wallMaskState == 0
                         ? bw::core::WallMaskOverride::unset()
                     : wallMaskState == 1
                         ? bw::core::WallMaskOverride::disabled()
                         : bw::core::WallMaskOverride::image(
                               wallMaskResource,
                               static_cast<uint8_t>(wallMaskChannel),
                               wallMaskBlend, wallMaskBlendColour);
        if (auto image = value.imageData()) {
          auto* renderSystem = editorRenderSystem();
          if (!renderSystem) {
            throw runtime_error("The editor resource system is unavailable.");
          }
          auto* manager = renderSystem->resourceManager();
          string namesp;
          string name;
          wp::application::resourcesystem::Resource::splitName(
              image->resourceName, "World", &namesp, &name);
          auto resource = manager->getResource(name, namesp);
          auto imageResource = dynamic_pointer_cast<
              wp::application::resourcesystem::ImageResource>(resource);
          if (!imageResource) {
            throw runtime_error("The named resource is not an ImageResource.");
          }
          manager->createResource(resource);
          manager->loadResource(resource);
          // The channel is only known once the image is decoded, so validate
          // after loading (alpha on an RGB image has no data to sample).
          if (image->channel >= imageResource->getNumChannels()) {
            throw runtime_error(
                "The image does not have the selected channel.");
          }
        }
        if (!transactUndoableActionAtomically(doc, "Set Mesh Edge Wall Mask", [&](Document* doc) { return setMeshEdgeWallMaskOverride(doc, edgeIndex, value); })) {
          wallMaskError = "The selected edge cannot accept a wall mask.";
        } else {
          string dependencyError;
          if (!editorRenderSystem()->loadWorldDependencies(
                  doc->getWorld()->getDependentResourceNames(), "World",
                  &dependencyError)) {
            throw runtime_error(dependencyError);
          }
          wallMaskError.clear();
        }
      } catch (exception const& error) {
        wallMaskError = error.what();
      }
    }

    ImGui::SameLine();
    auto const canPreviewWallMask =
        wallMaskState ==
            static_cast<int>(bw::core::WallMaskOverride::State::Image) &&
        primaryWall && wallSchema;
    ImGui::BeginDisabled(!canPreviewWallMask);
    if (ImGui::Button("Preview blend target##SelectedMeshEdge")) {
      wallMaskPreviewEdge = edgeIndex;
      wallMaskPreviewParams.assign(wallMaskBlend.size(), 0.0f);
      for (size_t i = 0;
           i < primaryWall->paramValues.size() &&
           i < wallMaskPreviewParams.size();
           ++i) {
        wallMaskPreviewParams[i] = primaryWall->paramValues[i];
      }
      wallMaskPreviewColour = primaryWall->baseColour;
      wallMaskPreviewBlendEnabled = false;
      ImGui::OpenPopup("Blend mask target preview##SelectedMeshEdge");
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    if (ImGui::BeginPopupModal(
            "Blend mask target preview##SelectedMeshEdge", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
      if (!primaryWall || !wallSchema || wallMaskPreviewEdge != edgeIndex) {
        ImGui::TextDisabled(
            "The selected edge no longer has a previewable wall Sub-material.");
      } else {
        if (!gMaterialPickerThumbnails) {
          if (auto* renderSystem = editorRenderSystem()) {
            gMaterialPickerThumbnails =
                make_unique<SubMaterialThumbnailRenderer>(*renderSystem);
          }
        }

        ImGui::Checkbox(
            "Blend using selected wall mask##WallMaskPreview",
            &wallMaskPreviewBlendEnabled);

        auto standardTexture =
            gMaterialPickerThumbnails
                ? gMaterialPickerThumbnails->texture(primaryWall->id)
                : 0u;
        auto secondaryTexture =
            gMaterialPickerThumbnails
                ? gMaterialPickerThumbnails->draftTexture(
                      primaryWall->id, primaryWall->materialIndex,
                      wallMaskPreviewParams, wallMaskPreviewColour)
                : 0u;
        auto displayedSecondaryTexture = secondaryTexture;
        if (wallMaskPreviewBlendEnabled) {
          displayedSecondaryTexture = wallMaskBlendedPreviewTexture(
              standardTexture, secondaryTexture, wallMaskResource,
              wallMaskChannel, wallMaskPreviewParams,
              wallMaskPreviewColour);
        }
        constexpr float previewSize =
            static_cast<float>(SubMaterialThumbnailRenderer::size) * 4.0f;
        auto renderPreviewImage = [&](char const* title, uint32_t texture) {
          ImGui::BeginGroup();
          ImGui::TextUnformatted(title);
          if (texture) {
            ImGui::Image(static_cast<ImTextureID>(texture),
                         {previewSize, previewSize}, {0.0f, 1.0f},
                         {1.0f, 0.0f});
          } else {
            ImGui::BeginDisabled();
            ImGui::Button("Unavailable", {previewSize, previewSize});
            ImGui::EndDisabled();
          }
          ImGui::EndGroup();
        };
        renderPreviewImage("Standard parameters", standardTexture);
        ImGui::SameLine();
        renderPreviewImage(
            wallMaskPreviewBlendEnabled ? "Masked blend" : "Secondary parameters",
            displayedSecondaryTexture);

        ImGui::SeparatorText("Secondary parameters");
        ImGui::ColorEdit3(
            "Base colour##WallMaskPreview", wallMaskPreviewColour.data());
        for (size_t i = 0;
             i < wallSchema->parameters.size() &&
             i < wallMaskPreviewParams.size();
             ++i) {
          auto const& parameter = wallSchema->parameters[i];
          ImGui::SliderFloat(
              parameter.name.c_str(), &wallMaskPreviewParams[i],
              parameter.minimum, parameter.maximum);
        }
      }

      auto const canAcceptPreview =
          primaryWall && wallSchema && wallMaskPreviewEdge == edgeIndex;
      ImGui::BeginDisabled(!canAcceptPreview);
      if (ImGui::Button("Revert##WallMaskPreview")) {
        wallMaskPreviewParams.assign(wallMaskBlend.size(), 0.0f);
        for (size_t i = 0;
             i < primaryWall->paramValues.size() &&
             i < wallMaskPreviewParams.size();
             ++i) {
          wallMaskPreviewParams[i] = primaryWall->paramValues[i];
        }
        wallMaskPreviewColour = primaryWall->baseColour;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Restore the standard Sub-material parameters and base colour.");
      }
      ImGui::SameLine();
      if (ImGui::Button("OK##WallMaskPreview")) {
        for (size_t i = 0;
             i < wallMaskBlend.size() && i < wallMaskPreviewParams.size();
             ++i) {
          wallMaskBlend[i] = wallMaskPreviewParams[i];
        }
        wallMaskBlendColour = wallMaskPreviewColour;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel##WallMaskPreview")) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (!wallMaskError.empty()) {
      ImGui::TextWrapped("%s", wallMaskError.c_str());
    }

    if (ImGui::Button("Split##SelectedMeshEdge")) {
      transact(doc, "Split Mesh Edge", [&] { splitMeshEdges(doc, indices); });
    }
    ImGui::SameLine();
    auto canDelete =
        doc->previewMeshSubObjectDeletionCount(
            Settings::MeshSubMode::Edge, indices) > 0;
    ImGui::BeginDisabled(!canDelete);
    if (ImGui::Button(ICON_FA_TRASH "##DeleteSelectedMeshEdge")) {
      transact(doc, "Delete Mesh Edge", [&] { deleteMeshSubObjects(doc, Settings::MeshSubMode::Edge, indices); });
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip(canDelete
                            ? "Delete selected edge"
                            : "This Ring cannot contain fewer than three vertices");
    }
  }

  if (ImGui::Button("Recentre mesh")) {
    transact(doc, "Recentre Mesh", [&] { recentreActiveMesh(doc); });
  }
  widgets::HelpMarker(
      "Restores the relationship between the Primitive's position/size and its geometry.");
}


}  // namespace editor
