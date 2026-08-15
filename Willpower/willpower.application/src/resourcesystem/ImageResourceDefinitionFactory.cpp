#include <freeimage/FreeImage.h>

#include <mpp/ProgrammaticTextureStream.h>

#include "willpower/common/Exceptions.h"

#include "willpower/application/resourcesystem/ImageResourceDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;

ImageResourceDefinitionFactory::ImageResourceDefinitionFactory(string const& factoryType)
    : ResourceDefinitionFactory("Image", factoryType) {
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE