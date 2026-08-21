#pragma once

#include <string>
#include <set>
#include <map>
#include <memory>

#include <willpower/geometry/Mesh.h>

#include <willpower/common/Vector2.h>

#include "core/World.h"

#include "HoverableType.h"
#include "Settings.h"

namespace editor {

// The rule behind Settings::showAllStepPrimitives: with it off, a Primitive
// produced by a LayerBuildStep after its Layer's active step is not shown at
// all. A Primitive belonging to no step in this Layer (getOwningStepIndex
// returns ~0u) is always shown. The ghost is shown whatever the step rule
// says in Primitive mode, and never in Mesh mode, where it is hidden and
// inert. DefinePrefabs is the exception to show-all: its Primitives are shown
// only while that exact step is active, because they have no world-space
// relationship to neighbouring steps (ADR-0017).
//
// The world overlay and Document's selection queries (hover, rubber-band,
// Select All) use this predicate. The generator uses the stricter fold
// predicate below, because a visible Prefab is authoring content and never
// world geometry. Pure logic with no ImGui dependency, unlike the rest of
// UiHelpers - kept here so Document (and its unit tests) can use it without
// linking ImGui.
bool primitiveVisibleForActiveStep(
    bw::core::Layer const& layer,
    bw::core::Primitive const* primitive,
    Settings const& settings);

// Applies the visibility rule and unconditionally excludes Primitives owned
// by DefinePrefabs, including its currently visible selected Prefab.
bool primitiveParticipatesInEditorFold(
    bw::core::Layer const& layer,
    bw::core::Primitive const* primitive,
    Settings const& settings);

struct DocumentHover {
  HoverableType type{HoverableType::None};
  std::vector<uint32_t> indices;
};

struct WorldSnapshot {
  std::string serializedWorld;
  float accelerationGridSize{-1.0f};
  bw::core::LayerSelection layerSelection;
  bool alwaysUpdateWorldVertices{false};
  bool hasDynamicGenerator{false};
  bool alwaysUpdateGeneratorVertices{false};
  bool allowCommitIfVisible{false};
  float scheduledGenerationInterval{5.0f};
};

class Document {
  bool mModified;

  std::string mFilepath;

  std::shared_ptr<bw::core::World> mWorld;

  // Held here rather than on one World, so that every World the Document
  // builds - new, opened, or restored from an undo snapshot - generates
  // through the same filter.
  bw::core::PrimitiveFilter mPrimitiveFilter;

  std::set<uint32_t> mSelectedPrimitiveIndices;

  uint32_t mSelectedWorldVertexIndex;

  uint32_t mSelectedTriggerLineIndex;

  uint32_t mActiveMeshPrimitiveIndex{~0u};
  std::unique_ptr<wp::geometry::Mesh> mActiveMesh;
  std::set<uint32_t> mSelectedMeshVertexIndices;
  std::set<uint32_t> mSelectedMeshEdgeIndices;
  std::set<uint32_t> mSelectedMeshRingIndices;
  std::string mMeshHoverExplanation;

  // Sub-object drag state (ticket #180). Kept as a snapshot taken at the
  // start of the gesture, rather than the live mesh, so each frame's
  // candidate delta is validated against a fixed reference and clamping
  // never compounds an earlier frame's rejection.
  std::unique_ptr<wp::geometry::Mesh> mMeshDragStartSnapshot;
  std::set<uint32_t> mMeshDragAffectedVertices;
  uint32_t mMeshDragAnchorVertexIndex{~0u};
  wp::Vector2 mMeshDragAnchorStartPosition;
  wp::Vector2 mMeshDragLastValidDelta;

  // Draw tool state (ticket #183). Transient gesture state like the drag
  // snapshot above: the Ring being drawn is not Document data and never
  // enters undo history - only the MeshPrimitive it closes into does, as a
  // single entry.
  bool mMeshDrawToolArmed{false};
  std::vector<wp::Vector2> mMeshDrawVertices;
  uint32_t mMeshDrawContainingRingIndex{~0u};
  uint32_t mMeshDrawContainingPrimitiveIndex{~0u};
  bool mMeshDrawCreatesHole{false};
  bool mMeshDrawCreatesIsland{false};
  std::string mMeshDrawRejection;
  wp::Vector2 mMeshDrawRejectedPosition;

