#pragma once

#include <applib/GameDefaultDefinitionFactory.h>

#include "Platform.h"
#include "willpower/common/DataNode.h"

class GameDefinitionFactory : public applib::GameDefaultDefinitionFactory {
public:
  GameDefinitionFactory();

  void create(wp::application::resourcesystem::Resource* resource, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::DataNode* node) override;
};
