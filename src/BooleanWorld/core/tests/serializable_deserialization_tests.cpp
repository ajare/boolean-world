#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/BinarySerializer.h>
#include <core/PrimitivePropertySet.h>
#include <core/RectanglePolygon.h>
#include <core/Serializable.h>
#include <core/SerializationException.h>
#include <core/YamlSerializer.h>

namespace {

class DeserializationProbe final : public bw::core::Serializable {
public:
  explicit DeserializationProbe(bool result)
      : mResult(result) {
  }

private:
  bool mResult;

  bool childrenModified() const override {
    return false;
  }

  void serializeImpl(std::shared_ptr<bw::core::Serializer>, bw::core::SerializationWorkData&) const override {
  }

  bool deserializeImpl(std::shared_ptr<bw::core::Serializer>, bw::core::SerializationWorkData&) override {
    return mResult;
  }
};

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void successfulDeserializationLeavesObjectUnmodified() {
  DeserializationProbe probe(true);
  bw::core::SerializationWorkData workData;

  require(probe.isModified(), "new Serializable was unexpectedly unmodified");
  require(probe.deserialize(nullptr, workData),
          "successful deserializeImpl result was not returned");
  require(!probe.isModified(),
          "freshly deserialized Serializable was marked modified");
}

void failedDeserializationReturnsFailureIndependentlyOfModifiedState() {
  DeserializationProbe probe(false);
  bw::core::SerializationWorkData workData;

  require(!probe.deserialize(nullptr, workData),
          "failed deserializeImpl result was not returned");
  require(!probe.isModified(),
          "deserialization result leaked into the modified state");
}

void propertySetRoundTripsSurfaceResourceIds() {
  bw::core::PrimitivePropertySet original;
  original.floorZ = 0.0f;
  original.ceilingZ = 48.0f;
  original.liquidLevel = 12.5f;
  original.liquidType = bw::core::LiquidType::Water;
  original.floorMaterialId = "weathered_slate";
  original.ceilingMaterialId = "polished_slate";
  original.wallMaterialId = "";
  original.floorEmbossPresetId = "weathered_blocks";
  original.ceilingEmbossPresetId = "";
  original.wallEmbossPresetId = "chiselled_edges";

  bw::core::SerializationWorkData writeWorkData;
  auto writer = std::shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::toString());
  original.serialize(writer, writeWorkData);
  writer->serialize();
  auto yaml = static_cast<bw::core::YamlSerializer*>(writer.get())->getSerializedString();

  auto reader = std::shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::fromString(yaml));
  reader->deserialize();

  bw::core::PrimitivePropertySet roundTripped;
  bw::core::SerializationWorkData readWorkData;
  require(roundTripped.deserialize(reader, readWorkData),
          "property set failed to round-trip Sub-material ids");
  require(roundTripped.floorMaterialId == original.floorMaterialId,
          "floor Sub-material id did not round-trip");
  require(roundTripped.ceilingMaterialId == original.ceilingMaterialId,
          "ceiling Sub-material id did not round-trip");
  require(roundTripped.wallMaterialId == original.wallMaterialId,
          "an empty wall Sub-material id did not round-trip as empty");
  require(roundTripped.floorEmbossPresetId == original.floorEmbossPresetId,
          "floor Emboss-preset id did not round-trip");
  require(roundTripped.ceilingEmbossPresetId == original.ceilingEmbossPresetId,
          "empty ceiling Emboss-preset id did not round-trip");
  require(roundTripped.wallEmbossPresetId == original.wallEmbossPresetId,
          "wall Emboss-preset id did not round-trip");
  require(roundTripped.liquidLevel == original.liquidLevel,
          "liquid level did not round-trip the same way floorZ/ceilingZ do");
  require(roundTripped.liquidType == original.liquidType,
          "liquid type did not round-trip the same way liquid level does");
  require(roundTripped.floorZ.evaluate({123.0f, -45.0f}) == 0.0f &&
              roundTripped.ceilingZ.evaluate({123.0f, -45.0f}) == 48.0f &&
              roundTripped.floorZ.gradient == wp::Vector2::ZERO &&
              roundTripped.ceilingZ.gradient == wp::Vector2::ZERO,
          "serialized scalar elevations did not load as zero-gradient Elevation planes");
  require(yaml.find("floorZ: 0") != std::string::npos &&
              yaml.find("ceilingZ: 48") != std::string::npos,
          "horizontal Elevation planes did not retain the scalar wire format");
}

void propertySetRoundTripsElevationSpans() {
  bw::core::PrimitivePropertySet original;
  original.floorSpan = {35.0f, -3.0f, 12.0f};
  original.ceilingSpan = {-75.0f, 64.0f, 72.0f};

  bw::core::SerializationWorkData writeWorkData;
  auto writer = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::toString());
  original.serialize(writer, writeWorkData);
  auto yaml = static_cast<bw::core::YamlSerializer*>(writer.get())
                  ->getSerializedString();

  auto reader = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString(yaml));
  reader->deserialize();
  bw::core::PrimitivePropertySet copy;
  bw::core::SerializationWorkData readWorkData;
  require(copy.deserialize(reader, readWorkData) &&
              copy.floorSpan == original.floorSpan &&
              copy.ceilingSpan == original.ceilingSpan,
          "Elevation spans did not round-trip through YAML");
  require(yaml.find("floorElevationAngle: 35") != std::string::npos &&
              yaml.find("floorLowerElevation: -3") != std::string::npos &&
              yaml.find("floorUpperElevation: 12") != std::string::npos &&
              yaml.find("ceilingElevationAngle: -75") != std::string::npos,
          "authored Elevation-span values were not explicit in YAML");
}

