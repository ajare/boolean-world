#pragma once

#include <string>
#include <vector>

#include "core/Elevation.h"
#include "core/LiquidType.h"
#include "core/Serializable.h"

namespace bw {
namespace core {
class Primitive;

struct PrimitivePropertySet : public Serializable {
  // Affine surface planes. The historical names remain while flat consumers
  // migrate; assigning a scalar creates a horizontal (zero-gradient) plane.
  Elevation floorZ{0.0f}, ceilingZ{48.0f};

  // Authored liquid volume scalar, meaningful only on a Primitive whose
  // operation is Union - see ComputeLiquidLevels. Inert (but still stored) on
  // any other operation.
  float liquidLevel{0};

  // Which kind of liquid liquidLevel pours, and so which render material a
  // face wet from this Primitive uses - see LiquidMaterialIndex. Inert (but
  // still stored) wherever liquidLevel itself is.
  LiquidType liquidType{LiquidType::Water};

  // Stable Sub-material id references - see SubMaterial.h. A missing or
  // empty id is a valid, if unresolved, state; resolution against a loaded
  // ProcMaterial catalog happens elsewhere.
  std::string floorMaterialId;
  std::string ceilingMaterialId;
  std::string wallMaterialId;

  // Optional stable references into the sole global Embossing catalog. An
  // empty id explicitly means that this surface has no Embossing.
  std::string floorEmbossPresetId;
  std::string ceilingEmbossPresetId;
  std::string wallEmbossPresetId;

public:
  bool childrenModified() const override;

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;
};

}  // namespace core
}  // namespace bw
