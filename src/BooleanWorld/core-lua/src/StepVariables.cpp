#include "core-lua/StepVariables.h"

#include <algorithm>
#include <cmath>

namespace bw {
namespace core {

using namespace std;

bool StepVariableDefinition::accepts(BuildVariableValue const& value) const {
  switch (type) {
    case BuildVariableType::String: {
      auto const* text = get_if<string>(&value);
      return text && text->find('\0') == string::npos &&
             (choices.empty() ||
              find(choices.begin(), choices.end(), *text) != choices.end());
    }
    case BuildVariableType::Integer: {
      auto const* integer = get_if<int64_t>(&value);
      return integer && *integer >= integerMinimum && *integer <= integerMaximum;
    }
    case BuildVariableType::Float: {
      auto const* number = get_if<double>(&value);
      return number && isfinite(*number) && *number >= floatMinimum &&
             *number <= floatMaximum;
    }
    case BuildVariableType::Boolean:
      return holds_alternative<bool>(value);
  }
  return false;
}

}  // namespace core
}  // namespace bw
