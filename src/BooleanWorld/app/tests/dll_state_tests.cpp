#include <iostream>
#include <stdexcept>
#include <string>

#include "DLLState.h"

namespace {

void require(bool condition, std::string const& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void rejectsUnknownArguments() {
  DLLState state;
  bool threadedLoading = true;

  require(state.setArgument("ThreadedLoading", "false", threadedLoading) == 0,
          "Known DLL argument was rejected.");
  require(!threadedLoading, "Known DLL argument was not applied.");

  require(state.setArgument("ThreadedLoadng", "true", threadedLoading) != 0,
          "Unknown DLL argument was accepted.");
  require(!threadedLoading, "Unknown DLL argument changed configuration.");
}

void resetsStateFactoryEnumerationOnEntry() {
  DLLState state;

  require(state.getNextStateFactoryIndex() == 0,
          "Initial state-factory enumeration did not start at the first factory.");
  require(state.getNextStateFactoryIndex() == 1,
          "State-factory enumeration did not advance.");

  state.resetStateFactoryEnumeration();

  require(state.getNextStateFactoryIndex() == 0,
          "State-factory enumeration did not reset for the next DLL entry.");
}

}  // namespace

int main() {
  try {
    rejectsUnknownArguments();
    resetsStateFactoryEnumerationOnEntry();
    std::cout << "DLL state regression tests passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
