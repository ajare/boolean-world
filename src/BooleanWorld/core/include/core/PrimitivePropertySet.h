#pragma once

#include <string>
#include <vector>

#include "core/Serializable.h"

namespace bw {
namespace core {
class Primitive;

struct PrimitivePropertySet : public Serializable {
  float floorZ{0}, ceilingZ{48};

  // Authored water volume scalar, meaningful only on a Primitive whose
  // operation is Union - see ComputeWaterLevels. Inert (but still stored) on
  // any other operation.
  float waterLevel{0};

  // Stable Sub-material id references - see SubMaterial.h. A missing or
  // empty id is a valid, if unresolved, state; resolution against a loaded
  // ProcMaterial catalog happens elsewhere.
  std::string floorMaterialId;
  std::string ceilingMaterialId;
  std::string wallMaterialId;

public:
  bool childrenModified() const override;

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;
};

}  // namespace core
}  // namespace bw
