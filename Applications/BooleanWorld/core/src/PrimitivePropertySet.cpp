#include "core/PrimitivePropertySet.h"
#include "core/Primitive.h"

namespace bw {
namespace core {
using namespace std;

bool PrimitivePropertySet::childrenModified() const {
  return floorMaterialDef.isModified() || ceilingMaterialDef.isModified() || wallMaterialDef.isModified();
}

void PrimitivePropertySet::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->beginMap("primitivePropertySet");
  {
    // Heights
    serializer->writeFloat("floorZ", floorZ);
    serializer->writeFloat("ceilingZ", ceilingZ);

    // Floor material
    serializer->beginMap("floorMaterial");
    {
      serializer->writeUint32("materialIndex", floorMaterialIndex);
      floorMaterialDef.serialize(serializer, workData);

      serializer->endMap();
    }

    // Ceiling material
    serializer->beginMap("ceilingMaterial");
    {
      serializer->writeUint32("materialIndex", ceilingMaterialIndex);
      ceilingMaterialDef.serialize(serializer, workData);

      serializer->endMap();
    }

    // Wall material
    serializer->beginMap("wallMaterial");
    {
      serializer->writeUint32("materialIndex", wallMaterialIndex);
      wallMaterialDef.serialize(serializer, workData);

      serializer->endMap();
    }

    serializer->endMap();  // primitivePropertySet
  }
}

bool PrimitivePropertySet::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  float floorZ_{0}, ceilingZ_{40};

  uint32_t floorMaterialIndex_{0};
  MaterialDefinition floorMaterialDef_{};

  uint32_t ceilingMaterialIndex_{0};
  MaterialDefinition ceilingMaterialDef_{};

  uint32_t wallMaterialIndex_{0};
  MaterialDefinition wallMaterialDef_{};

  try {
    serializer->beginMap("primitivePropertySet");
    {
      floorZ_ = serializer->readFloat("floorZ");
      ceilingZ_ = serializer->readFloat("ceilingZ");

      serializer->beginMap("floorMaterial");
      {
        floorMaterialIndex_ = serializer->readUint32("materialIndex");
        floorMaterialDef_.deserialize(serializer, workData);

        serializer->endMap();
      }

      serializer->beginMap("ceilingMaterial");
      {
        ceilingMaterialIndex_ = serializer->readUint32("materialIndex");
        ceilingMaterialDef_.deserialize(serializer, workData);

        serializer->endMap();
      }

      serializer->beginMap("wallMaterial");
      {
        wallMaterialIndex_ = serializer->readUint32("materialIndex");
        wallMaterialDef_.deserialize(serializer, workData);

        serializer->endMap();
      }

      serializer->endMap();  // primitivePropertySet
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  floorZ = floorZ_;
  ceilingZ = ceilingZ_;
  floorMaterialIndex = floorMaterialIndex_;
  floorMaterialDef = floorMaterialDef_;
  ceilingMaterialIndex = ceilingMaterialIndex_;
  ceilingMaterialDef = ceilingMaterialDef_;
  wallMaterialIndex = wallMaterialIndex_;
  wallMaterialDef = wallMaterialDef_;

  return true;
}

}  // namespace core
}  // namespace bw
