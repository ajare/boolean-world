#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

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

void propertySetRoundTripsSubMaterialIds() {
  bw::core::PrimitivePropertySet original;
  original.floorZ = 0.0f;
  original.ceilingZ = 48.0f;
  original.floorMaterialId = "weathered_slate";
  original.ceilingMaterialId = "polished_slate";
  original.wallMaterialId = "";

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
}

void propertySetToleratesMissingSubMaterialIds() {
  auto serializer = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString("floorZ: 0\nceilingZ: 48\n"));
  serializer->deserialize();

  bw::core::PrimitivePropertySet properties;
  bw::core::SerializationWorkData workData;
  require(properties.deserialize(serializer, workData),
          "property set treated missing Sub-material ids as a deserialization failure");
  require(properties.floorMaterialId.empty(),
          "a missing floor Sub-material id was not left empty");
  require(properties.ceilingMaterialId.empty(),
          "a missing ceiling Sub-material id was not left empty");
  require(properties.wallMaterialId.empty(),
          "a missing wall Sub-material id was not left empty");
}

}  // namespace

int main() {
  try {
    successfulDeserializationLeavesObjectUnmodified();
    failedDeserializationReturnsFailureIndependentlyOfModifiedState();
    propertySetRoundTripsSubMaterialIds();
    propertySetToleratesMissingSubMaterialIds();
    std::cout << "Serializable deserialization coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
