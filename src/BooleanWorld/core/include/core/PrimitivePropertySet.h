#pragma once

#include <vector>

#include "core/Serializable.h"
#include "core/MaterialDefinition.h"

namespace bw {
namespace core {
class Primitive;

struct PrimitivePropertySet : public Serializable {
  float floorZ{0}, ceilingZ{48};

  uint32_t floorMaterialIndex{0};
  MaterialDefinition floorMaterialDef;

  uint32_t ceilingMaterialIndex{0};
  MaterialDefinition ceilingMaterialDef;

  uint32_t wallMaterialIndex{0};
  MaterialDefinition wallMaterialDef;

public:
  bool childrenModified() const override;

  void serializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) const override;

  bool deserializeImpl(std::shared_ptr<Serializer> serializer, SerializationWorkData& workData) override;
};

}  // namespace core
}  // namespace bw