#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace editor {

// This is the editor's command catalogue. Adding an authored operation means
// adding it here once; command types, stable ids and history labels are all
// generated from this list.
#define EDITOR_ACTION_COMMANDS(X) \
  X(SetEditorMode, "Set editor mode", setEditorMode) \
  X(SetMeshSubMode, "Set mesh sub-mode", setMeshSubMode) \
  X(SetWorldName, "Set World name", setWorldName) \
  X(SetWorldWedgeGenerationParameters, "Set World Wedge settings", setWorldWedgeGenerationParameters) \
  X(AddLayer, "Add Layer", addLayer) \
  X(SetLayerBuildStepEnabled, "Toggle Layer step", setLayerBuildStepEnabled) \
  X(AddLayerBuildStep, "Add Layer step", addLayerBuildStep) \
  X(RemoveLayerBuildStep, "Remove Layer step", removeLayerBuildStep) \
  X(MoveLayerBuildStep, "Move Layer step", moveLayerBuildStep) \
  X(SetLayerBuildStepName, "Rename Layer step", setLayerBuildStepName) \
  X(SetRunScriptScriptName, "Select RunScript Lua script", setRunScriptScriptName) \
  X(SetRunScriptSeed, "Set RunScript seed", setRunScriptSeed) \
  X(SetRunScriptExtraResourceNames, "Set RunScript extra resources", setRunScriptExtraResourceNames) \
  X(SetRunScriptStepVariableValue, "Set RunScript Step build variable", setRunScriptStepVariableValue) \
  X(ClearRunScriptStepVariableValue, "Revert RunScript Step build variable", clearRunScriptStepVariableValue) \
  X(SetTileMapMapSize, "Set TileMap Map size", setTileMapMapSize) \
  X(SetTileMapCellSize, "Set TileMap cell size", setTileMapCellSize) \
  X(ToggleTileMapCell, "Toggle TileMap cell", toggleTileMapCell) \
  X(MovePrimitiveToLayerBuildStep, "Move Primitive to Layer step", movePrimitiveToLayerBuildStep) \
  X(SelectPrefab, "Select Prefab", selectPrefab) \
  X(CreatePrefab, "Create Prefab", createPrefab) \
  X(RenamePrefab, "Rename Prefab", renamePrefab) \
  X(DeletePrefab, "Delete Prefab", deletePrefab) \
  X(SetPrefabTilingType, "Set Prefab tiling type", setPrefabTilingType) \
  X(SetPrefabTileSize, "Set Prefab tile size", setPrefabTileSize) \
  X(SetPrefabTags, "Set Prefab tags", setPrefabTags) \
  X(BindPrefabField, "Bind PrefabField", bindPrefabField) \
  X(SelectPrefabForField, "Select Prefab for field", selectPrefabForField) \
  X(PlacePrefabInstance, "Place Prefab instance", placePrefabInstance) \
  X(PlacePrefabInstanceWithMode, "Place Prefab instance", placePrefabInstanceWithMode) \
  X(ClearPrefabInstance, "Clear Prefab instance", clearPrefabInstance) \
  X(RotatePrefabInstance, "Rotate Prefab instance", rotatePrefabInstance) \
  X(SetPrefabInstanceMode, "Set Prefab Tile mode", setPrefabInstanceMode) \
  X(SetWorldDescription, "Set World description", setWorldDescription) \
  X(SetPlayerStartPosition, "Set player start position", setPlayerStartPosition) \
  X(SetPlayerStartAngle, "Set player start angle", setPlayerStartAngle) \
  X(SelectWorldVertex, "Select World vertex", selectWorldVertex) \
  X(SelectTriggerLine, "Select TriggerLine", selectTriggerLine) \
  X(DeleteTriggerLine, "Delete TriggerLine", deleteTriggerLine) \
  X(SetTriggerLineSide, "Set TriggerLine side", setTriggerLineSide) \
  X(SelectPrimitive, "Select Primitive", selectPrimitive) \
  X(TogglePrimitiveSelected, "Toggle Primitive selection", togglePrimitiveSelected) \
  X(SelectPrimitives, "Select Primitives", selectPrimitives) \
  X(AddPrimitivesToSelection, "Add Primitives to selection", addPrimitivesToSelection) \
  X(TogglePrimitivesSelected, "Toggle Primitives in selection", togglePrimitivesSelected) \
  X(ClearSelections, "Clear selections", clearSelections) \
  X(SelectMeshSubObjects, "Select Mesh sub-objects", selectMeshSubObjects) \
  X(AddMeshSubObjectsToSelection, "Add Mesh sub-objects to selection", addMeshSubObjectsToSelection) \
  X(ToggleMeshSubObjectsSelected, "Toggle Mesh sub-object selection", toggleMeshSubObjectsSelected) \
  X(SelectAllMeshSubObjects, "Select all Mesh sub-objects", selectAllMeshSubObjects) \
  X(DeleteMeshSubObjects, "Delete Mesh sub-objects", deleteMeshSubObjects) \
  X(SplitMeshEdges, "Split Mesh edges", splitMeshEdges) \
  X(SliceMesh, "Slice Mesh Ring", sliceMesh) \
  X(FillMeshHole, "Fill Mesh Hole", fillMeshHole) \
  X(SetMeshVertexPosition, "Set Mesh vertex position", setMeshVertexPosition) \
  X(SetMeshVertexMetadata, "Set Prefab vertex metadata", setMeshVertexMetadata) \
  X(SetMeshEdgeMetadata, "Set Prefab edge metadata", setMeshEdgeMetadata) \
  X(SetMeshEdgeCollisionOverride, "Set Mesh edge collision override", setMeshEdgeCollisionOverride) \
  X(SetMeshEdgeVisible, "Set Mesh edge visibility", setMeshEdgeVisible) \
  X(SetMeshEdgeNormalMapOverride, "Set Mesh edge normal map", setMeshEdgeNormalMapOverride) \
  X(SetMeshEdgeWallMaskOverride, "Set Mesh edge wall mask", setMeshEdgeWallMaskOverride) \
  X(RecentreActiveMesh, "Recentre Mesh", recentreActiveMesh) \
  X(CreateMeshPrimitiveFromDrawnRing, "Create Mesh Primitive", createMeshPrimitiveFromDrawnRing) \
  X(CreatePrimitiveFromGhost, "Create Primitive", createPrimitiveFromGhost) \
  X(BeginClonePlacement, "Clone Primitives", beginClonePlacement) \
  X(DecomposeMeshPrimitive, "Decompose MeshPrimitive", decomposeMeshPrimitive) \
  X(CloneRotatedPrimitive, "Clone and rotate Primitive", cloneRotatedPrimitive) \
  X(DeletePrimitives, "Delete Primitives", deletePrimitives) \
  X(BakePrimitives, "Bake Primitives", bakePrimitives) \
  X(ClipPrimitivesToGrid, "Clip Primitives to grid", clipPrimitivesToGrid) \
  X(SetPrimitiveOperation, "Set Primitive operation", setPrimitiveOperation) \
  X(SetPrimitiveFillRule, "Set Primitive fill rule", setPrimitiveFillRule) \
  X(SetPrimitiveOrientation, "Set Primitive orientation", setPrimitiveOrientation) \
  X(SetPrimitiveSize, "Set Primitive size", setPrimitiveSize) \
  X(SetPrimitivePosition, "Set Primitive position", setPrimitivePosition) \
  X(SetPrimitiveTransformOffset, "Set Primitive transform offset", setPrimitiveTransformOffset) \
  X(SetPrimitiveInfluenceOriginOffset, "Set Primitive influence origin", setPrimitiveInfluenceOriginOffset) \
  X(SetPrimitiveFollowOrbitAngle, "Set Primitive follow-orbit angle", setPrimitiveFollowOrbitAngle) \
  X(SetPrimitivePriority, "Set Primitive priority", setPrimitivePriority) \
  X(AddPrimitiveAudioEmitter, "Add AudioEmitter", addPrimitiveAudioEmitter) \
  X(SetPrimitiveAudioEmitterOffset, "Set AudioEmitter offset", setPrimitiveAudioEmitterOffset) \
  X(SetPrimitiveAudioEmitterHeightOffset, "Set AudioEmitter floor offset", setPrimitiveAudioEmitterHeightOffset) \
  X(SetPrimitiveAudioEmitterSoundId, "Set AudioEmitter sound", setPrimitiveAudioEmitterSoundId) \
  X(DeletePrimitiveAudioEmitter, "Delete AudioEmitter", deletePrimitiveAudioEmitter) \
  X(SetPrimitiveSubMaterial, "Set Sub-material", setPrimitiveSubMaterial) \
  X(SetPrimitiveEmbossPreset, "Set Emboss preset", setPrimitiveEmbossPreset) \
  X(SetPrimitiveProperties, "Set Primitive properties", setPrimitiveProperties) \
  X(CreateSubMaterial, "Create Sub-material", createSubMaterial) \
  X(RenameSubMaterial, "Rename Sub-material", renameSubMaterial) \
  X(EditSubMaterial, "Edit Sub-material", editSubMaterial) \
  X(DeleteSubMaterial, "Delete Sub-material", deleteSubMaterial) \
  X(CreateEmbossPreset, "Create Emboss preset", createEmbossPreset) \
  X(RenameEmbossPreset, "Rename Emboss preset", renameEmbossPreset) \
  X(EditEmbossPreset, "Edit Emboss preset", editEmbossPreset) \
  X(DeleteEmbossPreset, "Delete Emboss preset", deleteEmbossPreset) \
  X(IncreasePrimitivePriority, "Increase Primitive priority", increasePrimitivePriority) \
  X(DecreasePrimitivePriority, "Decrease Primitive priority", decreasePrimitivePriority) \
  X(SetPrimitiveAnimatedPropertyEvent, "Set Primitive animation event", setPrimitiveAnimatedPropertyEvent) \
  X(AddPrimitiveAnimatedPropertyEvent, "Add Primitive animation event", addPrimitiveAnimatedPropertyEvent) \
  X(DeletePrimitiveAnimatedPropertyEvent, "Delete Primitive animation event", deletePrimitiveAnimatedPropertyEvent) \
  X(SetPrimitiveCaptureMode, "Set Primitive capture mode", setPrimitiveCaptureMode) \
  X(AddAnimationKeyToPrimitive, "Add animation key", addAnimationKeyToPrimitive) \
  X(RemoveAnimationKeyFromPrimitive, "Remove animation key", removeAnimationKeyFromPrimitive) \
  X(AddKeyToInterpolator, "Add interpolator point", addKeyToInterpolator) \
  X(RemoveKeyFromInterpolator, "Remove interpolator point", removeKeyFromInterpolator) \
  X(UpdateAnimationKeyInInterpolator, "Move interpolator point", updateAnimationKeyInInterpolator) \
  X(SetInterpolatorEasing, "Set interpolator easing", setInterpolatorEasing) \
  X(AddTransform, "Add transform", addTransform) \
  X(RemoveTransform, "Remove transform", removeTransform) \
  X(SwapTransforms, "Swap transforms", swapTransforms) \
  X(SetTransformOperand, "Set transform operand", setTransformOperand) \
  X(SetTransformInput, "Set transform input", setTransformInput) \
  X(SetTransformConstant, "Set transform constant", setTransformConstant) \
  X(SetTransformFnMultiplier, "Set transform function", setTransformFnMultiplier) \
  X(SetTransformTriggerLine, "Set transform TriggerLine", setTransformTriggerLine) \
  X(SetTransformOperation, "Set transform operation", setTransformOperation) \
  X(SetWorldBuildVariable, "Set World build variable", setWorldBuildVariable) \
  X(RemoveWorldBuildVariable, "Remove World build variable", removeWorldBuildVariable) \
  X(RenameWorldBuildVariable, "Rename World build variable", renameWorldBuildVariable) \
  X(SetLayerBuildVariable, "Set Layer build variable", setLayerBuildVariable) \
  X(RemoveLayerBuildVariable, "Remove Layer build variable", removeLayerBuildVariable) \
  X(RenameLayerBuildVariable, "Rename Layer build variable", renameLayerBuildVariable)

