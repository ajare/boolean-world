#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <set>
#include <string>
#include <variant>
#include <vector>
#include <utility>

#include <core/BuildVariables.h>
#include <core/DefinePrefabs.h>
#include <core/DefineTileMaps.h>
#include <core/Emboss.h>
#include <core/PrefabField.h>

#include "Undo.h"
#include "Document.h"
#include "Commands.h"

namespace bw::core {
class RunScript;
}

namespace editor {

class EmbossingCatalogLibrary;
class ProcMaterialLibrary;

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
  bool rightClicked{false};
  bool rightReleased{false};
  bool rightDragging{false};
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
  std::vector<uint32_t> mPendingPrimitiveClick;

  bool mBoxSelectPending{false};
  bool mBoxSelectDragging{false};
  wp::Vector2 mBoxSelectStartScreen;

  bool mTileMapPaintActive{false};
  int mTileMapPaintValue{0};
  // Map index, cell X and cell Y for the last point in a paint gesture.
  std::optional<std::array<uint32_t, 3>> mTileMapLastPaintCell;

  bool mMovingSelectedPrimitives{false};
  // A move is measured against the whole gesture rather than frame by
  // frame, so grid snapping quantises where the selection has been dragged
  // to instead of quantising - and so losing - each frame's own delta.
  wp::Vector2 mPrimitiveDragCumulativeDelta;
  wp::Vector2 mPrimitiveDragAppliedDelta;
  bool mScalingSelectedPrimitives{false};
  bool mRotatingSelectedPrimitives{false};
  bool mMovingSelectedTriggerLine{false};
  int mMovingSelectedTriggerLinePart{-1};

  bool mMovingMeshSelection{false};
  wp::Vector2 mMeshDragCumulativeDelta;

  bool mPlayerProxyDragActive{false};
  bool mRotatingPlayerProxy{false};
  std::vector<uint32_t> mPendingMeshSubObjectClick;

  void applyPrimitiveClick(
      Document* doc, std::vector<uint32_t> const& hoveredIndices,
      bool control, bool shift);
  void applyMeshSubObjectClick(
      Document* doc, Settings::MeshSubMode subMode,
      std::vector<uint32_t> const& hoveredIndices,
      bool control, bool shift);

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

