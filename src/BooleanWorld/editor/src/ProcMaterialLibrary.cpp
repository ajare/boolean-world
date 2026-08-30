#include "ProcMaterialLibrary.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

#include <core/SerializationWorkData.h>
#include <core/YamlSerializer.h>

namespace editor {
using namespace std;
namespace fs = std::filesystem;

namespace {

string errors(bw::core::ProcMaterialData const& data) {
  string result;
  for (auto const& error : data.getDeserializationErrors()) {
    if (!result.empty()) result += "; ";
    result += error;
  }
  return result;
}

void validateValues(
    bw::core::TechniqueSchema const* schema, vector<float> const& values,
    array<float, 3> const& colour) {
  if (!schema) throw invalid_argument("The selected Technique has no schema");
  if (values.size() != schema->parameters.size()) {
    throw invalid_argument("Sub-material parameter count does not match its Technique schema");
  }
  for (size_t i = 0; i < values.size(); ++i) {
    if (values[i] < schema->parameters[i].minimum ||
        values[i] > schema->parameters[i].maximum) {
      throw invalid_argument("Sub-material parameter '" + schema->parameters[i].name +
                             "' is outside its Technique schema bounds");
    }
  }
  if (any_of(colour.begin(), colour.end(), [](float component) {
        return component < 0.0f || component > 1.0f;
      })) {
    throw invalid_argument("Sub-material base colour components must be between 0 and 1");
  }
}

// Chip generation has no Technique schema either.
void validateChip(bw::core::ChipGenerationParameters const& chip) {
  if (!bw::core::ChipParametersAreValid(chip)) {
    throw invalid_argument("Sub-material Chip generation parameters are invalid");
  }
}

string makeId(string const& name) {
  string id;
  bool separator{false};
  for (unsigned char character : name) {
    if (isalnum(character)) {
      if (separator && !id.empty()) id += '_';
      id += static_cast<char>(tolower(character));
      separator = false;
    } else {
      separator = true;
    }
  }
  return id.empty() ? "sub_material" : id;
}

bw::core::ProcMaterialData loadCatalog(fs::path const& path) {
  auto serializer = shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromFile(path.string()));
  serializer->deserialize();

  bw::core::ProcMaterialData data;
  bw::core::SerializationWorkData workData;
  if (!data.deserialize(serializer, workData)) {
    throw runtime_error(
        "Could not load ProcMaterial '" + path.string() + "': " + errors(data));
  }
  return data;
}

}  // namespace

void ProcMaterialLibrary::load(fs::path const& resourcesManifest) {
  auto root = YAML::LoadFile(resourcesManifest.string());
  auto resources = root["Resources"]["Resource"];
  if (!resources || !resources.IsSequence()) {
    throw runtime_error("Resources manifest has no Resources.Resource list: " +
                        resourcesManifest.string());
  }

  map<string, fs::path> textFiles;
  for (auto const& resource : resources) {
    if (resource["type"] && resource["type"].as<string>() == "TextFile" &&
        resource["name"] && resource["location"]) {
      textFiles[resource["name"].as<string>()] =
          resourcesManifest.parent_path() / resource["location"].as<string>();
    }
  }

  vector<ProcMaterialCatalog> loaded;
  map<string, string> idOwners;
  for (auto const& resource : resources) {
    if (!resource["type"] || resource["type"].as<string>() != "ProcMaterial" ||
        !resource["name"]) {
      continue;
    }

    string yamlReference;
    auto dependencies = resource["DependentResources"]["DependentResource"];
    if (dependencies.IsMap()) {
      if (dependencies["id"] && dependencies["id"].as<string>() == "Yaml" &&
          dependencies["ref"])
        yamlReference = dependencies["ref"].as<string>();
    } else if (dependencies.IsSequence()) {
      for (auto const& dependency : dependencies) {
        if (dependency["id"] && dependency["id"].as<string>() == "Yaml" &&
            dependency["ref"]) {
          yamlReference = dependency["ref"].as<string>();
          break;
        }
      }
    }

    auto textFile = textFiles.find(yamlReference);
    if (textFile == textFiles.end()) {
      throw runtime_error("ProcMaterial '" + resource["name"].as<string>() +
                          "' has no resolvable Yaml TextFile dependency");
    }

    ProcMaterialCatalog catalog{
        resource["name"].as<string>(), textFile->second, loadCatalog(textFile->second)};
    for (auto const& id : catalog.data.subMaterialIds()) {
      auto [owner, inserted] = idOwners.emplace(id, catalog.resourceName);
      if (!inserted) {
        throw runtime_error("Duplicate Sub-material id '" + id + "' in ProcMaterials '" +
                            owner->second + "' and '" + catalog.resourceName + "'");
      }
    }
    loaded.push_back(move(catalog));
  }

  mCatalogs = move(loaded);
  ++mRevision;
}

ProcMaterialCatalog& ProcMaterialLibrary::findCatalog(string const& resourceName) {
  auto found = find_if(mCatalogs.begin(), mCatalogs.end(), [&](auto const& catalog) {
    return catalog.resourceName == resourceName;
  });
  if (found == mCatalogs.end()) {
    throw invalid_argument("Unknown ProcMaterial resource '" + resourceName + "'");
  }
  return *found;
}

void ProcMaterialLibrary::save(ProcMaterialCatalog const& catalog) const {
  auto serializer = shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::toFile(catalog.filepath.string()));
  bw::core::SerializationWorkData workData;
  catalog.data.serialize(serializer, workData);
  serializer->serialize();
}

vector<ProcMaterialCatalog> const& ProcMaterialLibrary::catalogs() const {
  return mCatalogs;
}

