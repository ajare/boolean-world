#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "core/DefinePrefabs.h"
#include "core/LayerBuildStep.h"

namespace bw::core {

// A Tile is identified by both its size-specific grid and integer coordinates.
struct BW_API Tile {
  PrefabTileSize size{PrefabTileSize::Size64};
  int32_t x{0};
  int32_t y{0};

  auto operator<=>(Tile const&) const = default;
};

enum class TileMode : uint32_t {
  Replace = 0,
  Add = 1,
};

struct BW_API PrefabInstance {
  uint32_t prefabId{~0u};
  uint32_t rotation{0};
  TileMode mode{TileMode::Replace};
};

// Places references to a DefinePrefabs step on four nested, size-specific Tile
// grids. The cloned output is a rebuild cache; only the references below are
// authored.
class BW_API PrefabField final : public LayerBuildStep {
  uint32_t mDefinePrefabsStepId{~0u};
  std::map<Tile, PrefabInstance> mInstances;
  mutable std::vector<std::unique_ptr<Primitive>> mBuiltPrimitives;
  mutable std::vector<Primitive const*> mHiddenPrimitives;
  uint32_t mSelectedPrefabId{~0u};
  Tile mSelectedTile{};
  bool mHasSelectedTile{false};

  void serializeArgs(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;
  bool deserializeArgs(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;

public:
  [[nodiscard]] std::string getType() const override;
  [[nodiscard]] bool mayBeFirstStep() const override;
  [[nodiscard]] LayerBuildStep* copy(std::map<VertexTransformerObject const*, VertexTransformerObject*>& primitiveMap) const override;
  void execute(LayerBuildContext& context) const override;
  [[nodiscard]] bool primitivesParticipateInBuild() const override;
  [[nodiscard]] bool permitsDirectPrimitiveEditing() const override;
  [[nodiscard]] bool acceptsNewPrimitives() const override;
  uint32_t adoptPrimitive(Primitive* primitive) override;
  void replacePrimitive(Primitive* oldPrimitive, Primitive* newPrimitive) override;
  [[nodiscard]] bool ownsPrimitive(Primitive const* primitive) const override;

  void bind(Layer const& layer, DefinePrefabs const* step);
  [[nodiscard]] uint32_t getDefinePrefabsStepId() const;
  [[nodiscard]] DefinePrefabs* getDefinePrefabs(Layer const& layer) const;

  void setSelectedPrefab(DefinePrefabs const& definitions, Prefab const* prefab);
  void clearSelectedPrefab();
  [[nodiscard]] Prefab* getSelectedPrefab(Layer const& layer) const;

  void selectTile(Tile tile);
  void clearSelectedTile();
  [[nodiscard]] bool hasSelectedTile() const;
  [[nodiscard]] Tile getSelectedTile() const;
  [[nodiscard]] Tile tileAt(PrefabTileSize size, wp::Vector2 const& position) const;
  // Uses the selected Prefab's grid. Throws when no Prefab is selected.
  [[nodiscard]] Tile tileAt(Layer const& layer, wp::Vector2 const& position) const;
  // Selects the smallest occupied Tile under a point, or clears selection.
  [[nodiscard]] bool selectOccupiedTileAt(wp::Vector2 const& position);

  bool placeSelected(Layer& layer, Tile tile);
  bool clearInstance(Layer& layer, Tile tile);
  bool rotateInstance(Layer& layer, Tile tile, bool next);
  bool setInstanceMode(Layer& layer, Tile tile, TileMode mode);
  [[nodiscard]] PrefabInstance const* getInstance(Tile tile) const;
  [[nodiscard]] std::map<Tile, PrefabInstance> const& getInstances() const;
  [[nodiscard]] bool referencesPrefab(uint32_t prefabId) const;
  [[nodiscard]] bool canMigratePrefabSize(
      uint32_t prefabId, PrefabTileSize from, PrefabTileSize to) const;
  void migratePrefabSize(
      uint32_t prefabId, PrefabTileSize from, PrefabTileSize to);
  [[nodiscard]] bool isHiddenGeneratedPrimitive(Primitive const* primitive) const;
};

}  // namespace bw::core
