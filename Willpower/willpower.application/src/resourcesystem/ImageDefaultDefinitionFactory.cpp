#include <utils/StringUtils.h>
#include <utils/XmlReader.h>

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ImageDefaultDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;
using namespace utils;

ImageDefaultDefinitionFactory::ImageDefaultDefinitionFactory()
    : ImageResourceDefinitionFactory("") {
}

void ImageDefaultDefinitionFactory::create(Resource* resource, ResourceManager* resourceMgr, utils::XmlNode* node) {
  WP_UNUSED(resource);
  WP_UNUSED(resourceMgr);
  WP_UNUSED(node);
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE