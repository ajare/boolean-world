#include <utils/StringUtils.h>

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/MaterialDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;
using namespace utils;

MaterialDefaultDefinitionFactory::MaterialDefaultDefinitionFactory()
    : MaterialResourceDefinitionFactory("") {
}

void MaterialDefaultDefinitionFactory::create(Resource* resource, ResourceManager* resourceMgr, utils::XmlNode* node) {
  WP_UNUSED(resourceMgr);

  auto materialRes = static_cast<MaterialResource*>(resource);

  auto texturesNode = node->getChild("Textures");
  auto textureNode = texturesNode->getOptionalChild("Texture");
  if (textureNode) {
    do {
      string textureType = textureNode->getAttribute("type");

      if (textureType == "resource") {
        addResourceTextureDefinition(materialRes, textureNode->getAttribute("sampler"), textureNode->getValue());
      } else if (textureType == "default") {
        addDefaultTextureDefinition(materialRes, textureNode->getAttribute("sampler"));
      } else {
        string errMsg = "material '" + materialRes->getName() +
                        "' has invalid texture type '" + textureType + "'.";

        throw ResourceException(materialRes, errMsg);
      }
    } while (textureNode->next());
  }
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE