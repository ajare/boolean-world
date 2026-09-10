#pragma once

#include <willpower/application/resourcesystem/ResourceDefinitionFactory.h>

#include "Platform.h"

class TriplanarMaterialResourceDefinitionFactory final
    : public wp::application::resourcesystem::ResourceDefinitionFactory {
public:
  TriplanarMaterialResourceDefinitionFactory();

  void create(
      wp::application::resourcesystem::Resource* resource,
      wp::application::resourcesystem::ResourceManager* resourceMgr,
      wp::DataNode* node) override;
};
