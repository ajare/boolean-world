#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/BinarySerializer.h>
#include <core/PrimitivePropertySet.h>
#include <core/Serializable.h>
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
}

void propertySetRoundTripsEmbossPresetIdsInBinary() {
  bw::core::PrimitivePropertySet original;
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
              copy.floorEmbossPresetId == "floor_relief" &&
              copy.ceilingEmbossPresetId.empty() &&
              copy.wallEmbossPresetId == "wall_relief",
          "per-surface Emboss-preset ids did not round-trip in binary data");
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
    propertySetRoundTripsEmbossPresetIdsInBinary();
    propertySetRejectsLegacyShapeWithoutEmbossPresetIds();
    std::cout << "Serializable deserialization coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