ProcMaterialCatalog const* ProcMaterialLibrary::findCatalogForSubMaterial(
    string const& subMaterialId) const {
  for (auto const& catalog : mCatalogs) {
    for (auto const& subMaterial : catalog.data.subMaterials) {
      if (subMaterial.id == subMaterialId) return &catalog;
    }
  }
  return nullptr;
}

bw::core::SubMaterial const* ProcMaterialLibrary::findSubMaterial(
    string const& subMaterialId) const {
  auto const* catalog = findCatalogForSubMaterial(subMaterialId);
  if (!catalog) return nullptr;
  for (auto const& subMaterial : catalog->data.subMaterials) {
    if (subMaterial.id == subMaterialId) return &subMaterial;
  }
  return nullptr;
}

bw::core::SubMaterial const* ProcMaterialLibrary::findSubMaterialByMaterialIndex(
    uint32_t materialIndex) const {
  for (auto const& catalog : mCatalogs) {
    for (auto const& subMaterial : catalog.data.subMaterials) {
      if (subMaterial.materialIndex == materialIndex) return &subMaterial;
    }
  }
  return nullptr;
}

string ProcMaterialLibrary::createSubMaterial(
    string const& resourceName, string const& displayName,
    uint32_t materialIndex, vector<float> const& paramValues,
    array<float, 3> const& baseColour,
    bw::core::ChipGenerationParameters const& chip) {
  if (displayName.empty()) throw invalid_argument("Sub-material name must not be empty");
  auto& catalog = findCatalog(resourceName);
  validateValues(catalog.data.findTechniqueSchema(materialIndex), paramValues, baseColour);
  validateChip(chip);

  auto stem = makeId(displayName);
  auto id = stem;
  uint32_t suffix{2};
  while (findSubMaterial(id)) id = stem + "_" + to_string(suffix++);

  auto previous = catalog.data;
  bw::core::SubMaterial created;
  created.id = id;
  created.displayName = displayName;
  created.materialIndex = materialIndex;
  created.paramValues = paramValues;
  created.baseColour = baseColour;
  created.chip = chip;
  catalog.data.subMaterials.push_back(move(created));
  try {
    save(catalog);
  } catch (...) {
    catalog.data = move(previous);
    throw;
  }
  ++mRevision;
  return id;
}

void ProcMaterialLibrary::renameSubMaterial(
    string const& subMaterialId, string const& displayName) {
  if (displayName.empty()) throw invalid_argument("Sub-material name must not be empty");
  auto* owner = findCatalogForSubMaterial(subMaterialId);
  if (!owner) throw invalid_argument("Unknown Sub-material id '" + subMaterialId + "'");
  auto& catalog = findCatalog(owner->resourceName);
  auto found = find_if(catalog.data.subMaterials.begin(), catalog.data.subMaterials.end(),
                       [&](auto const& value) { return value.id == subMaterialId; });
  auto previous = found->displayName;
  found->displayName = displayName;
  try {
    save(catalog);
  } catch (...) {
    found->displayName = move(previous);
    throw;
  }
  ++mRevision;
}

void ProcMaterialLibrary::editSubMaterial(
    string const& subMaterialId, vector<float> const& paramValues,
    array<float, 3> const& baseColour,
    bw::core::ChipGenerationParameters const& chip) {
  auto* owner = findCatalogForSubMaterial(subMaterialId);
  if (!owner) throw invalid_argument("Unknown Sub-material id '" + subMaterialId + "'");
  auto& catalog = findCatalog(owner->resourceName);
  auto found = find_if(catalog.data.subMaterials.begin(), catalog.data.subMaterials.end(),
                       [&](auto const& value) { return value.id == subMaterialId; });
  validateValues(catalog.data.findTechniqueSchema(found->materialIndex), paramValues, baseColour);
  validateChip(chip);
  auto previousValues = found->paramValues;
  auto previousColour = found->baseColour;
  auto previousChip = found->chip;
  found->paramValues = paramValues;
  found->baseColour = baseColour;
  found->chip = chip;
  try {
    save(catalog);
  } catch (...) {
    found->paramValues = move(previousValues);
    found->baseColour = previousColour;
    found->chip = previousChip;
    throw;
  }
  ++mRevision;
}

void ProcMaterialLibrary::deleteSubMaterial(string const& subMaterialId) {
  auto* owner = findCatalogForSubMaterial(subMaterialId);
  if (!owner) throw invalid_argument("Unknown Sub-material id '" + subMaterialId + "'");
  auto& catalog = findCatalog(owner->resourceName);
  auto previous = catalog.data;
  erase_if(catalog.data.subMaterials,
           [&](auto const& value) { return value.id == subMaterialId; });
  try {
    save(catalog);
  } catch (...) {
    catalog.data = move(previous);
    throw;
  }
  ++mRevision;
}

ProcMaterialLibrarySnapshot ProcMaterialLibrary::captureSnapshot() const {
  return {mCatalogs, mRevision};
}

void ProcMaterialLibrary::restoreSnapshot(ProcMaterialLibrarySnapshot const& snapshot) {
  if (mRevision == snapshot.revision) return;
  auto previous = mCatalogs;
  mCatalogs = snapshot.catalogs;
  try {
    for (auto const& catalog : mCatalogs) save(catalog);
  } catch (...) {
    mCatalogs = move(previous);
    throw;
  }
  mRevision = snapshot.revision;
}

ProcMaterialLibrary& procMaterialLibrary() {
  static ProcMaterialLibrary library;
  return library;
}

}  // namespace editor
