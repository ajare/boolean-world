#pragma once

#include <string>

#include <willpower/application/resourcesystem/ResourceDefinitionFactory.h>

#include "Platform.h"
#include "willpower/common/DataNode.h"

// Loads the one process-wide Embossing catalog. A second differently named
// catalog is rejected: Emboss preset ids intentionally have one global owner.
class EmbossingCatalogResourceDefinitionFactory
    : public wp::application::resourcesystem::ResourceDefinitionFactory {
  std::string mLoadedCatalog;

public:
  EmbossingCatalogResourceDefinitionFactory();

  void create(
      wp::application::resourcesystem::Resource* resource,
      wp::application::resourcesystem::ResourceManager* resourceMgr,
      wp::DataNode* node) override;
};
