#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include <core/DefinePrefabs.h>
#include <core/PrefabField.h>

#include "Undo.h"
#include "Document.h"

namespace editor {

// Raw, toolkit-independent input consumed by EditorInteraction. Screen-space
// values are retained because the existing transform gestures are measured in
// pixels; worldPosition is used for picking and rubber-band bounds. dragDelta
// is screen pixels, not world units - a gesture that must track the cursor
// 1:1 in world space (moving a Primitive, a TriggerLine, a mesh vertex) has
// to divide it by zoom itself; a gesture whose sensitivity is deliberately
// screen-space and zoom-independent (scale, rotate) uses it as-is.
struct PointerInput {
  wp::Vector2 screenPosition;
  wp::Vector2 worldPosition;
  wp::Vector2 boxSelectStartWorld;
  wp::Vector2 dragDelta;
  float zoom{1.0f};
  bool cursorInWorldView{false};
  bool cursorInMiniMap{false};
  bool leftClicked{false};
  bool leftDown{false};
  bool leftReleased{false};
  bool leftDragging{false};
  bool control{false};
  bool shift{false};
  bool alt{false};
};

// Owns the state of one editor pointer gesture. All decisions about hover,
// click modifiers, stacked-hit cycling, rubber-band selection, and dragging
// selected authored objects live here rather than in the ImGui main loop.
class EditorInteraction {
  DocumentHover mHover;
  std::vector<uint32_t> mCycledPrimitiveIndices;
  int mCycledPrimitiveIndex{-1};

  bool mBoxSelectPending{false};
  bool mBoxSelectDragging{false};
  wp::Vector2 mBoxSelectStartScreen;

  bool mMovingSelectedPrimitives{false};
  bool mScalingSelectedPrimitives{false};
  bool mRotatingSelectedPrimitives{false};
  bool mMovingSelectedTriggerLine{false};
  int mMovingSelectedTriggerLinePart{-1};

  bool mMovingMeshSelection{false};
  wp::Vector2 mMeshDragCumulativeDelta;

  void applyPrimitiveClick(Document* doc, bool control, bool shift);
  void applyMeshSubObjectClick(
      Document* doc, Settings::MeshSubMode subMode, bool control, bool shift);

public:
  void updateSelection(
      Document* doc,
      bw::core::WorldData const* worldData,
      Settings& settings,
      PointerInput const& input);

  void updateDrag(
      Document* doc,
      Settings const& settings,
      PointerInput const& input);

  // Keyboard routing for PrefabField authoring. Returns true when an active
  // PrefabField consumed the key, including an intentional no-op.
  bool applyPrefabShortcut(Document* doc, bool place, bool clear);
  bool movePrefabTileCursor(Document* doc, int32_t x, int32_t y);
  bool rotateSelectedPrefabInstance(Document* doc, bool next);

