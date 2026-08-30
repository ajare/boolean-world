#include <unordered_set>

#include "core/EmbossingCatalogData.h"

namespace bw::core {
using namespace std;

EmbossPreset const* EmbossingCatalogData::findPreset(string const& id) const {
  for (auto const& preset : presets) {
    if (preset.id == id) {
      return &preset;
    }
  }
  return nullptr;
}

bool EmbossingCatalogData::childrenModified() const {
  for (auto const& preset : presets) {
    if (preset.isModified()) {
      return true;
    }
  }
  return false;
}

void EmbossingCatalogData::serializeImpl(
    shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->beginMap("embossingCatalog");
  {
    serializer->beginArray("presets");
    {
      for (auto const& preset : presets) {
        preset.serialize(serializer, workData);
      }
      serializer->endArray();
    }
    serializer->endMap();
  }
}

bool EmbossingCatalogData::deserializeImpl(
    shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  vector<EmbossPreset> presets_;

  try {
    serializer->beginMap("embossingCatalog");
    {
      serializer->beginArray("presets");
      {
        while (serializer->nextArrayItem()) {
          EmbossPreset preset;
          if (!preset.deserialize(serializer, workData)) {
            copyErrorsAndWarnings(&preset, true, true);
            return false;
          }
          presets_.push_back(move(preset));
        }
        serializer->endArray();
      }
      serializer->endMap();
    }
  } catch (exception const& error) {
    addDeserializationError(error.what());
    return false;
  }

  unordered_set<string> ids;
  for (auto const& preset : presets_) {
    if (!ids.insert(preset.id).second) {
      addDeserializationError("Duplicate Emboss preset id '" + preset.id + "'.");
      return false;
    }
  }

  presets = move(presets_);
  return true;
}

}  // namespace bw::core
