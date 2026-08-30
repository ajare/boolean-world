#include "EmbossingCatalogLibrary.h"

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

string errors(bw::core::EmbossingCatalogData const& data) {
  string result;
  for (auto const& error : data.getDeserializationErrors()) {
    if (!result.empty()) result += "; ";
    result += error;
  }
  return result;
}

bw::core::EmbossingCatalogData loadCatalog(fs::path const& path) {
  auto serializer = shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::fromFile(path.string()));
  serializer->deserialize();
  bw::core::EmbossingCatalogData data;
  bw::core::SerializationWorkData workData;
  if (!data.deserialize(serializer, workData)) {
    throw runtime_error("Could not load Embossing catalog '" + path.string() +
                        "': " + errors(data));
  }
  return data;
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
  return id.empty() ? "emboss_preset" : id;
}

void validate(string const& displayName, bw::core::EmbossData const& emboss) {
  if (displayName.empty()) {
    throw invalid_argument("Emboss preset name must not be empty");
  }
  if (!bw::core::EmbossIsInRange(emboss)) {
    throw invalid_argument("Emboss preset values are outside their authoring limits");
  }
}

}  // namespace

void EmbossingCatalogLibrary::load(fs::path const& resourcesManifest) {
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

  fs::path catalogPath;
  string catalogName;
  for (auto const& resource : resources) {
    if (!resource["type"] ||
        resource["type"].as<string>() != "EmbossingCatalog" ||
        !resource["name"]) {
      continue;
    }
    if (!catalogPath.empty()) {
      throw runtime_error("Only one global Embossing catalog may be authored; found '" +
                          catalogName + "' and '" +
                          resource["name"].as<string>() + "'");
    }

    string yamlReference;
    auto dependencies = resource["DependentResources"]["DependentResource"];
    if (dependencies.IsMap()) {
      if (dependencies["id"] && dependencies["id"].as<string>() == "Yaml" &&
          dependencies["ref"]) {
        yamlReference = dependencies["ref"].as<string>();
      }
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
      throw runtime_error("Embossing catalog '" +
                          resource["name"].as<string>() +
                          "' has no resolvable Yaml TextFile dependency");
    }
    catalogName = resource["name"].as<string>();
    catalogPath = textFile->second;
  }

  if (catalogPath.empty()) {
    throw runtime_error("Resources manifest has no global Embossing catalog");
  }

  auto loaded = loadCatalog(catalogPath);
  mFilepath = move(catalogPath);
  mResourceName = move(catalogName);
  mData = move(loaded);
  ++mRevision;
}

void EmbossingCatalogLibrary::save() const {
  if (mFilepath.empty()) {
    throw logic_error("The global Embossing catalog has not been loaded");
  }
  auto serializer = shared_ptr<bw::core::Serializer>(
      bw::core::YamlSerializer::toFile(mFilepath.string()));
  bw::core::SerializationWorkData workData;
  mData.serialize(serializer, workData);
  serializer->serialize();
}

bw::core::EmbossingCatalogData const& EmbossingCatalogLibrary::data() const {
  return mData;
}

string const& EmbossingCatalogLibrary::resourceName() const {
  return mResourceName;
}

bw::core::EmbossPreset const* EmbossingCatalogLibrary::findPreset(
    string const& presetId) const {
  return mData.findPreset(presetId);
}

string EmbossingCatalogLibrary::createPreset(
    string const& displayName, bw::core::EmbossData const& emboss) {
  validate(displayName, emboss);
  auto stem = makeId(displayName);
  auto id = stem;
  uint32_t suffix{2};
  while (findPreset(id)) id = stem + "_" + to_string(suffix++);

  auto previous = mData;
  bw::core::EmbossPreset created;
  created.id = id;
  created.displayName = displayName;
  created.emboss = emboss;
  mData.presets.push_back(move(created));
  try {
    save();
  } catch (...) {
    mData = move(previous);
    throw;
  }
  ++mRevision;
  return id;
}

void EmbossingCatalogLibrary::renamePreset(
    string const& presetId, string const& displayName) {
  auto found = find_if(mData.presets.begin(), mData.presets.end(),
                       [&](auto const& preset) { return preset.id == presetId; });
  if (found == mData.presets.end()) {
    throw invalid_argument("Unknown Emboss preset id '" + presetId + "'");
  }
  validate(displayName, found->emboss);
  auto previous = found->displayName;
  found->displayName = displayName;
  try {
    save();
  } catch (...) {
    found->displayName = move(previous);
    throw;
  }
  ++mRevision;
}

void EmbossingCatalogLibrary::editPreset(
    string const& presetId, bw::core::EmbossData const& emboss) {
  auto found = find_if(mData.presets.begin(), mData.presets.end(),
                       [&](auto const& preset) { return preset.id == presetId; });
  if (found == mData.presets.end()) {
    throw invalid_argument("Unknown Emboss preset id '" + presetId + "'");
  }
  validate(found->displayName, emboss);
  auto previous = found->emboss;
  found->emboss = emboss;
  try {
    save();
  } catch (...) {
    found->emboss = previous;
    throw;
  }
  ++mRevision;
}

void EmbossingCatalogLibrary::deletePreset(string const& presetId) {
  if (!findPreset(presetId)) {
    throw invalid_argument("Unknown Emboss preset id '" + presetId + "'");
  }
  auto previous = mData;
  erase_if(mData.presets,
           [&](auto const& preset) { return preset.id == presetId; });
  try {
    save();
  } catch (...) {
    mData = move(previous);
    throw;
  }
  ++mRevision;
}

EmbossingCatalogSnapshot EmbossingCatalogLibrary::captureSnapshot() const {
  return {mFilepath, mData, mRevision};
}

void EmbossingCatalogLibrary::restoreSnapshot(
    EmbossingCatalogSnapshot const& snapshot) {
  if (mRevision == snapshot.revision) return;
  auto previousPath = mFilepath;
  auto previousData = mData;
  mFilepath = snapshot.filepath;
  mData = snapshot.data;
  try {
    save();
  } catch (...) {
    mFilepath = move(previousPath);
    mData = move(previousData);
    throw;
  }
  mRevision = snapshot.revision;
}

EmbossingCatalogLibrary& embossingCatalogLibrary() {
  static EmbossingCatalogLibrary library;
  return library;
}

}  // namespace editor
