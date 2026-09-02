#include <iostream>
#include <stdexcept>

#include <sol/sol.hpp>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void evaluatingATrivialExpressionReturnsItsResult() {
  sol::state lua;

  sol::protected_function_result result = lua.script("return 1 + 2");
  require(result.valid(), "evaluating a trivial Lua expression failed");

  int value = result;
  require(value == 3, "a trivial Lua expression did not evaluate to the expected result");
}

}  // namespace

int main() {
  try {
    evaluatingATrivialExpressionReturnsItsResult();
    std::cout << "sol2 evaluates a trivial Lua expression through the vendored Lua 5.4 runtime\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
