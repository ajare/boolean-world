#pragma once

#include <string>
#include <set>
#include <map>

#include <willpower/common/Vector2.h>

#include "core/World.h"

#include "Settings.h"

namespace editor {

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

  std::set<uint32_t> mSelectedPrimitiveIndices;

  uint32_t mSelectedWorldVertexIndex;

  uint32_t mSelectedTriggerLineIndex;

  wp::Vector2 mPlayerOldProxyPosition, mPlayerProxyPosition;

  float mPlayerOldProxyAngle, mPlayerProxyAngle;

  static Document* msInstance;

private:
  void reset();

  std::shared_ptr<bw::core::World> createWorld(float size, float gridSize);

  void loadTiledPrefabFile(std::string const& filepath, std::shared_ptr<bw::core::World> world);

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

  std::shared_ptr<bw::core::World> getWorld();

  bw::core::Primitive* getGhost();

  void updateGhost(std::shared_ptr<bw::core::World> world, bw::core::Primitive* primitive);

  uint32_t getHoveredPrimitiveIndex(wp::Vector2 const& mouseWorldPos, Settings const& settings) const;

  std::vector<uint32_t> getHoveredPrimitiveIndices(wp::Vector2 const& mouseWorldPos, Settings const& settings) const;

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

  void addPrefabInstance(bw::core::World const* prefab, int tileX, int tileY, float rotation, bw::core::Layer* destinationLayer);
};

}  // namespace editor
