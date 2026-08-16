#include "willpower/application/resourcesystem/AudioBankResourceDefinitionFactory.h"
#include "willpower/application/resourcesystem/ResourceExceptions.h"
#include "willpower/common/Exceptions.h"

namespace WP_NAMESPACE {
namespace application {
namespace resourcesystem {
using namespace std;
using namespace wp;

AudioBankResourceDefinitionFactory::AudioBankResourceDefinitionFactory(string const& factoryType)
    : ResourceDefinitionFactory("AudioBank", factoryType) {
}

}  // namespace resourcesystem
}  // namespace application
}  // namespace WP_NAMESPACE