  wp::Vector2 mPlayerOldProxyPosition, mPlayerProxyPosition;

  float mPlayerOldProxyAngle, mPlayerProxyAngle;

  static Document* msInstance;

private:
  void reset();

  std::shared_ptr<bw::core::World> createWorld(float size, float gridSize);

public:
  Document();

  virtual ~Document();

  static Document* instance();

  bool isActive() const;

  void setModified(bool modified = true);

  bool isModified() const;

  std::string const& getFilepath() const;

  bool hasFilepath() const;

  void setWorld(bw::core::World const& world);

  WorldSnapshot captureWorldSnapshot() const;

  void restoreWorldSnapshot(WorldSnapshot const& snapshot);

  // Applies to the current World immediately, which regenerates its world
  // data, and to every World built afterwards.
  void setPrimitiveFilter(bw::core::PrimitiveFilter filter);

  std::shared_ptr<bw::core::World> getWorld();

  bw::core::Primitive* getGhost();

  void updateGhost(std::shared_ptr<bw::core::World> world, bw::core::Primitive* primitive);

  // Resolves the editor's hover priority without depending on ImGui:
  // generated World vertices, then trigger lines, then Primitives.
  DocumentHover getHover(
      wp::Vector2 const& mouseWorldPos,
      Settings const& settings,
      bw::core::WorldData const* worldData) const;

  uint32_t getHoveredPrimitiveIndex(wp::Vector2 const& mouseWorldPos, Settings const& settings) const;

  std::vector<uint32_t> getHoveredPrimitiveIndices(wp::Vector2 const& mouseWorldPos, Settings const& settings) const;

  // Primitives whose bounds overlap a rubber-band selection rectangle, in the
  // same index space (and subject to the same ignore rules - ghost, hidden
  // animated primitives) as getHoveredPrimitiveIndex(es).
  std::vector<uint32_t> getPrimitiveIndicesInBounds(wp::BoundingBox const& worldBounds, Settings const& settings) const;

  // Every Primitive in the current context - the active Layer, filtered to
  // its active step (and earlier) unless showAllStepPrimitives opts out -
  // for Select All.
  std::vector<uint32_t> getSelectablePrimitiveIndices(Settings const& settings) const;

  // Mesh-mode eligibility is deliberately stricter than ordinary Primitive
  // selection: only a MeshPrimitive produced by the selected step itself is
  // directly editable.
  [[nodiscard]] std::string meshIneligibilityReason(uint32_t primitiveIndex) const;
  [[nodiscard]] uint32_t getPrimitiveIndexAt(wp::Vector2 const& worldPosition) const;
  bool activateMesh(uint32_t primitiveIndex);
  void clearActiveMesh();
  [[nodiscard]] uint32_t getActiveMeshPrimitiveIndex() const;
  [[nodiscard]] wp::geometry::Mesh const* getActiveMesh() const;
  [[nodiscard]] std::vector<uint32_t> getHoveredMeshSubObjectIndices(
      wp::Vector2 const& worldPosition, Settings const& settings) const;
  [[nodiscard]] std::set<uint32_t> getMeshSubObjectIndicesInBounds(
      wp::BoundingBox const& worldBounds, Settings const& settings) const;
  [[nodiscard]] std::set<uint32_t> getSelectableMeshSubObjectIndices(
      Settings::MeshSubMode subMode) const;
  [[nodiscard]] std::set<uint32_t> const& getSelectedMeshSubObjectIndices(
      Settings::MeshSubMode subMode) const;
  [[nodiscard]] std::set<uint32_t> const& getSelectedMeshVertexIndices() const;
  [[nodiscard]] std::set<uint32_t> const& getSelectedMeshEdgeIndices() const;
  [[nodiscard]] std::set<uint32_t> const& getSelectedMeshRingIndices() const;
  void setSelectedMeshSubObjectIndices(
      Settings::MeshSubMode subMode, std::set<uint32_t> const& indices);
  void addSelectedMeshSubObjectIndices(
      Settings::MeshSubMode subMode, std::set<uint32_t> const& indices);
  void toggleSelectedMeshSubObjectIndices(
      Settings::MeshSubMode subMode, std::set<uint32_t> const& indices);
  void restoreMeshSelection(
      uint32_t activeMeshPrimitiveIndex,
      std::set<uint32_t> const& vertices,
      std::set<uint32_t> const& edges,
      std::set<uint32_t> const& rings);
  void setMeshHoverExplanation(std::string explanation);
  [[nodiscard]] std::string const& getMeshHoverExplanation() const;