  DocumentHover const& getHover() const;
  bool boxSelectPending() const;
  bool boxSelectDragging() const;
  wp::Vector2 const& getBoxSelectStartScreen() const;
};

bool recordCurrentState(Document* doc, bool modifying);

// Editor-mode changes are preferences, not authored edits: they clear the
// current selection without entering undo history or dirtying the Document.
void setEditorMode(Document* doc, Settings& settings, Settings::Mode mode);

void setMeshSubMode(Document* doc, Settings& settings, Settings::MeshSubMode subMode);

bool setWorldName(Document* doc, std::string const& name);

bool addLayer(Document* doc, std::string const& name);

bool setLayerBuildStepEnabled(Document* doc, bw::core::Layer* layer, uint32_t stepIndex, bool enabled);

bool addLayerBuildStep(
    Document* doc, bw::core::Layer* layer, std::string const& type);

bool removeLayerBuildStep(Document* doc, bw::core::Layer* layer, uint32_t stepIndex);

bool moveLayerBuildStep(Document* doc, bw::core::Layer* layer, uint32_t fromIndex, uint32_t toIndex);

// Prefab selection is ephemeral focus and is called directly. The remaining
// operations are authored edits intended to run through transactUndoableAction.
bool selectPrefab(
    Document* doc, bw::core::Layer* layer, bw::core::DefinePrefabs* step,
    bw::core::Prefab* prefab);
bool createPrefab(
    Document* doc, bw::core::Layer* layer, bw::core::DefinePrefabs* step);
bool renamePrefab(
    Document* doc, bw::core::Layer* layer, bw::core::DefinePrefabs* step,
    bw::core::Prefab* prefab, std::string const& name);
bool deletePrefab(
    Document* doc, bw::core::Layer* layer, bw::core::DefinePrefabs* step,
    bw::core::Prefab* prefab);
bool setPrefabTilingType(
    Document* doc, bw::core::Layer* layer, bw::core::DefinePrefabs* step,
    bw::core::PrefabTilingType type);
bool setPrefabSize(
    Document* doc, bw::core::Layer* layer, bw::core::DefinePrefabs* step,
    float size);

bool bindPrefabField(
    Document* doc, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::DefinePrefabs* definitions);
bool selectPrefabForField(
    Document* doc, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Prefab* prefab);
bool placePrefabInstance(
    Document* doc, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Tile tile);
bool clearPrefabInstance(
    Document* doc, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Tile tile);
bool rotatePrefabInstance(
    Document* doc, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Tile tile, bool next);

bool setWorldDescription(Document* doc, std::string const& desc);

bool setPlayerStartPosition(Document* doc, wp::Vector2 const& pos);

bool setPlayerStartAngle(Document* doc, float angle);

bool selectWorldVertex(Document* doc, uint32_t worldVertexIndex);

bool selectTriggerLine(Document* doc, uint32_t triggerLineIndex);

bool deleteTriggerLine(Document* doc, uint32_t triggerLineIndex);

bool selectPrimitive(Document* doc, uint32_t primitiveIndex);

bool togglePrimitiveSelected(Document* doc, uint32_t primitiveIndex);

bool selectPrimitives(Document* doc, std::set<uint32_t> const& primitiveIndices);

bool addPrimitivesToSelection(Document* doc, std::set<uint32_t> const& primitiveIndices);

bool togglePrimitivesSelected(Document* doc, std::set<uint32_t> const& primitiveIndices);

bool clearSelections(Document* doc);

bool selectMeshSubObjects(
    Document* doc, Settings::MeshSubMode subMode,
    std::set<uint32_t> const& indices);
bool addMeshSubObjectsToSelection(
    Document* doc, Settings::MeshSubMode subMode,
    std::set<uint32_t> const& indices);
bool toggleMeshSubObjectsSelected(
    Document* doc, Settings::MeshSubMode subMode,
    std::set<uint32_t> const& indices);
bool selectAllMeshSubObjects(Document* doc, Settings::MeshSubMode subMode);

// Deletes the sub-mode's selected sub-objects from the active mesh
// (Document::deleteMeshSubObjects). Returns true if anything was removed.
bool deleteMeshSubObjects(
    Document* doc, Settings::MeshSubMode subMode,
    std::set<uint32_t> const& indices);

// Splits every given edge of the active mesh at its midpoint
// (Document::splitMeshEdges). Returns true if any edge was split.
bool splitMeshEdges(Document* doc, std::set<uint32_t> const& edgeIndices);

// Duplicates a hole Ring as a top-level filled polygon.
bool fillMeshHole(Document* doc, uint32_t holeRingIndex);

// One-shot vertex placement for the Mesh panel's numeric coordinate field,
// as opposed to the frame-by-frame drag EditorInteraction drives directly
// through Document::updateMeshDrag. Refused (returning false, leaving the
// mesh unchanged) if it would break an invariant.
bool setMeshVertexPosition(Document* doc, uint32_t vertexIndex, wp::Vector2 const& position);

// Restores the relationship between the active mesh's Primitive position
// and size and its geometry ("Recentre mesh").
bool recentreActiveMesh(Document* doc);

// Closes the draw tool's in-progress Ring into a new MeshPrimitive
// (Document::closeMeshDrawRing) carrying the default materials. This is the
// one undoable action of a whole drawing gesture: placing vertices, stepping
// back over them and discarding the Ring all touch nothing but the tool's own
// transient state.
bool createMeshPrimitiveFromDrawnRing(Document* doc);

bool createPrimitiveFromGhost(Document* doc);

bool clonePrimitive(Document* doc, uint32_t primitiveIndex);

// Replaces a multi-Ring MeshPrimitive with one Primitive per Ring. Filled
// Rings become Union operands and holes become Difference operands.
bool decomposeMeshPrimitive(Document* doc, uint32_t primitiveIndex);

bool cloneRotatedPrimitive(Document* doc, uint32_t primitiveIndex, float angle);

bool deletePrimitives(Document* doc, std::set<uint32_t> const& primitiveIndices);

bool bakePrimitives(Document* doc, std::set<uint32_t> const& primitiveIndices);

bool clipPrimitivesToGrid(Document* doc, std::set<uint32_t> const& primitiveIndices, float gridSize);

bool setPrimitiveOperation(Document* doc, bw::core::Primitive* primitive, bw::core::Primitive::Operation op);

bool setPrimitiveFillRule(Document* doc, bw::core::Primitive* primitive, bw::core::Primitive::FillRule fillRule);

bool setPrimitiveOrientation(Document* doc, bw::core::Primitive* primitive, float orient);

bool setPrimitiveSize(Document* doc, bw::core::Primitive* primitive, float size);

bool setPrimitivePosition(Document* doc, bw::core::Primitive* primitive, wp::Vector2 const& position);

bool setPrimitiveTransformOffset(Document* doc, bw::core::Primitive* primitive, wp::Vector2 const& transformOrigin);

bool setPrimitiveInfluenceOriginOffset(Document* doc, bw::core::Primitive* primitive, wp::Vector2 const& influenceOriginOffset);

bool setPrimitiveFollowOrbitAngle(Document* doc, bw::core::Primitive* primitive, bool orient);

bool setPrimitivePriority(Document* doc, bw::core::Primitive* primitive, uint8_t priority);

void setPrimitiveDefaultMaterial(uint32_t materialIndex, bw::core::MaterialDefinitionData* materialDefinition);

void setPrimitiveDefaultMaterials(bw::core::Primitive* prim);

bool increasePrimitivePriority(Document* doc, bw::core::Primitive* primitive);

bool decreasePrimitivePriority(Document* doc, bw::core::Primitive* primitive);

bool setPrimitiveAnimatedPropertyEvent(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index, uint32_t eventType, bw::core::AnimatedPropertyEventTriggerType triggerType, float value);

bool deletePrimitiveAnimatedPropertyEvent(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index);

bool addAnimationKeyToPrimitive(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, float time, float value);

bool removeAnimationKeyFromPrimitive(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index);

bool addKeyToInterpolator(Document* doc, std::string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, float time, float value);

bool removeKeyFromInterpolator(Document* doc, std::string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index);

bool updateAnimationKeyInInterpolator(Document* doc, std::string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index, float time, float value);

bool setInterpolatorEasing(Document* doc, std::string const& lerperName, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index, bw::core::Easing easing);

bool addTransform(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key);

bool removeTransform(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index);

bool swapTransforms(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index1, uint32_t index2);

bool setTransformOperand(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t operandIndex, bw::core::tTransform::OperandType operand);

bool setTransformInput(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t inputIndex, bw::core::InputType input);

bool setTransformConstant(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t constantIndex, float constant);

bool setTransformFnMultiplier(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t fnMulIndex, float value);

bool setTransformTriggerLine(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, uint32_t indexIndex, uint32_t index);

bool setTransformOperation(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t transformIndex, bw::core::tTransform::Operation operation);

}  // namespace editor