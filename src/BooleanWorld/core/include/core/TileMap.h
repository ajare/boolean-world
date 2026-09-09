#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "core/LayerBuildStep.h"

namespace bw::core {

// A data-only LayerBuildStep containing a finite square binary grid. The Map
// occupies [0, mapSize) on both World-plane axes; cells are addressed from
// its lower-left corner with zero-based coordinates.
class BW_API TileMap final : public LayerBuildStep {
  uint32_t mMapSize{256};
  uint32_t mCellSize{32};
  std::vector<uint8_t> mCells;

  [[nodiscard]] size_t cellIndex(uint32_t x, uint32_t y) const;
  void resetCells();

  void serializeArgs(std::shared_ptr<Serializer> serializer,
                     SerializationWorkData& workData) const override;
  bool deserializeArgs(std::shared_ptr<Serializer> serializer,
                       SerializationWorkData& workData) override;

public:
  TileMap();

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

  // Changing either size clears every authored cell.
  void setMapSize(uint32_t size);
  void setCellSize(uint32_t size);
  [[nodiscard]] uint32_t getMapSize() const;
  [[nodiscard]] uint32_t getCellSize() const;
  [[nodiscard]] uint32_t getWidth() const;
  [[nodiscard]] uint32_t getHeight() const;

  [[nodiscard]] int getCell(uint32_t x, uint32_t y) const;
  void setCell(uint32_t x, uint32_t y, int value);
  void toggleCell(uint32_t x, uint32_t y);
};

}  // namespace bw::core
