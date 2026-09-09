#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

void handleShortcuts(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  // The preview owns its camera and selected-surface editor. In particular,
  // undo/new/open would replace the World and invalidate the preview's
  // authored Primitive references while it is still rendering them.
  if (preview3DIsOpen()) return;

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

  // TileMap authoring excludes interaction with every other authored object.
  // File operations and TileMap undo/redo above remain available; view
  // navigation is handled by the World view itself.
  if (doc->isActive() && dynamic_cast<bw::core::TileMap*>(
                             doc->getWorld()->getActiveLayer()->getActiveStep())) {
    return;
  }

  if (settings.mode == Settings::Mode::Mesh &&
      !ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
    if (ImGui::Shortcut(ImGuiKey_1 | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
      setMeshSubMode(doc, settings, Settings::MeshSubMode::Vertex);
    }
    if (ImGui::Shortcut(ImGuiKey_2 | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
      setMeshSubMode(doc, settings, Settings::MeshSubMode::Edge);
    }
    if (ImGui::Shortcut(ImGuiKey_3 | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
      setMeshSubMode(doc, settings, Settings::MeshSubMode::Polygon);
    }
  }

  if (ImGui::Shortcut(ImGuiKey_C | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      if (doc->hasSelection() && !doc->getSelectedPrimitiveIndices().empty()) {
        beginClonePlacement(doc, doc->getSelectedPrimitiveIndices());
      }
    }
  }

  if (ImGui::Shortcut(ImGuiKey_A | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      if (settings.mode == Settings::Mode::Mesh) {
        if (doc->getActiveMesh()) {
          transact(doc, CommandId::SelectAllMeshSubObjects, [&] { selectAllMeshSubObjects(doc, settings.meshSubMode); });
        }
      } else if (doc->isActive()) {
        auto indices = doc->getSelectablePrimitiveIndices(settings);
        transact(doc, CommandId::SelectPrimitives, [&] { selectPrimitives(doc, set<uint32_t>(indices.begin(), indices.end())); });
      }
    }
  }

  if (ImGui::Shortcut(ImGuiKey_D | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      if (doc->hasSelection() &&
          (settings.mode != Settings::Mode::Mesh || doc->getActiveMesh())) {
        transact(doc, CommandId::ClearSelections, [&] { clearSelections(doc); });
      }
    }
  }

  if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
    auto rotatePrefab = [&](bool next) {
      auto* layer = doc->isActive() ? doc->getWorld()->getActiveLayer() : nullptr;
      auto* field = layer
                        ? dynamic_cast<bw::core::PrefabField*>(
                              layer->getActiveStep())
                        : nullptr;
      if (field && field->getSelectedPrefab(*layer)) {
        if (!mouseInteractingWithBackground()) return;
        if (settings.renderMiniMap) {
          auto miniMapBounds = getMiniMapBounds(doc);
          miniMapBounds.setPosition(
              miniMapBounds.getMinExtent() + gWorldViewScreenOrigin);
          auto mouse = ImGui::GetMousePos();
          if (miniMapBounds.pointInside(mouse.x, mouse.y)) return;
        }
      }
      gEditorInteraction.rotateSelectedPrefabInstance(doc, next);
    };
    if (ImGui::Shortcut(ImGuiKey_LeftArrow | ImGuiMod_Shift, ImGuiInputFlags_RouteGlobal)) {
      rotatePrefab(false);
    } else if (ImGui::Shortcut(ImGuiKey_RightArrow | ImGuiMod_Shift, ImGuiInputFlags_RouteGlobal)) {
      rotatePrefab(true);
    } else if (ImGui::GetIO().KeyMods == ImGuiMod_None) {
      if (ImGui::Shortcut(ImGuiKey_LeftArrow, ImGuiInputFlags_RouteGlobal)) {
        gEditorInteraction.movePrefabTileCursor(doc, -1, 0);
      } else if (ImGui::Shortcut(ImGuiKey_RightArrow, ImGuiInputFlags_RouteGlobal)) {
        gEditorInteraction.movePrefabTileCursor(doc, 1, 0);
      } else if (ImGui::Shortcut(ImGuiKey_UpArrow, ImGuiInputFlags_RouteGlobal)) {
        gEditorInteraction.movePrefabTileCursor(doc, 0, 1);
      } else if (ImGui::Shortcut(ImGuiKey_DownArrow, ImGuiInputFlags_RouteGlobal)) {
        gEditorInteraction.movePrefabTileCursor(doc, 0, -1);
      }
    }
  }

  if (ImGui::Shortcut(ImGuiKey_Space, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      gEditorInteraction.applyPrefabShortcut(doc, true, false);
    }
  }

  if (ImGui::Shortcut(ImGuiKey_Delete, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      if (!gEditorInteraction.applyPrefabShortcut(doc, false, true)) {
        if (settings.mode == Settings::Mode::Mesh) {
          if (doc->getActiveMesh()) {
            auto const& indices = doc->getSelectedMeshSubObjectIndices(settings.meshSubMode);
            if (!indices.empty()) {
              auto previewCount = doc->previewMeshSubObjectDeletionCount(settings.meshSubMode, indices);
              if (previewCount > 0) {
                char const* subObjectLabel =
                    settings.meshSubMode == Settings::MeshSubMode::Vertex ? "Vertex(es)"
                    : settings.meshSubMode == Settings::MeshSubMode::Edge ? "Edge(s)"
                                                                          : "Polygon(s)";
                transact(doc, CommandId::DeleteMeshSubObjects, [&] { deleteMeshSubObjects(doc, settings.meshSubMode, indices); });
              }
            }
          }
        } else if (doc->hasSelection()) {
          auto const& primitiveIndices = doc->getSelectedPrimitiveIndices();

          if (!primitiveIndices.empty()) {
            transact(doc, CommandId::DeletePrimitives, [&] { deletePrimitives(doc, primitiveIndices); });
          }

          auto triggerLineIndex = doc->getSelectedTriggerLineIndex();

          if (triggerLineIndex != ~0u) {
            transact(doc, CommandId::DeleteTriggerLine, [&] { deleteTriggerLine(doc, triggerLineIndex); });
          }
        }
      }
    }
  }

  // In Polygon sub-mode Ctrl+Shift+C fills one explicitly selected hole.
  // Otherwise it retains its draw-tool meaning; arming is deliberate and
  // one-way, with Esc standing between drawing and not drawing in two stages.
  if (ImGui::Shortcut(ImGuiKey_C | ImGuiMod_Ctrl | ImGuiMod_Shift, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      auto const& selectedRings = doc->getSelectedMeshRingIndices();
      if (settings.mode == Settings::Mode::Mesh &&
          settings.meshSubMode == Settings::MeshSubMode::Polygon &&
          doc->getActiveMesh() && selectedRings.size() == 1 &&
          doc->getActiveMesh()->getPolygon(*selectedRings.begin()).isHole()) {
        auto holeRing = *selectedRings.begin();
        transact(doc, CommandId::FillMeshHole, [&] { fillMeshHole(doc, holeRing); });
      } else {
        doc->armMeshDrawTool(settings);
      }
    }
  }

  if (ImGui::Shortcut(ImGuiKey_Backspace, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      doc->removeLastMeshDrawVertex();
    }
  }

  if (ImGui::Shortcut(ImGuiKey_Escape, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      // Esc discards a clone in flight exactly as the right button does.
      if (doc->clonePlacementArmed()) {
        cancelClonePlacement(doc);
      } else if (!doc->escapeMeshSlice()) {
        doc->escapeMeshDraw();
      }
    }
  }

  if (ImGui::Shortcut(ImGuiKey_S | ImGuiMod_Ctrl | ImGuiMod_Shift, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      if (settings.mode == Settings::Mode::Mesh && doc->getActiveMesh()) {
        if (settings.meshSubMode == Settings::MeshSubMode::Vertex) {
          doc->armMeshSliceTool(settings);
        } else if (settings.meshSubMode == Settings::MeshSubMode::Edge) {
          auto const& indices = doc->getSelectedMeshSubObjectIndices(settings.meshSubMode);
          if (!indices.empty()) {
            auto previewCount = doc->previewMeshEdgeSplitCount(indices);
            if (previewCount > 0) {
              transact(doc, CommandId::SplitMeshEdges, [&] { splitMeshEdges(doc, indices); });
            }
          }
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
        transact(doc, CommandId::DecreasePrimitivePriority, [&] { decreasePrimitivePriority(doc, prim); });
      }
    }
  }

  if (ImGui::Shortcut(ImGuiKey_RightBracket | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      if (doc->hasSelection() && !doc->getSelectedPrimitiveIndices().empty()) {
        auto const& indices = doc->getSelectedPrimitiveIndices();
        uint32_t index = *indices.begin();

        auto prim = doc->getWorld()->getPrimitive(index);
        transact(doc, CommandId::IncreasePrimitivePriority, [&] { increasePrimitivePriority(doc, prim); });
      }
    }
  }

  if (ImGui::Shortcut(ImGuiKey_B | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      if (doc->hasSelection() && !doc->getSelectedPrimitiveIndices().empty()) {
        auto const& indices = doc->getSelectedPrimitiveIndices();
        transact(doc, CommandId::BakePrimitives, [&] { bakePrimitives(doc, indices); });
      }
    }
  }

  if (ImGui::Shortcut(ImGuiKey_G, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      settings.showGrid = !settings.showGrid;
    }
  }

  if (ImGui::Shortcut(ImGuiKey_H, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      goHome(doc);
    }
  }

  // Blender's View > Frame All.
  if (ImGui::Shortcut(ImGuiKey_Home, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      frameAllWorld(doc);
    }
  }

  if (ImGui::Shortcut(ImGuiKey_M, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      setEditorMode(doc, settings, Settings::Mode::Mesh);
    }
  }

  if (ImGui::Shortcut(ImGuiKey_P, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      setEditorMode(doc, settings, Settings::Mode::Primitive);
    }
  }

  if (ImGui::Shortcut(ImGuiKey_G | ImGuiMod_Ctrl, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused() && doc->isActive()) {
      settings.ghostActive = !settings.ghostActive;
      enableGhost(doc, settings.ghostActive);
    }
  }

  if (ImGui::Shortcut(ImGuiKey_G | ImGuiMod_Shift, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      cycleGridSize(settings);
    }
  }

  if (ImGui::Shortcut(ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused()) {
      if (!doc->getSelectedPrimitiveIndices().empty()) {
        goHome(doc);
      }
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

  if (settings.mode != Settings::Mode::Mesh &&
      ImGui::Shortcut(ImGuiKey_C, ImGuiInputFlags_RouteGlobal)) {
    if (!ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused() && doc->isActive() &&
        doc->getWorld()->getActiveLayer()->getActiveStep()->acceptsNewPrimitives()) {
      transact(doc, CommandId::CreatePrimitiveFromGhost, [&] { createPrimitiveFromGhost(doc); });
    }
  }
}

void handleMouseInteraction(ViewContext&) {
}

}  // namespace editor
