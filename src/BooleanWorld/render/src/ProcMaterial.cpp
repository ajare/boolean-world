#include <willpower/application/resourcesystem/TextFileResource.h>
#include <willpower/application/resourcesystem/ResourceExceptions.h>

#include <core/YamlSerializer.h>

#include "ProcMaterial.h"

using namespace std;
using namespace wp;

ProcMaterial::ProcMaterial(string const& name,
                           string const& namesp,
                           string const& source,
                           map<string, string> const& tags,
                           application::resourcesystem::ResourceLocation* location)
    : application::resourcesystem::Resource(name, namesp, "ProcMaterial", source, tags, location) {
}

void ProcMaterial::destroy() {
  mData = bw::core::ProcMaterialData();
}

void ProcMaterial::loadFromYaml(application::resourcesystem::ResourcePtr resource) {
  auto res = static_cast<application::resourcesystem::TextFileResource*>(resource.get());

  auto ser = shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::fromString(res->getText()));
  ser->deserialize();

  bw::core::ProcMaterialData data;
  auto workData = bw::core::SerializationWorkData{};

  if (!data.deserialize(ser, workData)) {
    string message = "Could not load ProcMaterial from YAML.";

    for (auto const& error : data.getDeserializationErrors()) {
      message += " " + error;
    }

    throw application::resourcesystem::ResourceException(this, message);
  }

  mData = move(data);
}

bw::core::ProcMaterialData const& ProcMaterial::getData() const {
  return mData;
}
