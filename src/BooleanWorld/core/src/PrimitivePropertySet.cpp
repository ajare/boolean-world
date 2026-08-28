#include "core/PrimitivePropertySet.h"
#include "core/Primitive.h"

namespace bw {
namespace core {
using namespace std;

bool PrimitivePropertySet::childrenModified() const {
  return false;
}

void PrimitivePropertySet::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->beginMap("primitivePropertySet");
  {
    // Heights
    serializer->writeFloat("floorZ", floorZ);
    serializer->writeFloat("ceilingZ", ceilingZ);
    serializer->writeFloat("liquidLevel", liquidLevel);

    // Sub-material id references
    serializer->writeString("floorMaterial", floorMaterialId);
    serializer->writeString("ceilingMaterial", ceilingMaterialId);
    serializer->writeString("wallMaterial", wallMaterialId);

    serializer->endMap();  // primitivePropertySet
  }
}

bool PrimitivePropertySet::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  float floorZ_{0}, ceilingZ_{40};
  float liquidLevel_{0};

  string floorMaterialId_, ceilingMaterialId_, wallMaterialId_;

  try {
    serializer->beginMap("primitivePropertySet");
    {
      floorZ_ = serializer->readFloat("floorZ");
      ceilingZ_ = serializer->readFloat("ceilingZ");
      liquidLevel_ = serializer->readFloat("liquidLevel", true, 0.0f);

      floorMaterialId_ = serializer->readString("floorMaterial", true, "");
      ceilingMaterialId_ = serializer->readString("ceilingMaterial", true, "");
      wallMaterialId_ = serializer->readString("wallMaterial", true, "");

      serializer->endMap();  // primitivePropertySet
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  floorZ = floorZ_;
  ceilingZ = ceilingZ_;
  liquidLevel = liquidLevel_;
  floorMaterialId = floorMaterialId_;
  ceilingMaterialId = ceilingMaterialId_;
  wallMaterialId = wallMaterialId_;

  return true;
}

}  // namespace core
}  // namespace bw
