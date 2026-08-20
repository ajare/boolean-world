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
// all. A ghost, and a Primitive belonging to no step in this Layer
// (getOwningStepIndex returns ~0u), are always shown.
//
// Both halves of "not shown" go through here - the world view's overlay
// skips it, the world data generator's filter keeps it out of the fold so it
// contributes no geometry either, and Document's own selection queries
// (hover, rubber-band, Select All) exclude it from the current context. Pure
// logic with no ImGui dependency, unlike the rest of UiHelpers - kept here so
// Document (and its unit tests) can use it without linking ImGui.
bool primitiveVisibleForActiveStep(
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
  std::string mMeshHoverExplanation;

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
  void setMeshHoverExplanation(std::string explanation);
  [[nodiscard]] std::string const& getMeshHoverExplanation() const;

  bool indexInSelection(uint32_t index) const;

  void setSelectedWorldVertexIndex(uint32_t index);

  void setSelectedTriggerLineIndex(uint32_t index);

  void setSelectedPrimitiveIndices(std::set<uint32_t> const& indices);

  void addSelectedPrimitiveIndex(uint32_t index);

  void addSelectedPrimitiveIndices(std::set<uint32_t> const& indices);

  void removeSelectedPrimitiveIndex(uint32_t index);

  void removeSelectedPrimitiveIndices(std::set<uint32_t> const& indices);

  void clearSelections();

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