  // Begins a rigid-group drag of the current sub-mode's selection: the
  // group moves together or not at all, so a clamped move never deforms
  // the selection's shape.
  void beginMeshDrag(Settings::MeshSubMode subMode);

  // Applies totalWorldDelta - accumulated since beginMeshDrag - snapped to
  // gridSize around the drag's anchor vertex first when snapToGrid is set,
  // to the active mesh, provided every affected vertex's move stays clear
  // of both invariants (Ring simplicity, hole-in-outer containment).
  // Otherwise the mesh is left at the last delta that validated. Returns
  // the delta actually applied, which may be smaller than requested.
  wp::Vector2 updateMeshDrag(wp::Vector2 const& totalWorldDelta, bool snapToGrid, float gridSize);

  void endMeshDrag();

  // Writes the active mesh's current geometry back into its MeshPrimitive,
  // as a drag gesture does on release. Returns false with no active mesh.
  bool commitMeshDrag();

  // A one-shot vertex move outside a drag gesture (the Mesh panel's numeric
  // coordinate field). Validated the same way; returns false and leaves the
  // mesh unchanged if the move would break an invariant.
  bool moveMeshVertexTo(uint32_t vertexIndex, wp::Vector2 const& position);

  // Restores the relationship between the active mesh's Primitive position
  // and size and its geometry, by recentring the Primitive on the mesh's
  // current bounds. Returns false if there is no active mesh or its bounds
  // are degenerate.
  bool recentreActiveMesh();

  // Deletes the given sub-mode's sub-objects from the active mesh, in
  // ascending index order, refusing (and skipping) any item whose Ring
  // would drop to two vertices/edges, or whose Ring/hole containment
  // invariant would break. A vertex heals its Ring by joining its
  // neighbours; an edge welds its two endpoints together at the edge's
  // midpoint; a Ring is removed outright, and removing the last Ring
  // deletes the whole MeshPrimitive. Returns the number of items actually
  // removed.
  uint32_t deleteMeshSubObjects(
      Settings::MeshSubMode subMode, std::set<uint32_t> const& indices);

  // A side-effect-free run of deleteMeshSubObjects, for building an
  // accurate "N removed" report before the real, undoable delete runs.
  [[nodiscard]] uint32_t previewMeshSubObjectDeletionCount(
      Settings::MeshSubMode subMode, std::set<uint32_t> const& indices) const;

  // The draw tool (Ctrl+Shift+C). An empty reason means it can be armed;
  // otherwise the reason is what the Mesh panel shows beside the disabled
  // control.
  [[nodiscard]] std::string meshDrawToolUnavailableReason(Settings const& settings) const;
  bool armMeshDrawTool(Settings const& settings);
  void disarmMeshDrawTool();
  [[nodiscard]] bool meshDrawToolArmed() const;
  [[nodiscard]] std::vector<wp::Vector2> const& getMeshDrawVertices() const;
  [[nodiscard]] uint32_t getMeshDrawContainingRingIndex() const;
  [[nodiscard]] bool meshDrawCreatesNewPrimitive() const;
  [[nodiscard]] bool meshDrawCreatesHole() const;
  [[nodiscard]] bool meshDrawCreatesIsland() const;
  [[nodiscard]] std::string const& getMeshDrawRejection() const;
  [[nodiscard]] wp::Vector2 const& getMeshDrawRejectedPosition() const;

