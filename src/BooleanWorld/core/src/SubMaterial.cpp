#include "core/SubMaterial.h"

#include "core/Defines.h"
#include "core/SerializationException.h"

namespace bw {
namespace core {
using namespace std;

namespace {

bool inRange(float value, EmbossParameterLimits const& limits) {
  return value >= limits.minimum && value <= limits.maximum;
}

}  // namespace

bool SubMaterial::childrenModified() const {
  return false;
}

EmbossParameterLimits ChipDepthLimits() {
  return {0.0f, 8.0f};
}

EmbossParameterLimits ChipReachLimits() {
  return {0.0f, 128.0f};
}

bool ChipIsInRange(float chipDepth, float chipReach) {
  return inRange(chipDepth, ChipDepthLimits()) &&
         inRange(chipReach, ChipReachLimits());
}

void SubMaterial::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->beginMap("subMaterial");
  {
    serializer->writeString("id", id);
    serializer->writeString("name", displayName);
    serializer->writeUint32("materialIndex", materialIndex);

    serializer->beginArray("params", false);
    {
      for (auto value : paramValues) {
        serializer->writeFloat("", value);
      }

      serializer->endArray();
    }

    serializer->beginArray("baseColour", false);
    {
      for (auto component : baseColour) {
        serializer->writeFloat("", component);
      }

      serializer->endArray();
    }

    SerializeEmboss(serializer, "emboss", emboss);

    serializer->writeFloat("chipDepth", chipDepth);
    serializer->writeFloat("chipReach", chipReach);

    serializer->endMap();  // subMaterial
  }
}

bool SubMaterial::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  string id_, displayName_;
  uint32_t materialIndex_{0};
  vector<float> paramValues_;
  array<float, 3> baseColour_{};
  EmbossData emboss_;
  float chipDepth_{0.0f};
  float chipReach_{0.0f};

  try {
    serializer->beginMap("subMaterial");
    {
      id_ = serializer->readString("id");
      displayName_ = serializer->readString("name");
      materialIndex_ = serializer->readUint32("materialIndex");

      serializer->beginArray("params");
      {
        while (serializer->nextArrayItem()) {
          if (paramValues_.size() >= BW_MATERIAL_PARAMS_MAX) {
            throw SerializationException("Too many SubMaterial parameters.");
          }

          paramValues_.push_back(serializer->readFloat());
        }

        serializer->endArray();
      }

      serializer->beginArray("baseColour");
      {
        size_t i = 0;

        while (serializer->nextArrayItem()) {
          if (i >= baseColour_.size()) {
            throw SerializationException("Too many colour components.");
          }

          baseColour_[i++] = serializer->readFloat();
        }

        serializer->endArray();
      }

      // Absent in a catalog written before embossing existed, and in one that
      // simply embosses nothing: every field falls back to its default.
      emboss_ = DeserializeEmboss(serializer, "emboss");

      // Absent in a catalog written before chipping existed: falls back to
      // not chipping at all, exactly like a missing emboss block.
      auto optional = !serializer->isPositional();
      chipDepth_ = serializer->readFloat("chipDepth", optional, chipDepth_);
      chipReach_ = serializer->readFloat("chipReach", optional, chipReach_);

      serializer->endMap();  // subMaterial
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  if (id_.empty()) {
    addDeserializationError("SubMaterial id must not be empty.");
    return false;
  }

  if (!EmbossIsInRange(emboss_)) {
    addDeserializationError(
        "SubMaterial emboss values must fall within their authoring limits.");
    return false;
  }

  if (!ChipIsInRange(chipDepth_, chipReach_)) {
    addDeserializationError(
        "SubMaterial chip depth/reach must fall within their authoring limits.");
    return false;
  }

  // Commit
  id = move(id_);
  displayName = move(displayName_);
  materialIndex = materialIndex_;
  paramValues = move(paramValues_);
  baseColour = baseColour_;
  emboss = emboss_;
  chipDepth = chipDepth_;
  chipReach = chipReach_;

  return true;
}

}  // namespace core
}  // namespace bw
