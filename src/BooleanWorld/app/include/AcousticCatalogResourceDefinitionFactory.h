#pragma once

#include <string>

#include <willpower/application/resourcesystem/ResourceDefinitionFactory.h>

// Acoustic preset ids have one process-wide catalog, matching the ownership
// model of Emboss preset ids.
class AcousticCatalogResourceDefinitionFactory
    : public wp::application::resourcesystem::ResourceDefinitionFactory {
  std::string mLoadedCatalog;

public:
  AcousticCatalogResourceDefinitionFactory();

  void create(
      wp::application::resourcesystem::Resource* resource,
      wp::application::resourcesystem::ResourceManager* resourceMgr,
      wp::DataNode* node) override;
};
