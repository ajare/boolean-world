#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <core/EmbossingCatalogData.h>
#include <core/YamlSerializer.h>

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) throw std::runtime_error(message);
}

bool load(std::string const& yaml, bw::core::EmbossingCatalogData& catalog) {
  auto serializer = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString(yaml));
  serializer->deserialize();
  bw::core::SerializationWorkData workData;
  return catalog.deserialize(serializer, workData);
}

std::string preset(std::string const& id, float radius = 12.0f) {
  return "  - id: " + id + "\n"
         "    name: Blocks\n"
         "    emboss:\n"
         "      pattern: Square\n"
         "      radius: " + std::to_string(radius) + "\n"
         "      depth: 0.5\n"
         "      depthVariation: 0.1\n"
         "      runningBondWidth: 50\n"
         "      runningBondOffset: 50\n"
         "      voronoiRounding: 0.25\n";
}

void namedPresetLoadsAndRoundTrips() {
  bw::core::EmbossingCatalogData catalog;
  require(load("presets:\n" + preset("catalog.blocks"), catalog),
          "valid Embossing catalog did not load");
  require(catalog.findPreset("catalog.blocks") != nullptr,
          "loaded Emboss preset was not findable by stable id");
  require(catalog.findPreset("catalog.blocks")->displayName == "Blocks",
          "Emboss preset display name was not loaded");

  auto writer = std::shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::toString());
  bw::core::SerializationWorkData workData;
  catalog.serialize(writer, workData);
  writer->serialize();
  auto yaml = static_cast<bw::core::YamlSerializer*>(writer.get())
                  ->getSerializedString();
  bw::core::EmbossingCatalogData copy;
  require(load(yaml, copy) && copy.presets.size() == 1 &&
              copy.presets[0].emboss == catalog.presets[0].emboss,
          "Embossing catalog did not round-trip");
}

void invalidCatalogsAreRejected() {
  bw::core::EmbossingCatalogData duplicate;
  require(!load("presets:\n" + preset("duplicate") + preset("duplicate"),
                duplicate),
          "duplicate Emboss preset ids were accepted");

  bw::core::EmbossingCatalogData invalid;
  require(!load("presets:\n" + preset("invalid", 10000.0f), invalid),
          "out-of-range Emboss preset values were accepted");
}

}  // namespace

int main() {
  try {
    namedPresetLoadsAndRoundTrips();
    invalidCatalogsAreRejected();
    std::cout << "Embossing catalog coverage passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