  // The position a draw click actually acts on: snapped to the grid while
  // the grid is shown, since close detection and placement both run on the
  // snapped position rather than the raw cursor.
  [[nodiscard]] static wp::Vector2 snapMeshDrawPosition(
      wp::Vector2 const& worldPosition, bool snapToGrid, float gridSize);

  // Whether a click at this (already snapped) position lands on the first
  // placed vertex with enough vertices behind it to bound a region. Below
  // three the first vertex refuses, and the click places nothing either -
  // placement rejects a position already occupied by a placed vertex.
  [[nodiscard]] bool meshDrawClickWouldClose(
      wp::Vector2 const& position, Settings const& settings) const;

  bool placeMeshDrawVertex(wp::Vector2 const& position, Settings const& settings);

  // Backspace: steps back over the last placed vertex.
  bool removeLastMeshDrawVertex();

  // Esc, in two stages: discards the in-progress Ring and stays armed, or
  // disarms when there is nothing in progress. False if the tool was not
  // armed at all.
  bool escapeMeshDraw();

  // Closes the in-progress Ring into a new MeshPrimitive on the currently-
  // selected LayerBuildStep, with its winding forced to the canonical
  // direction and its operation, fill rule and priority taken from the
  // ghost. Disarms the tool, makes the new mesh active with nothing
  // sub-selected. Returns the new Primitive, or nullptr if it refused.
  bw::core::Primitive* closeMeshDrawRing();

  // Splits every given edge at its (unsnapped) midpoint, leaving both
  // resulting half-edges selected so repeated splits subdivide further.
  // Returns the number of edges split.
  uint32_t splitMeshEdges(std::set<uint32_t> const& edgeIndices);

  // Duplicates a selected hole boundary as a top-level filled Ring while
  // retaining the original hole on its outer ComplexPolygon.
  bool fillMeshHole(uint32_t holeRingIndex);

  bool indexInSelection(uint32_t index) const;

  void setSelectedWorldVertexIndex(uint32_t index);

  void setSelectedTriggerLineIndex(uint32_t index);

  void setSelectedPrimitiveIndices(std::set<uint32_t> const& indices);

  void addSelectedPrimitiveIndex(uint32_t index);

  void addSelectedPrimitiveIndices(std::set<uint32_t> const& indices);

  void removeSelectedPrimitiveIndex(uint32_t index);

  void removeSelectedPrimitiveIndices(std::set<uint32_t> const& indices);

  void clearSelections();
  void clearMeshSelections();

  std::set<uint32_t> const& getSelectedPrimitiveIndices() const;

  bool anyPrimitiveIndicesSelected(std::vector<uint32_t> const& indices) const;

  uint32_t getSelectedWorldVertexIndex() const;

  uint32_t getSelectedTriggerLineIndex() const;

  uint32_t getHoveredTriggerLineIndex(wp::Vector2 const& mouseWorldPos, Settings const& settings) const;

  bool hasSelection() const;

  void setPlayerProxyPosition(wp::Vector2 const& pos);

  wp::Vector2 const& getPlayerProxyPosition() const;

  wp::Vector2 const& getPlayerOldProxyPosition() const;

  void setPlayerProxyAngle(float angle);

  float getPlayerProxyAngle() const;

  float getPlayerOldProxyAngle() const;

  void newDoc();

  void closeDoc();

  bool openDoc(std::string const& filepath);

  void saveDoc();

  void saveDocAs(std::string const& filepath);

  // Exports a single Layer to its own file, independent of the rest of the
  // World - ".layer" (binary) or ".layer.yaml", chosen by filepath's
  // extension.
  void exportLayer(bw::core::Layer const* layer, std::string const& filepath) const;

  // Imports a standalone ".layer"/".layer.yaml" file into the current
  // World's Layer collection, returning the new Layer (with a fresh id if
  // its own collided with one the World already had), or nullptr on
  // failure.
  bw::core::Layer* importLayer(std::string const& filepath);
};

}  // namespace editor
