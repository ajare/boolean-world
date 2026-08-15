#include <utils/StringUtils.h>

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ShaderResourceDefinitionFactory.h"
#include "willpower/application/resourcesystem/ShaderResource.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;

ShaderResourceDefinitionFactory::ShaderResourceDefinitionFactory(string const& factoryType)
    : ResourceDefinitionFactory("Shader", factoryType) {
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE