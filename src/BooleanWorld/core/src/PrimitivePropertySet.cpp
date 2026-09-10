#include "core/PrimitivePropertySet.h"
#include "core/Primitive.h"

#include <cmath>
#include <limits>

namespace bw {
namespace core {
using namespace std;

namespace {

void writeSurfaceMaterial(
    shared_ptr<Serializer> const& serializer, string const& field,
    SurfaceMaterialReference const& material) {
  serializer->beginMap(field);
  serializer->writeString("kind", string(surfaceMaterialKindName(material.kind)));
  serializer->writeString("reference", material.reference);
  serializer->endMap();
}

SurfaceMaterialReference readSurfaceMaterial(
    shared_ptr<Serializer> const& serializer, string const& field) {
  // Pre-#442 World compatibility is intentionally limited to scalar values in
  // keyed formats. Positional data always follows the current two-field
  // representation; no general string-to-kind inference is performed.
  if (!serializer->isPositional() &&
      (!serializer->hasField(field) || !serializer->fieldIsMap(field))) {
    return SurfaceMaterialReference::subMaterial(
        serializer->readString(field, true, ""));
  }

  serializer->beginMap(field);
  auto const kind = surfaceMaterialKindFromName(
      serializer->readString("kind"));
  auto reference = serializer->readString("reference");
  serializer->endMap();
  return {kind, std::move(reference)};
}

}  // namespace

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

    // Every newly written surface assignment uses the tagged contract.
    writeSurfaceMaterial(serializer, "floorMaterial", floorMaterial);
    writeSurfaceMaterial(serializer, "ceilingMaterial", ceilingMaterial);
    writeSurfaceMaterial(serializer, "wallMaterial", wallMaterial);

    // Emboss-preset id references are always explicit, including the empty
    // no-relief state. This is the format marker for the ownership break.
    serializer->writeString("floorEmbossPreset", floorEmbossPresetId);
    serializer->writeString("ceilingEmbossPreset", ceilingEmbossPresetId);
    serializer->writeString("wallEmbossPreset", wallEmbossPresetId);

    // Appended after the historical property payload so positional Worlds can
    // still be read. The old affine fields above remain a derived compatibility
    // projection rather than authored slope controls.
    serializer->writeFloat("floorElevationAngle", floorSpan.directionAngle);
    serializer->writeFloat("floorLowerElevation", floorSpan.lowerElevation);
    serializer->writeFloat("floorUpperElevation", floorSpan.upperElevation);
    serializer->writeFloat(
        "ceilingElevationAngle", ceilingSpan.directionAngle);
    serializer->writeFloat(
        "ceilingLowerElevation", ceilingSpan.lowerElevation);
    serializer->writeFloat(
        "ceilingUpperElevation", ceilingSpan.upperElevation);

    serializer->endMap();  // primitivePropertySet
  }
}

bool PrimitivePropertySet::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  Elevation floorZ_{0.0f}, ceilingZ_{40.0f};
  ElevationSpan floorSpan_, ceilingSpan_{0.0f, 40.0f, 40.0f};
  bool floorSpanAuthored_ = false;
  bool ceilingSpanAuthored_ = false;
  float liquidLevel_{0};
  LiquidType liquidType_{LiquidType::Water};

  SurfaceMaterialReference floorMaterial_, ceilingMaterial_, wallMaterial_;
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

      floorMaterial_ = readSurfaceMaterial(serializer, "floorMaterial");
      ceilingMaterial_ = readSurfaceMaterial(serializer, "ceilingMaterial");
      wallMaterial_ = readSurfaceMaterial(serializer, "wallMaterial");

      // Required in YAML and positional binary data: there is deliberately no
      // compatibility path for the old Primitive property shape.
      floorEmbossPresetId_ = serializer->readString("floorEmbossPreset");
      ceilingEmbossPresetId_ = serializer->readString("ceilingEmbossPreset");
      wallEmbossPresetId_ = serializer->readString("wallEmbossPreset");

      auto const missing = std::numeric_limits<float>::quiet_NaN();
      auto const floorAngle =
          serializer->readFloat("floorElevationAngle", true, missing);
      if (std::isfinite(floorAngle)) {
        floorSpan_.directionAngle = floorAngle;
        floorSpan_.lowerElevation =
            serializer->readFloat("floorLowerElevation");
        floorSpan_.upperElevation =
            serializer->readFloat("floorUpperElevation");
        floorSpanAuthored_ = true;
      }
      auto const ceilingAngle =
          serializer->readFloat("ceilingElevationAngle", true, missing);
      if (std::isfinite(ceilingAngle)) {
        ceilingSpan_.directionAngle = ceilingAngle;
        ceilingSpan_.lowerElevation =
            serializer->readFloat("ceilingLowerElevation");
        ceilingSpan_.upperElevation =
            serializer->readFloat("ceilingUpperElevation");
        ceilingSpanAuthored_ = true;
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
  floorSpan = floorSpan_;
  ceilingSpan = ceilingSpan_;
  floorSpanAuthored = floorSpanAuthored_;
  ceilingSpanAuthored = ceilingSpanAuthored_;
  liquidLevel = liquidLevel_;
  liquidType = liquidType_;
  floorMaterial = std::move(floorMaterial_);
  ceilingMaterial = std::move(ceilingMaterial_);
  wallMaterial = std::move(wallMaterial_);
  floorEmbossPresetId = floorEmbossPresetId_;
  ceilingEmbossPresetId = ceilingEmbossPresetId_;
  wallEmbossPresetId = wallEmbossPresetId_;

  return true;
}

}  // namespace core
}  // namespace bw
