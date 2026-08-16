#pragma once

#include "willpower/application/Platform.h"
#include "willpower/application/resourcesystem/Resource.h"
#include "willpower/application/resourcesystem/ResourceFactory.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {

class WP_APPLICATION_API XmlFileResourceDefinitionFactory : public ResourceDefinitionFactory {
public:
  explicit XmlFileResourceDefinitionFactory(std::string const& factoryType);
};

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE
