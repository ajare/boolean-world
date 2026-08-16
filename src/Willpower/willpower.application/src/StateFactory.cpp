#include "willpower/application/StateFactory.h"

using namespace std;

namespace WP_NAMESPACE {
namespace application {

StateFactory::StateFactory(string const& type)
    : mType(type) {
}

string const& StateFactory::getType() const {
  return mType;
}

}  // namespace application
}  // namespace WP_NAMESPACE