#pragma once

#include <map>
#include <string>

#include <willpower/application/resourcesystem/Resource.h>
#include <willpower/application/resourcesystem/ResourceFactory.h>

#include <core/ProcMaterialData.h>

#include "Platform.h"

// A catalog Resource wrapping bw::core::ProcMaterialData - see ADR-0023.
// Performs no GPU-resource work: create()/load() parse the referenced
// TextFile resource's YAML content into mData and expose it, the same way
// Map wraps a parsed bw::core::World.
class ProcMaterial : public wp::application::resourcesystem::Resource {
  bw::core::ProcMaterialData mData;

  void destroy() override;

public:
  ProcMaterial(std::string const& name,
               std::string const& namesp,
               std::string const& source,
               std::map<std::string, std::string> const& tags,
               wp::application::resourcesystem::ResourceLocation* location);

  void loadFromYaml(wp::application::resourcesystem::ResourcePtr resource);

  bw::core::ProcMaterialData const& getData() const;
};

class ProcMaterialResourceFactory : public wp::application::resourcesystem::ResourceFactory {
public:
  ProcMaterialResourceFactory()
      : wp::application::resourcesystem::ResourceFactory("ProcMaterial") {
  }

  wp::application::resourcesystem::Resource* createResource(std::string const& name, std::string const& namesp, std::string const& source, std::map<std::string, std::string> const& tags, wp::application::resourcesystem::ResourceLocation* location) override {
    return new ProcMaterial(name, namesp, source, tags, location);
  }
};
