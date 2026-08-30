#include <willpower/application/resourcesystem/ResourceExceptions.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include "EmbossingCatalog.h"
#include "EmbossingCatalogResourceDefinitionFactory.h"

using namespace std;
using namespace wp;

EmbossingCatalogResourceDefinitionFactory::
    EmbossingCatalogResourceDefinitionFactory()
    : application::resourcesystem::ResourceDefinitionFactory(
          "EmbossingCatalog", "") {}

void EmbossingCatalogResourceDefinitionFactory::create(
    application::resourcesystem::Resource* resource,
    application::resourcesystem::ResourceManager*, DataNode* node) {
  auto catalog = static_cast<EmbossingCatalog*>(resource);
  auto const& qualifiedName = catalog->getQualifiedName();
  if (!mLoadedCatalog.empty() && mLoadedCatalog != qualifiedName) {
    throw application::resourcesystem::ResourceException(
        resource,
        "Only one global Embossing catalog may be loaded; '" +
            mLoadedCatalog + "' is already loaded.");
  }

  auto resourceName = node->getChild("Resource")->getValue();
  catalog->loadFromYaml(catalog->getDependentResource(resourceName));
  mLoadedCatalog = qualifiedName;
}
