#include "core/EmbossPreset.h"

namespace bw::core {
using namespace std;

bool EmbossPreset::childrenModified() const {
  return false;
}

void EmbossPreset::serializeImpl(
    shared_ptr<Serializer> serializer, SerializationWorkData&) const {
  serializer->beginMap("embossPreset");
  {
    serializer->writeString("id", id);
    serializer->writeString("name", displayName);
    SerializeEmboss(serializer, "emboss", emboss);
    serializer->endMap();
  }
}

bool EmbossPreset::deserializeImpl(
    shared_ptr<Serializer> serializer, SerializationWorkData&) {
  string id_;
  string displayName_;
  EmbossData emboss_;

  try {
    serializer->beginMap("embossPreset");
    {
      id_ = serializer->readString("id");
      displayName_ = serializer->readString("name");
      emboss_ = DeserializeEmboss(serializer, "emboss");
      serializer->endMap();
    }
  } catch (exception const& error) {
    addDeserializationError(error.what());
    return false;
  }

  if (id_.empty()) {
    addDeserializationError("Emboss preset id must not be empty.");
    return false;
  }
  if (displayName_.empty()) {
    addDeserializationError("Emboss preset name must not be empty.");
    return false;
  }
  if (!EmbossIsInRange(emboss_)) {
    addDeserializationError(
        "Emboss preset values must fall within their authoring limits.");
    return false;
  }

  id = move(id_);
  displayName = move(displayName_);
  emboss = emboss_;
  return true;
}

}  // namespace bw::core
