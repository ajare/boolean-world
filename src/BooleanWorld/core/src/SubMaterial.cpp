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

EmbossParameterLimits ChipArrisLengthLimits() {
  return {0.01f, 128.0f};
}

EmbossParameterLimits ChipDepthLimits() {
  return {0.01f, 8.0f};
}

EmbossParameterLimits ChipReachLimits() {
  // Width is half-reach and may never exceed two world units.
  return {0.01f, 4.0f};
}

EmbossParameterLimits ChipSpacingLimits() {
  return {0.1f, 256.0f};
}

EmbossParameterLimits ChipProbabilityLimits() {
  return {0.0f, 1.0f};
}

bool ChipParametersAreValid(ChipGenerationParameters const& parameters) {
  return inRange(parameters.minimumArrisLength, ChipArrisLengthLimits()) &&
         inRange(parameters.minimumDepth, ChipDepthLimits()) &&
         inRange(parameters.maximumDepth, ChipDepthLimits()) &&
         inRange(parameters.minimumReach, ChipReachLimits()) &&
         inRange(parameters.maximumReach, ChipReachLimits()) &&
         inRange(parameters.minimumSpacing, ChipSpacingLimits()) &&
         inRange(parameters.probability, ChipProbabilityLimits()) &&
         parameters.minimumDepth <= parameters.maximumDepth &&
         parameters.minimumReach <= parameters.maximumReach &&
         parameters.maximumReach * 0.5f <= parameters.minimumArrisLength &&
         parameters.minimumSpacing >= parameters.maximumReach + 0.1f;
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

    serializer->beginMap("chip");
    {
      serializer->writeFloat("minimumArrisLength", chip.minimumArrisLength);
      serializer->writeFloat("minimumDepth", chip.minimumDepth);
      serializer->writeFloat("maximumDepth", chip.maximumDepth);
      serializer->writeFloat("minimumReach", chip.minimumReach);
      serializer->writeFloat("maximumReach", chip.maximumReach);
      serializer->writeFloat("minimumSpacing", chip.minimumSpacing);
      serializer->writeFloat("probability", chip.probability);

      serializer->endMap();
    }

    serializer->endMap();  // subMaterial
  }
}

bool SubMaterial::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  string id_, displayName_;
  uint32_t materialIndex_{0};
  vector<float> paramValues_;
  array<float, 3> baseColour_{};
  EmbossData emboss_;
  ChipGenerationParameters chip_;

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

      // Absent fields retain the non-chipping defaults. ProcMaterial catalogs
      // are map-based YAML resources, so this also migrates catalogs written
      // before randomized multi-Chip generation existed.
      auto optional = !serializer->isPositional();
      serializer->beginMap("chip");
      {
        chip_.minimumArrisLength = serializer->readFloat(
            "minimumArrisLength", optional, chip_.minimumArrisLength);
        chip_.minimumDepth = serializer->readFloat(
            "minimumDepth", optional, chip_.minimumDepth);
        chip_.maximumDepth = serializer->readFloat(
            "maximumDepth", optional, chip_.maximumDepth);
        chip_.minimumReach = serializer->readFloat(
            "minimumReach", optional, chip_.minimumReach);
        chip_.maximumReach = serializer->readFloat(
            "maximumReach", optional, chip_.maximumReach);
        chip_.minimumSpacing = serializer->readFloat(
            "minimumSpacing", optional, chip_.minimumSpacing);
        chip_.probability = serializer->readFloat(
            "probability", optional, chip_.probability);

        serializer->endMap();
      }

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

  if (!ChipParametersAreValid(chip_)) {
    addDeserializationError(
        "SubMaterial Chip generation parameters are invalid.");
    return false;
  }

  // Commit
  id = move(id_);
  displayName = move(displayName_);
  materialIndex = materialIndex_;
  paramValues = move(paramValues_);
  baseColour = baseColour_;
  emboss = emboss_;
  chip = chip_;

  return true;
}

}  // namespace core
}  // namespace bw
