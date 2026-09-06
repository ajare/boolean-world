#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <willpower/common/Vector2.h>

#include "core/Serializable.h"

namespace bw::core {

// Authored point sound source data. Core deliberately treats soundId as an
// opaque stable reference; resolving it belongs to an audio host.
struct EmitterPlacementKey {
  int32_t tileX{0};
  int32_t tileY{0};
  uint32_t gridSize{0};

  bool operator==(EmitterPlacementKey const&) const = default;
};

// Immutable generation output. Identity is the authored GUID together with
// the optional placement key; directly-authored Primitives have no key.
struct CapturedAudioEmitter {
  wp::Vector2 position;
  float height{0.0f};
  std::string soundId;
  std::string guid;
  float cullRadius{0.0f};
  std::optional<EmitterPlacementKey> placementKey;
};

struct AudioEmitter : public Serializable {
  wp::Vector2 offset;
  float heightOffset{0.0f};
  std::string soundId;
  std::string guid;
  float cullRadius{0.0f};

private:
  bool childrenModified() const override;

protected:
  void serializeImpl(std::shared_ptr<Serializer> serializer,
                     SerializationWorkData& workData) const override;
  bool deserializeImpl(std::shared_ptr<Serializer> serializer,
                       SerializationWorkData& workData) override;
};

}  // namespace bw::core
