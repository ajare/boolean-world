#include <willpower/application/resourcesystem/ResourceExceptions.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include "ProcMaterialResourceDefinitionFactory.h"
#include "ProcMaterial.h"

using namespace std;
using namespace wp;

ProcMaterialResourceDefinitionFactory::ProcMaterialResourceDefinitionFactory()
    : application::resourcesystem::ResourceDefinitionFactory("ProcMaterial", "") {
}

void ProcMaterialResourceDefinitionFactory::create(application::resourcesystem::Resource* resource, application::resourcesystem::ResourceManager* resourceMgr, DataNode* node) {
  VAR_UNUSED(resourceMgr);

  auto procMaterial = static_cast<ProcMaterial*>(resource);

  auto resourceNode = node->getChild("Resource");
  auto resourceName = resourceNode->getValue();

  auto depResource = procMaterial->getDependentResource(resourceName);

  procMaterial->loadFromYaml(depResource);

  // Sub-material id uniqueness is scoped to this catalog by
  // ProcMaterialData::deserializeImpl(); global uniqueness across every
  // catalog this factory has loaded is checked here - see ADR-0023.
  auto const& qualifiedName = procMaterial->getQualifiedName();
  for (auto const& id : procMaterial->getData().subMaterialIds()) {
    auto it = mSubMaterialOwners.find(id);
    if (it != mSubMaterialOwners.end() && it->second != qualifiedName) {
      throw application::resourcesystem::ResourceException(
          resource,
          "Sub-material id '" + id + "' is already used by ProcMaterial '" + it->second + "'.");
    }

    mSubMaterialOwners[id] = qualifiedName;
  }
}
