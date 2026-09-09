#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <core/BuildVariables.h>

namespace bw {
namespace core {

// A LuaScript resource declaration for one Step build variable. The common
// type/value belongs to core; these constraints are specific to RunScript
// authoring resources.
struct StepVariableDefinition {
  std::string name;
  BuildVariableType type{BuildVariableType::String};
  BuildVariableValue defaultValue{std::string{}};

  // Empty means free text. Non-empty means the String widget and value are
  // restricted to this ordered list.
  std::vector<std::string> choices;

  int64_t integerMinimum{0};
  int64_t integerMaximum{0};
  double floatMinimum{0.0};
  double floatMaximum{0.0};

  [[nodiscard]] bool accepts(BuildVariableValue const& value) const;
};

}  // namespace core
}  // namespace bw
