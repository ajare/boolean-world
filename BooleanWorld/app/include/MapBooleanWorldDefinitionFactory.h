#pragma once

#include <applib/MapResourceDefinitionFactory.h>

#include "Platform.h"
#include "willpower/common/DataNode.h"

class MapBooleanWorldDefinitionFactory : public applib::MapResourceDefinitionFactory {
public:
  MapBooleanWorldDefinitionFactory();

  void create(wp::application::resourcesystem::Resource* resource, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::DataNode* node) override;
};