  // Handles a right-button gesture that began on the runtime player proxy.
  // Returns true while it owns the gesture, so view navigation can stand down.
  bool updatePlayerProxy(Document* doc, PointerInput const& input);

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

bool playerProxyHitTest(Document const* doc, wp::Vector2 const& worldPosition);

// Editor-mode changes are preferences, not authored edits: they clear the
// current selection without entering undo history or dirtying the Document.
void setEditorMode(Document* doc, Settings& settings, Settings::Mode mode);

void setMeshSubMode(Document* doc, Settings& settings, Settings::MeshSubMode subMode);

bool setWorldName(Document* doc, std::string const& name);
bool setWorldBuildVariable(
    Document* doc, std::string const& name, bw::core::BuildVariableValue value);
bool removeWorldBuildVariable(Document* doc, std::string const& name);
bool renameWorldBuildVariable(
    Document* doc, std::string const& oldName, std::string const& newName);
bool setLayerBuildVariable(
    Document* doc, bw::core::Layer* layer, std::string const& name,
    bw::core::BuildVariableValue value);
bool removeLayerBuildVariable(
    Document* doc, bw::core::Layer* layer, std::string const& name);
bool renameLayerBuildVariable(
    Document* doc, bw::core::Layer* layer, std::string const& oldName,
    std::string const& newName);

bool setWorldWedgeGenerationParameters(
    Document* doc,
    bw::core::WedgeGenerationParameters const& parameters);

bool addLayer(Document* doc, std::string const& name);

bool setLayerBuildStepEnabled(Document* doc, bw::core::Layer* layer, uint32_t stepIndex, bool enabled);

bool addLayerBuildStep(
    Document* doc, bw::core::Layer* layer, std::string const& type);

bool removeLayerBuildStep(Document* doc, bw::core::Layer* layer, uint32_t stepIndex);

bool moveLayerBuildStep(Document* doc, bw::core::Layer* layer, uint32_t fromIndex, uint32_t toIndex);

// Authored RunScript arguments. Each action rebuilds the Layer immediately so
// script output and downstream failure state stay in lockstep with the panel.
bool setLayerBuildStepName(
    Document* doc, bw::core::Layer* layer, uint32_t stepIndex,
    std::string const& name);
bool setRunScriptScriptName(
    Document* doc, bw::core::Layer* layer, bw::core::RunScript* step,
    std::string const& scriptName);
bool setRunScriptSeed(
    Document* doc, bw::core::Layer* layer, bw::core::RunScript* step,
    uint64_t seed);
bool setRunScriptExtraResourceNames(
    Document* doc, bw::core::Layer* layer, bw::core::RunScript* step,
    std::vector<std::string> const& names);
bool setRunScriptStepVariableValue(
    Document* doc, bw::core::Layer* layer, bw::core::RunScript* step,
    std::string const& name,
    std::variant<std::string, int64_t, double, bool> const& value);
bool clearRunScriptStepVariableValue(
    Document* doc, bw::core::Layer* layer, bw::core::RunScript* step,
    std::string const& name);

// Re-homes one authored Primitive into another build step of the same type,
// keeping the Primitive itself rather than a copy. The Layer rebuilds, so the
// Primitive's index changes; the action re-selects it at its new index.
bool movePrimitiveToLayerBuildStep(
    Document* doc,
    bw::core::Layer* layer,
    bw::core::Primitive* primitive,
    uint32_t targetStepIndex);

bool setTileMapMapSize(
    Document* doc, bw::core::Layer* layer,
    bw::core::DefineTileMaps* definitions, uint32_t size);
bool setTileMapCellSize(
    Document* doc, bw::core::Layer* layer,
    bw::core::DefineTileMaps* definitions, uint32_t size);
bool setNumTileMaps(
    Document* doc, bw::core::Layer* layer,
    bw::core::DefineTileMaps* definitions, uint32_t count);
bool toggleTileMapCell(
    Document* doc, bw::core::Layer* layer,
    bw::core::DefineTileMaps* definitions, bw::core::TileMap* tileMap,
    uint32_t x, uint32_t y);

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
// Empty when the Prefab can be deleted, otherwise the reason it cannot -
// i.e. the same check deletePrefab throws on, without the throw.
std::string prefabDeletionBlockedReason(
    bw::core::Layer const* layer, bw::core::DefinePrefabs const* step,
    bw::core::Prefab const* prefab);
bool setPrefabTilingType(
    Document* doc, bw::core::Layer* layer, bw::core::DefinePrefabs* step,
    bw::core::PrefabTilingType type);
std::string prefabSizeChangeBlockedReason(
    bw::core::Layer const* layer, bw::core::DefinePrefabs const* step,
    bw::core::Prefab const* prefab, bw::core::PrefabTileSize size);
bool setPrefabTileSize(
    Document* doc, bw::core::Layer* layer, bw::core::DefinePrefabs* step,
    bw::core::Prefab* prefab, bw::core::PrefabTileSize size);
bool setPrefabTags(
    Document* doc, bw::core::Layer* layer, bw::core::DefinePrefabs* step,
    bw::core::Prefab* prefab, std::set<std::string> const& tags);

bool bindPrefabField(
    Document* doc, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::DefinePrefabs* definitions);
bool selectPrefabForField(
    Document* doc, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Prefab* prefab);
bool placePrefabInstance(
    Document* doc, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Tile tile);
bool placePrefabInstanceWithMode(
    Document* doc, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Tile tile, bw::core::TileMode mode);
bool clearPrefabInstance(
    Document* doc, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Tile tile);
bool rotatePrefabInstance(
    Document* doc, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Tile tile, bool next);
bool setPrefabInstanceMode(
    Document* doc, bw::core::Layer* layer, bw::core::PrefabField* field,
    bw::core::Tile tile, bw::core::TileMode mode);

bool setWorldDescription(Document* doc, std::string const& desc);

bool setPlayerStartPosition(Document* doc, wp::Vector2 const& pos);

bool setPlayerStartAngle(Document* doc, float angle);

bool selectWorldVertex(Document* doc, uint32_t worldVertexIndex);

bool selectTriggerLine(Document* doc, uint32_t triggerLineIndex);

bool deleteTriggerLine(Document* doc, uint32_t triggerLineIndex);

bool setTriggerLineSide(Document* doc, bw::core::WorldTriggerLine* triggerLine, bw::core::WorldTriggerLineSide side);

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

// Completes the armed Slice tool at its second Vertex.
bool sliceMesh(Document* doc, uint32_t secondVertexIndex);

// Duplicates a hole Ring as a top-level filled polygon.
bool fillMeshHole(Document* doc, uint32_t holeRingIndex);

// One-shot vertex placement for the Mesh panel's numeric coordinate field,
// as opposed to the frame-by-frame drag EditorInteraction drives directly
// through Document::updateMeshDrag. Refused (returning false, leaving the
// mesh unchanged) if it would break an invariant.
bool setMeshVertexPosition(Document* doc, uint32_t vertexIndex, wp::Vector2 const& position);
bool setMeshVertexMetadata(
    Document* doc, uint32_t vertexIndex,
    std::map<std::string, std::string> const& metadata);
bool setMeshEdgeMetadata(
    Document* doc, uint32_t edgeIndex,
    std::map<std::string, std::string> const& metadata);

// Sets or clears the active mesh edge's wall collision override. Refused on
// an edge whose connectivity is not External.
bool setMeshEdgeCollisionOverride(
    Document* doc, uint32_t edgeIndex, std::optional<bool> collides);

// Sets the active mesh edge's wall-render override (Document::
// setActiveMeshEdgeVisible). Same External-only gating as above.
bool setMeshEdgeVisible(Document* doc, uint32_t edgeIndex, bool visible);

// Commits the complete Wall normal-map value in one undoable editor action.
bool setMeshEdgeNormalMapOverride(
    Document* doc, uint32_t edgeIndex,
    bw::core::WallNormalMapOverride const& overrideValue);

// Commits the complete Wall mask value (state, resource, channel, and blend
// parameters) in one undoable editor action. Sibling to the normal-map action.
bool setMeshEdgeWallMaskOverride(
    Document* doc, uint32_t edgeIndex,
    bw::core::WallMaskOverride const& overrideValue);

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

// Clones the given Primitives and hands the clones to the pointer: they
// follow the cursor as a rigid group until a left click places them
// (commitClonePlacement) or a right click discards them
// (cancelClonePlacement). The undo snapshot is taken before the clones
// exist and is only pushed on placement, so a discarded clone leaves the
// World and the history exactly as it found them.
bool beginClonePlacement(Document* doc, std::set<uint32_t> const& primitiveIndices);
void commitClonePlacement(Document* doc);
void cancelClonePlacement(Document* doc);

// Replaces a MeshPrimitive with one Union MeshPrimitive per filled region,
// retaining each region's direct Holes.
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

// AudioEmitter authoring is index-based because emitters have no author-facing
// names. Add creates the opaque authored GUID; edits deliberately leave it and
// the not-yet-exposed cull radius unchanged.
bool addPrimitiveAudioEmitter(
    Document* doc, bw::core::Primitive* primitive);
bool setPrimitiveAudioEmitterOffset(
    Document* doc, bw::core::Primitive* primitive, uint32_t emitterIndex,
    wp::Vector2 const& offset);
bool setPrimitiveAudioEmitterHeightOffset(
    Document* doc, bw::core::Primitive* primitive, uint32_t emitterIndex,
    float heightOffset);
bool setPrimitiveAudioEmitterSoundId(
    Document* doc, bw::core::Primitive* primitive, uint32_t emitterIndex,
    std::string const& soundId);
bool deletePrimitiveAudioEmitter(
    Document* doc, bw::core::Primitive* primitive, uint32_t emitterIndex);

enum class PrimitiveMaterialSurface {
  Floor,
  Ceiling,
  Wall
};

// Assigns one stable Sub-material id. Intended to be called through
// transactUndoableAction by the material picker.
bool setPrimitiveSubMaterial(
    Document* doc, bw::core::Primitive* primitive,
    PrimitiveMaterialSurface surface, std::string const& subMaterialId);

// Assigns one optional global Emboss-preset id to exactly one surface.
bool setPrimitiveEmbossPreset(
    Document* doc, bw::core::Primitive* primitive,
    PrimitiveMaterialSurface surface, std::string const& embossPresetId);

// The properties a floor or ceiling nudge produces, clamped so that a floor
// never rises past its own ceiling and a ceiling never drops below its own
// floor - an inverted pair draws an inside-out room and gives the walls
// between the two a negative height. A Wall is the gap between two polygons
// and has no height of its own, so nudging one changes nothing.
//
// Pure, so a caller can see whether a nudge would move anything before it
// opens a transaction for it.
[[nodiscard]] bw::core::PrimitivePropertySet movedSurfaceZ(
    bw::core::PrimitivePropertySet properties,
    PrimitiveMaterialSurface surface, float delta);

// Adjusts the non-negative authored Liquid level only when a floor owns the
// selected surface.
[[nodiscard]] bw::core::PrimitivePropertySet movedLiquidLevel(
    bw::core::PrimitivePropertySet properties,
    PrimitiveMaterialSurface surface, float delta);

enum class ElevationSpanEnd { Lower,
                              Upper };

// Chooses the authored end edge lying along the view direction in the World
// plane. A perpendicular view chooses neither end. Walls do not own an
// Elevation span.
[[nodiscard]] std::optional<ElevationSpanEnd> elevationSpanEndTowardsView(
    bw::core::Primitive const& primitive, PrimitiveMaterialSurface surface,
    wp::Vector2 const& viewDirection);

// Moves just one authored slope end. Movement toward the opposing surface is
// clamped before the floor and ceiling cross anywhere in the Primitive's
// local axis-aligned bounds.
[[nodiscard]] bw::core::PrimitivePropertySet movedElevationSpanEnd(
    bw::core::Primitive const& primitive, PrimitiveMaterialSurface surface,
    ElevationSpanEnd end, float delta);

// Assigns a whole property set. Intended to be called through
// transactUndoableAction.
bool setPrimitiveProperties(
    Document* doc, bw::core::Primitive* primitive,
    bw::core::PrimitivePropertySet const& properties);

// ProcMaterial authoring actions save immediately through ProcMaterialLibrary's
// bw::core::Serializer path. Rename deliberately changes only the display name;
// the stable id held by Primitives never changes.
bool createSubMaterial(
    Document* doc, ProcMaterialLibrary* library,
    std::string const& resourceName, std::string const& displayName,
    uint32_t materialIndex, std::vector<float> const& paramValues,
    std::array<float, 3> const& baseColour,
    bw::core::ChipGenerationParameters const& chip,
    std::string* createdId = nullptr);
bool renameSubMaterial(
    Document* doc, ProcMaterialLibrary* library,
    std::string const& subMaterialId, std::string const& displayName);
bool editSubMaterial(
    Document* doc, ProcMaterialLibrary* library,
    std::string const& subMaterialId, std::vector<float> const& paramValues,
    std::array<float, 3> const& baseColour,
    bw::core::ChipGenerationParameters const& chip);
// Empty means deletion is allowed. Otherwise lists every Primitive index and
// referenced surface in the currently-open Document.
std::string subMaterialDeletionBlockedReason(
    Document* doc, std::string const& subMaterialId);
bool deleteSubMaterial(
    Document* doc, ProcMaterialLibrary* library,
    std::string const& subMaterialId, std::string* blockedReason = nullptr);

// Global Embossing-catalog authoring. All mutations persist immediately;
// callers use transactUndoableActionAtomically so failed validation or a
// blocked deletion does not alter history.
bool createEmbossPreset(
    Document* doc, EmbossingCatalogLibrary* library,
    std::string const& displayName, bw::core::EmbossData const& emboss,
    std::string* createdId = nullptr);
bool renameEmbossPreset(
    Document* doc, EmbossingCatalogLibrary* library,
    std::string const& presetId, std::string const& displayName);
bool editEmbossPreset(
    Document* doc, EmbossingCatalogLibrary* library,
    std::string const& presetId, bw::core::EmbossData const& emboss);
// Empty means deletion is allowed. Otherwise identifies every authored
// Primitive surface retaining the preset, including disabled steps and
// Prefab definitions.
std::string embossPresetDeletionBlockedReason(
    Document* doc, std::string const& presetId);
bool deleteEmbossPreset(
    Document* doc, EmbossingCatalogLibrary* library,
    std::string const& presetId, std::string* blockedReason = nullptr);

void setPrimitiveDefaultMaterials(bw::core::Primitive* prim);

bool increasePrimitivePriority(Document* doc, bw::core::Primitive* primitive);

bool decreasePrimitivePriority(Document* doc, bw::core::Primitive* primitive);

bool setPrimitiveAnimatedPropertyEvent(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index, uint32_t eventType, bw::core::AnimatedPropertyEventTriggerType triggerType, float value);

bool addPrimitiveAnimatedPropertyEvent(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t eventType, bw::core::AnimatedPropertyEventTriggerType triggerType, float value);

bool deletePrimitiveAnimatedPropertyEvent(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, uint32_t index);

bool setPrimitiveCaptureMode(Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, bw::core::ValueCaptureMode mode);

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

// Named command façade over the implementation functions above. Command
// metadata and the complete list live in Commands.h; these small value types
// make actions discoverable and callable without repeating a history label at
// each call site.
#define EDITOR_DECLARE_COMMAND(type, label, function)              \
  struct type {                                                    \
    static constexpr CommandId id = CommandId::type;               \
    static constexpr char const* name() { return label; }          \
    template <typename... Args>                                    \
    static decltype(auto) execute(Document& doc, Args&&... args) { \
      return function(&doc, std::forward<Args>(args)...);          \
    }                                                              \
  };
EDITOR_ACTION_COMMANDS(EDITOR_DECLARE_COMMAND)
#undef EDITOR_DECLARE_COMMAND

}  // namespace editor