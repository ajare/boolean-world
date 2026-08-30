#include <willpower/application/resourcesystem/ResourceExceptions.h>
#include <willpower/application/resourcesystem/TextFileResource.h>

#include <core/YamlSerializer.h>

#include "EmbossingCatalog.h"

using namespace std;
using namespace wp;

EmbossingCatalog::EmbossingCatalog(
    string const& name, string const& namesp, string const& source,
    map<string, string> const& tags,
    application::resourcesystem::ResourceLocation* location)
    : application::resourcesystem::Resource(
          name, namesp, "EmbossingCatalog", source, tags, location) {}

void EmbossingCatalog::destroy() {
  mData = bw::core::EmbossingCatalogData{};
}

void EmbossingCatalog::loadFromYaml(
    application::resourcesystem::ResourcePtr resource) {
  auto text = static_cast<application::resourcesystem::TextFileResource*>(
      resource.get());
  auto serializer = shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromString(text->getText()));
  serializer->deserialize();

  bw::core::EmbossingCatalogData data;
  bw::core::SerializationWorkData workData;
  if (!data.deserialize(serializer, workData)) {
    string message = "Could not load Embossing catalog from YAML.";
    for (auto const& error : data.getDeserializationErrors()) {
      message += " " + error;
    }
    throw application::resourcesystem::ResourceException(this, message);
  }
  mData = move(data);
}

bw::core::EmbossingCatalogData const& EmbossingCatalog::getData() const {
  return mData;
}
