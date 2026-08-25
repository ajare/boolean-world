#pragma once

#include <map>
#include <string>

#include <willpower/application/resourcesystem/Resource.h>
#include <willpower/application/resourcesystem/ResourceDefinitionFactory.h>

#include "Platform.h"
#include "willpower/common/DataNode.h"

// Resolves the TextFile dependent resource named by the Definitions' Resource
// child and hands it to ProcMaterial::loadFromYaml(), then enforces
// Sub-material id uniqueness across every ProcMaterial resource this factory
// has ever loaded - see issue #260's acceptance criteria and ADR-0023. One
// instance is registered per DLL lifetime (DLL.cpp), so mSubMaterialOwners
// accumulates across every ProcMaterial resource that instance parses.
class ProcMaterialResourceDefinitionFactory : public wp::application::resourcesystem::ResourceDefinitionFactory {
  std::map<std::string, std::string> mSubMaterialOwners;

public:
  ProcMaterialResourceDefinitionFactory();

  void create(wp::application::resourcesystem::Resource* resource, wp::application::resourcesystem::ResourceManager* resourceMgr, wp::DataNode* node) override;
};