#define EDITOR_GESTURE_COMMANDS(X) \
  X(Edit, "Edit") \
  X(MoveMeshSelection, "Move Mesh selection") \
  X(TransformPrimitives, "Transform Primitives") \
  X(MoveTriggerLine, "Move TriggerLine") \
  X(MovePreviewSurface, "Move preview surface") \
  X(EditPrimitiveShape, "Edit Primitive shape") \
  X(CreateTriggerLine, "Create TriggerLine") \
  X(PlacePrimitiveField, "Place Primitive field") \
  X(SwapPrimitivePriorities, "Swap Primitive priorities") \
  X(PaintTileMapCells, "Paint TileMap cells")

enum class CommandId : uint16_t {
#define EDITOR_COMMAND_ENUM(type, label, function) type,
  EDITOR_ACTION_COMMANDS(EDITOR_COMMAND_ENUM)
#undef EDITOR_COMMAND_ENUM
#define EDITOR_GESTURE_ENUM(type, label) type,
  EDITOR_GESTURE_COMMANDS(EDITOR_GESTURE_ENUM)
#undef EDITOR_GESTURE_ENUM
  Count
};

struct CommandInfo {
  CommandId id;
  std::string_view name;
};

inline constexpr auto kCommandRegistry = std::array{
#define EDITOR_COMMAND_INFO(type, label, function) CommandInfo{CommandId::type, label},
  EDITOR_ACTION_COMMANDS(EDITOR_COMMAND_INFO)
#undef EDITOR_COMMAND_INFO
#define EDITOR_GESTURE_INFO(type, label) CommandInfo{CommandId::type, label},
  EDITOR_GESTURE_COMMANDS(EDITOR_GESTURE_INFO)
#undef EDITOR_GESTURE_INFO
};

inline constexpr auto const& commandRegistry() { return kCommandRegistry; }

inline constexpr CommandInfo const& commandInfo(CommandId id) {
  auto const index = static_cast<size_t>(id);
  if (index >= kCommandRegistry.size()) {
    throw std::out_of_range("unknown editor command id");
  }
  return kCommandRegistry[index];
}

}  // namespace editor
