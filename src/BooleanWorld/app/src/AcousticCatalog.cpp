#include "AcousticCatalog.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

#include <willpower/application/resourcesystem/ResourceExceptions.h>
#include <willpower/application/resourcesystem/TextFileResource.h>

using namespace std;
using namespace wp;

namespace {

array<float, 3> readBands(YAML::Node const& node, char const* field) {
  auto bands = node[field];
  if (!bands || !bands.IsSequence() || bands.size() != 3) {
    throw runtime_error(
        string("Acoustic preset '") + field + "' must contain three bands.");
  }

  array<float, 3> result{};
  for (size_t i = 0; i < result.size(); ++i) result[i] = bands[i].as<float>();
  return result;
}

bool coefficientIsValid(float value) {
  return isfinite(value) && value >= 0.0f && value <= 1.0f;
}

bool coefficientsAreValid(AcousticPreset const& preset) {
  return coefficientIsValid(preset.scattering) &&
         all_of(
             preset.absorption.begin(), preset.absorption.end(),
             coefficientIsValid) &&
         all_of(
             preset.transmission.begin(), preset.transmission.end(),
             coefficientIsValid);
}

}  // namespace

AcousticCatalog::AcousticCatalog(
    string const& name, string const& namesp, string const& source,
    map<string, string> const& tags,
    application::resourcesystem::ResourceLocation* location)
    : application::resourcesystem::Resource(
          name, namesp, "AcousticCatalog", source, tags, location) {}

void AcousticCatalog::destroy() {
  mPresets.clear();
}

void AcousticCatalog::loadFromYaml(
    application::resourcesystem::ResourcePtr resource) {
  auto text = static_cast<application::resourcesystem::TextFileResource*>(
      resource.get());

  try {
    auto root = YAML::Load(text->getText());
    auto nodes = root["presets"];
    if (!nodes || !nodes.IsSequence()) {
      throw runtime_error("Acoustic catalog must contain a presets array.");
    }

    vector<AcousticPreset> presets;
    set<string> ids;
    for (auto const& node : nodes) {
      AcousticPreset preset;
      preset.id = node["id"].as<string>();
      preset.displayName = node["name"].as<string>();
      preset.absorption = readBands(node, "absorption");
      preset.scattering = node["scattering"].as<float>();
      preset.transmission = readBands(node, "transmission");

      if (preset.id.empty()) {
        throw runtime_error("Acoustic preset id must not be empty.");
      }
      if (!ids.insert(preset.id).second) {
        throw runtime_error("Duplicate Acoustic preset id '" + preset.id + "'.");
      }
      if (!coefficientsAreValid(preset)) {
        throw runtime_error(
            "Acoustic preset '" + preset.id +
            "' has a coefficient outside [0, 1].");
      }
      presets.push_back(move(preset));
    }
    mPresets = move(presets);
  } catch (exception const& error) {
    throw application::resourcesystem::ResourceException(
        this, "Could not load Acoustic catalog from YAML. " +
                  string(error.what()));
  }
}

vector<AcousticPreset> const& AcousticCatalog::getPresets() const {
  return mPresets;
}
