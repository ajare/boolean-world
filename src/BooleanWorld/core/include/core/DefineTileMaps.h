#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "core/LayerBuildStep.h"
#include "core/TileMap.h"

namespace bw::core {

// A data-only LayerBuildStep owning an indexed collection of TileMaps with
// shared Map and cell dimensions.
class BW_API DefineTileMaps final : public LayerBuildStep {
public:
  static constexpr uint32_t MaxTileMaps = 16;

private:
  friend class TileMap;

  uint32_t mMapSize{256};
  uint32_t mCellSize{32};
  std::vector<std::unique_ptr<TileMap>> mTileMaps;
  bool mDeserializeLegacyTileMap{false};

  explicit DefineTileMaps(bool deserializeLegacyTileMap);
  void resetTileMaps();
  void tileMapModified();

  void serializeArgs(std::shared_ptr<Serializer> serializer,
                     SerializationWorkData& workData) const override;
  bool deserializeArgs(std::shared_ptr<Serializer> serializer,
                       SerializationWorkData& workData) override;

public:
  DefineTileMaps();

  [[nodiscard]] static DefineTileMaps* instantiateLegacyTileMap();
  [[nodiscard]] static bool isMapSize(uint32_t size);
  [[nodiscard]] static bool isCellSize(uint32_t size);

  [[nodiscard]] std::string getType() const override;
  [[nodiscard]] bool mayBeFirstStep() const override;
  [[nodiscard]] LayerBuildStep* copy(
      std::map<VertexTransformerObject const*, VertexTransformerObject*>&
          primitiveMap) const override;
  void execute(LayerBuildContext& context) const override;
  [[nodiscard]] bool primitivesParticipateInBuild() const override;
  [[nodiscard]] bool permitsDirectPrimitiveEditing() const override;
  [[nodiscard]] bool acceptsNewPrimitives() const override;
  uint32_t adoptPrimitive(Primitive* primitive) override;
  void replacePrimitive(Primitive* oldPrimitive,
                        Primitive* newPrimitive) override;
  void releasePrimitive(Primitive* primitive) override;
  [[nodiscard]] bool ownsPrimitive(Primitive const* primitive) const override;

  void setMapSize(uint32_t size);
  void setCellSize(uint32_t size);
  [[nodiscard]] uint32_t getMapSize() const;
  [[nodiscard]] uint32_t getCellSize() const;

  void setNumTileMaps(uint32_t count);
  [[nodiscard]] uint32_t getNumTileMaps() const;
  [[nodiscard]] TileMap* getTileMap(uint32_t index);
  [[nodiscard]] TileMap const* getTileMap(uint32_t index) const;
};

}  // namespace bw::core
