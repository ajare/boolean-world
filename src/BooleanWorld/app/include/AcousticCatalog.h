#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include <willpower/application/resourcesystem/Resource.h>
#include <willpower/application/resourcesystem/ResourceFactory.h>

// App-side acoustic data. These values mirror Steam Audio's seven material
// coefficients without exposing a Steam Audio type or any acoustic maths to
// core.
struct AcousticPreset {
  std::string id;
  std::string displayName;
  std::array<float, 3> absorption{};
  float scattering{0.0f};
  std::array<float, 3> transmission{};

  bool operator==(AcousticPreset const&) const = default;
};

class AcousticCatalog : public wp::application::resourcesystem::Resource {
  std::vector<AcousticPreset> mPresets;

  void destroy() override;

public:
  AcousticCatalog(
      std::string const& name, std::string const& namesp,
      std::string const& source, std::map<std::string, std::string> const& tags,
      wp::application::resourcesystem::ResourceLocation* location);

  void loadFromYaml(wp::application::resourcesystem::ResourcePtr resource);
  [[nodiscard]] std::vector<AcousticPreset> const& getPresets() const;
};

class AcousticCatalogResourceFactory
    : public wp::application::resourcesystem::ResourceFactory {
public:
  AcousticCatalogResourceFactory()
      : wp::application::resourcesystem::ResourceFactory("AcousticCatalog") {}

  wp::application::resourcesystem::Resource* createResource(
      std::string const& name, std::string const& namesp,
      std::string const& source, std::map<std::string, std::string> const& tags,
      wp::application::resourcesystem::ResourceLocation* location) override {
    return new AcousticCatalog(name, namesp, source, tags, location);
  }
};
