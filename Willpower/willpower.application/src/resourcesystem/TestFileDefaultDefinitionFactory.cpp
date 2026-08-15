#include <utils/StringUtils.h>

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/TextFileDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;
using namespace utils;

TextFileDefaultDefinitionFactory::TextFileDefaultDefinitionFactory()
    : TextFileResourceDefinitionFactory("") {
}

void TextFileDefaultDefinitionFactory::create(Resource* resource, ResourceManager* resourceMgr, utils::XmlNode* node) {
  WP_UNUSED(resource);
  WP_UNUSED(resourceMgr);
  WP_UNUSED(node);
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE