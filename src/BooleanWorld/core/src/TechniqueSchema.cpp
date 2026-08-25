#include "core/TechniqueSchema.h"

#include "core/Defines.h"
#include "core/SerializationException.h"

namespace bw {
namespace core {
using namespace std;

bool TechniqueSchema::childrenModified() const {
  return false;
}

void TechniqueSchema::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->beginMap("techniqueSchema");
  {
    serializer->writeUint32("materialIndex", materialIndex);

    serializer->beginArray("parameters");
    {
      for (auto const& parameter : parameters) {
        serializer->beginMap("");
        {
          serializer->writeString("name", parameter.name);
          serializer->writeFloat("min", parameter.minimum);
          serializer->writeFloat("max", parameter.maximum);
          serializer->writeFloat("default", parameter.defaultValue);

          serializer->endMap();
        }
      }

      serializer->endArray();
    }

    serializer->endMap();  // techniqueSchema
  }
}

bool TechniqueSchema::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  uint32_t materialIndex_{0};
  vector<TechniqueParameterSchema> parameters_;

  try {
    serializer->beginMap("techniqueSchema");
    {
      materialIndex_ = serializer->readUint32("materialIndex");

      serializer->beginArray("parameters");
      {
        while (serializer->nextArrayItem()) {
          if (parameters_.size() >= BW_MATERIAL_PARAMS_MAX) {
            throw SerializationException("Too many TechniqueSchema parameters.");
          }

          serializer->beginMap("");
          {
            TechniqueParameterSchema parameter;
            parameter.name = serializer->readString("name");
            parameter.minimum = serializer->readFloat("min");
            parameter.maximum = serializer->readFloat("max");
            parameter.defaultValue = serializer->readFloat("default");
            parameters_.push_back(move(parameter));

            serializer->endMap();
          }
        }

        serializer->endArray();
      }

      serializer->endMap();  // techniqueSchema
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  if (materialIndex_ >= BW_MATERIAL_COUNT) {
    addDeserializationError("TechniqueSchema materialIndex " + to_string(materialIndex_) + " is out of range.");
    return false;
  }

  for (auto const& parameter : parameters_) {
    if (parameter.minimum > parameter.maximum) {
      addDeserializationError("TechniqueSchema parameter '" + parameter.name + "' has minimum greater than maximum.");
      return false;
    }
    if (parameter.defaultValue < parameter.minimum || parameter.defaultValue > parameter.maximum) {
      addDeserializationError("TechniqueSchema parameter '" + parameter.name + "' default is out of bounds.");
      return false;
    }
  }

  // Commit
  materialIndex = materialIndex_;
  parameters = move(parameters_);

  return true;
}

}  // namespace core
}  // namespace bw
