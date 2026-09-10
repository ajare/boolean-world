#include "ProcMaterialLibrary.h"

#include <memory>
#include <vector>

#include <willpower/application/resourcesystem/ResourceManager.h>

#include "TriplanarMaterial.h"

namespace editor {
namespace {

std::string qualifiedResourceName(
    wp::application::resourcesystem::Resource const& resource) {
  return resource.getNamespace().empty() ? "/" + resource.getName()
                                         : resource.getQualifiedName();
}

}  // namespace

std::vector<TriplanarMaterialEntry> discoverLoadedTriplanarMaterials(
    wp::application::resourcesystem::ResourceManager* resourceManager) {
  std::vector<TriplanarMaterialEntry> result;
  if (!resourceManager) return result;

  for (auto const& resource :
       resourceManager->getResourcesByType("TriplanarMaterial")) {
    if (!resourceManager->isResourceLoaded(resource)) continue;
    auto material = std::dynamic_pointer_cast<TriplanarMaterial>(resource);
    if (!material || !material->getAlbedo()) continue;
    result.push_back(
        {qualifiedResourceName(*material),
         qualifiedResourceName(*material->getAlbedo()), material->getTileWidth(),
         material->getBlendSharpness()});
  }
  return result;
}

}  // namespace editor
