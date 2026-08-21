#include <algorithm>

#include "Actions.h"
#include "UiHelpers.h"

namespace editor {
using namespace std;

namespace {

void beginTransform(Document* doc, string const& name) {
  if (!undoableActionInProgress()) {
    beginUndoableAction(
        doc, name,
        bind(recordCurrentState, placeholders::_1, true), 0.0f);
  }
}

}  // namespace

void EditorInteraction::applyPrimitiveClick(
    Document* doc, bool control, bool shift) {
  if (mHover.type != HoverableType::Primitive || mHover.indices.empty()) {
    return;
  }

  mCycledPrimitiveIndex =
      (mCycledPrimitiveIndex + 1) % static_cast<int>(mHover.indices.size());
  auto hoveredIndex = mHover.indices[mCycledPrimitiveIndex];
  auto const& selection = doc->getSelectedPrimitiveIndices();

  if (control) {
    transactUndoableAction(
        doc, "Toggle Primitive " + to_string(hoveredIndex),
        bind(togglePrimitiveSelected, placeholders::_1, hoveredIndex));
  } else if (shift) {
    if (!doc->indexInSelection(hoveredIndex)) {
      transactUndoableAction(
          doc, "Add Primitive " + to_string(hoveredIndex) + " To Selection",
          bind(addPrimitivesToSelection, placeholders::_1,
               set<uint32_t>{hoveredIndex}));
    }
  } else if (selection != set<uint32_t>{hoveredIndex}) {
    transactUndoableAction(
        doc, "Select Primitive " + to_string(hoveredIndex),
        bind(selectPrimitive, placeholders::_1, hoveredIndex));
  }
}

void EditorInteraction::applyMeshSubObjectClick(
    Document* doc, Settings::MeshSubMode subMode, bool control, bool shift) {
  if (mHover.type != HoverableType::MeshSubObject || mHover.indices.empty()) {
    return;
  }
  if (mCycledPrimitiveIndices != mHover.indices) {
    mCycledPrimitiveIndices = mHover.indices;
    mCycledPrimitiveIndex = -1;
  }
  mCycledPrimitiveIndex =
      (mCycledPrimitiveIndex + 1) % static_cast<int>(mHover.indices.size());
  auto index = mHover.indices[mCycledPrimitiveIndex];
  auto indices = set<uint32_t>{index};
  auto const& selection = doc->getSelectedMeshSubObjectIndices(subMode);

  if (control) {
    transactUndoableAction(
        doc, "Toggle Mesh Sub-object " + to_string(index),
        bind(toggleMeshSubObjectsSelected, placeholders::_1, subMode, indices));
  } else if (shift) {
    if (!selection.contains(index)) {
      transactUndoableAction(
          doc, "Add Mesh Sub-object " + to_string(index) + " To Selection",
          bind(addMeshSubObjectsToSelection, placeholders::_1, subMode, indices));
    }
  } else if (selection != indices) {
    transactUndoableAction(
        doc, "Select Mesh Sub-object " + to_string(index),
        bind(selectMeshSubObjects, placeholders::_1, subMode, indices));
  }
}

void EditorInteraction::updateSelection(
    Document* doc,
    bw::core::WorldData const* worldData,
    Settings& settings,
    PointerInput const& input) {
  auto* layer = doc->isActive() ? doc->getWorld()->getActiveLayer() : nullptr;
  auto* prefabField = layer ? dynamic_cast<bw::core::PrefabField*>(layer->getActiveStep()) : nullptr;
  if (prefabField) {
    mHover = {};
    mBoxSelectPending = false;
    mBoxSelectDragging = false;
    if (!prefabField->getDefinePrefabs(*layer)) {
      return;
    }
    if (input.leftClicked && input.cursorInWorldView && !input.cursorInMiniMap) {
      auto tile = prefabField->tileAt(*layer, input.worldPosition);
      prefabField->selectTile(tile);
      if (prefabField->getSelectedPrefab(*layer)) {
        transactUndoableActionAtomically(
            doc, "Place Prefab Instance",
            bind(placePrefabInstance, placeholders::_1, layer, prefabField, tile));
      }
    }
    return;
  }

  if (settings.mode == Settings::Mode::Mesh && doc->meshDrawToolArmed()) {
    // An armed draw tool owns the left button outright: a click places a
    // vertex or closes the Ring, and nothing selects, cycles or rubber-bands
    // behind it. Nothing is hoverable while it is armed either.
    mHover = DocumentHover{};
    doc->setMeshHoverExplanation("");

    if (input.leftClicked && input.cursorInWorldView && !input.cursorInMiniMap) {
      auto position = Document::snapMeshDrawPosition(
          input.worldPosition, settings.showGrid, settings.gridSize);

      if (doc->meshDrawClickWouldClose(position, settings)) {
        transactUndoableAction(
            doc, "Create Mesh Primitive", createMeshPrimitiveFromDrawnRing);
        settings.activeMeshPrimitiveIndex = doc->getActiveMeshPrimitiveIndex();
      } else if (doc->placeMeshDrawVertex(position, settings)) {
        settings.activeMeshPrimitiveIndex = doc->getActiveMeshPrimitiveIndex();
      }
    }
    return;
  }

  mHover = input.cursorInWorldView
               ? doc->getHover(input.worldPosition, settings, worldData)
               : DocumentHover{};

  if (settings.mode == Settings::Mode::Mesh) {
    auto rawIndex = input.cursorInWorldView
                        ? doc->getPrimitiveIndexAt(input.worldPosition)
                        : ~0u;
    doc->setMeshHoverExplanation(
        input.cursorInWorldView ? doc->meshIneligibilityReason(rawIndex) : "");

    if (input.leftClicked) {
      if (mHover.type == HoverableType::Primitive && !mHover.indices.empty() &&
          doc->activateMesh(mHover.indices.front())) {
        settings.activeMeshPrimitiveIndex = mHover.indices.front();
        auto hits = doc->getHoveredMeshSubObjectIndices(input.worldPosition, settings);
        mHover = hits.empty() ? DocumentHover{}
                              : DocumentHover{HoverableType::MeshSubObject, move(hits)};
      }

      if (mHover.type == HoverableType::MeshSubObject) {
        applyMeshSubObjectClick(doc, settings.meshSubMode, input.control, input.shift);
      } else if (!input.cursorInMiniMap) {
        mBoxSelectPending = true;
        mBoxSelectDragging = false;
        mBoxSelectStartScreen = input.screenPosition;
      }
    }

    constexpr float meshBoxSelectDragThresholdSq = 4.0f;
    if (mBoxSelectPending && input.leftDown && !input.leftClicked) {
      auto delta = input.screenPosition - mBoxSelectStartScreen;
      if (!mBoxSelectDragging && delta.lengthSq() > meshBoxSelectDragThresholdSq) {
        mBoxSelectDragging = true;
      }
    }
    if (!input.leftReleased) {
      return;
    }
    if (!mBoxSelectPending) {
      return;
    }

    mBoxSelectPending = false;
    if (mBoxSelectDragging) {
      mBoxSelectDragging = false;
      wp::Vector2 minExtent{min(input.boxSelectStartWorld.x, input.worldPosition.x),
                            min(input.boxSelectStartWorld.y, input.worldPosition.y)};
      wp::Vector2 maxExtent{max(input.boxSelectStartWorld.x, input.worldPosition.x),
                            max(input.boxSelectStartWorld.y, input.worldPosition.y)};
      auto indices = doc->getMeshSubObjectIndicesInBounds(
          wp::BoundingBox(minExtent, maxExtent - minExtent), settings);
      if (indices.empty()) {
        if (!input.control && !input.shift) {
          transactUndoableAction(doc, "Clear selection", clearSelections);
        }
      } else if (input.control) {
        transactUndoableAction(
            doc, "Toggle Mesh Sub-objects In Selection Box",
            bind(toggleMeshSubObjectsSelected, placeholders::_1,
                 settings.meshSubMode, indices));
      } else if (input.shift) {
        transactUndoableAction(
            doc, "Add Mesh Sub-objects In Selection Box",
            bind(addMeshSubObjectsToSelection, placeholders::_1,
                 settings.meshSubMode, indices));
      } else {
        transactUndoableAction(
            doc, "Select Mesh Sub-objects In Selection Box",
            bind(selectMeshSubObjects, placeholders::_1,
                 settings.meshSubMode, indices));
      }
    } else if (!input.control && !input.shift) {
      transactUndoableAction(doc, "Clear selection", clearSelections);
    }
    return;
  }

  // updateSelection runs before updateDrag. Once a Primitive drag is under
  // way, its release belongs to that drag rather than to deferred click or
  // box-selection processing. This especially matters for Difference
  // Primitives: their interior is intentionally not a selectable solid face,
  // so treating release as a background click would clear their selection.
  if (mMovingSelectedPrimitives || mScalingSelectedPrimitives ||
      mRotatingSelectedPrimitives) {
    if (input.leftReleased) {
      mBoxSelectPending = false;
      mBoxSelectDragging = false;
    }
    return;
  }

  // This chord belongs to view navigation, never object selection.
  if (input.shift && input.alt) {
    return;
  }

  if (input.leftClicked) {
    switch (mHover.type) {
      case HoverableType::Primitive:
        // A selected member of a stack defers cycling until release. A lone
        // hit, or a wholly new stack, acts on press as before.
        if (mHover.indices.size() == 1 ||
            !doc->anyPrimitiveIndicesSelected(mHover.indices)) {
          if (mCycledPrimitiveIndices != mHover.indices) {
            mCycledPrimitiveIndices = mHover.indices;
            mCycledPrimitiveIndex = -1;
          }
          applyPrimitiveClick(doc, input.control, input.shift);
        }
        break;

      case HoverableType::TriggerLine:
        transactUndoableAction(
            doc, "Select TriggerLine " + to_string(mHover.indices.front()),
            bind(selectTriggerLine, placeholders::_1, mHover.indices.front()));
        break;

      case HoverableType::WorldVertex:
        transactUndoableAction(
            doc, "Select World Vertex " + to_string(mHover.indices.front()),
            bind(selectWorldVertex, placeholders::_1, mHover.indices.front()));
        break;

      case HoverableType::None:
        if (!input.cursorInMiniMap) {
          mBoxSelectPending = true;
          mBoxSelectDragging = false;
          mBoxSelectStartScreen = input.screenPosition;
        }
        break;
    }
  }

  constexpr float boxSelectDragThresholdSq = 4.0f;
  if (mBoxSelectPending && input.leftDown && !input.leftClicked) {
    auto delta = input.screenPosition - mBoxSelectStartScreen;
    if (!mBoxSelectDragging && delta.lengthSq() > boxSelectDragThresholdSq) {
      mBoxSelectDragging = true;
    }
  }

  if (!input.leftReleased) {
    return;
  }

  if (mBoxSelectPending) {
    mBoxSelectPending = false;

    if (mBoxSelectDragging) {
      mBoxSelectDragging = false;
      wp::Vector2 minExtent{min(input.boxSelectStartWorld.x, input.worldPosition.x),
                            min(input.boxSelectStartWorld.y, input.worldPosition.y)};
      wp::Vector2 maxExtent{max(input.boxSelectStartWorld.x, input.worldPosition.x),
                            max(input.boxSelectStartWorld.y, input.worldPosition.y)};
      auto inBounds = doc->getPrimitiveIndicesInBounds(
          wp::BoundingBox(minExtent, maxExtent - minExtent), settings);
      set<uint32_t> indices(inBounds.begin(), inBounds.end());

      if (!indices.empty()) {
        if (input.control) {
          transactUndoableAction(
              doc, "Toggle Primitives In Selection Box",
              bind(togglePrimitivesSelected, placeholders::_1, indices));
        } else if (input.shift) {
          transactUndoableAction(
              doc, "Add Primitives In Selection Box",
              bind(addPrimitivesToSelection, placeholders::_1, indices));
        } else {
          transactUndoableAction(
              doc, "Select Primitives In Selection Box",
              bind(selectPrimitives, placeholders::_1, indices));
        }
      } else if (!input.control && !input.shift) {
        transactUndoableAction(doc, "Clear selection", clearSelections);
      }
    } else if (!input.control && !input.shift) {
      transactUndoableAction(doc, "Clear selection", clearSelections);
    }
  } else {
    applyPrimitiveClick(doc, input.control, input.shift);
  }
}

void EditorInteraction::updateDrag(
    Document* doc, Settings const& settings, PointerInput const& input) {
  auto* layer = doc->isActive() ? doc->getWorld()->getActiveLayer() : nullptr;
  if (layer && dynamic_cast<bw::core::PrefabField*>(layer->getActiveStep())) {
    return;
  }

  // Shift and Alt are inert here by construction: unlike Primitive mode
  // below, nothing in this branch ever inspects them.
  if (settings.mode == Settings::Mode::Mesh) {
    if (doc->meshDrawToolArmed() || mBoxSelectPending ||
        (input.cursorInMiniMap && input.leftDragging)) {
      return;
    }

    auto const& selection =
        doc->getSelectedMeshSubObjectIndices(settings.meshSubMode);

    if (input.leftReleased) {
      if (mMovingMeshSelection) {
        doc->commitMeshDrag();
        doc->endMeshDrag();
        if (undoableActionInProgress()) {
          commitUndoableAction(doc);
        }
      }
      mMovingMeshSelection = false;
      mMeshDragCumulativeDelta = {};
      return;
    }

    if (input.leftDragging && doc->getActiveMesh() && !selection.empty()) {
      if (!mMovingMeshSelection) {
        mMovingMeshSelection = true;
        mMeshDragCumulativeDelta = {};
        doc->beginMeshDrag(settings.meshSubMode);
        beginTransform(doc, "Move Mesh Selection");
      }

      mMeshDragCumulativeDelta +=
          wp::Vector2{input.dragDelta.x, -input.dragDelta.y} / input.zoom;
      doc->updateMeshDrag(
          mMeshDragCumulativeDelta, settings.showGrid, settings.gridSize);
    }
    return;
  }

  if ((input.shift && input.alt) || mBoxSelectPending ||
      (input.cursorInMiniMap && input.leftDragging)) {
    return;
  }

  auto const& primitiveSelection = doc->getSelectedPrimitiveIndices();
  auto selectedTriggerLineIndex = doc->getSelectedTriggerLineIndex();
  if (primitiveSelection.empty() && selectedTriggerLineIndex == ~0u) {
    return;
  }

  if (!primitiveSelection.empty()) {
    if (input.leftReleased) {
      if ((mMovingSelectedPrimitives || mScalingSelectedPrimitives ||
           mRotatingSelectedPrimitives) &&
          undoableActionInProgress()) {
        commitUndoableAction(doc);
      }
      mMovingSelectedPrimitives = false;
      mScalingSelectedPrimitives = false;
      mRotatingSelectedPrimitives = false;
    }

    if (input.leftDragging) {
      if (input.shift) {
        if (!mScalingSelectedPrimitives) {
          mScalingSelectedPrimitives = true;
          beginTransform(doc, "Transform Primitive(s)");
        }

        for (auto index : primitiveSelection) {
          auto primitive = doc->getWorld()->getPrimitive(index);
          auto const key = bw::core::VertexTransformer::Key::Scale;
          auto newScale = primitive->getAnimationInterpolator(key).getValue(0.0f) -
                          input.dragDelta.y * 0.01f;
          {
            auto mutation = primitive->mutate();
            auto& animation = mutation.animation(key);
            animation.updatePoint(0, 0.0f, newScale);
            if (animation.getNumPoints() == 2) {
              animation.updatePoint(1, 1.0f, newScale);
            }
          }
          primitive->updateVertexPositions();
        }
      } else if (mScalingSelectedPrimitives) {
        mScalingSelectedPrimitives = false;
      }

      if (input.alt) {
        if (!mRotatingSelectedPrimitives) {
          mRotatingSelectedPrimitives = true;
          beginTransform(doc, "Transform Primitive(s)");
        }

        for (auto index : primitiveSelection) {
          auto primitive = doc->getWorld()->getPrimitive(index);
          auto const key = bw::core::VertexTransformer::Key::Angle;
          auto newAngle = primitive->getAnimationInterpolator(key).getValue(0.0f) +
                          input.dragDelta.x;
          {
            auto mutation = primitive->mutate();
            auto& animation = mutation.animation(key);
            animation.updatePoint(0, 0.0f, newAngle);
            if (animation.getNumPoints() == 2) {
              animation.updatePoint(1, 1.0f, newAngle);
            }
          }
          primitive->updateVertexPositions();
        }
      } else if (mRotatingSelectedPrimitives) {
        mRotatingSelectedPrimitives = false;
      }

      if (!mScalingSelectedPrimitives && !mRotatingSelectedPrimitives) {
        if (!mMovingSelectedPrimitives) {
          mMovingSelectedPrimitives = true;
          beginTransform(doc, "Transform Primitive(s)");
        }
        for (auto index : primitiveSelection) {
          auto primitive = doc->getWorld()->getPrimitive(index);
          primitive->setPosition(
              primitive->getPosition() +
              wp::Vector2{input.dragDelta.x, -input.dragDelta.y} / input.zoom);
          primitive->updateVertexPositions();
        }
      }
    }
  }

  if (selectedTriggerLineIndex != ~0u) {
    if (input.leftReleased) {
      if (mMovingSelectedTriggerLine && undoableActionInProgress()) {
        commitUndoableAction(doc);
      }
      mMovingSelectedTriggerLine = false;
      mMovingSelectedTriggerLinePart = -1;
    }

    if (input.leftDragging) {
      if (!mMovingSelectedTriggerLine) {
        mMovingSelectedTriggerLine = true;
        beginTransform(doc, "Move TriggerLine");
      }

      auto world = doc->getWorld();
      auto triggerLine = world->getTriggerLine(selectedTriggerLineIndex);
      auto p0 = triggerLine->getPoint(0);
      auto p1 = triggerLine->getPoint(1);
      auto radiusSq = settings.triggerLineHandleRadius *
                      settings.triggerLineHandleRadius;
      auto movement = wp::Vector2{input.dragDelta.x, -input.dragDelta.y} / input.zoom;

      if (input.worldPosition.distanceToSq(p0) <= radiusSq ||
          mMovingSelectedTriggerLinePart == 0) {
        world->setTriggerLinePoint(selectedTriggerLineIndex, 0, p0 + movement);
        mMovingSelectedTriggerLinePart = 0;
      } else if (input.worldPosition.distanceToSq(p1) <= radiusSq ||
                 mMovingSelectedTriggerLinePart == 1) {
        world->setTriggerLinePoint(selectedTriggerLineIndex, 1, p1 + movement);
        mMovingSelectedTriggerLinePart = 1;
      } else {
        world->moveTriggerLine(selectedTriggerLineIndex, movement);
        mMovingSelectedTriggerLinePart = 2;
      }
    }
  }
}

bool EditorInteraction::applyPrefabShortcut(Document* doc, bool place, bool clear) {
  auto* layer = doc->isActive() ? doc->getWorld()->getActiveLayer() : nullptr;
  auto* field = layer ? dynamic_cast<bw::core::PrefabField*>(layer->getActiveStep()) : nullptr;
  if (!field) return false;
  if (!field->hasSelectedTile()) return true;

  auto tile = field->getSelectedTile();
  if (place && field->getSelectedPrefab(*layer)) {
    transactUndoableActionAtomically(
        doc, "Place Prefab Instance",
        bind(placePrefabInstance, placeholders::_1, layer, field, tile));
  } else if (clear) {
    transactUndoableActionAtomically(
        doc, "Clear Prefab Instance",
        bind(clearPrefabInstance, placeholders::_1, layer, field, tile));
  }
  return true;
}

DocumentHover const& EditorInteraction::getHover() const {
  return mHover;
}

bool EditorInteraction::boxSelectPending() const {
  return mBoxSelectPending;
}

bool EditorInteraction::boxSelectDragging() const {
  return mBoxSelectDragging;
}

wp::Vector2 const& EditorInteraction::getBoxSelectStartScreen() const {
  return mBoxSelectStartScreen;
}

}  // namespace editor
