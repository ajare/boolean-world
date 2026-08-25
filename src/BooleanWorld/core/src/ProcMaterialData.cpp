#include <algorithm>
#include <unordered_set>

#include "core/ProcMaterialData.h"

namespace bw {
namespace core {
using namespace std;

vector<string> ProcMaterialData::subMaterialIds() const {
  vector<string> ids;
  ids.reserve(subMaterials.size());
  for (auto const& subMaterial : subMaterials) {
    ids.push_back(subMaterial.id);
  }
  return ids;
}

TechniqueSchema const* ProcMaterialData::findTechniqueSchema(uint32_t materialIndex) const {
  for (auto const& schema : techniqueSchemas) {
    if (schema.materialIndex == materialIndex) {
      return &schema;
    }
  }
  return nullptr;
}

bool ProcMaterialData::childrenModified() const {
  for (auto const& schema : techniqueSchemas) {
    if (schema.isModified()) {
      return true;
    }
  }
  for (auto const& subMaterial : subMaterials) {
    if (subMaterial.isModified()) {
      return true;
    }
  }
  return false;
}

void ProcMaterialData::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->beginMap("procMaterial");
  {
    serializer->writeString("program3d", program3d);
    serializer->writeString("program2d", program2d);

    serializer->beginArray("techniqueSchemas");
    {
      for (auto const& schema : techniqueSchemas) {
        schema.serialize(serializer, workData);
      }

      serializer->endArray();
    }

    serializer->beginArray("subMaterials");
    {
      for (auto const& subMaterial : subMaterials) {
        subMaterial.serialize(serializer, workData);
      }

      serializer->endArray();
    }

    serializer->endMap();  // procMaterial
  }
}

bool ProcMaterialData::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  string program3d_, program2d_;
  vector<TechniqueSchema> techniqueSchemas_;
  vector<SubMaterial> subMaterials_;

  try {
    serializer->beginMap("procMaterial");
    {
      program3d_ = serializer->readString("program3d");
      program2d_ = serializer->readString("program2d");

      serializer->beginArray("techniqueSchemas");
      {
        while (serializer->nextArrayItem()) {
          TechniqueSchema schema;
          if (!schema.deserialize(serializer, workData)) {
            copyErrorsAndWarnings(&schema, true, true);
            return false;
          }
          techniqueSchemas_.push_back(move(schema));
        }

        serializer->endArray();
      }

      serializer->beginArray("subMaterials");
      {
        while (serializer->nextArrayItem()) {
          SubMaterial subMaterial;
          if (!subMaterial.deserialize(serializer, workData)) {
            copyErrorsAndWarnings(&subMaterial, true, true);
            return false;
          }
          subMaterials_.push_back(move(subMaterial));
        }

        serializer->endArray();
      }

      serializer->endMap();  // procMaterial
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Cross-validation: TechniqueSchema materialIndex must be unique within
  // this catalog - two schemas for the same Technique would leave a
  // SubMaterial's bounds check ambiguous.
  {
    unordered_set<uint32_t> seenTechniques;
    for (auto const& schema : techniqueSchemas_) {
      if (!seenTechniques.insert(schema.materialIndex).second) {
        addDeserializationError(
            "Duplicate TechniqueSchema for materialIndex " + to_string(schema.materialIndex) + ".");
        return false;
      }
    }
  }

  // Cross-validation: SubMaterial id uniqueness within this catalog. Global
  // uniqueness across every loaded catalog is validated at a higher seam
  // once multiple catalogs can be loaded together (subMaterialIds() exists
  // for that check to be built on top of, without a data-shape change).
  {
    unordered_set<string> seenIds;
    for (auto const& subMaterial : subMaterials_) {
      if (!seenIds.insert(subMaterial.id).second) {
        addDeserializationError("Duplicate SubMaterial id '" + subMaterial.id + "'.");
        return false;
      }
    }
  }

  // Cross-validation: every SubMaterial must select a Technique this
  // catalog actually defines a schema for, with a matching parameter count,
  // and each value within that schema parameter's bounds.
  for (auto const& subMaterial : subMaterials_) {
    TechniqueSchema const* schema = nullptr;
    for (auto const& candidate : techniqueSchemas_) {
      if (candidate.materialIndex == subMaterial.materialIndex) {
        schema = &candidate;
        break;
      }
    }

    if (!schema) {
      addDeserializationError(
          "SubMaterial '" + subMaterial.id + "' references materialIndex " +
          to_string(subMaterial.materialIndex) + " with no TechniqueSchema.");
      return false;
    }

    if (subMaterial.paramValues.size() != schema->parameters.size()) {
      addDeserializationError(
          "SubMaterial '" + subMaterial.id + "' has " + to_string(subMaterial.paramValues.size()) +
          " parameter value(s), expected " + to_string(schema->parameters.size()) +
          " for materialIndex " + to_string(subMaterial.materialIndex) + ".");
      return false;
    }

    for (size_t i = 0; i < subMaterial.paramValues.size(); ++i) {
      auto const& paramSchema = schema->parameters[i];
      auto value = subMaterial.paramValues[i];

      if (value < paramSchema.minimum || value > paramSchema.maximum) {
        addDeserializationError(
            "SubMaterial '" + subMaterial.id + "' parameter '" + paramSchema.name +
            "' value is out of bounds.");
        return false;
      }
    }
  }

  // Commit
  program3d = move(program3d_);
  program2d = move(program2d_);
  techniqueSchemas = move(techniqueSchemas_);
  subMaterials = move(subMaterials_);

  return true;
}

}  // namespace core
}  // namespace bw
