#include "AcousticCatalogResourceDefinitionFactory.h"

#include <willpower/application/resourcesystem/ResourceExceptions.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include "AcousticCatalog.h"

using namespace std;
using namespace wp;

AcousticCatalogResourceDefinitionFactory::
    AcousticCatalogResourceDefinitionFactory()
    : application::resourcesystem::ResourceDefinitionFactory(
          "AcousticCatalog", "") {}

void AcousticCatalogResourceDefinitionFactory::create(
    application::resourcesystem::Resource* resource,
    application::resourcesystem::ResourceManager*, DataNode* node) {
  auto catalog = static_cast<AcousticCatalog*>(resource);
  auto const& qualifiedName = catalog->getQualifiedName();
  if (!mLoadedCatalog.empty() && mLoadedCatalog != qualifiedName) {
    throw application::resourcesystem::ResourceException(
        resource,
        "Only one global Acoustic catalog may be loaded; '" +
            mLoadedCatalog + "' is already loaded.");
  }

  auto resourceName = node->getChild("Resource")->getValue();
  catalog->loadFromYaml(catalog->getDependentResource(resourceName));
  mLoadedCatalog = qualifiedName;
}