void legacyElevationPlanesMigrateAfterGeometryIsAvailable() {
  auto reader = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString(
          "floorZ: 5\nfloorGradient: [2, -1]\n"
          "ceilingZ: 40\nceilingGradient: [0, 0]\n"
          "floorEmbossPreset: ''\nceilingEmbossPreset: ''\n"
          "wallEmbossPreset: ''\n"));
  reader->deserialize();

  bw::core::PrimitivePropertySet legacy;
  bw::core::SerializationWorkData readWorkData;
  require(legacy.deserialize(reader, readWorkData) &&
              !legacy.floorSpanAuthored,
          "legacy affine Elevation data was not identified for migration");

  bw::core::RectanglePolygon primitive(
      bw::core::Primitive::Operation::Union,
      bw::core::Primitive::FillRule::NonZero, 1.0f);
  primitive.setSize(2.0f, 4.0f);
  primitive.setProperties(legacy);
  auto migrated = primitive.getElevationPlane(
      bw::core::PrimitiveSurface::Floor);
  for (wp::Vector2 const point :
       {wp::Vector2{-1.0f, -2.0f}, wp::Vector2{0.0f, 0.0f},
        wp::Vector2{1.0f, 2.0f}}) {
    require(
        std::abs(
            migrated.evaluate(point) -
            (5.0f + 2.0f * point.x - point.y)) < 0.001f,
        "legacy affine Elevation values changed during migration");
  }
}

void propertySetRoundTripsEmbossPresetIdsInBinary() {
  bw::core::PrimitivePropertySet original;
  original.floorSpan = {15.0f, 4.0f, 20.0f};
  original.ceilingSpan = {120.0f, 48.0f, 60.0f};
  original.floorEmbossPresetId = "floor_relief";
  original.ceilingEmbossPresetId = "";
  original.wallEmbossPresetId = "wall_relief";

  auto writer = std::shared_ptr<bw::core::Serializer>(
      bw::core::BinarySerializer::toString());
  bw::core::SerializationWorkData writeWorkData;
  original.serialize(writer, writeWorkData);
  auto bytes = static_cast<bw::core::BinarySerializer*>(writer.get())
                   ->getSerializedString();

  auto reader = std::shared_ptr<bw::core::Serializer>(
      bw::core::BinarySerializer::fromString(bytes));
  reader->deserialize();
  bw::core::PrimitivePropertySet copy;
  bw::core::SerializationWorkData readWorkData;
  require(copy.deserialize(reader, readWorkData) &&
              copy.floorSpan == original.floorSpan &&
              copy.ceilingSpan == original.ceilingSpan &&
              copy.floorEmbossPresetId == "floor_relief" &&
              copy.ceilingEmbossPresetId.empty() &&
              copy.wallEmbossPresetId == "wall_relief",
          "Elevation planes and Emboss-preset ids did not round-trip in binary data");
}

void malformedYamlArraysReportTheirPathInsteadOfAborting() {
  auto serializer = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString("items: not-an-array\n"));
  serializer->deserialize();
  serializer->beginArray("items");
  try {
    (void)serializer->nextArrayItem();
  } catch (bw::core::SerializationException const& error) {
    auto const message = std::string(error.what());
    require(message.find("/items/") != std::string::npos &&
                message.find("Expected a sequence") != std::string::npos,
            "malformed YAML array error did not identify its path and shape");
    serializer->endArray();
    return;
  }
  throw std::runtime_error("a malformed YAML array was accepted");
}

void propertySetRejectsLegacyShapeWithoutEmbossPresetIds() {
  auto serializer = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString(
          "floorZ: 0\nceilingZ: 48\n"
          "floorMaterial: ''\nceilingMaterial: ''\nwallMaterial: ''\n"));
  serializer->deserialize();

  bw::core::PrimitivePropertySet properties;
  bw::core::SerializationWorkData workData;
  require(!properties.deserialize(serializer, workData),
          "legacy property data without explicit Emboss-preset ids was accepted");
}

}  // namespace

int main() {
  try {
    successfulDeserializationLeavesObjectUnmodified();
    failedDeserializationReturnsFailureIndependentlyOfModifiedState();
    propertySetRoundTripsSurfaceResourceIds();
    propertySetRoundTripsElevationSpans();
    legacyElevationPlanesMigrateAfterGeometryIsAvailable();
    propertySetRoundTripsEmbossPresetIdsInBinary();
    malformedYamlArraysReportTheirPathInsteadOfAborting();
    propertySetRejectsLegacyShapeWithoutEmbossPresetIds();
    std::cout << "Serializable deserialization coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
