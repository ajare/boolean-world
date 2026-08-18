#include <algorithm>
#include <filesystem>

#include <willpower/application/resourcesystem/TextFileResource.h>
#include <willpower/application/resourcesystem/ResourceExceptions.h>

#include <core/BinarySerializer.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/YamlSerializer.h>

#include "Map.h"

using namespace std;
using namespace wp;
using namespace wp::geometry;

Map::Map(string const& name,
         string const& namesp,
         string const& source,
         map<string, string> const& tags,
         application::resourcesystem::ResourceLocation* location,
         wp::Logger* logger)
    : applib::Map(name, namesp, source, tags, location, 512), mWorld(nullptr), mwLogger(logger) {
}

Map::~Map() {
  delete mWorld;
}

bw::core::World* Map::getWorld() {
  return mWorld;
}

bw::core::World const* Map::getWorld() const {
  return mWorld;
}

void Map::loadWorldFromYaml(wp::application::resourcesystem::ResourcePtr resource) {
  delete mWorld;
  mWorld = nullptr;

  auto res = static_cast<wp::application::resourcesystem::TextFileResource*>(resource.get());
  string text = res->getText();

  // The resource's source carries the original filename (e.g. "world.world" or
  // "world.yaml"), so use its extension to pick the matching Serializer. Worlds
  // exported from the editor as .world files are binary, not YAML, and parsing
  // one as YAML text either fails outright or silently misreads the data.
  auto ext = filesystem::path(resource->getSource()).extension().string();
  transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  shared_ptr<bw::core::Serializer> ser = ext == ".world"
      ? shared_ptr<bw::core::Serializer>(bw::core::BinarySerializer::fromString(text))
      : shared_ptr<bw::core::Serializer>(bw::core::YamlSerializer::fromString(text));

  ser->deserialize();

  mWorld = new bw::core::World(1.0f, -1.0f);

  // Create grid with cell size 512
  auto workData = bw::core::SerializationWorkData{512.0f};

  if (mWorld->deserialize(ser, workData)) {
    auto const& warnings = mWorld->getDeserializationWarnings();

    if (!warnings.empty()) {
      for (auto const& warning : warnings) {
        mwLogger->warn(warning);
      }
    }

    mWorld->setWorldDataGenerator(new bw::core::DynamicWorldDataGenerator(mWorld));
  } else {
    auto const& errors = mWorld->getDeserializationErrors();

    if (!errors.empty()) {
      for (auto const& error : errors) {
        mwLogger->error(error);
      }
    }

    throw wp::application::resourcesystem::ResourceException(resource.get(), "Could not load World from YAML.");
  }
}
