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

std::string propertySetWithTooManyMaterialValues(std::string const& invalidMaterial,
                                                 bool tooManyBaseColour) {
  std::string const validParameters = "[0, 0, 0, 0, 0, 0, 0, 0]";
  std::string const tooManyParameters = "[0, 0, 0, 0, 0, 0, 0, 0, 0]";
  std::string const validColour = "[0, 0, 0]";
  std::string const tooManyColourComponents = "[0, 0, 0, 0]";

  std::string yaml = "floorZ: 0\nceilingZ: 48\n";
  for (auto const* material : {"floorMaterial", "ceilingMaterial", "wallMaterial"}) {
    bool const isInvalidMaterial = invalidMaterial == material;
    yaml += std::string(material) + ":\n";
    yaml += "  materialIndex: 0\n  materialDef:\n    params: ";
    yaml += isInvalidMaterial && !tooManyBaseColour ? tooManyParameters : validParameters;
    yaml += "\n    baseColour: ";
    yaml += isInvalidMaterial && tooManyBaseColour ? tooManyColourComponents : validColour;
    yaml += '\n';
  }
  return yaml;
}

bool containsError(bw::core::PrimitivePropertySet const& properties,
                   std::string const& expected) {
  for (auto const& error : properties.getDeserializationErrors()) {
    if (error == expected) {
      return true;
    }
  }
  return false;
}

void malformedMaterialDefinitionStopsPropertySetDeserialization() {
  struct MalformedArray {
    bool tooManyColourComponents;
    char const* error;
  };

  for (auto const& malformed : {MalformedArray{false, "Too many MaterialDefinition parameters."},
                                MalformedArray{true, "Too many colour components."}}) {
    for (auto const* material : {"floorMaterial", "ceilingMaterial", "wallMaterial"}) {
      auto serializer = std::shared_ptr<bw::core::Serializer>(
          bw::core::YamlSerializer::fromString(
              propertySetWithTooManyMaterialValues(material, malformed.tooManyColourComponents)));
      serializer->deserialize();

      bw::core::PrimitivePropertySet properties;
      bw::core::SerializationWorkData workData;
      require(!properties.deserialize(serializer, workData),
              std::string("property set accepted malformed ") + material);
      require(containsError(properties, malformed.error),
              std::string("property set did not report malformed ") + material);
    }
  }
}

}  // namespace

int main() {
  try {
    successfulDeserializationLeavesObjectUnmodified();
    failedDeserializationReturnsFailureIndependentlyOfModifiedState();
    malformedMaterialDefinitionStopsPropertySetDeserialization();
    std::cout << "Serializable deserialization coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
