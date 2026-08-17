#pragma once

#include <cstring>
#include <string>

class DLLState {
  int mNextStateFactory = 0;

public:
  int setArgument(char const* arg, char const* value, bool& threadedLoading) const {
    if (std::strcmp(arg, "ThreadedLoading") != 0) {
      return 1;
    }

    std::string const argumentValue(value);
    if (argumentValue == "true") {
      threadedLoading = true;
    } else if (argumentValue == "false") {
      threadedLoading = false;
    } else {
      return 1;
    }

    return 0;
  }

  int getNextStateFactoryIndex() {
    return mNextStateFactory++;
  }

  void resetStateFactoryEnumeration() {
    mNextStateFactory = 0;
  }
};
