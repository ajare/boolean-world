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
    // Elevation planes. Keep the historical scalar names for their base
    // elevations so keyed legacy Worlds load as horizontal planes.
    serializer->writeFloat("floorZ", floorZ.baseElevation);
    serializer->writeVector2("floorGradient", floorZ.gradient);
    serializer->writeFloat("ceilingZ", ceilingZ.baseElevation);
    serializer->writeVector2("ceilingGradient", ceilingZ.gradient);
    serializer->writeFloat("liquidLevel", liquidLevel);
    serializer->writeString("liquidType", LiquidTypeName(liquidType));

    // Sub-material id references
    serializer->writeString("floorMaterial", floorMaterialId);
    serializer->writeString("ceilingMaterial", ceilingMaterialId);
    serializer->writeString("wallMaterial", wallMaterialId);

    // Emboss-preset id references are always explicit, including the empty
    // no-relief state. This is the format marker for the ownership break.
    serializer->writeString("floorEmbossPreset", floorEmbossPresetId);
    serializer->writeString("ceilingEmbossPreset", ceilingEmbossPresetId);
    serializer->writeString("wallEmbossPreset", wallEmbossPresetId);

    serializer->endMap();  // primitivePropertySet
  }
}

bool PrimitivePropertySet::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  Elevation floorZ_{0.0f}, ceilingZ_{40.0f};
  float liquidLevel_{0};
  LiquidType liquidType_{LiquidType::Water};

  string floorMaterialId_, ceilingMaterialId_, wallMaterialId_;
  string floorEmbossPresetId_, ceilingEmbossPresetId_, wallEmbossPresetId_;

  try {
    serializer->beginMap("primitivePropertySet");
    {
      floorZ_.baseElevation = serializer->readFloat("floorZ");
      floorZ_.gradient = serializer->readVector2(
          "floorGradient", !serializer->isPositional(), wp::Vector2::ZERO);
      ceilingZ_.baseElevation = serializer->readFloat("ceilingZ");
      ceilingZ_.gradient = serializer->readVector2(
          "ceilingGradient", !serializer->isPositional(), wp::Vector2::ZERO);
      liquidLevel_ = serializer->readFloat("liquidLevel", true, 0.0f);
      liquidType_ = LiquidTypeFromName(
          serializer->readString("liquidType", true, "Water"));

      floorMaterialId_ = serializer->readString("floorMaterial", true, "");
      ceilingMaterialId_ = serializer->readString("ceilingMaterial", true, "");
      wallMaterialId_ = serializer->readString("wallMaterial", true, "");

      // Required in YAML and positional binary data: there is deliberately no
      // compatibility path for the old Primitive property shape.
      floorEmbossPresetId_ = serializer->readString("floorEmbossPreset");
      ceilingEmbossPresetId_ = serializer->readString("ceilingEmbossPreset");
      wallEmbossPresetId_ = serializer->readString("wallEmbossPreset");

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
  liquidType = liquidType_;
  floorMaterialId = floorMaterialId_;
  ceilingMaterialId = ceilingMaterialId_;
  wallMaterialId = wallMaterialId_;
  floorEmbossPresetId = floorEmbossPresetId_;
  ceilingEmbossPresetId = ceilingEmbossPresetId_;
  wallEmbossPresetId = wallEmbossPresetId_;

  return true;
}

}  // namespace core
}  // namespace bw
