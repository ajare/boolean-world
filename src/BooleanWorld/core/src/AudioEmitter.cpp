#include "core/AudioEmitter.h"

namespace bw::core {
using namespace std;

bool AudioEmitter::childrenModified() const {
  return false;
}

void AudioEmitter::serializeImpl(
    shared_ptr<Serializer> serializer, SerializationWorkData&) const {
  serializer->beginMap("audioEmitter");
  {
    serializer->writeVector2("offset", offset);
    serializer->writeFloat("heightOffset", heightOffset);
    serializer->writeString("soundId", soundId);
    serializer->writeString("guid", guid);
    serializer->writeFloat("cullRadius", cullRadius);
    serializer->endMap();  // audioEmitter
  }
}

bool AudioEmitter::deserializeImpl(
    shared_ptr<Serializer> serializer, SerializationWorkData&) {
  wp::Vector2 offset_;
  float heightOffset_;
  string soundId_;
  string guid_;
  float cullRadius_;

  try {
    serializer->beginMap("audioEmitter");
    {
      offset_ = serializer->readVector2("offset");
      heightOffset_ = serializer->readFloat("heightOffset");
      soundId_ = serializer->readString("soundId");
      guid_ = serializer->readString("guid");
      cullRadius_ = serializer->readFloat("cullRadius");
      serializer->endMap();  // audioEmitter
    }
  } catch (exception const& error) {
    addDeserializationError(error.what());
    return false;
  }

  offset = offset_;
  heightOffset = heightOffset_;
  soundId = move(soundId_);
  guid = move(guid_);
  cullRadius = cullRadius_;
  return true;
}

}  // namespace bw::core
