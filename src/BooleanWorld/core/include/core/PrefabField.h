#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "core/DefinePrefabs.h"
#include "core/LayerBuildStep.h"

namespace bw::core {

struct BW_API Tile {
  int32_t x{0};
  int32_t y{0};

  auto operator<=>(Tile const&) const = default;
};

struct BW_API PrefabInstance {
  uint32_t prefabId{~0u};
  uint32_t rotation{0};
};

// Places references to a DefinePrefabs step on an infinite tile grid. The
// cloned output is a rebuild cache; only the references below are authored.
class BW_API PrefabField final : public LayerBuildStep {
  uint32_t mDefinePrefabsStepId{~0u};
  std::map<Tile, PrefabInstance> mInstances;
  mutable std::vector<std::unique_ptr<Primitive>> mBuiltPrimitives;
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
  [[nodiscard]] Tile tileAt(Layer const& layer, wp::Vector2 const& position) const;

  bool placeSelected(Layer& layer, Tile tile);
  bool clearInstance(Layer& layer, Tile tile);
  // Advances (or reverses) an occupied Tile through its tiling type's
  // ordered rotation table. Empty Tiles are intentionally unchanged.
  bool rotateInstance(Layer& layer, Tile tile, bool next);
  [[nodiscard]] PrefabInstance const* getInstance(Tile tile) const;
  [[nodiscard]] std::map<Tile, PrefabInstance> const& getInstances() const;
  [[nodiscard]] bool referencesPrefab(uint32_t prefabId) const;
};

}  // namespace bw::core
