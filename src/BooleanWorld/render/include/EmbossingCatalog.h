#pragma once

#include <map>
#include <string>

#include <willpower/application/resourcesystem/Resource.h>
#include <willpower/application/resourcesystem/ResourceFactory.h>

#include <core/EmbossingCatalogData.h>

#include "Platform.h"

class EmbossingCatalog : public wp::application::resourcesystem::Resource {
  bw::core::EmbossingCatalogData mData;

  void destroy() override;

public:
  EmbossingCatalog(
      std::string const& name, std::string const& namesp,
      std::string const& source, std::map<std::string, std::string> const& tags,
      wp::application::resourcesystem::ResourceLocation* location);

  void loadFromYaml(wp::application::resourcesystem::ResourcePtr resource);
  [[nodiscard]] bw::core::EmbossingCatalogData const& getData() const;
};

class EmbossingCatalogResourceFactory
    : public wp::application::resourcesystem::ResourceFactory {
public:
  EmbossingCatalogResourceFactory()
      : wp::application::resourcesystem::ResourceFactory("EmbossingCatalog") {}

  wp::application::resourcesystem::Resource* createResource(
      std::string const& name, std::string const& namesp,
      std::string const& source, std::map<std::string, std::string> const& tags,
      wp::application::resourcesystem::ResourceLocation* location) override {
    return new EmbossingCatalog(name, namesp, source, tags, location);
  }
};
