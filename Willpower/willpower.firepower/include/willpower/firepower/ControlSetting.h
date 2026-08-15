#pragma once

#include <vector>
#include <functional>

namespace WP_NAMESPACE {
namespace firepower {
typedef std::function<float(float, void*)> ControlFunction;

enum class ControlOptions {
  None,
  Scalar,
  Function
};

struct ControlSetting {
  ControlOptions option;
  float scalar;
  ControlFunction function;

  ControlSetting()
      : scalar(0.0f), function({}) {
    option = ControlOptions::None;
  }

  ControlSetting(float _scalar)
      : scalar(_scalar), function({}), option(ControlOptions::Scalar) {
  }

  ControlSetting(ControlFunction _function)
      : scalar(0.0f), function(_function), option(ControlOptions::Function) {
  }
};

}  // namespace firepower
}  // namespace WP_NAMESPACE
