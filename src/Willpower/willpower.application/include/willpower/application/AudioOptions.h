#pragma once

#include "willpower/application/Platform.h"

namespace WP_NAMESPACE {
namespace application {

struct AudioOptions {
  bool synchronous{false};
  int numChannels{1024};
};

}  // namespace application
}  // namespace WP_NAMESPACE
