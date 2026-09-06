#include "core-lua/ScriptParameters.h"

#include <algorithm>
#include <cmath>

namespace bw {
namespace core {

using namespace std;

bool ScriptParameterDefinition::accepts(
    ScriptParameterValue const& value) const {
  switch (type) {
    case ScriptParameterType::String: {
      auto const* text = get_if<string>(&value);
      return text &&
             (choices.empty() ||
              find(choices.begin(), choices.end(), *text) != choices.end());
    }
    case ScriptParameterType::Integer: {
      auto const* integer = get_if<int64_t>(&value);
      return integer && *integer >= integerMinimum &&
             *integer <= integerMaximum;
    }
    case ScriptParameterType::Number: {
      auto const* number = get_if<double>(&value);
      return number && isfinite(*number) && *number >= numberMinimum &&
             *number <= numberMaximum;
    }
    case ScriptParameterType::Boolean:
      return holds_alternative<bool>(value);
  }
  return false;
}

char const* scriptParameterTypeName(ScriptParameterType type) {
  switch (type) {
    case ScriptParameterType::String:
      return "string";
    case ScriptParameterType::Integer:
      return "integer";
    case ScriptParameterType::Number:
      return "number";
    case ScriptParameterType::Boolean:
      return "boolean";
  }
  return "unknown";
}

}  // namespace core
}  // namespace bw
