#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "StateMapTransitionBooleanWorld.h"

namespace {

class TestableStateMapTransition : public applib::StateMapTransition {
public:
  using StateMapTransition::getPreWork;
};

class TestableStateMapTransitionBooleanWorld : public StateMapTransitionBooleanWorld {
public:
  using StateMapTransitionBooleanWorld::getPreWork;
};

using PreWorkFunction = decltype(&TestableStateMapTransition::getPreWork);

static_assert(std::is_same_v<decltype(&TestableStateMapTransitionBooleanWorld::getPreWork),
                             PreWorkFunction>);

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void mapTransitionDoesNotOverridePreWork() {
  require(std::is_same_v<decltype(&TestableStateMapTransitionBooleanWorld::getPreWork),
                         PreWorkFunction>,
          "map transition must inherit the empty pre-work implementation");
}

}  // namespace

int main() {
  try {
    mapTransitionDoesNotOverridePreWork();
    std::cout << "Map transition pre-work passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
