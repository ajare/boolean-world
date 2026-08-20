#include <algorithm>

#include "Actions.h"

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

void EditorInteraction::updateSelection(
    Document* doc,
    bw::core::WorldData const* worldData,
    Settings const& settings,
    PointerInput const& input) {
  mHover = input.cursorInWorldView
               ? doc->getHover(input.worldPosition, settings, worldData)
               : DocumentHover{};

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
  // Mesh mode is a shell in this ticket: it may select a MeshPrimitive, but
  // no viewport drag is an authored edit yet.
  if (settings.mode == Settings::Mode::Mesh) {
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
              wp::Vector2{input.dragDelta.x, -input.dragDelta.y});
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
      auto movement = wp::Vector2{input.dragDelta.x, -input.dragDelta.y};

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
