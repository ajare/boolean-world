#include "ProcMaterialLibrary.h"

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

ProcMaterialLibrary& procMaterialLibrary() {
  static ProcMaterialLibrary library;
  return library;
}

}  // namespace editor
